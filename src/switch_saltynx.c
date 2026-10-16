/*
 * switch_saltynx.c -- the frame rate for SaltyNX overlays (Status Monitor
 * and its forks, e.g. Horizon OC Monitor) on the Switch.
 *
 * Those overlays do not measure FPS themselves: they read it from a block
 * in SaltyNX's shared memory that its NX-FPS plugin fills in by hooking the
 * game's present call (NVN, EGL or vkQueuePresentKHR). This NRO links NVK
 * and Mesa statically, so there is nothing to hook. Here the port fills in
 * the block itself, in NX-FPS's format.
 *
 * Protocol (SaltyNX saltysd_core/saltysd_proc, Status-Monitor-Overlay
 * source/Utils.hpp):
 *   - named port "SaltySD", one session at a time; requests carry the
 *     "SFCI" magic and the PID, which libnx's serviceDispatch produces;
 *   - command 7 answers a copy of the shared memory handle (one 0x1000
 *     page), command 6 (size) reserves bytes in it and answers the offset,
 *     command 0 ends the session;
 *   - the overlay scans the page 4 bytes at a time for the first
 *     0x465053 magic, clears pluginActive and counts the game as running
 *     if it is set again within 100 ms; then it reads FPS (frames in the
 *     last whole second) and averages FPSticks[10] (frame times in system
 *     ticks) -- all zero shows as "inf", so they are seeded.
 *
 * Things learnt the hard way by other ports (the sa2-nx Switch port
 * documents them), handled here in our own way:
 *   - started from a forwarder, the process may be allowed a single port
 *     session, already taken by "sm:": the connection then fails with
 *     0x10801 although SaltyNX is there. The session cap is raised when the
 *     loader allows svcSetResourceLimitLimitValue; otherwise "sm:" is let
 *     go for the few milliseconds of the handshake;
 *   - SaltyNX only resets its page allocator when it injects into a retail
 *     game, never for homebrew: reserving on every boot would use up the
 *     page. The page is mapped first and an existing block is reused;
 *   - with the NRO running in a game's title, SaltyNX may have injected its
 *     own Core, which maps the page and leaves an idle NX-FPS block: that
 *     block is the one the overlay finds, so it is the one written to;
 *   - the overlay may be opened at any time and the sysmodule may answer
 *     late: a thread retries once a second while there is no valid block,
 *     and checks the magic is still there (the page is shared).
 *
 * Without SaltyNX nothing happens beyond a log line. NFSU2_SALTYNX=0 in
 * nfsu2x_env.txt turns it off.
 */
#ifdef __SWITCH__

#include <switch.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NXFPS_MAGIC 0x465053u
#define PAGE_BYTES 0x1000
#define API_VULKAN 3                        /* 1 NVN, 2 EGL, 3 Vulkan */

typedef struct {
    uint16_t width, height, calls;
} __attribute__((packed)) ResCalls;

/* NX-FPS's NxFpsSharedBlock; Status Monitor asserts 174 bytes. */
typedef struct {
    uint32_t magic;
    uint8_t fps;
    float fps_avg;
    bool plugin_active;
    uint8_t fps_locked;
    uint8_t fps_mode;
    uint8_t zero_sync;
    uint8_t patch_applied;
    uint8_t api;
    uint32_t ticks[10];
    uint8_t buffers;
    uint8_t set_buffers;
    uint8_t active_buffers;
    uint8_t set_active_buffers;
    uint8_t display_sync;
    ResCalls render_calls[8];
    ResCalls viewport_calls[8];
    bool force_original_rr;
    bool dont_force_60_docked;
    bool force_suspend;
    uint8_t current_rr;
    float read_speed;
    uint8_t fps_locked_docked;
    uint64_t frame_number;
    int8_t expected_set_buffers;
} __attribute__((packed)) FpsBlock;

_Static_assert(sizeof(FpsBlock) == 174, "NxFpsSharedBlock layout");

static SharedMemory s_shm;
static uint8_t *s_page;                     /* mapped page, ours or Core's */
static _Atomic(FpsBlock *) s_block;
static _Atomic uint32_t s_last_fps;         /* for seeding a new block */
static _Atomic uint32_t s_res;              /* width << 16 | height */
static Thread s_thread;
static volatile bool s_stop;
static bool s_thread_up;
static int s_tries;
static _Atomic int s_overlay_seen;          /* an overlay cleared pluginActive */
static _Atomic uint32_t s_frames_reported;  /* presents written to the block */
static _Atomic uint32_t s_probes;           /* overlay "alive?" checks seen */
static int s_moves;                         /* block switches, for the log */

