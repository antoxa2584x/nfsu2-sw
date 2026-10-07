/**
 * Need for Speed: Underground 2 (Xbox) - recompiled game entry point
 *
 * Boot order (same on every host):
 *   crash reporter -> load default.xbe -> xbox_MemoryLayoutInit ->
 *   xbox_kernel_init -> xbox_path_init -> xbox_kernel_bridge_init ->
 *   g_esp = XBOX_STACK_TOP -> recomp_dispatch_init -> xbe_entry_point()
 *
 * Game data is the extracted disc (default.xbe, NFSUNDER/, B3/). Where it is
 * looked for:
 *   Windows/Linux: $NFSU2_GAME_DIR, else game/ next to the executable,
 *                   else ./game in the working directory
 *   Switch:        sdmc:/switch/nfsu2x/game
 */

#ifdef _WIN32
#  include <windows.h>
#  include <dbghelp.h>
#else
#  include <signal.h>
#  include <unistd.h>
#endif
#if defined(__APPLE__) && !defined(__SWITCH__)
#  include <mach-o/dyld.h>          /* _NSGetExecutablePath */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <xbox/xboxrecomp.h>

#ifdef __SWITCH__
/* libnx lives in switch_nx.c: <switch.h> and the Win32 vocabulary collide. */
void switch_boot(void);
void switch_shutdown(void);
void switch_error_dialog(const char *msg, const char *details);
#elif !defined(_WIN32)
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message,
                             void *window);
#endif

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi;
extern ptrdiff_t g_xbox_mem_offset;
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;

typedef struct MCPXAPUState MCPXAPUState;
extern MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram_ptr);
extern MCPXAPUState *g_apu_state;
extern uint64_t mcpx_apu_mmio_read(MCPXAPUState *d, uint64_t addr, unsigned int size);
extern void mcpx_apu_mmio_write(MCPXAPUState *d, uint64_t addr, uint64_t val,
                                unsigned int size);

#define NFSU2_APU_BASE 0xFE800000u
#define NFSU2_APU_END  0xFE880000u

static uint32_t apu_mmio_rd(void *opaque, uint32_t off, unsigned size)
{
    return (uint32_t)mcpx_apu_mmio_read((MCPXAPUState *)opaque, off, size);
}

static void apu_mmio_wr(void *opaque, uint32_t off, uint32_t val, unsigned size)
{
    mcpx_apu_mmio_write((MCPXAPUState *)opaque, off, val, size);
}

extern void xbe_entry_point(void);
extern int recomp_dispatch_init(void);
extern void xbox_path_init(const char *game_dir, const char *save_dir);

#define NFSU2_ENTRY_POINT 0x0021B1CEu


#if defined(__SWITCH__)
#  define NFSU2_DEFAULT_GAME_DIR "sdmc:/switch/nfsu2x/game"
#  define NFSU2_DEFAULT_SAVE_DIR "sdmc:/switch/nfsu2x/save"
#else
#  define NFSU2_DEFAULT_GAME_DIR "game"
#  define NFSU2_DEFAULT_SAVE_DIR NULL
#endif

/* ------------------------------------------------------------------ */
/* Crash reporting                                                     */
/* ------------------------------------------------------------------ */

/* What the guest was doing: its registers and the return addresses on its
 * stack. This is the useful half of any crash report -- the host PC names a
 * generated sub_XXXXXXXX at best, the guest stack names the whole chain. */
