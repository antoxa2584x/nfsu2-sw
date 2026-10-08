/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* The generated register model (g_eax/g_esp are thread-local there) and the
 * XBOX_PTR/MEM32 accessors; the register names below are its macros. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern ptrdiff_t g_xbox_mem_offset;

/* ── CRT memmove/memcpy ───────────────────────────────────────
 *
 * Two copies of the MSVC CRT memmove are linked (0x002A7EE0 and 0x002A9450;
 * memcpy is the same body). Their tail and backward-copy paths dispatch
 * through UnwindDown-style vectors indexed with a *negative* register -- the
 * table address in the instruction is the end of the table -- which the
 * lifter's forward table reader cannot follow, so those jumps were lowered
 * to indirect tail calls into the middle of the function and failed to
 * resolve: every copy whose length was not a multiple of 4, and every
 * overlapping backward copy, silently lost its tail.
 *
 * Replaced by the host memmove. cdecl (dst, src, count), returns dst, and
 * esi/edi are preserved, as the original restores them. Generated bodies are
 * skipped by tools.recomp --exclude-manual reading this file. */
static void crt_memmove(void)
{
    uint32_t dst = MEM32(esp + 4);
    uint32_t src = MEM32(esp + 8);
    uint32_t n   = MEM32(esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;   /* return address; cdecl, the caller pops the arguments */
}

void sub_002A7EE0(void) { crt_memmove(); }

/* ── Bounding box against the view frustum, sub_0009A330 ─────────────
 *
 * thiscall (this, const float min[3], const float max[3], matrix), ret 12.
 * The box's centre c = (max + min) * K and half-size e = max - c (both
 * stored as floats, as the original does), then for each of six planes
 * (n, d) at [this] + 0x140 + 16k: r = |n|.e, dist = n.c + d; below K2 on
 * dist + r means outside (return 0); below K2 on dist - r means the box
 * straddles that plane. Returns 2 when inside all six, 1 when straddling.
 * With a matrix the box is first transformed by it (sub_0009A250, below).
 *
 * It is the hottest function of NFSU2's main thread in a race (~5% on the
 * console): every object, every view, every frame. Written in C it keeps
 * everything in registers. The arithmetic is the lifted code's -- doubles,
 * in the same order -- so results match it exactly; RECOMP_NATIVE_CHECK=1
 * runs both and reports any difference. RECOMP_NATIVE=0 turns it off. */
extern void sub_0009A330_gen(void);
extern void sub_0009A250_gen(void);

/* sub_0009A250: cdecl (matrix, float min[3], float max[3]) -- the box's
 * corners transformed by a 4x4 row-major matrix, as a box again (Arvo): both
 * start at the translation row, and for each row i and column j the larger
 * of min[i]*M[i][j] and max[i]*M[i][j] goes to max[j], the smaller to min[j].
 * As the original: the max product is rounded to float before the compare
 * (it goes through memory), the sums are kept in extended (here double)
 * precision and stored as floats at the end, and an unordered compare adds
 * the min product to max. */
static void box_transform(uint32_t m, float mn[3], float mx[3])
{
    double hi[3], lo[3];
    int i, j;
    for (j = 0; j < 3; j++)
        hi[j] = lo[j] = (double)MEMF(m + 0x30u + 4u * j);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            double mm = (double)MEMF(m + 16u * i + 4u * j);
            double e = (double)mn[i] * mm;
            double f = (double)(float)((double)mx[i] * mm);
            if (e < f) { hi[j] += f; lo[j] += e; }
            else       { hi[j] += e; lo[j] += f; }
        }
    for (j = 0; j < 3; j++) {
        mn[j] = (float)lo[j];
        mx[j] = (float)hi[j];
    }
}

static uint32_t frustum_box_native(uint32_t self, uint32_t pmin, uint32_t pmax,
                                   uint32_t matrix, uint32_t *out_ecx, uint32_t *out_edx)
{
    const double K = (double)MEMF(0x003408BCu), K2 = (double)MEMF(0x0033FA8Cu);
    float mn[3] = { MEMF(pmin), MEMF(pmin + 4), MEMF(pmin + 8) };
    float mx[3] = { MEMF(pmax), MEMF(pmax + 4), MEMF(pmax + 8) };
    float ax, ay, az, bx, by, bz;
    if (matrix)
        box_transform(matrix, mn, mx);
    ax = mn[0]; ay = mn[1]; az = mn[2];
    bx = mx[0]; by = mx[1]; bz = mx[2];
    float cx = (float)(((double)bx + (double)ax) * K);
    float cy = (float)(((double)by + (double)ay) * K);
    float cz = (float)(((double)bz + (double)az) * K);
    float ex = (float)((double)bx - (double)cx);
    float ey = (float)((double)by - (double)cy);
    float ez = (float)((double)bz - (double)cz);
    uint32_t planes = MEM32(self) + 0x144u, k;
    int straddle = 0;

    for (k = 0; k < 6; k++) {
        uint32_t p = planes + 16u * k;
        double nx = MEMF(p - 4), ny = MEMF(p), nz = MEMF(p + 4), d = MEMF(p + 8);
        double r = (fabs(ny) * ey + fabs(nz) * ez) + fabs(nx) * ex;
        double dist = ((cy * ny + cz * nz) + cx * nx) + d;
        if (dist + r < K2) {
            *out_ecx = p;
            *out_edx = k + 1;
            return 0;
        }
        if (dist - r < K2)
            straddle = 1;
    }
    *out_ecx = planes + 96u;
    *out_edx = 7;
    return straddle ? 1u : 2u;
}

void sub_0009A330(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    static unsigned long calls, with_matrix, mismatches;
    uint32_t self = ecx, pmin = MEM32(esp + 4), pmax = MEM32(esp + 8);
    uint32_t matrix = MEM32(esp + 12), r, rc, rd;

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    calls++;
    with_matrix += matrix != 0;
    if (mode == 0) {
        sub_0009A330_gen();
        return;
    }
    r = frustum_box_native(self, pmin, pmax, matrix, &rc, &rd);
    if (mode == 2) {
        sub_0009A330_gen();             /* pops its own arguments */
        if (eax != r && mismatches++ < 20)
            fprintf(stderr, "[native] sub_0009A330 mismatch: lifted %u native %u "
                    "(box %08X-%08X this %08X)\n", eax, r, pmin, pmax, self);
        if ((calls & 0xFFFFF) == 0)
            fprintf(stderr, "[native] sub_0009A330: %lu calls, %lu with a matrix, "
                    "%lu mismatches\n", calls, with_matrix, mismatches);
        return;
    }
    eax = r;
    ecx = rc;
    edx = rd;
    esp += 16;                          /* return address + three arguments */
}

/* sub_0009A250 on its own (it has other callers): box_transform on guest
 * memory. Returns max (eax), as the original leaves it; cdecl. */
void sub_0009A250(void)
{
    static int mode = -1;
    uint32_t m = MEM32(esp + 4), pmin = MEM32(esp + 8), pmax = MEM32(esp + 12);
    float mn[3], mx[3];
    int j;

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (!mode) {
        sub_0009A250_gen();
        return;
    }
    for (j = 0; j < 3; j++) {
        mn[j] = MEMF(pmin + 4u * j);
        mx[j] = MEMF(pmax + 4u * j);
    }
    box_transform(m, mn, mx);
    if (mode == 2) {
        static unsigned long calls, bad;
        sub_0009A250_gen();             /* writes the guest's boxes itself */
        calls++;
        for (j = 0; j < 3; j++)
            if (memcmp(&mn[j], (const void *)XBOX_PTR(pmin + 4u * j), 4)
                || memcmp(&mx[j], (const void *)XBOX_PTR(pmax + 4u * j), 4)) {
                if (bad++ < 20)
                    fprintf(stderr, "[native] sub_0009A250 mismatch at %u: native %g %g,"
                            " lifted %g %g\n", j, mn[j], mx[j],
                            MEMF(pmin + 4u * j), MEMF(pmax + 4u * j));
                break;
            }
        if ((calls & 0xFFFFF) == 0)
            fprintf(stderr, "[native] sub_0009A250: %lu calls, %lu mismatches\n", calls, bad);
        return;
    }
    for (j = 0; j < 3; j++) {
        MEMF(pmin + 4u * j) = mn[j];
        MEMF(pmax + 4u * j) = mx[j];
    }
    eax = pmax;
    ecx = pmin + 12u;
    edx = m + 8u + 48u;
    esp += 4;                           /* cdecl: the caller pops the arguments */
}
void sub_002A9450(void) { crt_memmove(); }

/* ── D3D fence wait, D3D_BlockOnTime (0x002E8F20) ────────────
 *
 * stdcall (fence, flags), ret 8. The stock XDK 5849 D3D keeps its device at
 * 0x002F77A0, reached through the global at 0x002F7798:
 *   +0x2C  write sequence, bumped as fences are inserted
 *   +0x30  pointer to the semaphore the GPU releases as it completes work
 * and this spins until *(+0x30) catches up with the fence it was asked for.
 *
 * Without the pushbuffer executor nothing ever releases the semaphore, so the
 * GPU is reported caught up. With it (RECOMP_PB_EXEC) that would let D3D
 * reuse ring space and vertex memory the executor has not read yet (see the
 * toolkit's d3d8ltcg-device-context.md), so instead the real semaphore is
 * handed to the executor once and the original wait runs against it. */
#define NFSU2_D3D_DEVICE_PTR 0x002F7798u
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_002E8F20_gen(void);