static FpsBlock *find_block(uint8_t *page, size_t bytes)
{
    for (size_t o = 0; page && o + sizeof(FpsBlock) <= bytes; o += 4) {
        uint32_t m;
        memcpy(&m, page + o, 4);
        if (m == NXFPS_MAGIC)
            return (FpsBlock *)(page + o);
    }
    return NULL;
}

static bool block_ok(const FpsBlock *b)
{
    return b && b->magic == NXFPS_MAGIC;
}

/* Every NX-FPS magic in the page, for the log: the overlay reads the first. */
static void log_page(const char *why)
{
    char line[160];
    int n = 0, len;
    if (!s_page)
        return;
    len = snprintf(line, sizeof line, "[saltynx] %s; FPS marks in the page at:", why);
    for (size_t o = 0; o + 4 <= PAGE_BYTES && len < (int)sizeof line - 8; o += 4) {
        uint32_t m;
        memcpy(&m, s_page + o, 4);
        if (m == NXFPS_MAGIC) {
            len += snprintf(line + len, sizeof line - len, " +0x%X", (unsigned)o);
            n++;
        }
    }
    printf("%s%s\n", line, n ? "" : " none");
}

/* A new block: alive at once, ticks seeded so the overlay's average is a
 * number from the first read. */
static void seed(FpsBlock *b)
{
    uint32_t fps = atomic_load(&s_last_fps);
    uint32_t res = atomic_load(&s_res);
    b->api = API_VULKAN;
    b->fps = (uint8_t)fps;
    if (fps && !b->ticks[0]) {
        uint32_t t = (uint32_t)(armGetSystemTickFreq() / fps);
        for (int i = 0; i < 10; i++)
            b->ticks[i] = t;
    }
    if (res) {
        ResCalls r = { (uint16_t)(res >> 16), (uint16_t)res, (uint16_t)(fps ? fps : 1) };
        b->render_calls[0] = r;
        b->viewport_calls[0] = r;
    }
    b->plugin_active = true;
}

/* ── Getting the page ──────────────────────────────────────────────── */

/* Core injected into this process maps the page itself: find it among our
 * own shared-memory mappings by the magic. No port session needed. */
static bool page_from_own_mappings(void)
{
    u64 addr = 0;
    for (int n = 0; n < 4096; n++) {
        MemoryInfo mi;
        u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, addr)) || !mi.size)
            return false;
        if (mi.type == MemType_SharedMem && (mi.perm & Perm_R) && mi.size >= PAGE_BYTES
            && find_block((uint8_t *)(uintptr_t)mi.addr, PAGE_BYTES)) {
            s_page = (uint8_t *)(uintptr_t)mi.addr;
            return true;
        }
        if (mi.addr + mi.size <= addr)
            return false;
        addr = mi.addr + mi.size;
    }
    return false;
}

static Handle open_limit(void)
{
    u64 h = 0;
    if (R_SUCCEEDED(svcGetInfo(&h, InfoType_ResourceLimit, INVALID_HANDLE, 0)) && h)
        return (Handle)h;
    if (R_SUCCEEDED(svcGetInfo(&h, InfoType_ResourceLimit, CUR_PROCESS_HANDLE, 0)) && h)
        return (Handle)h;
    return INVALID_HANDLE;
}

static bool session_free(Handle lim)
{
    s64 cur = 0, max = 0;
    if (R_FAILED(svcGetResourceLimitCurrentValue(&cur, lim, LimitableResource_Sessions))
        || R_FAILED(svcGetResourceLimitLimitValue(&max, lim, LimitableResource_Sessions)))
        return true;                        /* unknown: just try */
    return cur < max;
}

/* Room for one more port session. *sm_dropped: "sm:" was let go and must
 * come back. Raising the cap is permanent and tried first; dropping "sm:"
 * only from the third try, once boot has opened its services. */
static bool make_room(bool *sm_dropped)
{
    Handle lim = open_limit();
    bool ok;
    *sm_dropped = false;
    if (lim == INVALID_HANDLE)
        return true;
    ok = session_free(lim);
    if (!ok && envIsSyscallHinted(0x7E)) {  /* svcSetResourceLimitLimitValue */
        s64 max = 0;
        svcGetResourceLimitLimitValue(&max, lim, LimitableResource_Sessions);
        if (R_SUCCEEDED(svcSetResourceLimitLimitValue(lim, LimitableResource_Sessions, max + 4))) {
            printf("[saltynx] session cap raised %ld -> %ld\n", (long)max, (long)(max + 4));
            ok = session_free(lim);
        }
    }
    if (!ok && s_tries >= 3 && (s_tries < 15 || s_tries % 60 == 0)) {
        smExit();
        if (session_free(lim)) {
            *sm_dropped = true;
            ok = true;
        } else {
            smInitialize();
        }
    }
    svcCloseHandle(lim);
    return ok;
}