void print_guest_state(void)
{
    fprintf(stderr, "  guest: eax=%08X ecx=%08X edx=%08X ebx=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx);
    fprintf(stderr, "         esi=%08X edi=%08X esp=%08X\n",
            g_esi, g_edi, g_esp);

    if (g_xbox_mem_offset && g_esp >= XBOX_STACK_BASE
            && g_esp < XBOX_STACK_BASE + XBOX_STACK_SIZE) {
        const uint32_t *sp =
            (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
        int shown = 0, i;
        fprintf(stderr, "  guest stack (return addresses, innermost first):\n");
        for (i = 0; i < 512 && shown < 24; i++) {
            uint32_t v = sp[i];
            if (g_esp + (uint32_t)i * 4u >= XBOX_STACK_BASE + XBOX_STACK_SIZE)
                break;
            if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
                fprintf(stderr, "    [esp+%-4d] 0x%08X\n", i * 4, v);
                shown++;
            }
        }
    }
    {
        unsigned k;
        fprintf(stderr, "  recent ICALL targets:");
        for (k = 0; k < 16; k++)
            fprintf(stderr, " %08X", g_icall_trace[(g_icall_trace_idx + k) & 15]);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

#ifdef _WIN32
static LONG CALLBACK veh_handler(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t fault = ep->ExceptionRecord->ExceptionInformation[1];
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
        DWORD64 disp = 0;

        /* The NV2A aperture belongs to the runtime's own handlers. */
        if (fault >= 0xFD000000 && fault < 0xFE000000)
            return EXCEPTION_CONTINUE_SEARCH;

        fprintf(stderr, "[CRASH] access violation at RIP=0x%llX, %s 0x%llX "
                        "(guest VA 0x%08X)\n",
                (unsigned long long)ep->ContextRecord->Rip,
                ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                (unsigned long long)fault,
                (uint32_t)(fault - (uintptr_t)g_xbox_mem_offset));
        memset(buf, 0, sizeof(buf));
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &disp, sym))
            fprintf(stderr, "  in %s+0x%llX\n", sym->Name,
                    (unsigned long long)disp);
        print_guest_state();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void install_crash_reporter(void)
{
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    AddVectoredExceptionHandler(1, veh_handler);
}
#elif defined(__SWITCH__)
/* The CPU exception handler is in switch_nx.c; it calls print_guest_state. */
static void install_crash_reporter(void) {}
#else
#include <execinfo.h>

static void crash_signal(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    fprintf(stderr, "[CRASH] signal %d, fault addr %p (guest VA 0x%08X)\n",
            sig, si->si_addr,
            (uint32_t)((uintptr_t)si->si_addr - (uintptr_t)g_xbox_mem_offset));
    print_guest_state();
    {
        /* The host frames name the lifted function (sub_XXXXXXXX) that
         * faulted -- the guest stack alone does not on a worker thread. */
        void *frames[32];
        int n = backtrace(frames, 32);
        fprintf(stderr, "  host backtrace:\n");
        fflush(stderr);
        backtrace_symbols_fd(frames, n, 2);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_crash_reporter(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    {
        static char altstack[64 * 1024];
        stack_t ss;
        ss.ss_sp = altstack;
        ss.ss_size = sizeof(altstack);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}
#endif

/* ------------------------------------------------------------------ */

#if !defined(__SWITCH__)
/* A directory is the game data if it holds default.xbe. fopen rather than
 * access(): that answer is the same on Windows and on sdmc. */
static int has_game(const char *dir)
{
    char path[4200];
    FILE *f;

    if (snprintf(path, sizeof path, "%s/default.xbe", dir) >= (int)sizeof path)
        return 0;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* Where the game data is, without NFSU2_GAME_DIR: a game/ directory next to
 * the executable first (so a build can be shipped with the disc beside it
 * and nothing configured), then ./game in the working directory, which is
 * the original default. $NFSU2_GAME_DIR always wins. */
static const char *resolve_game_dir(char *buf, size_t cap)
{
    const char *env = getenv("NFSU2_GAME_DIR");
    char exe[4096], dir[4096];
    size_t i, len;

    if (env && env[0])
        return env;

    exe[0] = 0;
#if defined(_WIN32)
    {
        DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe);
        if (!n || n >= sizeof exe)
            exe[0] = 0;
    }
#elif defined(__APPLE__)
    {
        uint32_t sz = (uint32_t)sizeof exe;
        if (_NSGetExecutablePath(exe, &sz) != 0)
            exe[0] = 0;
    }
#elif defined(__linux__)
    {
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0)
            exe[n] = 0;
        else
            exe[0] = 0;
    }
#endif
    /* The executable's directory, up to the last separator. */
    len = strlen(exe);
    for (i = len; i > 0; i--)
        if (exe[i - 1] == '/' || exe[i - 1] == '\\')
            break;
    if (i > 0 && i < sizeof dir) {
        memcpy(dir, exe, i);
        dir[i] = 0;
        if (i > 0 && (dir[i - 1] == '/' || dir[i - 1] == '\\'))
            dir[--i] = 0;               /* "build/" -> "build", "/" -> "" */
        if (snprintf(buf, cap, "%s/game", dir) < (int)cap && has_game(buf))
            return buf;
    }
    return NFSU2_DEFAULT_GAME_DIR;          /* ./game in the working directory */
}
#endif

static void *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    data = malloc((size_t)size);
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data)
        *out_size = (size_t)size;
    return data;
}

static void fatal(const char *msg)
{
    fprintf(stderr, "[FATAL] %s\n", msg);
#if defined(_WIN32)
    MessageBoxA(NULL, msg, "NFSU2 recomp", MB_ICONERROR);
#elif defined(__SWITCH__)
    switch_error_dialog(msg, NULL);
#else
    SDL_ShowSimpleMessageBox(0x10, "NFSU2 recomp", msg, NULL);
#endif
}

static int game_main(void);
void xbox_guest_pin(int interrupt);   /* win32_compat.c: the Xbox's one CPU */
void xbox_gil_enter(void);             /* kernel_bridge.c: the guest lock */
void xbox_gil_leave(void);
void xbox_gil_mark_main(void);
void nfsu2_text_patch_init(void);    /* text_patch.c: Switch button names */

#ifdef __SWITCH__
#include <pthread.h>
/* The first guest thread runs inline on whichever host thread boots the
 * title, and Horizon's main thread stack is small for recompiled code; boot
 * on a thread with a stack sized like a desktop main thread instead. */
void xbox_nx_spread_thread(void);   /* win32_compat.c: off core 0, all cores allowed */
void xbox_nx_track_thread(void *entry);   /* win32_compat.c: Switch [perf] report */

static void *game_thread(void *arg)
{
    xbox_nx_spread_thread();
    xbox_nx_track_thread(NULL);          /* "game:" in the [perf] report */
    *(int *)arg = game_main();
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_attr_t attr;
    pthread_t th;
    int rc = 1;

    (void)argc;
    (void)argv;
    switch_boot();
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u * 1024 * 1024);
    if (pthread_create(&th, &attr, game_thread, &rc) == 0)
        pthread_join(th, NULL);
    else
        fprintf(stderr, "[FATAL] cannot start the game thread\n");
    switch_shutdown();
    return rc;
}
#elif !defined(__APPLE__)
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return game_main();
}
#endif