void sub_002E8F20(void)
{
    static int exec = -1;
    uint32_t dev = MEM32(NFSU2_D3D_DEVICE_PTR);
    uint32_t sem = dev ? MEM32(dev + 0x30) : 0;

    if (exec < 0)
        exec = getenv("RECOMP_PB_EXEC") != NULL;
    if (exec) {
        static uint32_t registered;
        if (sem && sem != registered) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
            registered = sem;
        }
        if (MEM32(esp) == 0x002E953Cu && MEM32(esp + 12) == 0x000AEDDBu) {
            /* The main loop (sub_000AEA90) calls BlockOnFence on the fence
             * of the frame it just built, right before Present: the whole
             * frame has to be through the GPU before the next one starts,
             * so the game and the pushbuffer executor take turns instead
             * of overlapping (Eden race: game waited ~50% of the time,
             * executor ~20% idle). RECOMP_FRAME_LAG=1 waits for the
             * previous frame's fence instead: one frame in flight, as on a
             * PC. Race frames checked on Linux, no corruption. */
            static int lag = -1;
            static uint32_t prev;
            if (lag < 0) {
                const char *e = getenv("RECOMP_FRAME_LAG");
                lag = e && *e == '1';
                fprintf(stderr, "[D3D] frame fence: %s\n",
                        lag ? "previous frame (RECOMP_FRAME_LAG=1)" : "this frame");
            }
            if (lag) {
                uint32_t mine = MEM32(esp + 4);
                if (prev)
                    MEM32(esp + 4) = prev;
                prev = mine;
            }
        }
        sub_002E8F20_gen();
        return;
    }
    if (sem)
        MEM32(sem) = MEM32(dev + 0x2C);     /* "GPU" is always caught up */
    eax = 0;
    esp += 4 + 8;                           /* return address, 2 stdcall args */
}

/* ── D3D interrupt handlers at DISPATCH_LEVEL ────────────────
 *
 * D3D services the GPU from its DPC (0x002F2530) and, with the interrupt
 * masked, from its own busy-waits (BlockUntilIdle and friends) on the game
 * thread: both call the vblank handler (0x002F1D80) and the PGRAPH handler
 * (0x002F22F0), which queues flips. On the Xbox the two never interleave --
 * one is a DPC, the other runs with the interrupt off. Here the DPC runs on
 * the kernel timer thread, and lifted code hands over the guest lock at any
 * function entry, so the vblank count could move between D3D computing a
 * flip's target vblank and checking it. D3D retires a flip only when the
 * count *equals* its target, so that flip stayed pending forever and
 * PersistDisplay, which waits for pending flips, never returned: the hang
 * starting a Quick Race or Career. Run at DISPATCH_LEVEL (the dispatch lock,
 * kernel_hal.c), neither can be interrupted by the other.
 *
 * Raising blocks on the lock that the timer thread holds while it waits for
 * the guest lock, so the guest lock is let go for the raise, as a kernel
 * call would; already at DISPATCH (inside the DPC) there is nothing to take. */
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);

static uint8_t d3d_isr_enter(void)
{
    uint8_t old = xbox_CurrentIrql();

    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    return old;
}

static void d3d_isr_leave(uint8_t old)
{
    if (old < 2)
        xbox_KfLowerIrql(old);
}

/* Flips retired by a handler call: D3D's retire index, at +0x1BC of the
 * block both handlers take in ecx (their own reads index the pending-flip
 * slots from it). The pushbuffer executor's FLIP_STALL waits on these, as
 * PGRAPH waits on the retire's PGRAPH_INCREMENT write. */
extern void nv2a_pb_flip_retired(unsigned n);

/* Vblank handler, thiscall. It ends by writing PCRTC_INTR and spinning until
 * PMC_INTR bit 24 drops. Its callers (the DPC, D3D's busy-waits) have read
 * the bits before calling it, so this is the vblank being taken: clear them
 * now, and the spin ends at once instead of holding the guest lock until the
 * runtime's ack thread comes round. */
extern void sub_002F1D80_gen(void);

void sub_002F1D80(void)
{
    uint32_t blk = ecx;
    /* Read only once at DISPATCH: read before, a retire by the other
     * handler (DPC thread vs a D3D busy-wait) in between was counted by
     * both, the executor's flip index ran one off for good, and every
     * FLIP_STALL then waited out its 250 ms (Carbon, console, 3.7 fps). */
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + 0x1BC);

    xbox_Nv2aVblankTaken();
    sub_002F1D80_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);
    d3d_isr_leave(old);
}

/* PGRAPH handler, thiscall, ecx = the device's hardware block (+0 =
 * 0xFD000000). Reads a software-method trap from PGRAPH_INTR / NSOURCE /
 * TRAPPED_ADDR / TRAPPED_DATA, acknowledges it by writing PGRAPH_INTR back,
 * and acts on it: NOP(5) sets the event BlockOnTime sleeps on, a flip trap
 * queues the flip (sub_002F2080). The acknowledge is write-1-to-clear on
 * hardware and a no-op on RAM. Left pending, every turn of a busy-wait took
 * the same trap again (tens of thousands of flips a second, until the flip
 * targets were millions of vblanks ahead); guessed from the handler's
 * PGRAPH_FIFO writes, a trap posted during a call was sometimes dropped (the
 * fence event never set). So the executor posts traps under a lock held here
 * around the call, and a call that found one reports it taken, which clears
 * it (nv2a_pb_exec.c). */
extern void sub_002F22F0_gen(void);
extern void nv2a_pb_trap_lock(void);
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);

void sub_002F22F0(void)
{
    volatile uint32_t *nv = (volatile uint32_t *)XBOX_PTR(0xFD000000u);
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + 0x1BC);     /* at DISPATCH, as above */
    uint32_t nsource;

    nv2a_pb_trap_lock();
    nsource = nv[0x400108 / 4];
    sub_002F22F0_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);   /* a flip queued on its vblank retires at once */
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
    d3d_isr_leave(old);
}

/* ── Stream read, unlocked precheck (0x00072F90) ─────────────
 *
 * cdecl (stream). Checks a 'STRM' stream's byte count at +8 and reads its
 * buffer descriptor through +0xC -- before taking the stream's lock
 * (sub_00251C41, the call after the reads). The stream thread fills those
 * fields in at the same moment. On the Xbox's one CPU the gap is almost
 * never hit; with guest threads running in parallel, the reader saw a count
 * with +0xC still holding the allocator's 0xAA fill and dereferenced
 * 0xAAAAAAAA -- the crash after Start, nearly every time on a Switch, now
 * and then on a PC. A descriptor that is not a pointer yet means the stream
 * is not ready: answer as the function does with no data (0), and the caller
 * asks again. */
extern void sub_00072F90_gen(void);

void sub_00072F90(void)
{
    uint32_t stream = MEM32(esp + 4);

    if (stream && MEM32(stream + 8)) {
        uint32_t hdr = MEM32(stream);
        uint32_t desc = MEM32(stream + 0xC);
        if (hdr && MEM32(hdr) == 0x4D525453u                /* 'STRM' */
                && (desc < 0x10000u || desc == 0xAAAAAAAAu)) {
            /* Not ready: let the stream thread (which may be waiting for the
             * guest lock) get on with filling it in, then say "no data". */
            recomp_spin_yield();
            eax = 0;
            esp += 4;                   /* return address; the caller pops the argument */
            return;
        }
    }
    sub_00072F90_gen();
}

/* ── VP6 movie frames, sub_002618F0 ──────────────────────────
 *
 * cdecl (decoder, data, size, width, height): decodes one VP6 frame (the
 * payload of an EA MV0K/MV0F chunk) with On2's decoder. The decoder block:
 *   +0x1B0/+0x1B4  picture width/height    +0x1B8/+0x1BC  Y/UV stride
 *   +0x21C/+0x220/+0x224  Y/U/V offsets in a frame buffer
 *   +0x244  buffer being decoded   +0x254  last decoded (sub_0026144D's
 *           output, and the reference) -- swapped after every frame
 * Planes are bottom-up with a 48 (Y) / 24 (UV) pixel border for motion
 * vectors past the edge. The movie player (sub_0025F909) then passes +0x254
 * on to be drawn.
 *
 * Lifted, this is most of the CPU time of a movie on the Switch, so by
 * default FFmpeg decodes the frame (src/movie_vp6.c) into +0x244 and the
 * buffers are swapped as the original does. The border is not filled in:
 * only On2's own motion compensation read it. NFSU2_NATIVE_VP6=0 runs the
 * lifted decoder, =2 runs both and compares the pictures. */
#ifdef NFSU2_NATIVE_VP6
#include "movie_vp6.h"

extern void sub_002618F0_gen(void);

typedef struct {
    uint32_t y, u, v, y_stride, uv_stride, w, h;
} Vp6Planes;

static Vp6Planes vp6_planes(uint32_t dec, uint32_t buf)
{
    Vp6Planes p;
    p.y_stride = MEM32(dec + 0x1B8);
    p.uv_stride = MEM32(dec + 0x1BC);
    p.y = buf + MEM32(dec + 0x21C) + (p.y_stride + 1u) * 48u;
    p.u = buf + MEM32(dec + 0x220) + (p.uv_stride + 1u) * 24u;
    p.v = buf + MEM32(dec + 0x224) + (p.uv_stride + 1u) * 24u;
    p.w = MEM32(dec + 0x1B0);
    p.h = MEM32(dec + 0x1B4);
    return p;
}

/* NFSU2_NATIVE_VP6=2: FFmpeg into a scratch picture, compared with what the
 * lifted decoder left at +0x254. */
static void vp6_check(uint32_t dec, uint32_t data, uint32_t size)
{
    static unsigned long frames, bad;
    static uint8_t *pic;
    Vp6Planes p = vp6_planes(dec, MEM32(dec + 0x254));
    uint32_t cw = p.w / 2, row, diff = 0;

    if (p.w > 4096 || p.h > 4096)
        return;
    if (!pic && !(pic = malloc(4096u * 4096u * 3u / 2u)))
        return;
    if (nfsu2_vp6_decode(dec, (const uint8_t *)XBOX_PTR(data), (int)size,
                         pic, pic + p.w * p.h, pic + p.w * p.h + cw * (p.h / 2),
                         (int)p.w, (int)cw, (int)p.w, (int)p.h) == 0) {
        for (row = 0; row < p.h; row++)
            diff += memcmp(pic + row * p.w, (const void *)XBOX_PTR(p.y + row * p.y_stride), p.w) != 0;
        for (row = 0; row < p.h / 2; row++) {
            diff += memcmp(pic + p.w * p.h + row * cw,
                           (const void *)XBOX_PTR(p.u + row * p.uv_stride), cw) != 0;
            diff += memcmp(pic + p.w * p.h + cw * (p.h / 2) + row * cw,
                           (const void *)XBOX_PTR(p.v + row * p.uv_stride), cw) != 0;
        }
    } else {
        diff = 1;
    }
    frames++;
    if (diff && bad++ < 20)
        fprintf(stderr, "[movie] VP6 check: frame %lu (%u bytes) differs in %u rows\n",
                frames, size, diff);
    if (frames % 300 == 0)
        fprintf(stderr, "[movie] VP6 check: %lu frames, %lu differ\n", frames, bad);
}