/* One handshake: map the page if needed, reserve a block only if none is
 * there yet. Returns the last port result for the log. */
static Result handshake(u64 *offset, bool *reserved)
{
    bool sm_dropped;
    Result rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
    *reserved = false;
    if (!make_room(&sm_dropped))
        return MAKERESULT(Module_Kernel, KernelError_OutOfResource);

    for (int lap = 0; lap < (sm_dropped ? 1 : 3); lap++) {
        Handle port;
        Service srv = {0};
        rc = svcConnectToNamedPort(&port, "SaltySD");
        if (R_FAILED(rc)) {
            svcSleepThread(10000000ull);
            continue;
        }
        srv.session = port;
        if (!s_page) {
            Handle h = INVALID_HANDLE;
            rc = serviceDispatch(&srv, 7, .in_send_pid = true,
                                 .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
                                 .out_handles = &h);
            if (R_SUCCEEDED(rc)) {
                shmemLoadRemote(&s_shm, h, PAGE_BYTES, Perm_Rw);
                if (R_SUCCEEDED(shmemMap(&s_shm)))
                    s_page = (uint8_t *)shmemGetAddr(&s_shm);
                else
                    shmemClose(&s_shm);
            }
        }
        if (s_page && !find_block(s_page, PAGE_BYTES)) {
            u64 size = sizeof(FpsBlock);
            rc = serviceDispatchInOut(&srv, 6, size, *offset, .in_send_pid = true);
            /* SaltyNX's page starts with a 4-byte header it writes itself (the
             * display refresh rate at +1; its clears start at +4), but its
             * first reservation still answers offset 0: a block there loses
             * its magic within a second. Take the next slot instead. */
            if (R_SUCCEEDED(rc) && *offset < 4) {
                printf("[saltynx] SaltyNX gave offset 0 (its header): reserving the next slot\n");
                rc = serviceDispatchInOut(&srv, 6, size, *offset, .in_send_pid = true);
            }
            *reserved = R_SUCCEEDED(rc) && *offset >= 4 && *offset + sizeof(FpsBlock) <= PAGE_BYTES;
        }
        {
            u64 zero = 0;
            serviceDispatchIn(&srv, 0, zero, .in_send_pid = true);
        }
        svcCloseHandle(port);               /* the port takes one session */
        break;
    }
    if (sm_dropped)
        smInitialize();
    return rc;
}

static void attach(void)
{
    u64 offset = 0;
    bool reserved = false;
    Result rc = 0;
    FpsBlock *b;

    if (!s_page)
        rc = handshake(&offset, &reserved);
    else if (!find_block(s_page, PAGE_BYTES))
        rc = handshake(&offset, &reserved);
    if (!s_page && !page_from_own_mappings()) {
        if (++s_tries == 5 || s_tries == 60)
            printf("[saltynx] no SaltyNX page yet (0x%X); retrying every second\n", rc);
        return;
    }
    b = find_block(s_page, PAGE_BYTES);
    if (!b && reserved) {
        b = (FpsBlock *)(s_page + offset);
        memset(b, 0, sizeof *b);
        b->expected_set_buffers = -1;
        b->magic = NXFPS_MAGIC;
    }
    if (!b) {
        if (++s_tries == 5)
            printf("[saltynx] page mapped but no room for the FPS block; retrying\n");
        return;
    }
    seed(b);
    if (!atomic_exchange(&s_block, b))
        printf("[saltynx] FPS published for Status Monitor (block at +0x%X)\n",
               (unsigned)((uint8_t *)b - s_page));
    s_tries = 0;
}