#if defined(__APPLE__) && !defined(__SWITCH__)
#include <pthread.h>
#include <unistd.h>
/* Cocoa belongs to the process' main thread: the SDL window has to be
 * created there, events have to be pumped there (the executor thread would
 * otherwise own NSApp), and without that pump the layer never shows a
 * frame -- which is how a port ends up with sound but a black screen. So
 * the title runs on a worker with a desktop-sized stack and main drives
 * SDL. The renderer is built on main before the title starts: ready() is
 * lazy everywhere else and would land on the executor thread. */
static volatile int s_game_done;
static int s_game_rc;

static void *game_thread(void *arg)
{
    (void)arg;
    s_game_rc = game_main();
    s_game_done = 1;
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_attr_t attr;
    pthread_t th;
#if defined(NFSU2_VULKAN)
    void nv2a_vk_pump(void);
#endif

    (void)argc;
    (void)argv;
#if defined(NFSU2_VULKAN)
    {
        const char *gl = getenv("NFSU2_GL");
        if (!gl || strcmp(gl, "0") != 0) {
            int nv2a_vk_ready(void);
            nv2a_vk_ready();            /* window + device, on this thread */
        }
    }
#endif
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u * 1024 * 1024);
    if (pthread_create(&th, &attr, game_thread, NULL) != 0) {
        fprintf(stderr, "[FATAL] cannot start the game thread\n");
        return 1;
    }
    while (!s_game_done) {
#if defined(NFSU2_VULKAN)
        nv2a_vk_pump();
#endif
        usleep(2000);
    }
    pthread_join(th, NULL);
    return s_game_rc;
}
#endif