void sub_002618F0(void)
{
    int mode = nfsu2_vp6_mode();
    uint32_t dec = MEM32(esp + 4), data = MEM32(esp + 8), size = MEM32(esp + 12);
    uint32_t cur;
    Vp6Planes p;

    if (mode != 1) {
        sub_002618F0_gen();
        if (mode == 2 && eax == 0)
            vp6_check(dec, data, size);
        return;
    }
    cur = MEM32(dec + 0x244);
    p = vp6_planes(dec, cur);
    if (nfsu2_vp6_decode(dec, (const uint8_t *)XBOX_PTR(data), (int)size,
                         (uint8_t *)XBOX_PTR(p.y), (uint8_t *)XBOX_PTR(p.u),
                         (uint8_t *)XBOX_PTR(p.v), (int)p.y_stride, (int)p.uv_stride,
                         (int)p.w, (int)p.h) == 0) {
        MEM32(dec + 0x244) = MEM32(dec + 0x254);
        MEM32(dec + 0x254) = cur;
    }
    /* A frame FFmpeg rejects leaves the last picture up. */
    MEM32(dec + 0x1E8) = size;
    MEM32(0x00469D04u) += 1;            /* the decoder's frame counter */
    eax = 0;
    esp += 4;                           /* cdecl: the caller pops the arguments */
}
#else
/* Built without FFmpeg (NFSU2_FFMPEG_DIR): the lifted decoder. */
extern void sub_002618F0_gen(void);
void sub_002618F0(void) { sub_002618F0_gen(); }
#endif

/* ── 4x4 matrix multiply, sub_002213EA ─────────────────────────
 *
 * stdcall (out, a, b), ret 12, eax = out: out = a * b, row-major floats, in
 * SSE (shufps a[i][j] across the row, mulps by row j of b, addps). Every
 * xmm register of the lifted body is thread-local, so each call was ~100
 * TLS accesses (calls on Horizon). sub_000A2EA0 calls it twice per object
 * drawn; at a drag start line (Coastal Express, ~2300 draws a frame) it was
 * 15-18% of the main thread on x86, native -22% main-thread time per frame.
 * Same sums in the same order, ((p0 + p1) + p2) + p3, no FMA contraction;
 * all rows are computed before the store, as out may alias a or b.
 * RECOMP_NATIVE=0 lifted, RECOMP_NATIVE_CHECK=1 both and compare (Linux race:
 * 0 mismatches in 4.4M calls). */
extern void sub_002213EA_gen(void);

__attribute__((optimize("fp-contract=off")))
static void mat4_mul(float *o, const float *a, const float *b)
{
    float r[16];
    int i, k;
    for (i = 0; i < 4; i++)
        for (k = 0; k < 4; k++) {
            float s = a[4 * i] * b[k];
            s = s + a[4 * i + 1] * b[4 + k];
            s = s + a[4 * i + 2] * b[8 + k];
            s = s + a[4 * i + 3] * b[12 + k];
            r[4 * i + k] = s;
        }
    memcpy(o, r, sizeof r);
}

void sub_002213EA(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    uint32_t out = MEM32(esp + 4), a = MEM32(esp + 8), b = MEM32(esp + 12);

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (mode == 0) {
        sub_002213EA_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        float o[16], ma[16], mb[16];
        memcpy(ma, (const void *)XBOX_PTR(a), 64);
        memcpy(mb, (const void *)XBOX_PTR(b), 64);
        mat4_mul(o, ma, mb);
        sub_002213EA_gen();             /* pops its own arguments */
        calls++;
        if (memcmp(o, (const void *)XBOX_PTR(out), 64) && bad++ < 20)
            fprintf(stderr, "[native] sub_002213EA mismatch: out %08X a %08X b %08X\n",
                    out, a, b);
        if ((calls & 0xFFFFF) == 0)
            fprintf(stderr, "[native] sub_002213EA: %lu calls, %lu mismatches\n", calls, bad);
        return;
    }
    mat4_mul((float *)XBOX_PTR(out), (const float *)XBOX_PTR(a), (const float *)XBOX_PTR(b));
    eax = out;
    esp += 16;                          /* return address + three arguments */
}

/* ── Float to integer, sub_002A68EC (_ftol2) ──────────────────
 *
 * MSVC's CRT _ftol2: st(0) truncated to a 64-bit integer in edx:eax, popped.
 * Every (int)float cast in the title calls it (589 sites); the hottest
 * function of the main thread in a Linux race (4.4%). It rounds with fistp
 * (the control word's mode), then corrects towards zero by the sign of the
 * float (x - r): x >= 0 and x - r < 0 -> r - 1; x < 0 (sign of (float)x)
 * and x - r > 0 -> r + 1. r == 0 or the integer indefinite is returned as
 * is. Exact, including ecx, the frame it publishes and the x87 top.
 * RECOMP_NATIVE=0 lifted, RECOMP_NATIVE_CHECK=1 both and compare. */
extern void sub_002A68EC_gen(void);

__attribute__((noinline, cold)) static int64_t ftol_fist_slow(double x, uint16_t cw)
{
    return recomp_fist(x, cw, 64);
}

static inline int64_t ftol_fist(double x, uint16_t cw)
{
    if (__builtin_expect(((cw >> 10) & 3u) == 0 && fabs(x) < 0x1p63, 1))
        return (int64_t)rint(x);        /* host rounding is never changed: nearest */
    return ftol_fist_slow(x, cw);       /* other modes, NaN, out of range */
}

static inline void ftol2_native(double x, uint32_t *peax, uint32_t *pedx, uint32_t *pecx)
{
    int64_t r = ftol_fist(x, g_fp_control_word);
    uint32_t lo = (uint32_t)r, hi = (uint32_t)((uint64_t)r >> 32), d;
    float fx = (float)x, fd;
    uint32_t sx;

    if (lo == 0 && (hi & 0x7FFFFFFFu) == 0) {   /* 0 or the indefinite */
        *peax = lo;
        *pedx = hi;
        return;
    }
    fd = (float)(x - (double)r);
    memcpy(&d, &fd, 4);
    memcpy(&sx, &fx, 4);
    if (sx & 0x80000000u) {
        d ^= 0x80000000u;
        r += d >= 0x80000001u;          /* carry of d + 0x7FFFFFFF */
    } else {
        r -= d >= 0x80000001u;          /* borrow */
    }
    *peax = (uint32_t)r;
    *pedx = (uint32_t)((uint64_t)r >> 32);
    *pecx = d + 0x7FFFFFFFu;
}

/* Modes other than plain native, out of line so that the hot path below is
 * small enough for LTO to inline into its 581 lifted callers. */
static int s_ftol_mode = -1;            /* 0 lifted, 1 native, 2 native + check */

__attribute__((noinline, cold)) static void ftol2_slow(void)
{
    double x = g_fp_stack[g_fp_top & 7];
    uint32_t a, d, c = ecx, frame = esp - 4u;

    if (s_ftol_mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *k = getenv("RECOMP_NATIVE_CHECK");
        s_ftol_mode = (e && *e == '0') ? 0 : (k && *k == '1') ? 2 : 1;
    }
    if (s_ftol_mode == 0) {
        sub_002A68EC_gen();
        return;
    }
    ftol2_native(x, &a, &d, &c);
    if (s_ftol_mode == 2) {
        static unsigned long calls, bad;
        int top = (g_fp_top + 1) & 7;
        uint32_t sp = esp + 4u;
        sub_002A68EC_gen();
        calls++;
        if ((eax != a || edx != d || ecx != c || g_fp_top != top || esp != sp
             || g_ebp != frame) && bad++ < 20)
            fprintf(stderr, "[native] sub_002A68EC mismatch for %.17g: lifted %08X:%08X "
                    "ecx %08X, native %08X:%08X ecx %08X\n", x, edx, eax, ecx, d, a, c);
        if ((calls & 0xFFFFFF) == 0)
            fprintf(stderr, "[native] sub_002A68EC: %lu calls, %lu mismatches\n", calls, bad);
        return;
    }
    eax = a;
    edx = d;
    ecx = c;
    g_fp_top = (g_fp_top + 1) & 7;
    g_ebp = g_seh_ebp = frame;
    esp += 4;
}

inline void sub_002A68EC(void)
{
    uint32_t a, d, c, frame;
    int top;

    if (__builtin_expect(s_ftol_mode != 1, 0)) {
        ftol2_slow();                   /* first call, lifted or check mode */
        return;
    }
    top = g_fp_top;
    c = ecx;
    frame = esp - 4u;
    ftol2_native(g_fp_stack[top & 7], &a, &d, &c);
    eax = a;
    edx = d;
    ecx = c;
    g_fp_top = (top + 1) & 7;
    g_ebp = g_seh_ebp = frame;          /* as its push ebp / mov ebp, esp left them */
    esp += 4;                           /* return address */
}

/* ── Small math leaves of the main thread ─────────────────────
 *
 * Each about 0.3-0.8% of the main thread in a Linux race, more on the
 * console, where every x87 slot and register the lifted bodies touch is
 * thread-local. Exact as the lifted code computes them (x87 values as
 * doubles, rounded to float where the original stores), including the
 * registers, flags and frame they leave behind. RECOMP_NATIVE=0 lifted,
 * RECOMP_NATIVE_CHECK=1 both and compare. */