static void salty_thread(void *arg)
{
    (void)arg;
    while (!s_stop) {
        FpsBlock *b = atomic_load(&s_block);
        if (!block_ok(b)) {
            if (b && s_moves < 20)
                log_page("our FPS block was wiped (SaltyNX clears the page when a process starts or ends)");
            atomic_store(&s_block, NULL);
            attach();
            b = atomic_load(&s_block);
            if (b && s_moves++ < 20)
                log_page("after re-attaching");
        } else {
            /* The overlay reads the first mark in the page. A block left
             * before ours (a stale one, or Core's) would hide ours: use it. */
            FpsBlock *first = find_block(s_page, PAGE_BYTES);
            if (first && first != b) {
                if (s_moves++ < 20)
                    printf("[saltynx] another FPS block at +0x%X comes before ours at +0x%X: writing there\n",
                           (unsigned)((uint8_t *)first - s_page), (unsigned)((uint8_t *)b - s_page));
                seed(first);
                atomic_store(&s_block, first);
                b = first;
            }
            b->fps = (uint8_t)atomic_load(&s_last_fps);
        }
        if (atomic_exchange(&s_overlay_seen, 0) == 1) {
            static int told;
            if (told++ < 3)
                printf("[saltynx] an overlay is reading our block at +0x%X (it cleared pluginActive)\n",
                       b ? (unsigned)((uint8_t *)b - s_page) : 0u);
        }
        {
            static int secs, beats;
            if (++secs % 30 == 0 && beats < 20) {
                FpsBlock *cur = atomic_load(&s_block);
                beats++;
                printf("[saltynx] status: block at %s0x%X, %u frames written, %u overlay checks answered, fps %u\n",
                       cur ? "+" : "none ", cur && s_page ? (unsigned)((uint8_t *)cur - s_page) : 0u,
                       atomic_load(&s_frames_reported), atomic_load(&s_probes),
                       atomic_load(&s_last_fps));
            }
        }
        for (int i = 0; i < 10 && !s_stop; i++)
            svcSleepThread(100000000ull);   /* 1 s, quick to stop */
    }
}

/* ── Per frame ─────────────────────────────────────────────────────── */

/* Called by the renderer after each present with the picture's size. The
 * counting follows NX-FPS's PostFrame. */
void nv2a_host_frame_presented(uint32_t width, uint32_t height)
{
    static u64 last, second_start;
    static u32 itr, in_second;
    static u64 frames;
    static const FpsBlock *seen;
    FpsBlock *b = atomic_load_explicit(&s_block, memory_order_acquire);
    u64 now = armGetSystemTick(), freq = armGetSystemTickFreq();

    if (width && height)
        atomic_store(&s_res, (width & 0xFFFF) << 16 | (height & 0xFFFF));
    if (!second_start)
        second_start = now;
    in_second++;
    if (now - second_start > freq) {
        atomic_store(&s_last_fps, in_second > 255 ? 255 : in_second - 1);
        if (now - second_start > 2 * freq) {
            second_start = now;
            in_second = 0;
        } else {
            second_start += freq;
            in_second = 1;
        }
        if (block_ok(b))
            b->fps = (uint8_t)atomic_load(&s_last_fps);
    }
    if (!block_ok(b)) {
        last = now;
        return;
    }
    if (seen != b) {                        /* new block: old delta is stale */
        seen = b;
        last = 0;
    }
    if (last) {
        u64 d = now - last;
        b->ticks[itr] = (uint32_t)(d > 0xFFFFFFFFu ? 0xFFFFFFFFu : d);
        itr = (itr + 1) % 10;
        b->frame_number = ++frames;
        b->fps_avg = d ? (float)freq / (float)d : 0.f;
    }
    last = now;
    if (width && height) {
        uint8_t f = b->fps;
        ResCalls r = { (uint16_t)width, (uint16_t)height, (uint16_t)(f ? f : 1) };
        b->render_calls[0] = r;             /* never 0xFFFF: the overlay's query */
        b->viewport_calls[0] = r;
    }
    b->api = API_VULKAN;
    if (!b->plugin_active) {                /* the overlay's "are you alive?" */
        atomic_store(&s_overlay_seen, 1);
        atomic_fetch_add(&s_probes, 1);
    }
    b->plugin_active = true;
    if (atomic_fetch_add(&s_frames_reported, 1) == 0)
        printf("[saltynx] first frame written to the FPS block (%ux%u)\n", width, height);
}

void switch_saltynx_start(void)
{
    const char *e = getenv("NFSU2_SALTYNX");
    if (e && *e == '0') {
        printf("[saltynx] off (NFSU2_SALTYNX=0)\n");
        return;
    }
    s_stop = false;
    if (R_SUCCEEDED(threadCreate(&s_thread, salty_thread, NULL, NULL, 0x8000, 0x2C, -2))
        && R_SUCCEEDED(threadStart(&s_thread)))
        s_thread_up = true;
}

void switch_saltynx_stop(void)
{
    FpsBlock *b;
    if (s_thread_up) {
        s_stop = true;
        threadWaitForExit(&s_thread);
        threadClose(&s_thread);
        s_thread_up = false;
    }
    b = atomic_exchange(&s_block, NULL);
    if (block_ok(b))
        b->plugin_active = false;
    if (shmemGetAddr(&s_shm))
        shmemClose(&s_shm);
    s_page = NULL;
}

#endif /* __SWITCH__ */