static int game_main(void)
{
    xbox_guest_pin(0);     /* the boot thread becomes the title's first thread */
    char xbe_path[512];
    const char *game_dir;
    void *xbe_data;
    size_t xbe_size = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("=== Need for Speed: Underground 2 (Xbox) - static recompilation ===\n");
    install_crash_reporter();
    if (getenv("NFSU2_EXIT_TRACE"))
        atexit(print_guest_state);

    /* Runtime defaults this title needs; an explicit setting still wins.
     *   RECOMP_VBLANK      the linked D3D waits on its vblank ISR every frame
     *   RECOMP_AC97_READY  DirectSound needs the codec present to create; "plain"
     *                      is the variant that needs no fault handling */
#ifdef _WIN32
    if (!getenv("RECOMP_VBLANK"))     _putenv("RECOMP_VBLANK=1");
    if (!getenv("RECOMP_AC97_READY")) _putenv("RECOMP_AC97_READY=plain");
    if (!getenv("RECOMP_USB"))        _putenv("RECOMP_USB=1");
#else
    setenv("RECOMP_VBLANK", "1", 0);
    setenv("RECOMP_AC97_READY", "plain", 0);
    setenv("RECOMP_USB", "1", 0);       /* the pad is on the MCPX's OHCI */
    setenv("RECOMP_PB_EXEC", "1", 0);   /* the title draws through NV2A */
#endif
#if defined(__SWITCH__)
    /* Every periodic log line flushes to the SD card; keep the log to what
     * matters on a console. RECOMP_QUIET=0 in nfsu2x_env.txt brings it back. */
    setenv("RECOMP_QUIET", "1", 0);
#endif

    game_dir = getenv("NFSU2_GAME_DIR");
    if (!game_dir || !game_dir[0]) {
#if !defined(__SWITCH__)
        static char gamedir[4200];      /* kept: game_dir points into it */
        game_dir = resolve_game_dir(gamedir, sizeof gamedir);
#else
        game_dir = NFSU2_DEFAULT_GAME_DIR;
#endif
    }
    snprintf(xbe_path, sizeof(xbe_path), "%s/default.xbe", game_dir);

    xbe_data = load_file(xbe_path, &xbe_size);
    if (!xbe_data) {
        fprintf(stderr, "cannot read %s\n", xbe_path);
        fatal("Failed to load default.xbe. Put the extracted disc in a "
              "game directory next to the executable (or in the working "
              "directory), or set NFSU2_GAME_DIR.");
        return 1;
    }
    printf("XBE %s: %zu bytes\n", xbe_path, xbe_size);

    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        fatal("Failed to initialise the Xbox memory layout "
              "(the guest address range may be unavailable).");
        free(xbe_data);
        return 1;
    }
    g_xbox_mem_offset = xbox_GetMemoryOffset();
    printf("Xbox memory mapped at host offset 0x%llX\n",
           (unsigned long long)g_xbox_mem_offset);

    /* Game time per frame is capped: sub_001890C0 takes the real elapsed
     * time but at most 3.0 (the .data float at 0x3A4C64, read nowhere else)
     * x 1/60 s = 50 ms, and drops the rest. Below 20 fps the game ran slower
     * than real time (races looked slow-motion on the Switch at 13-19 fps).
     * NFSU2_SIM_STEPS sets the cap in 1/60 s units: default 6 = 100 ms, real
     * speed down to 10 fps; 3 = the original. */
    {
        const char *e = getenv("NFSU2_SIM_STEPS");
        float steps = e ? (float)atof(e) : 6.0f;
        if (steps >= 1.0f && steps <= 30.0f) {
            float *cap = (float *)((uint8_t *)xbox_GetMemoryBase() + 0x3A4C64);
            if (*cap == 3.0f) {
                *cap = steps;
                printf("[BOOT] game time cap %.0f ms per frame\n", steps * 1000.0f / 60.0f);
            }
        }
    }

    /* The emulated APU. DirectSound's accesses reach it through the MMIO
     * accessors the DSOUND section is lifted with (regen.sh passes
     * --mmio-sections DSOUND), so the APU registers stay plain memory and no
     * fault handling is needed -- the same on every host. NFSU2_APU=0 runs
     * without it (DirectSound then creates, but nothing ever plays).
     * Under RECOMP_AC97_READY=1 the runtime traps the APU instead (Windows). */
    if (getenv("RECOMP_AC97_READY")) {
        const char *apu = getenv("NFSU2_APU");
        int plain = strcmp(getenv("RECOMP_AC97_READY"), "plain") == 0;

        if (!plain || !apu || strcmp(apu, "0") != 0) {
            g_apu_state = mcpx_apu_init_standalone((uint8_t *)xbox_GetMemoryBase());
            fprintf(stderr, "[BOOT] emulated APU %s\n",
                    g_apu_state ? "up" : "FAILED to initialise");
            if (g_apu_state && plain)
                xbox_MmioRegister(NFSU2_APU_BASE, NFSU2_APU_END,
                                  apu_mmio_rd, apu_mmio_wr, g_apu_state);
        }
    }

    /* The USB host controllers the title's XPP library drives to find its
     * gamepad (RECOMP_USB). On hosts without fault handling their registers
     * are reached through the MMIO accessors the XPP section is lifted with. */
    {
        extern void xbox_OhciInit(void);
        xbox_OhciInit();
    }

    /* The NIC the title's XNET library drives (src/nic.c). */
    {
        extern void nfsu2_nic_init(void);
        nfsu2_nic_init();
    }

#ifndef _WIN32
    /* The GPU renderer (NFSU2_GL=0 falls back to the executor's CPU one). */
    {
        const char *gl = getenv("NFSU2_GL");
        if (!gl || strcmp(gl, "0") != 0) {
#if defined(NFSU2_VULKAN)
            extern void nv2a_vk_install(void);
            nv2a_vk_install();
#else
            extern void nv2a_gl_install(void);
            nv2a_gl_install();
#endif
        }
    }
#endif

    xbox_kernel_init();
    nfsu2_text_patch_init();
    xbox_path_init(game_dir, NFSU2_DEFAULT_SAVE_DIR);
    xbox_kernel_bridge_init();

    g_esp = XBOX_STACK_TOP;

    if (!recomp_dispatch_init())
        fprintf(stderr, "[BOOT] flat dispatch unavailable; "
                        "indirect calls will use the binary search\n");

    xbox_WatchdogStart();

    printf("Starting guest at 0x%08X (esp=0x%08X)\n", NFSU2_ENTRY_POINT, g_esp);
    xbox_gil_mark_main();      /* the frame-rate thread (RECOMP_GIL_EAGER) */
    xbox_gil_enter();          /* guest code from here on (kernel_bridge.c) */
    xbe_entry_point();
    xbox_gil_leave();

    printf("Guest returned. Cleaning up.\n");
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);
    return 0;
}