static int native_mode(void)
{
    const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
    return (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
}

static void native_report(const char *name, unsigned long *calls, unsigned long *bad, int ok,
                          uint32_t a0, uint32_t a1, uint32_t a2)
{
    ++*calls;
    if (!ok && (*bad)++ < 20)
        fprintf(stderr, "[native] %s mismatch: args %08X %08X %08X\n", name, a0, a1, a2);
    if ((*calls & 0xFFFFF) == 0 || *calls == 4096)
        fprintf(stderr, "[native] %s: %lu calls, %lu mismatches\n", name, *calls, *bad);
}

/* sub_0004BC20: cdecl (out, m, v) -- out = v * m, m a row-major 4x4 with
 * the translation in row 3; all three sums before the stores. */
extern void sub_0004BC20_gen(void);

static void xform_point(uint32_t m, uint32_t v, float o[3])
{
    double v0 = MEMF(v), v1 = MEMF(v + 4), v2 = MEMF(v + 8);
    o[0] = (float)(((MEMF(m + 0x20) * v2 + MEMF(m + 0x10) * v1) + MEMF(m + 0x00) * v0)
                   + MEMF(m + 0x30));
    o[1] = (float)(((MEMF(m + 0x24) * v2 + MEMF(m + 0x04) * v0) + MEMF(m + 0x14) * v1)
                   + MEMF(m + 0x34));
    o[2] = (float)(((MEMF(m + 0x28) * v2 + MEMF(m + 0x08) * v0) + MEMF(m + 0x18) * v1)
                   + MEMF(m + 0x38));
}

void sub_0004BC20(void)
{
    static int mode = -1;
    uint32_t out = MEM32(esp + 4), m = MEM32(esp + 8), v = MEM32(esp + 12), e0 = esp;
    float o[3];

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004BC20_gen();
        return;
    }
    xform_point(m, v, o);
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t sv_edx = edx;
        int top = g_fp_top;
        sub_0004BC20_gen();
        native_report("sub_0004BC20", &calls, &bad,
                      !memcmp(o, (const void *)XBOX_PTR(out), 12) && eax == out && ecx == v
                      && edx == sv_edx && g_fp_top == top && esp == e0 + 4u
                      && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u, out, m, v);
        return;
    }
    memcpy((void *)XBOX_PTR(out), o, 12);
    eax = out;
    ecx = v;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* sub_0004B260: cdecl (uint16 angle) -> st(0): a = angle * C, reduced to
 * x in range with a sign s, then s * (x - x^3 D1 + x^5 D2 - x^7 D3 + x^9 D4)
 * (a sine). Also writes the zero-extended angle back to its argument slot
 * and leaves fnstsw's word of the last compare in ax. */
extern void sub_0004B260_gen(void);

static double sine16(uint32_t angle, uint32_t *peax, int top0)
{
    const double a = (double)(int32_t)angle * MEMF(0x0034089Cu);
    double s = MEMF(0x003408ACu), x = a;
    int cmp = RECOMP_FCMP(a, (double)MEMF(0x00343644u));
    uint16_t cc = RECOMP_FCMP_CC(cmp);

    if (!(cc & 0x0100u)) {
        x = a - MEMF(0x00340AFCu);
    } else {
        cmp = RECOMP_FCMP(a, (double)MEMF(0x003408B0u));
        cc = RECOMP_FCMP_CC(cmp);
        if (!(cc & 0x0100u)) {
            x = a - MEMF(0x00343640u);
            s = MEMF(0x003408FCu);
        }
    }
    g_fp_cmp = cmp;
    g_fp_cc = cc;
    *peax = (uint32_t)(uint16_t)((((top0 + 6) & 7u) << 11) | cc);
    double x2 = x * x, x3 = x2 * x, x5 = x3 * x2, x7 = x5 * x2, x9 = x7 * x2;
    double acc = x - x3 * MEMF(0x0034363Cu);
    acc = acc + x5 * MEMF(0x00343638u);
    acc = acc - x7 * MEMF(0x00343634u);
    acc = acc + x9 * MEMF(0x00343630u);
    return acc * s;
}

void sub_0004B260(void)
{
    static int mode = -1;
    uint32_t angle = MEM16(esp + 4), e0 = esp, a;
    int top0 = g_fp_top;
    double r;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004B260_gen();
        return;
    }
    r = sine16(angle, &a, top0);
    if (mode == 2) {
        static unsigned long calls, bad;
        int cmp = g_fp_cmp;
        uint16_t cc = g_fp_cc;
        sub_0004B260_gen();
        native_report("sub_0004B260", &calls, &bad,
                      !memcmp(&r, &g_fp_stack[g_fp_top & 7], 8) && g_fp_top == ((top0 + 7) & 7)
                      && eax == a && g_fp_cmp == cmp && g_fp_cc == cc && esp == e0 + 4u
                      && MEM32(e0 + 4u) == angle && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u,
                      angle, 0, 0);
        return;
    }
    MEM32(e0 + 4u) = angle;
    g_fp_top = (top0 + 7) & 7;
    g_fp_stack[g_fp_top] = r;
    eax = a;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* sub_001C6900: cdecl (obj, const float p[3], float r) -> int. d = p - c
 * (c at obj+0x60); if d.n (n at obj+0x70) < -r: 0. Otherwise L = |d| - r
 * (as a float) and v = L > r ? A / L * r : A (A at obj+0x80), truncated
 * (cvttss2si, lifted as a cast). Frame aligned to 16; xmm0 is left as its
 * movups of the frame (v, L, caller's frame, return address). */
extern void sub_001C6900_gen(void);

static int32_t sphere_fade(uint32_t obj, uint32_t p, float r, int *pcmp, uint16_t *pcc,
                           float *pv, float *pl)
{
    double dx = (double)MEMF(p) - MEMF(obj + 0x60);
    double dy = (double)MEMF(p + 4) - MEMF(obj + 0x64);
    double dz = (double)MEMF(p + 8) - MEMF(obj + 0x68);
    double dot = (dy * MEMF(obj + 0x74) + dz * MEMF(obj + 0x78)) + dx * MEMF(obj + 0x70);
    double nr = -(double)r;
    float l, v;

    *pcmp = RECOMP_FCMP(nr, dot);
    *pcc = RECOMP_FCMP_CC(*pcmp);
    if (!(*pcc & 0x4100u))
        return -1;                      /* outside: returns 0, xmm0 untouched */
    l = (float)(sqrt((dx * dx + dy * dy) + dz * dz) - r);
    *pcmp = RECOMP_FCMP((double)l, (double)r);
    *pcc = RECOMP_FCMP_CC(*pcmp);
    v = (*pcc & 0x4100u) ? MEMF(obj + 0x80)
                         : (float)((double)MEMF(obj + 0x80) / l * r);
    *pv = v;
    *pl = l;
    return 0;
}

void sub_001C6900(void)
{
    static int mode = -1;
    uint32_t e0 = esp, obj = MEM32(esp + 4), p = MEM32(esp + 8), rv = MEM32(esp + 12);
    uint32_t frame = (e0 - 12u) & ~15u, res, sv_edx = edx;
    uint32_t caller_ebp = g_seh_ebp, ret = MEM32(e0);
    float r, v = 0, l = 0;
    int cmp, top = g_fp_top;
    uint16_t cc;
    RecompXmm x0;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_001C6900_gen();
        return;
    }
    memcpy(&r, &rv, 4);
    x0 = g_xmm0;
    if (sphere_fade(obj, p, r, &cmp, &cc, &v, &l) == 0) {
        res = (uint32_t)(int32_t)v;
        x0.f[0] = v;
        x0.f[1] = l;
        x0.u[2] = caller_ebp;
        x0.u[3] = ret;
    } else {
        res = 0;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        sub_001C6900_gen();
        native_report("sub_001C6900", &calls, &bad,
                      eax == res && ecx == obj && edx == sv_edx && g_fp_top == top
                      && g_fp_cmp == cmp && g_fp_cc == cc && !memcmp(&x0, &g_xmm0, 16)
                      && esp == e0 + 4u && g_ebp == frame && g_seh_ebp == frame, obj, p, rv);
        return;
    }
    eax = res;
    ecx = obj;
    g_fp_cmp = cmp;
    g_fp_cc = cc;
    g_xmm0 = x0;
    g_ebp = g_seh_ebp = frame;
    esp += 4;
}

/* sub_000113A0: cdecl (dst, src) -- a 4x4 float matrix copied row by row
 * (each row read before it is written; the middle two of each row through
 * the x87, as doubles). Its argument slot src ends as src[15]'s bits. */
extern void sub_000113A0_gen(void);

void sub_000113A0(void)
{
    static int mode = -1;
    uint32_t e0 = esp, dst = MEM32(esp + 4), src = MEM32(esp + 8), r;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_000113A0_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t want[16];
        for (r = 0; r < 16; r++) {
            volatile double f = MEMF(src + 4u * r);
            float g = (float)f;
            if ((r & 3) == 1 || (r & 3) == 2)
                memcpy(&want[r], &g, 4);
            else
                want[r] = MEM32(src + 4u * r);
        }
        sub_000113A0_gen();
        native_report("sub_000113A0", &calls, &bad,
                      !memcmp(want, (const void *)XBOX_PTR(dst), 64) && eax == dst
                      && ecx == want[12] && edx == want[15] && MEM32(e0 + 8u) == want[15]
                      && esp == e0 + 4u && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u,
                      dst, src, 0);
        return;
    }
    for (r = 0; r < 64; r += 16) {
        uint32_t w0 = MEM32(src + r), w3 = MEM32(src + r + 12);
        volatile double f1 = MEMF(src + r + 4), f2 = MEMF(src + r + 8);
        MEMF(dst + r + 4) = (float)f1;
        MEM32(dst + r) = w0;
        MEMF(dst + r + 8) = (float)f2;
        MEM32(dst + r + 12) = w3;
    }
    MEM32(e0 + 8u) = MEM32(src + 0x3C);
    eax = dst;
    ecx = MEM32(src + 0x30);
    edx = MEM32(src + 0x3C);
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* x87 compare as the lifted code records it; with sw, also fnstsw ax. */
static inline uint16_t fcmp_rec(double a, double b, int top, int sw)
{
    g_fp_cmp = RECOMP_FCMP(a, b);
    g_fp_cc = RECOMP_FCMP_CC(g_fp_cmp);
    if (sw)
        eax = (eax & 0xFFFF0000u) | (uint16_t)(((top & 7u) << 11) | g_fp_cc);
    return g_fp_cc;
}

/* sub_001C3430: thiscall (poly, const float p[2]) -> st(0), ret 4. Edge
 * functions e = (b.y - a.y)(p.x - a.x) - (p.y - a.y)(b.x - a.x) over the
 * corners at +0x30/+0x40/+0x50/+0x60 (a fourth edge when [poly+9] == 4);
 * returns their maximum, starting from K (0x3408FC) unless e1 is past it,
 * as soon as it is above Z (0x33FA8C). Each e goes through the argument
 * slot as a float. */
extern void sub_001C3430_gen(void);

static inline double poly_edge(uint32_t o, uint32_t p, uint32_t a, uint32_t b)
{
    return (float)((((double)MEMF(o + b + 4) - MEMF(o + a + 4))
                    * ((double)MEMF(p) - MEMF(o + a)))
                   - (((double)MEMF(p + 4) - MEMF(o + a + 4))
                      * ((double)MEMF(o + b) - MEMF(o + a))));
}

static double poly_max_edge(uint32_t o, uint32_t p, uint32_t slot, int t1)
{
    static const uint8_t edges[3][2] = { { 0x40, 0x50 }, { 0x60, 0x30 }, { 0x50, 0x60 } };
    const double k = MEMF(0x003408FCu), z = MEMF(0x0033FA8Cu);
    float e = (float)poly_edge(o, p, 0x30, 0x40);
    double m;
    int i;

    MEMF(slot) = e;
    if (!(fcmp_rec(k, e, t1, 1) & 0x4100u)) {
        m = k;
    } else {
        if (!(fcmp_rec(e, z, t1, 1) & 0x4100u))
            return e;
        m = e;
    }
    for (i = 0; i < 3; i++) {
        if (i == 2 && MEM8(o + 9) != 4)
            return m;
        e = (float)poly_edge(o, p, edges[i][0], edges[i][1]);
        MEMF(slot) = e;
        if (fcmp_rec(m, e, t1, 1) & 0x4100u)
            m = e;
        if (!(fcmp_rec(m, z, t1, i < 2) & 0x4100u) || i == 2)
            return m;
    }
    return m;
}

void sub_001C3430(void)
{
    static int mode = -1;
    uint32_t e0 = esp, o = ecx, p = MEM32(esp + 4);
    int top0 = g_fp_top, t1 = (top0 + 7) & 7;
    double r;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_001C3430_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t a0 = eax, a, sl;
        int cmp;
        uint16_t cc;
        r = poly_max_edge(o, p, e0 + 4u, t1);
        a = eax; cmp = g_fp_cmp; cc = g_fp_cc; sl = MEM32(e0 + 4u);
        eax = a0;
        MEM32(e0 + 4u) = p;
        sub_001C3430_gen();
        native_report("sub_001C3430", &calls, &bad,
                      !memcmp(&r, &g_fp_stack[t1], 8) && g_fp_top == t1 && eax == a
                      && g_fp_cmp == cmp && g_fp_cc == cc && MEM32(e0 + 4u) == sl
                      && ecx == o && edx == p && esp == e0 + 8u
                      && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u, o, p, 0);
        return;
    }
    r = poly_max_edge(o, p, e0 + 4u, t1);
    g_fp_top = t1;
    g_fp_stack[t1] = r;
    edx = p;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 8;                           /* return address + one argument */
}

/* sub_0004C6E0: cdecl (const float p[2], const float s[2], const float q[2],
 * float m) -> 1 when q lies within [p - m, p + s + m] on both axes. */
extern void sub_0004C6E0_gen(void);

void sub_0004C6E0(void)
{
    static int mode = -1;
    uint32_t e0 = esp, p = MEM32(esp + 4), s = MEM32(esp + 8), q = MEM32(esp + 12);
    uint32_t sv_eax = eax, sv_edx = edx, res = 0, a, d = edx;
    double m = MEMF(esp + 16);
    int top = g_fp_top, k, cmp;
    uint16_t cc;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004C6E0_gen();
        return;
    }
    for (k = 0; k < 2; k++) {
        if (!(fcmp_rec((double)MEMF(p + 4u * k) - m, MEMF(q + 4u * k), top, 1) & 0x4100u))
            break;
        d = s;
        if ((fcmp_rec(m + MEMF(s + 4u * k), MEMF(q + 4u * k), top, 1) & 0x4500u) == 0x0100u)
            break;
    }
    res = k == 2;
    a = res; cmp = g_fp_cmp; cc = g_fp_cc;
    if (mode == 2) {
        static unsigned long calls, bad;
        eax = sv_eax;
        edx = sv_edx;
        sub_0004C6E0_gen();
        native_report("sub_0004C6E0", &calls, &bad,
                      eax == a && ecx == q && edx == d && g_fp_cmp == cmp && g_fp_cc == cc
                      && g_fp_top == top && esp == e0 + 4u
                      && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u, p, s, q);
        return;
    }
    eax = res;
    ecx = q;
    edx = d;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* sub_001C65D0: thiscall (obj, float out[12]), ret 4: three rows of int16
 * (obj+0x2C..0x3C) times S (0x35D678) as floats, each row padded with a
 * zero dword. Its argument slot ends as the last int16 read. */
extern void sub_001C65D0_gen(void);

void sub_001C65D0(void)
{
    static int mode = -1;
    uint32_t e0 = esp, o = ecx, out = MEM32(esp + 4), r;
    const double sc = MEMF(0x0035D678u);
    float v[12];
    int top = g_fp_top;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_001C65D0_gen();
        return;
    }
    for (r = 0; r < 3; r++) {
        uint32_t b = o + 0x2Cu + 6u * r;
        v[4 * r]     = (float)((double)(int16_t)MEM16(b) * sc);
        v[4 * r + 1] = (float)((double)(int16_t)MEM16(b + 2) * sc);
        v[4 * r + 2] = (float)((double)(int16_t)MEM16(b + 4) * sc);
        memset(&v[4 * r + 3], 0, 4);
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t sv_esi = esi;
        sub_001C65D0_gen();
        native_report("sub_001C65D0", &calls, &bad,
                      !memcmp(v, (const void *)XBOX_PTR(out), 48) && eax == out
                      && ecx == (uint32_t)(int32_t)(int16_t)MEM16(o + 0x38) && edx == 0
                      && esi == sv_esi && MEM32(e0 + 4u) == ecx && g_fp_top == top
                      && esp == e0 + 8u && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u,
                      o, out, 0);
        return;
    }
    memcpy((void *)XBOX_PTR(out), v, 48);
    eax = out;
    ecx = (uint32_t)(int32_t)(int16_t)MEM16(o + 0x38);
    edx = 0;
    MEM32(e0 + 4u) = ecx;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 8;
}

/* sub_002EEA80: D3D's SSE 4x4 multiply, stdcall (out, a, b), ret 12 --
 * the same sums as sub_002213EA (mat4_mul). Leaves the result rows in
 * xmm2..xmm5 and a[3][2] * b row 2 / a[3][3] * b row 3 in xmm0 / xmm1;
 * eax = a, ecx = out. */
extern void sub_002EEA80_gen(void);

__attribute__((optimize("fp-contract=off")))
static void d3d_mat_mul(uint32_t out, uint32_t a, uint32_t b, RecompXmm x[6])
{
    float ma[16], mb[16], o[16];
    int k;
    memcpy(ma, (const void *)XBOX_PTR(a), 64);
    memcpy(mb, (const void *)XBOX_PTR(b), 64);
    mat4_mul(o, ma, mb);
    for (k = 0; k < 4; k++) {
        x[0].f[k] = ma[14] * mb[8 + k];
        x[1].f[k] = ma[15] * mb[12 + k];
    }
    memcpy(&x[2], o, 64);
}

void sub_002EEA80(void)
{
    static int mode = -1;
    uint32_t e0 = esp, out = MEM32(esp + 4), a = MEM32(esp + 8), b = MEM32(esp + 12);
    RecompXmm x[6];

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_002EEA80_gen();
        return;
    }
    d3d_mat_mul(out, a, b, x);
    if (mode == 2) {
        static unsigned long calls, bad;
        sub_002EEA80_gen();
        native_report("sub_002EEA80", &calls, &bad,
                      !memcmp(&x[2], (const void *)XBOX_PTR(out), 64) && eax == a && ecx == out
                      && !memcmp(&x[0], &g_xmm0, 16) && !memcmp(&x[1], &g_xmm1, 16)
                      && !memcmp(&x[2], &g_xmm2, 16) && !memcmp(&x[3], &g_xmm3, 16)
                      && !memcmp(&x[4], &g_xmm4, 16) && !memcmp(&x[5], &g_xmm5, 16)
                      && esp == e0 + 16u, out, a, b);
        return;
    }
    memcpy((void *)XBOX_PTR(out), &x[2], 64);
    g_xmm0 = x[0]; g_xmm1 = x[1]; g_xmm2 = x[2];
    g_xmm3 = x[3]; g_xmm4 = x[4]; g_xmm5 = x[5];
    eax = a;
    ecx = out;
    esp += 16;                          /* return address + three arguments */
}

/* sub_0004B940: cdecl (out, a, b): t = b * a (row-major), each element
 * summed in its own order (as the compiler scheduled it), then copied to
 * out by sub_000113A0 -- whose frame, ecx and edx it leaves behind. */
extern void sub_0004B940_gen(void);

static void mat_mul_ba(const float *A, const float *B, float *t)
{
    t[0] = (float)((((double)A[0] * B[0] + (double)B[3] * A[12]) + (double)B[2] * A[8]) + (double)B[1] * A[4]);
    t[1] = (float)((((double)A[13] * B[3] + (double)A[1] * B[0]) + (double)B[1] * A[5]) + (double)A[9] * B[2]);
    t[2] = (float)((((double)A[10] * B[2] + (double)A[6] * B[1]) + (double)A[2] * B[0]) + (double)B[3] * A[14]);
    t[3] = (float)((((double)A[7] * B[1] + (double)A[15] * B[3]) + (double)B[2] * A[11]) + (double)B[0] * A[3]);
    t[4] = (float)((((double)B[6] * A[8] + (double)A[4] * B[5]) + (double)B[7] * A[12]) + (double)B[4] * A[0]);
    t[5] = (float)((((double)B[6] * A[9] + (double)B[4] * A[1]) + (double)B[5] * A[5]) + (double)B[7] * A[13]);
    t[6] = (float)((((double)B[7] * A[14] + (double)B[6] * A[10]) + (double)A[6] * B[5]) + (double)B[4] * A[2]);
    t[7] = (float)((((double)A[15] * B[7] + (double)B[6] * A[11]) + (double)B[4] * A[3]) + (double)A[7] * B[5]);
    t[8] = (float)((((double)B[10] * A[8] + (double)B[9] * A[4]) + (double)B[11] * A[12]) + (double)B[8] * A[0]);
    t[9] = (float)((((double)B[10] * A[9] + (double)B[8] * A[1]) + (double)B[9] * A[5]) + (double)B[11] * A[13]);
    t[10] = (float)((((double)B[11] * A[14] + (double)B[10] * A[10]) + (double)A[6] * B[9]) + (double)B[8] * A[2]);
    t[11] = (float)((((double)A[15] * B[11] + (double)B[10] * A[11]) + (double)B[8] * A[3]) + (double)A[7] * B[9]);
    t[12] = (float)((((double)B[14] * A[8] + (double)B[13] * A[4]) + (double)B[15] * A[12]) + (double)B[12] * A[0]);
    t[13] = (float)((((double)B[14] * A[9] + (double)B[12] * A[1]) + (double)B[13] * A[5]) + (double)B[15] * A[13]);
    t[14] = (float)((((double)B[15] * A[14] + (double)B[14] * A[10]) + (double)A[6] * B[13]) + (double)B[12] * A[2]);
    t[15] = (float)((((double)A[15] * B[15] + (double)B[14] * A[11]) + (double)B[12] * A[3]) + (double)A[7] * B[13]);
}

void sub_0004B940(void)
{
    static int mode = -1;
    uint32_t e0 = esp, out = MEM32(esp + 4), a = MEM32(esp + 8), b = MEM32(esp + 12);
    uint32_t frame = ((e0 - 12u) & ~15u) - 0x50u, w12, w15;
    float A[16], B[16], t[16];
    int top = g_fp_top;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004B940_gen();
        return;
    }
    memcpy(A, (const void *)XBOX_PTR(a), 64);
    memcpy(B, (const void *)XBOX_PTR(b), 64);
    mat_mul_ba(A, B, t);
    memcpy(&w12, &t[12], 4);
    memcpy(&w15, &t[15], 4);
    if (mode == 2) {
        static unsigned long calls, bad;
        sub_0004B940_gen();
        native_report("sub_0004B940", &calls, &bad,
                      !memcmp(t, (const void *)XBOX_PTR(out), 64) && eax == out && ecx == w12
                      && edx == w15 && g_fp_top == top && esp == e0 + 4u
                      && g_ebp == frame && g_seh_ebp == frame, out, a, b);
        return;
    }
    memcpy((void *)XBOX_PTR(out), t, 64);
    eax = out;
    ecx = w12;
    edx = w15;
    g_ebp = g_seh_ebp = frame;
    esp += 4;
}

/* ── Colour grade table, sub_000A3CA0 ─────────────────────────
 *
 * cdecl (k): rebuilds grade table k (0/1) -- N x N A8R8G8B8 texels, N =
 * [0x39D324], swizzled (masks as sub_00098D70 makes them for N x N x 0),
 * at [0x3E8C18] + 4 * N * N * k -- from its four curve parameters at
 * 0x39D304 + 16k (P0..P3), and caches them at 0x3E8BF0 + 16k. Its callers
 * (sub_000A3FA0) rebuild whenever a parameter changed, which in races is
 * every frame: 2% of the main thread on x86, each texel four divisions and
 * four _ftol2 calls. Texel (i, j): channels from x = i/N (A from P3, B from
 * P1, as floats), y = j/N (R from P0 as a float, the last byte from P2
 * unrounded -- it never leaves the x87 stack), each through
 *   t = (K1 - 2v) * (K1 / P - K3)   (stored as a float)
 *   v < K2 ? v / (t + K1) : (t - v) / (t - K1)   (NaN: the second)
 * times K4, truncated. Exact, as the lifted code computes it (doubles,
 * float rounding where it stores). RECOMP_NATIVE=0 lifted,
 * RECOMP_NATIVE_CHECK=1 both and compare. */
extern void sub_000A3CA0_gen(void);

static inline double grade_curve(double v, double p, double k1, double k2, double k3)
{
    double t = (float)((k1 - (v + v)) * (k1 / p - k3));
    return v < k2 ? v / (t + k1) : (t - v) / (t - k1);
}

static inline uint32_t grade_byte(double v)
{
    uint32_t a, d, c = 0;
    ftol2_native(v, &a, &d, &c);
    return a & 0xFFu;
}

/* Fills out[] (N * N texels at their swizzled offsets) for table k. */
static void grade_table(uint32_t *out, uint32_t k, int32_t n)
{
    const double k1 = MEMF(0x003408ACu), k2 = MEMF(0x003408BCu);
    const double k3 = MEMF(0x00343F00u), k4 = MEMF(0x00343658u);
    const uint32_t pb = 0x0039D304u + 16u * k;
    const double p0 = MEMF(pb), p1 = MEMF(pb + 4), p2 = MEMF(pb + 8), p3 = MEMF(pb + 12);
    const double nf = (float)n;
    uint32_t mu = 0, mv = 0, bit = 1, lvl, us = 0, vs, base = (uint32_t)(n * n) * k;
    int32_t i, j;

    for (lvl = 1;; lvl <<= 1) {         /* sub_00098D70(n, n, 0) */
        int any = 0;
        if (lvl < (uint32_t)n) { mu |= bit; bit <<= 1; any = 1; }
        if (lvl < (uint32_t)n) { mv |= bit; bit <<= 1; any = 1; }
        if (!any)
            break;
    }
    for (i = 0; i < n; i++, us = (us - mu) & mu) {
        double x = (double)i / (double)n;
        uint32_t ca = grade_byte((double)(float)grade_curve((float)x, p3, k1, k2, k3) * k4);
        uint32_t cb = grade_byte((double)(float)grade_curve((float)x, p1, k1, k2, k3) * k4);
        for (j = 0, vs = 0; j < n; j++, vs = (vs - mv) & mv) {
            double y = (double)j / nf;
            uint32_t cr = grade_byte((double)(float)grade_curve((float)y, p0, k1, k2, k3) * k4);
            uint32_t cd = grade_byte(grade_curve(y, p2, k1, k2, k3) * k4);
            out[base + (us | vs)] = ca << 24 | cr << 16 | cb << 8 | cd;
        }
    }
}

void sub_000A3CA0(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    uint32_t k = MEM32(esp + 4), tab = MEM32(0x003E8C18u);
    int32_t n = (int32_t)MEM32(0x0039D324u);
    uint32_t e0 = esp, j;

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (mode == 0 || n <= 0 || n > 256) {
        sub_000A3CA0_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        size_t cnt = (size_t)n * (size_t)n * (k + 1u);
        uint32_t *mine = malloc(cnt * 4u), sv_ebx = ebx, sv_esi = esi, sv_edi = edi;
        int top = g_fp_top;
        memcpy(mine, (const void *)XBOX_PTR(tab), cnt * 4u);
        grade_table(mine, k, n);
        sub_000A3CA0_gen();
        calls++;
        if ((memcmp(mine, (const void *)XBOX_PTR(tab), cnt * 4u) || g_fp_top != top
             || esp != e0 + 4u || ebx != sv_ebx || esi != sv_esi || edi != sv_edi
             || eax != k << 4 || ecx != MEM32(0x0039D30Cu + 16u * k)
             || edx != MEM32(0x0039D310u + 16u * k) || g_ebp != e0 - 0x5Cu) && bad++ < 20) {
            for (j = 0; j < cnt && mine[j] == MEM32(tab + 4u * j); j++)
                ;
            fprintf(stderr, "[native] sub_000A3CA0 mismatch: table %u, n %d, texel %u "
                    "native %08X lifted %08X, ebp %08X (%08X)\n", k, n, j,
                    j < cnt ? mine[j] : 0, j < cnt ? MEM32(tab + 4u * j) : 0, g_ebp, e0 - 0x5Cu);
        }
        if ((calls & 0x3FF) == 0)
            fprintf(stderr, "[native] sub_000A3CA0: %lu calls, %lu mismatches\n", calls, bad);
        free(mine);
        return;
    }
    grade_table((uint32_t *)XBOX_PTR(tab), k, n);
    for (j = 0; j < 4; j++)
        MEM32(0x003E8BF0u + 16u * k + 4u * j) = MEM32(0x0039D304u + 16u * k + 4u * j);
    eax = k << 4;
    ecx = MEM32(0x0039D30Cu + 16u * k);
    edx = MEM32(0x0039D310u + 16u * k);
    g_ebp = g_seh_ebp = e0 - 0x5Cu;     /* the frame its last _ftol2 call published */
    esp += 4;                           /* cdecl: return address only */
}

/* ── Movie colour conversion, sub_0025ECB4 ───────────────────
 *
 * cdecl (y, u, v, dst, dst_end): one row of a VP6 picture to A8R8G8B8, two
 * pixels per step. MMX table lookups: four words per entry, Y at 0x3D0810,
 * U at 0x3D1010, V at 0x3D1810 (256 x 8 bytes each); pixel = Y[y] + (U[u] +
 * V[v]) with paddw wrap, packuswb to bytes. sub_0025F0B7 calls it for every
 * row of every movie frame, and FFmpeg does not replace it: lifted it was
 * the largest cost of a movie left after the decoder. Exact, including the
 * registers it leaves behind. RECOMP_NATIVE=0 lifted, RECOMP_NATIVE_CHECK=1
 * both and compare. */
extern void sub_0025ECB4_gen(void);

/* Last row written: lies in the movie texture (src/movie_crop.c). */
uint32_t nfsu2_movie_row;

#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

static inline __attribute__((unused)) uint8_t sat_u8(int16_t w)
{
    return w < 0 ? 0 : w > 255 ? 255 : (uint8_t)w;
}

static void yuv_row_native(uint32_t y, uint32_t u, uint32_t v, uint8_t *out, uint32_t n)
{
    const int16_t *ty = (const int16_t *)XBOX_PTR(0x003D0810u);
    const int16_t *tu = (const int16_t *)XBOX_PTR(0x003D1010u);
    const int16_t *tv = (const int16_t *)XBOX_PTR(0x003D1810u);
    const uint8_t *py = (const uint8_t *)XBOX_PTR(y);
    const uint8_t *pu = (const uint8_t *)XBOX_PTR(u);
    const uint8_t *pv = (const uint8_t *)XBOX_PTR(v);
    uint32_t i;
#if defined(__aarch64__)
    /* vadd wraps like paddw, vqmovun saturates like packuswb */
    for (i = 0; i < n; i++, out += 8) {
        int16x4_t c = vadd_s16(vld1_s16(tu + 4 * pu[i]), vld1_s16(tv + 4 * pv[i]));
        int16x4_t w0 = vadd_s16(vld1_s16(ty + 4 * py[2 * i]), c);
        int16x4_t w1 = vadd_s16(vld1_s16(ty + 4 * py[2 * i + 1]), c);
        vst1_u8(out, vqmovun_s16(vcombine_s16(w0, w1)));
    }
#elif defined(__SSE2__)
    for (i = 0; i < n; i++, out += 8) {
        __m128i c = _mm_add_epi16(_mm_loadl_epi64((const __m128i *)(tu + 4 * pu[i])),
                                  _mm_loadl_epi64((const __m128i *)(tv + 4 * pv[i])));
        __m128i w = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ty + 4 * py[2 * i])),
                                       _mm_loadl_epi64((const __m128i *)(ty + 4 * py[2 * i + 1])));
        w = _mm_add_epi16(w, _mm_unpacklo_epi64(c, c));
        _mm_storel_epi64((__m128i *)out, _mm_packus_epi16(w, w));
    }
#else
    int k;
    for (i = 0; i < n; i++, out += 8) {
        const int16_t *a = tu + 4 * pu[i], *b = tv + 4 * pv[i];
        const int16_t *y0 = ty + 4 * py[2 * i], *y1 = ty + 4 * py[2 * i + 1];
        for (k = 0; k < 4; k++) {
            int16_t c = (int16_t)(a[k] + b[k]);
            out[k] = sat_u8((int16_t)(y0[k] + c));
            out[4 + k] = sat_u8((int16_t)(y1[k] + c));
        }
    }
#endif
}

void sub_0025ECB4(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    uint32_t y = MEM32(esp + 4), u = MEM32(esp + 8), v = MEM32(esp + 12);
    uint32_t dst = MEM32(esp + 16), end = MEM32(esp + 20);
    /* do-while in the original: at least one step, until dst == end */
    uint32_t n = end > dst ? (end - dst) / 8u : 1u, last, k;

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (mode == 0 || ((end - dst) & 7u) || n > 4096u) {
        sub_0025ECB4_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long rows, bad;
        static uint8_t buf[4096 * 8];
        yuv_row_native(y, u, v, buf, n);
        sub_0025ECB4_gen();
        rows++;
        if (memcmp(buf, (const void *)XBOX_PTR(dst), n * 8u) && bad++ < 20)
            fprintf(stderr, "[native] sub_0025ECB4 mismatch: row %lu at %08X\n", rows, dst);
        if ((rows & 0xFFFF) == 0)
            fprintf(stderr, "[native] sub_0025ECB4: %lu rows, %lu mismatches\n", rows, bad);
        return;
    }
    nfsu2_movie_row = dst;
    yuv_row_native(y, u, v, (uint8_t *)XBOX_PTR(dst), n);

    /* What the last step leaves in the registers. */
    last = n - 1u;
    {
        const int16_t *ty = (const int16_t *)XBOX_PTR(0x003D0810u);
        const int16_t *tu = (const int16_t *)XBOX_PTR(0x003D1010u);
        const int16_t *tv = (const int16_t *)XBOX_PTR(0x003D1810u);
        const int16_t *a = tu + 4 * MEM8(u + last), *b = tv + 4 * MEM8(v + last);
        const int16_t *y1 = ty + 4 * MEM8(y + 2u * last + 1u);
        for (k = 0; k < 4; k++) {
            mm3.w[k] = b[k];
            mm2.w[k] = (int16_t)(a[k] + b[k]);
            mm1.w[k] = (int16_t)(y1[k] + mm2.w[k]);
        }
        memcpy(&mm0, (const void *)XBOX_PTR(dst + 8u * last), 8);
    }
    eax = 0;
    ecx = y + 2u * n;
    edx = u + n;
    esp += 4;                           /* cdecl: the caller pops the arguments */
}

/* ── Movie file names, sub_00129610 ──────────────────────────
 *
 * cdecl (buf, size, name): snprintf "%sMOVIES\\%s%s" -- the path of a movie
 * with its language suffix (_en.vp6 ...). The last one is kept for
 * src/movie_crop.c. */
extern void sub_00129610_gen(void);

char nfsu2_movie_name[64];

void sub_00129610(void)
{
    uint32_t buf = MEM32(esp + 4), i;

    sub_00129610_gen();
    for (i = 0; i < sizeof nfsu2_movie_name - 1 && MEM8(buf + i); i++)
        nfsu2_movie_name[i] = (char)MEM8(buf + i);
    nfsu2_movie_name[i] = 0;
    fprintf(stderr, "[movie] %s\n", nfsu2_movie_name);
}

/* ── DirectSound DSP command post (0x0032EB65) ───────────────
 *
 * thiscall, ecx = the DSP-side object. Copies a command block into the GP
 * DSP's scratch area, stores the command code at scratch + 0x810, and spins
 * until the DSP zeroes it (this loop does re-read memory). The scratch block
 * is ***(this + 8 + 0x10), the same chain the runtime's RECOMP_DSP_ACK notes
 * describe. There is no DSP, so register the word with the runtime's
 * completion list before running the original body. */
extern int xbox_ApuDspAckWord(uint32_t va);
extern void sub_0032EB65_gen(void);

void sub_0032EB65(void)
{
    uint32_t obj = MEM32(ecx + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
    sub_0032EB65_gen();
}

/* ── DirectSound AC'97 channel reset (0x0033518D) ────────────
 *
 * thiscall, ecx = channel object. Sets RR (bit 1) in the channel's NABM
 * control byte, then waits for the controller to clear it -- except MSVC
 * hoisted the load out of the loop, so the original reads the byte once and
 * spins on the register copy forever unless the bit is already clear at that
 * one read:
 *
 *     mov  byte [eax+0xFEC0010B], 2
 *     mov  cl, [eax+0xFEC0010B]
 *     and  cl, 2
 *   L: test cl, cl
 *     jne  L
 *
 * On hardware the reset has completed by then. The Windows runtime answers it
 * with a write trap on the NABM page; there is no fault handling on POSIX or
 * Switch, so this is the same function with the reset completing at once.
 * Everything else -- the lock taken through sub_0032BFF0/sub_0032C012, the
 * buffer-descriptor base store, the 0xFEC0017C write for channel 1 and the
 * sub_00334F97 re-arm -- is as the original does it. */
void sub_0033518D(void)
{
    uint32_t ebp_saved = g_ebp, frame, chan, nabm;

    PUSH32(esp, ebp_saved);
    frame = esp;
    g_ebp = frame;
    g_seh_ebp = frame;
    esp -= 8;
    MEM32(frame - 4) = 0;
    PUSH32(esp, esi);
    esi = ecx;
    chan = esi;

    ecx = frame - 8;
    PUSH32(esp, 0x003351A1u); RECOMP_ABI_CALL(0x0032BFF0u, sub_0032BFF0);

    nabm = MEM32(MEM32(chan) * 4 + 0x335DE0u);
    MEM8(nabm + 0xFEC0010Bu) = 2;
    MEM8(nabm + 0xFEC0010Bu) &= (uint8_t)~2u;   /* reset complete */
    eax = nabm;
    SET_LO8(ecx, 0);

    MEM32(nabm + 0xFEC00100u) = MEM32(chan + 0x1C);
    if (MEM32(chan) == 1)
        MEM32(0xFEC0017Cu) = MEM32(chan + 0x28);

    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = chan;
    PUSH32(esp, MEM8(chan + 0x24));
    PUSH32(esp, MEM8(chan + 0x25));
    eax = MEM8(chan + 0x25);
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x003351F4u); RECOMP_ABI_CALL(0x00334F97u, sub_00334F97);

    ecx = frame - 8;
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x003351FCu); RECOMP_ABI_CALL(0x0032C012u, sub_0032C012);

    POP32(esp, esi);
    esp = frame;
    POP32(esp, ebp_saved);
    g_ebp = ebp_saved;
    esp += 4;
}

/* ── Options -> Video: Car Reflections, Resolution Scale ─────
 *
 * The Xbox Video screen (sub_000CE8D0, options category 1 at 0x406CC4)
 * holds only the Screen Size slider. The port's own settings are appended
 * as rows built like the Gameplay screen's (sub_000CEA00): 0x54 bytes,
 * sub_0012AF20 constructor, added with sub_0015B2E0 -- the Player screen
 * (sub_000CF960) mixes sliders and such rows too. Each row's class is a
 * copy of the Speedometer Units row's vtable (0x34EFD0) in memory of our
 * own, so the two methods that know the setting, the input handler (vt+4,
 * sub_000B5140: left/right flips byte 0x406CDA) and the refresh (vt+0xC,
 * sub_000B51A0: label + value text), can tell our rows by their vtable.
 * Their texts are PC-only strings the Xbox never shows, renamed by
 * text_patch.c (s_always); On/Off are Jump Camera's.
 *
 * Car Reflections Off does two things: the renderer's cube stages sample
 * black (nv2a_vk_cube_maps, Vulkan only; GL never drew them), and the
 * title stops rendering its dynamic cube map. Six "EnvMap %d" views, one
 * per cube face (set up by sub_000A3BC0, pointers at 0x3F2AD0), are drawn
 * by the race render loop in sub_000ADF60 only while their byte at +8 is
 * set. The PC's update-rate scheduler, sub_0009B720 (thiscall on that
 * table, every frame before the render), sets those bytes from a schedule
 * (rows of six at 0x39CE20, row = frame % [0x39CE18]); after it, Off
 * clears all six.
 *
 * Resolution Scale (Vulkan build only): 1x, 1.5x (default), 2x, 2.5x,
 * handed to the renderer as nv2a_vk_scale_pct, applied at its next flip
 * (nv2a_vk.c rescale_surfaces). Without a saved value RECOMP_GL_SCALE
 * still decides, and the row shows the nearest step.
 *
 * Kept in nfsu2x_options.txt (sdmc:/switch/nfsu2x/ on the Switch, the
 * working directory elsewhere), not in the game profile; loaded at boot
 * (nfsu2_options_load, main.c). RECOMP_VK_CUBE=0 still forces the
 * reflections off in the renderer. */
#ifdef __SWITCH__
#  define NFSU2_OPTIONS_FILE "sdmc:/switch/nfsu2x/nfsu2x_options.txt"
#else
#  define NFSU2_OPTIONS_FILE "nfsu2x_options.txt"
#endif
#define FE_PAD_LEFT     0x9120409Eu
#define FE_PAD_RIGHT    0xB5971BF1u
#define ROW_VTABLE      0x0034EFD0u     /* Speedometer Units */
#define ROW_VTABLE_SIZE 0x48u
#define ENVMAP_TABLE    0x003F2AD0u     /* six cube-face view pointers */

#ifdef NFSU2_VULKAN
extern volatile int nv2a_vk_cube_maps;
extern volatile int nv2a_vk_scale_pct;
#endif
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
extern void sub_000CE8D0_gen(void);
extern void sub_000B5140_gen(void);
extern void sub_000B51A0_gen(void);
extern void sub_0009B720_gen(void);

static const int s_scale_pct[] = { 100, 150, 200, 250 };

static int s_refl = 1;                  /* 0 off, 1 on */
static int s_scale_idx = 1;             /* into s_scale_pct: 1.5x */
static int s_scale_saved;               /* nfsu2x_options.txt had scale= */

typedef struct {
    uint32_t label;                     /* text hashes, see text_patch.c */
    const uint32_t *texts;
    int n;
    int *value;
    void (*changed)(void);
    uint32_t vtable;                    /* guest copy of ROW_VTABLE */
} OptRow;

static void refl_changed(void)
{
#ifdef NFSU2_VULKAN
    nv2a_vk_cube_maps = s_refl;
#endif
}

#ifdef NFSU2_VULKAN
static void scale_changed(void)
{
    nv2a_vk_scale_pct = s_scale_pct[s_scale_idx];
    s_scale_saved = 1;
}
#endif

static const uint32_t s_refl_texts[] = { 0x0000CCFAu, 0x0000063Cu };    /* Off, On */
static const uint32_t s_scale_texts[] = {
    0x2A2A4AB3u, 0x822E4E9Bu, 0xB55E7665u, 0xD3588630u,                 /* 1x .. 2.5x */
};
static OptRow s_rows[] = {
    { 0x8FE9288Eu, s_refl_texts, 2, &s_refl, refl_changed, 0 },        /* Car Reflections */
#ifdef NFSU2_VULKAN
    { 0x4AC50BCFu, s_scale_texts, 4, &s_scale_idx, scale_changed, 0 }, /* Resolution Scale */
#endif
};
#define N_ROWS ((int)(sizeof s_rows / sizeof s_rows[0]))

void nfsu2_options_load(void)
{
    char line[128];
    FILE *f = fopen(NFSU2_OPTIONS_FILE, "r");
    int i;

    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "reflections=", 12))
                s_refl = line[12] != '0';
            else if (!strncmp(line, "scale=", 6)) {
                int pct = atoi(line + 6);
                for (i = 0; i < 4; i++)
                    if (s_scale_pct[i] == pct) {
                        s_scale_idx = i;
                        s_scale_saved = 1;
                    }
            }
        }
        fclose(f);
    }
    refl_changed();
#ifdef NFSU2_VULKAN
    {
        const char *e = getenv("RECOMP_GL_SCALE");
        if (!s_scale_saved && e && *e) {
            /* RECOMP_GL_SCALE stays in charge until the option is changed;
             * the row shows the nearest step. */
            double k = strtod(e, NULL), best = 1e9;
            for (i = 0; i < 4; i++) {
                double d = fabs(k * 100.0 - s_scale_pct[i]);
                if (d < best) { best = d; s_scale_idx = i; }
            }
        } else {
            nv2a_vk_scale_pct = s_scale_pct[s_scale_idx];
        }
    }
    fprintf(stderr, "[options] car reflections %s, resolution scale %d%%%s\n",
            s_refl ? "on" : "off", s_scale_pct[s_scale_idx],
            nv2a_vk_scale_pct ? "" : " (RECOMP_GL_SCALE)");
#else
    fprintf(stderr, "[options] car reflections %s\n", s_refl ? "on" : "off");
#endif
}

static void options_save(void)
{
    FILE *f = fopen(NFSU2_OPTIONS_FILE, "w");
    if (!f) {
        fprintf(stderr, "[options] cannot write " NFSU2_OPTIONS_FILE "\n");
        return;
    }
    fprintf(f, "reflections=%d\n", s_refl);
    if (s_scale_saved)
        fprintf(f, "scale=%d\n", s_scale_pct[s_scale_idx]);
    fclose(f);
}

static OptRow *opt_row(uint32_t obj)
{
    int i;
    uint32_t vt;

    if (!obj || !(vt = MEM32(obj)))
        return NULL;
    for (i = 0; i < N_ROWS; i++)
        if (s_rows[i].vtable == vt)
            return &s_rows[i];
    return NULL;
}

/* Face views off for this frame (on: the scheduler's choice stands). */
static void refl_views(void)
{
    uint32_t n, v;

    if (s_refl)
        return;
    for (n = 0; n < 6; n++)
        if ((v = MEM32(ENVMAP_TABLE + n * 4)) != 0)
            MEM8(v + 8) = 0;
}

/* Video screen (thiscall, ecx = options screen): the original rows, then
 * ours. */
void sub_000CE8D0(void)
{
    uint32_t screen = ecx, row;
    int i;

    sub_000CE8D0_gen();
    for (i = 0; i < N_ROWS; i++) {
        OptRow *o = &s_rows[i];
        if (!o->vtable) {
            o->vtable = xbox_ContiguousAlloc(ROW_VTABLE_SIZE, 16);
            if (!o->vtable)
                return;
            memcpy((void *)XBOX_PTR(o->vtable), (const void *)XBOX_PTR(ROW_VTABLE),
                   ROW_VTABLE_SIZE);
        }
        PUSH32(esp, 0x54u);                 /* operator new(0x54), cdecl */
        PUSH32(esp, 0x000CEAD4u);
        sub_0003ED50();
        esp += 4;
        row = eax;
        if (!row)
            return;
        PUSH32(esp, 1);                     /* row(&screen[0x100..0x120], 1) */
        PUSH32(esp, screen + 0x120u);
        PUSH32(esp, screen + 0x118u);
        PUSH32(esp, screen + 0x110u);
        PUSH32(esp, screen + 0x100u);
        ecx = row;
        PUSH32(esp, 0x000CEB0Cu);
        sub_0012AF20();                     /* thiscall, pops its 5 */
        MEM32(row) = o->vtable;
        PUSH32(esp, 1);                     /* screen->AddOption(row, 1) */
        PUSH32(esp, row);
        ecx = screen;
        PUSH32(esp, 0x000CEB23u);
        sub_0015B2E0();                     /* thiscall, pops its 2 */
    }
}

/* Row refresh (thiscall): label and value text. */
static void opt_row_text(uint32_t row, const OptRow *o)
{
    PUSH32(esp, o->label);
    PUSH32(esp, MEM32(row + 0x2Cu));
    PUSH32(esp, 0x000B51B1u);
    sub_00118BF0();
    esp += 8;
    PUSH32(esp, o->texts[*o->value]);
    PUSH32(esp, MEM32(row + 0x30u));
    PUSH32(esp, 0x000B51CEu);
    sub_00118BF0();
    esp += 8;
}

void sub_000B51A0(void)
{
    OptRow *o = opt_row(ecx);

    if (!o) {
        sub_000B51A0_gen();
        return;
    }
    opt_row_text(ecx, o);
    esp += 4;
}

/* Row input (thiscall, (?, message), ret 8): left/right step the value
 * (wrapping, like the game's two-value rows), then the arrow flash
 * (vt+0x40) and the refresh, as the original. */
void sub_000B5140(void)
{
    uint32_t row = ecx, msg = MEM32(esp + 8);
    OptRow *o = opt_row(row);

    if (!o) {
        sub_000B5140_gen();
        return;
    }
    if (msg == FE_PAD_LEFT || msg == FE_PAD_RIGHT) {
        *o->value = (*o->value + (msg == FE_PAD_RIGHT ? 1 : o->n - 1)) % o->n;
        o->changed();
        refl_views();
        options_save();
#ifdef NFSU2_VULKAN
        fprintf(stderr, "[options] car reflections %s, resolution scale %d%%\n",
                s_refl ? "on" : "off", s_scale_pct[s_scale_idx]);
#else
        fprintf(stderr, "[options] car reflections %s\n", s_refl ? "on" : "off");
#endif
    }
    PUSH32(esp, msg);
    ecx = row;
    PUSH32(esp, 0x000B5171u);
    sub_0012B3B0();                         /* thiscall, pops its 1 */
    opt_row_text(row, o);
    esp += 4 + 8;
}

/* Env-map update scheduler (thiscall, 2 args, ret 8). */
void sub_0009B720(void)
{
    sub_0009B720_gen();
    refl_views();
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
