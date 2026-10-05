/*
 * switch_nx.c -- Nintendo Switch boot glue for the NFSU2 recompilation.
 *
 * Kept apart from main.c because <switch.h> and the Win32 vocabulary the
 * runtime headers bring in define some of the same names (MemoryRegion,
 * Event, Thread...).
 *
 *   logs      an nxlink host when the NRO was started with `nxlink -s`,
 *             otherwise sdmc:/switch/nfsu2x/nfsu2x_log.txt, every line
 *             stamped with seconds since boot
 *   settings  sdmc:/switch/nfsu2x/nfsu2x_env.txt, KEY=VALUE per line, '#'
 *             comments -- a console has no environment
 *   loading   the game's logo from boot until the title first draws, on
 *             the window the renderer then takes over -- not a black screen
 *   crashes   libnx's CPU exception hook prints the host and guest state
 */
#ifdef __SWITCH__
#include <switch.h>

#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apu_xaudio2.h"

#define NFSU2_SWITCH_DIR "sdmc:/switch/nfsu2x"

extern ptrdiff_t g_xbox_mem_offset;
void print_guest_state(void);

static int s_nxlink = -1;
static u64 s_t0;

static double since_boot(void)
{
    return (double)armTicksToNs(armGetSystemTick() - s_t0) / 1e9;
}

/* ── The log ─────────────────────────────────────────────────────
 *
 * stdout and stderr both go to a "log:" device that appends to memory; a
 * thread writes that to the SD card twice a second. The runtime flushes
 * stderr after many of its lines, and with the streams on an SD file every
 * flush was a card write -- enough of them during boot to stretch it from
 * well under a second (Linux; Eden, whose SD is a host folder) to half a
 * minute on a console. A hung title is closed from HOME, which flushes
 * nothing, so the thread is also what keeps the tail of the log. */
#define LOG_CAP (512u * 1024u)

static Mutex  s_log_lock;
static char  *s_log_buf;
static size_t s_log_len;
static int    s_log_fd = -1;
static int    s_log_bol = 1;
static int    s_log_sync;       /* NFSU2_LOG_SYNC=1: every line to the card */
/* NFSU2_NO_LOG=1 (or NFSU2_LOG=0): no log, no [perf] reports, no profiler --
 * for playing rather than testing. Output is dropped at the log device;
 * the settings file is read after the log opens, so the file keeps its
 * first lines (the settings) and nothing after. */
static volatile int s_log_off;

static void log_drain_locked(void)
{
    size_t off = 0;
    while (off < s_log_len) {
        ssize_t w = write(s_log_fd, s_log_buf + off, s_log_len - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
    s_log_len = 0;
}

static void log_put_locked(const char *p, size_t n)
{
    if (s_log_len + n > LOG_CAP)
        log_drain_locked();
    if (n > LOG_CAP) {
        (void)!write(s_log_fd, p, n);
        return;
    }
    memcpy(s_log_buf + s_log_len, p, n);
    s_log_len += n;
}

static ssize_t log_write_r(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    size_t i = 0;
    (void)r; (void)fd;
    if (s_log_off)
        return (ssize_t)len;
    mutexLock(&s_log_lock);
    while (i < len) {
        const char *nl;
        size_t n;
        if (s_log_bol) {
            /* Integer formatting only. This runs inside the fprintf that
             * wrote the line, and a nested %f shares newlib's per-thread
             * dtoa buffer with it: every float on the line came out as the
             * digits of this timestamp ("[  20.069] [perf] 2.0 fps"). */
            char ts[24];
            u64 ms = armTicksToNs(armGetSystemTick() - s_t0) / 1000000ull;
            int k = snprintf(ts, sizeof ts, "[%4llu.%03llu] ",
                             (unsigned long long)(ms / 1000),
                             (unsigned long long)(ms % 1000));
            if (k > 0)
                log_put_locked(ts, (size_t)k);
            s_log_bol = 0;
        }
        nl = memchr(ptr + i, '\n', len - i);
        n = nl ? (size_t)(nl - (ptr + i)) + 1 : len - i;
        log_put_locked(ptr + i, n);
        i += n;
        if (nl)
            s_log_bol = 1;
    }
    if (s_log_sync) {
        /* A hard console freeze loses the RAM buffer; this keeps the log
         * exact up to the last line, at the old per-line SD cost. */
        log_drain_locked();
        fsync(s_log_fd);
    }
    mutexUnlock(&s_log_lock);
    return (ssize_t)len;
}

static int log_open_r(struct _reent *r, void *fs, const char *path, int flags, int mode)
{
    (void)r; (void)fs; (void)path; (void)flags; (void)mode;
    return 0;
}

static int log_close_r(struct _reent *r, void *fd)
{
    (void)r; (void)fd;
    return 0;
}

static int log_fstat_r(struct _reent *r, void *fd, struct stat *st)
{
    (void)r; (void)fd;
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFCHR;
    return 0;
}

static const devoptab_t s_log_dev = {
    .name       = "log",
    .structSize = sizeof(int),
    .open_r     = log_open_r,
    .close_r    = log_close_r,
    .write_r    = log_write_r,
    .fstat_r    = log_fstat_r,
};

static void log_flush(int locked_ok)
{
    fflush(stdout);
    fflush(stderr);
    if (s_log_fd < 0)
        return;
    if (locked_ok) {
        mutexLock(&s_log_lock);
        log_drain_locked();
        mutexUnlock(&s_log_lock);
    } else {
        /* Crash path: the faulting thread may hold the lock. */
        int got = mutexTryLock(&s_log_lock);
        log_drain_locked();
        if (got)
            mutexUnlock(&s_log_lock);
    }
}

/* ── SDL's audio thread ─────────────────────────────────────────
 *
 * switch-sdl2 plays through audren with two 1024-sample wave buffers
 * (21 ms each): when one finishes, its thread must drain our queue and add
 * the next before the other one runs out. It asks for
 * SDL_THREAD_PRIORITY_TIME_CRITICAL, which SDL's Switch port maps to 59 --
 * the time-sliced priority, where it waited behind busy game threads in
 * 10 ms slices and the renderer played gaps (crackle / hiss on the console;
 * Linux and Eden have idle cores). Wrapped at link time (--wrap, CMake):
 * time critical becomes 0x2B, what SDL gives HIGH. The thread only copies
 * 4 KB per buffer. RECOMP_NX_AUDIO_PRIO=0 leaves it at 59.
 *
 * The second wrapper counts gaps for [perf]: a wave buffer added when the
 * one before it has already finished playing means the voice ran dry. */
int __real_SDL_SYS_SetThreadPriority(int prio);
int __wrap_SDL_SYS_SetThreadPriority(int prio)
{
    const char *e = getenv("RECOMP_NX_AUDIO_PRIO");
    if (prio == 3 /* SDL_THREAD_PRIORITY_TIME_CRITICAL */ && !(e && *e == '0')) {
        Result rc = svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2B);
        fprintf(stderr, "  [AUDIO] SDL audio thread at priority 0x2B (rc 0x%X)\n", rc);
        if (R_SUCCEEDED(rc))
            return 0;
    }
    return __real_SDL_SYS_SetThreadPriority(prio);
}

static AudioDriverWaveBuf *s_aout_prev;
static volatile uint32_t   s_aout_bufs, s_aout_gaps, s_aout_worst_us;
static u64                 s_aout_last_tick;

bool __real_audrvVoiceAddWaveBuf(AudioDriver *d, int id, AudioDriverWaveBuf *wb);
bool __wrap_audrvVoiceAddWaveBuf(AudioDriver *d, int id, AudioDriverWaveBuf *wb)
{
    u64 now = armGetSystemTick();
    if (s_aout_prev && s_aout_prev != wb) {
        audrvUpdate(d);          /* fresh wave buffer states */
        if (s_aout_prev->state == AudioDriverWaveBufState_Done)
            s_aout_gaps++;
    }
    if (s_aout_last_tick) {
        uint32_t us = (uint32_t)(armTicksToNs(now - s_aout_last_tick) / 1000);
        if (us > s_aout_worst_us)
            s_aout_worst_us = us;
    }
    s_aout_last_tick = now;
    s_aout_prev = wb;
    s_aout_bufs++;
    return __real_audrvVoiceAddWaveBuf(d, id, wb);
}

/* Every 10 s: presented frames per second, texture memory, and how much of
 * a core each thread used -- the console has no profiler, and this is what
 * tells a slow movie decoder from a busy audio or GPU thread. The line's
 * timestamp also shows how long a stall lasted. */
uint32_t nv2a_gl_frame_count(void);
uint64_t nv2a_gl_texture_bytes(void);
void xbox_nx_thread_report(double interval_s);
void xbox_perf_sample(void);
uint32_t nv2a_gl_program_count(void);
int   mcpx_apu_frames_per_second(void);
float mcpx_apu_utilization(void);
void  xbox_vblank_report(double dt, char *buf, size_t n);   /* kernel_bridge.c */

static void perf_report(void)
{
    static uint32_t last_frames, last_seen;
    uint32_t f = nv2a_gl_frame_count();
    char vb[128];
    xbox_vblank_report(10.0, vb, sizeof vb);
    fprintf(stderr, "[perf] %.1f fps, textures %llu MB, %u shader programs, %s\n",
            (f - last_frames) / 10.0,
            (unsigned long long)(nv2a_gl_texture_bytes() >> 20), nv2a_gl_program_count(), vb);
    {
        /* A slow-down that builds up over a long session (2026-10-05:
         * heavy stutter after ~30 min until the game is restarted) would
         * show here as a heap that keeps growing. */
        struct mallinfo mi = mallinfo();
        fprintf(stderr, "[perf] heap %u MB in use, %u MB free in the arena\n",
                (unsigned)(mi.uordblks >> 20), (unsigned)(mi.fordblks >> 20));
    }
    /* 1500 frames/s is real time; below it, movies (clocked by DirectSound's
     * play cursor) run slow. */
    fprintf(stderr, "[perf] APU %d frames/s (1500 = real time), frame thread %.0f%% busy\n",
            mcpx_apu_frames_per_second(), mcpx_apu_utilization() * 100.0f);
    {
        int mcpx_apu_pacing_stats(char *buf, int cap);
        char pb[256];
        mcpx_apu_pacing_stats(pb, sizeof pb);
        fprintf(stderr, "[perf] APU pacing: %s\n", pb);
    }
    {
        /* Output health: device gaps (audren ran dry: SDL's thread late),
         * our queue running empty, blocks dropped on a full queue, and the
         * largest time between wave buffers (21.3 ms each). */
        static uint64_t last_under, last_drop;
        Xa2Stats st;
        xa2_get_stats(&st);
        fprintf(stderr, "[perf] audio out: %u buffers, %u device gaps, worst %u.%u ms between"
                        " buffers, %llu queue underruns, %llu dropped, peak %d\n",
                s_aout_bufs, s_aout_gaps, s_aout_worst_us / 1000, s_aout_worst_us / 100 % 10,
                (unsigned long long)(st.underruns - last_under),
                (unsigned long long)(st.dropped - last_drop), st.peak);
        last_under = st.underruns;
        last_drop = st.dropped;
        s_aout_bufs = s_aout_gaps = s_aout_worst_us = 0;
    }
    {
        /* The game's mixer late: ring samples the VP read unchanged from
         * one lap (50 ms) earlier. ~0.3% on Linux is music's own repeats. */
        void mcpx_apu_ring_stats(uint32_t *stale, uint32_t *total);
        uint32_t st, tot;
        mcpx_apu_ring_stats(&st, &tot);
        fprintf(stderr, "[perf] APU ring voices: %u of %u samples stale (%u.%02u%%)\n",
                st, tot, tot ? (unsigned)(100ull * st / tot) : 0,
                tot ? (unsigned)(10000ull * st / tot % 100) : 0);
    }
    last_frames = f;
    {
        /* RECOMP_GL_THREAD: is the GL thread starved, or the executor held
         * back by it? */
        void nv2a_gl_queue_stats(double secs, int *gl_idle_pct, int *exec_wait_pct);
        int idle, held;
        nv2a_gl_queue_stats(10.0, &idle, &held);
        if (idle >= 0)
            fprintf(stderr, "[perf] GL thread idle %d%% (queue empty), executor held back %d%% "
                    "(queue full / two frames queued)\n", idle, held);
    }
    {
        void xbox_main_wait_report(double secs);
        xbox_main_wait_report(10.0);
    }
    xbox_perf_sample();
    xbox_nx_thread_report(10.0);
    {
        /* A title that was presenting and then stops for 20 s is hung (the
         * longest loads still present their loading screen): take the
         * watchdog's snapshot of the main guest thread then, and once more
         * 20 s later to show whether it moves. The hang starting a Quick
         * Race on Eden was timing-dependent and never reproduced on demand;
         * this puts its stack in the next log that has it. */
        static int presented, still;
        void xbox_WatchdogDump(const char *why);
        if (f != last_seen) {
            presented = 1;
            still = 0;
        } else if (presented && (++still == 2 || still == 4)) {
            xbox_WatchdogDump(still == 2 ? "no frame presented for 20 s"
                                         : "still no frame after 40 s");
        }
        last_seen = f;
    }
}

static void *log_flusher(void *arg)
{
    unsigned n = 0;
    (void)arg;
    for (;;) {
        svcSleepThread(500000000ull);
        if (s_log_off)
            break;
        if (++n % 20 == 0)
            perf_report();
        log_flush(1);
    }
    return NULL;
}

static int log_open(void)
{
    s_log_buf = (char *)malloc(LOG_CAP);
    s_log_fd = open(NFSU2_SWITCH_DIR "/nfsu2x_log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!s_log_buf || s_log_fd < 0 || AddDevice(&s_log_dev) < 0)
        return 0;
    if (!freopen("log:", "w", stdout))
        return 0;
    dup2(fileno(stdout), fileno(stderr));
    setvbuf(stdout, NULL, _IOLBF, 0);         /* lines reach memory in order */
    setvbuf(stderr, NULL, _IOLBF, 0);
    {
        pthread_t t;
        if (pthread_create(&t, NULL, log_flusher, NULL) == 0)
            pthread_detach(t);
    }
    return 1;
}

static void load_env(void)
{
    FILE *f = fopen(NFSU2_SWITCH_DIR "/nfsu2x_env.txt", "r");
    char line[1024];

    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *eq, *end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
            *--end = 0;
        if (!line[0] || line[0] == '#' || !(eq = strchr(line, '=')) || eq == line)
            continue;
        *eq = 0;
        setenv(line, eq + 1, 1);
        printf("[switch] env %s=%s\n", line, eq + 1);
    }
    fclose(f);
}

#if !defined(NFSU2_VULKAN)
/* ── Loading screen ──────────────────────────────────────────────
 *
 * From boot until the title first draws: the game's logo, centred, with a
 * thin moving bar under it, on the SDL window and GL context the renderer
 * then adopts (nv2a_gl_adopt_window) and keeps drawing it on while the
 * title has only cleared its surfaces (nv2a_gl_draw_placeholder). A libnx
 * framebuffer on the same native window cannot be used: SDL_CreateWindow
 * blocks forever once one has been open. The logo is one textured quad
 * (Switch Mesa misplaces framebuffer blits), and the bar is scissored
 * clears. src/nfsu2_logo.h comes from assets/nfsu2_logo.png via tools/make_logo.py. */
#include <SDL.h>
#include <EGL/egl.h>
#include "nfsu2_logo.h"

#define LW 1280
#define LH 720
#define GL_COLOR_BUFFER_BIT_   0x00004000
#define GL_SCISSOR_TEST_       0x0C11
#define GL_TEXTURE_2D_         0x0DE1
#define GL_RGBA_               0x1908
#define GL_RGBA8_              0x8058
#define GL_UNSIGNED_BYTE_      0x1401
#define GL_LINEAR_             0x2601
#define GL_TEXTURE_MIN_FILTER_ 0x2801
#define GL_TEXTURE_MAG_FILTER_ 0x2800
#define GL_UNPACK_ALIGNMENT_   0x0CF5
#define GL_DRAW_FRAMEBUFFER_   0x8CA9
#define GL_ARRAY_BUFFER_       0x8892
#define GL_STATIC_DRAW_        0x88E4
#define GL_FLOAT_              0x1406
#define GL_TRIANGLE_STRIP_     0x0005
#define GL_VERTEX_SHADER_      0x8B31
#define GL_FRAGMENT_SHADER_    0x8B30
#define GL_LINK_STATUS_        0x8B82
#define GL_TEXTURE0_           0x84C0
#define GL_BLEND_              0x0BE2
#define GL_DEPTH_TEST_         0x0B71
#define GL_CULL_FACE_          0x0B44
#define GL_STENCIL_TEST_       0x0B90
#define GL_CURRENT_PROGRAM_    0x8B8D
#define GL_VERTEX_ARRAY_BINDING_ 0x85B5
#define GL_ARRAY_BUFFER_BINDING_ 0x8894
#define GL_TEXTURE_BINDING_2D_ 0x8069
#define GL_ACTIVE_TEXTURE_     0x84E0

static struct {
    void (*ClearColor)(float, float, float, float);
    void (*Clear)(unsigned);
    void (*Scissor)(int, int, int, int);
    void (*Enable)(unsigned);
    void (*Disable)(unsigned);
    void (*Viewport)(int, int, int, int);
    void (*GenTextures)(int, unsigned *);
    void (*BindTexture)(unsigned, unsigned);
    void (*TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
    void (*TexParameteri)(unsigned, unsigned, int);
    void (*PixelStorei)(unsigned, int);
    void (*BindFramebuffer)(unsigned, unsigned);
    void (*ActiveTexture)(unsigned);
    void (*GetIntegerv)(unsigned, int *);
    unsigned (*CreateShader)(unsigned);
    void (*ShaderSource)(unsigned, int, const char *const *, const int *);
    void (*CompileShader)(unsigned);
    unsigned (*CreateProgram)(void);
    void (*AttachShader)(unsigned, unsigned);
    void (*BindAttribLocation)(unsigned, unsigned, const char *);
    void (*LinkProgram)(unsigned);
    void (*GetProgramiv)(unsigned, unsigned, int *);
    void (*UseProgram)(unsigned);
    int (*GetUniformLocation)(unsigned, const char *);
    void (*Uniform4f)(int, float, float, float, float);
    void (*Uniform1i)(int, int);
    void (*GenVertexArrays)(int, unsigned *);
    void (*BindVertexArray)(unsigned);
    void (*GenBuffers)(int, unsigned *);
    void (*BindBuffer)(unsigned, unsigned);
    void (*BufferData)(unsigned, ptrdiff_t, const void *, unsigned);
    void (*EnableVertexAttribArray)(unsigned);
    void (*VertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
    void (*DrawArrays)(unsigned, int, int);
    void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void *);
} gl;

static SDL_Window    *s_win;
static SDL_GLContext  s_ctx;
static unsigned       s_logo_tex, s_logo_prog, s_logo_vao, s_logo_vbo;
static int            s_logo_rect;         /* uniform: x0 y0 x1 y1, NDC */
static volatile int   s_loader_stop;
static pthread_t      s_loader_thread;
static int            s_loader_running;
static volatile int   s_loader_ready;       /* 1 up, -1 failed */

#define BG_R (NFSU2_LOGO_BG_R / 255.0f)
#define BG_G (NFSU2_LOGO_BG_G / 255.0f)
#define BG_B (NFSU2_LOGO_BG_B / 255.0f)

static int s_vw = LW, s_vh = LH;           /* the real drawable, per frame */

/* A filled rectangle in top-left coordinates. */
static void rect(int x, int y, int w, int h, float r, float g, float b)
{
    if (w <= 0 || h <= 0)
        return;
    gl.Scissor(x, s_vh - y - h, w, h);
    gl.ClearColor(r, g, b, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT_);
}

static void logo_upload(void)
{
    unsigned char *rgba = malloc((size_t)NFSU2_LOGO_W * NFSU2_LOGO_H * 4);
    size_t i, o = 0, end = (size_t)NFSU2_LOGO_W * NFSU2_LOGO_H * 4;
    unsigned tex;

    if (!rgba)
        return;
    /* Rows stored bottom-first, GL's own order, so texture coordinates
     * run the same way as the quad's. */
    for (i = 0; i + 3 < sizeof k_nfsu2_logo_rle && o < end; i += 4)
        for (unsigned n = k_nfsu2_logo_rle[i]; n && o < end; n--, o += 4) {
            size_t row = o / (NFSU2_LOGO_W * 4), col = o % (NFSU2_LOGO_W * 4);
            unsigned char *d = rgba + (NFSU2_LOGO_H - 1 - row) * NFSU2_LOGO_W * 4 + col;
            d[0] = k_nfsu2_logo_rle[i + 1];
            d[1] = k_nfsu2_logo_rle[i + 2];
            d[2] = k_nfsu2_logo_rle[i + 3];
            d[3] = 0xFF;
        }
    gl.GenTextures(1, &tex);
    gl.BindTexture(GL_TEXTURE_2D_, tex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT_, 4);
    gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGBA8_, NFSU2_LOGO_W, NFSU2_LOGO_H, 0,
                  GL_RGBA_, GL_UNSIGNED_BYTE_, rgba);
    gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_);
    gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_LINEAR_);
    s_logo_tex = tex;
    {
        /* A textured quad, not a blit: Switch Mesa placed blits wrongly
         * (the logo landed in a corner whatever the destination said). */
        static const char *vs =
            "#version 330 core\n"
            "layout(location = 0) in vec2 p;\n"
            "uniform vec4 r;\n"
            "out vec2 uv;\n"
            "void main() { uv = p; gl_Position = vec4(mix(r.xy, r.zw, p), 0.0, 1.0); }\n";
        static const char *fs =
            "#version 330 core\n"
            "in vec2 uv;\n"
            "uniform sampler2D t;\n"
            "out vec4 c;\n"
            "void main() { c = texture(t, uv); }\n";
        static const float quad[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
        unsigned v = gl.CreateShader(GL_VERTEX_SHADER_), f = gl.CreateShader(GL_FRAGMENT_SHADER_);
        int ok = 0;
        gl.ShaderSource(v, 1, &vs, NULL);
        gl.CompileShader(v);
        gl.ShaderSource(f, 1, &fs, NULL);
        gl.CompileShader(f);
        s_logo_prog = gl.CreateProgram();
        gl.AttachShader(s_logo_prog, v);
        gl.AttachShader(s_logo_prog, f);
        gl.BindAttribLocation(s_logo_prog, 0, "p");
        gl.LinkProgram(s_logo_prog);
        gl.GetProgramiv(s_logo_prog, GL_LINK_STATUS_, &ok);
        if (!ok) {
            fprintf(stderr, "[switch] loading screen: logo shader did not link\n");
            s_logo_prog = 0;
        } else {
            gl.UseProgram(s_logo_prog);
            s_logo_rect = gl.GetUniformLocation(s_logo_prog, "r");
            gl.Uniform1i(gl.GetUniformLocation(s_logo_prog, "t"), 0);
            gl.UseProgram(0);
        }
        gl.GenVertexArrays(1, &s_logo_vao);
        gl.BindVertexArray(s_logo_vao);
        gl.GenBuffers(1, &s_logo_vbo);
        gl.BindBuffer(GL_ARRAY_BUFFER_, s_logo_vbo);
        gl.BufferData(GL_ARRAY_BUFFER_, sizeof quad, quad, GL_STATIC_DRAW_);
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 2, GL_FLOAT_, 0, 0, NULL);
        gl.BindVertexArray(0);
    }
    free(rgba);
}

static int loader_gl_up(void)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return 0;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    s_win = SDL_CreateWindow("NV2A", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             LW, LH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!s_win)
        return 0;
    s_ctx = SDL_GL_CreateContext(s_win);
    if (!s_ctx) {
        SDL_DestroyWindow(s_win);
        s_win = NULL;
        return 0;
    }
#define GLP(name) (*(void **)&gl.name = SDL_GL_GetProcAddress("gl" #name))
    if (!GLP(ClearColor) || !GLP(Clear) || !GLP(Scissor) || !GLP(Enable)
            || !GLP(Disable) || !GLP(Viewport) || !GLP(GenTextures)
            || !GLP(BindTexture) || !GLP(TexImage2D) || !GLP(TexParameteri)
            || !GLP(PixelStorei) || !GLP(BindFramebuffer) || !GLP(ActiveTexture)
            || !GLP(GetIntegerv) || !GLP(CreateShader) || !GLP(ShaderSource)
            || !GLP(CompileShader) || !GLP(CreateProgram) || !GLP(AttachShader)
            || !GLP(BindAttribLocation) || !GLP(LinkProgram) || !GLP(GetProgramiv)
            || !GLP(UseProgram) || !GLP(GetUniformLocation) || !GLP(Uniform4f)
            || !GLP(Uniform1i) || !GLP(GenVertexArrays) || !GLP(BindVertexArray)
            || !GLP(GenBuffers) || !GLP(BindBuffer) || !GLP(BufferData)
            || !GLP(EnableVertexAttribArray) || !GLP(VertexAttribPointer)
            || !GLP(DrawArrays) || !GLP(ReadPixels)) {
        gl.Clear = NULL;
        return 0;
    }
#undef GLP
    logo_upload();
    SDL_GL_SetSwapInterval(1);
    return 1;
}

/* The size of the surface actually on screen. SDL on the Switch reports
 * the window as created (1280x720); the EGL surface behind it follows the
 * display -- 1920x1080 docked -- and laying out for the smaller size left
 * the logo in a corner. */
static void screen_size(int *w, int *h)
{
    EGLDisplay d = eglGetCurrentDisplay();
    EGLSurface s = eglGetCurrentSurface(EGL_DRAW);
    EGLint ew = 0, eh = 0;

    *w = LW; *h = LH;
    if (d != EGL_NO_DISPLAY && s != EGL_NO_SURFACE
            && eglQuerySurface(d, s, EGL_WIDTH, &ew) && eglQuerySurface(d, s, EGL_HEIGHT, &eh)
            && ew > 0 && eh > 0) {
        *w = ew; *h = eh;
        return;
    }
    SDL_GL_GetDrawableSize(s_win, w, h);
    if (*w <= 0 || *h <= 0) { *w = LW; *h = LH; }
}

/* For the renderer's present: the same answer. */
int nv2a_gl_screen_size(int *w, int *h)
{
    screen_size(w, h);
    return 1;
}

static void loader_draw(unsigned frame)
{
    /* Laid out on the screen's real size (screen_size). The logo spans half
     * the width, centred, a little above the middle; the bar sits below. */
    int W = LW, H = LH, lw, lh, lx, ly, bar_w, bar_x, bar_y, bar_h, seg, pos, x0, x1;

    screen_size(&W, &H);
    s_vw = W; s_vh = H;
    lw = W / 2;
    lh = lw * NFSU2_LOGO_H / NFSU2_LOGO_W;
    lx = (W - lw) / 2;
    ly = (H - lh) / 2 - H / 24;
    bar_w = W * 9 / 32; bar_h = H / 180 > 2 ? H / 180 : 2;
    bar_x = (W - bar_w) / 2; bar_y = ly + lh + H / 20;
    seg = bar_w / 5;
    pos = (int)(frame * (unsigned)(W / 213 > 1 ? W / 213 : 1) % (unsigned)(bar_w + seg)) - seg;
    x0 = bar_x + (pos < 0 ? 0 : pos);
    x1 = bar_x + pos + seg;
    if (x1 > bar_x + bar_w)
        x1 = bar_x + bar_w;

    gl.Viewport(0, 0, W, H);
    gl.Disable(GL_SCISSOR_TEST_);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
    gl.ClearColor(BG_R, BG_G, BG_B, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT_);
    if (s_logo_prog) {
        /* The renderer's thread calls this too: put back what it relies on. */
        int prog, vao, buf, tex, unit;
        gl.GetIntegerv(GL_CURRENT_PROGRAM_, &prog);
        gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING_, &vao);
        gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING_, &buf);
        gl.GetIntegerv(GL_ACTIVE_TEXTURE_, &unit);
        gl.ActiveTexture(GL_TEXTURE0_);
        gl.GetIntegerv(GL_TEXTURE_BINDING_2D_, &tex);
        gl.Disable(GL_BLEND_);
        gl.Disable(GL_DEPTH_TEST_);
        gl.Disable(GL_CULL_FACE_);
        gl.Disable(GL_STENCIL_TEST_);
        gl.UseProgram(s_logo_prog);
        /* NDC, y up; texture row 0 (bottom-first upload) at the bottom. */
        gl.Uniform4f(s_logo_rect,
                     2.0f * lx / W - 1.0f, 1.0f - 2.0f * (ly + lh) / H,
                     2.0f * (lx + lw) / W - 1.0f, 1.0f - 2.0f * ly / H);
        gl.BindTexture(GL_TEXTURE_2D_, s_logo_tex);
        gl.BindVertexArray(s_logo_vao);
        gl.DrawArrays(GL_TRIANGLE_STRIP_, 0, 4);
        gl.BindVertexArray((unsigned)vao);
        gl.BindBuffer(GL_ARRAY_BUFFER_, (unsigned)buf);
        gl.BindTexture(GL_TEXTURE_2D_, (unsigned)tex);
        gl.ActiveTexture((unsigned)unit);
        gl.UseProgram((unsigned)prog);
    }
    gl.Enable(GL_SCISSOR_TEST_);
    rect(bar_x, bar_y, bar_w, bar_h, 0.14f, 0.16f, 0.18f);
    rect(x0, bar_y, x1 - x0, bar_h, 0.85f, 0.87f, 0.90f);
    gl.Disable(GL_SCISSOR_TEST_);
}

/* The renderer presents with nothing of the title's to show yet (it has
 * the window, the title has not drawn): keep the loading screen up rather
 * than a black frame. Draws into the default framebuffer. */
int nv2a_gl_draw_placeholder(void)
{
    if (!gl.Clear)
        return 0;
    loader_draw((unsigned)(since_boot() * 60.0));
    return 1;
}

static void *loader_main(void *arg)
{
    unsigned frame = 0;
    (void)arg;
    if (!loader_gl_up()) {
        fprintf(stderr, "[switch] loading screen: no GL window (%s)\n", SDL_GetError());
        s_loader_ready = -1;
        return NULL;
    }
    s_loader_ready = 1;
    while (!s_loader_stop) {
        loader_draw(frame++);
        SDL_GL_SwapWindow(s_win);
        {
            SDL_Event e;
            while (SDL_PollEvent(&e)) { }
        }
    }
    SDL_GL_MakeCurrent(s_win, NULL);     /* the renderer's thread takes it */
    return NULL;
}

static void loader_start(void)
{
    if (getenv("NFSU2_LOADER") && strcmp(getenv("NFSU2_LOADER"), "0") == 0)
        return;
    s_loader_running = pthread_create(&s_loader_thread, NULL, loader_main, NULL) == 0;
}

/* The renderer needs the display: stop drawing, give it the window. */
int nv2a_gl_adopt_window(void **win, void **ctx)
{
    if (!s_loader_running)
        return 0;
    while (!s_loader_ready)
        svcSleepThread(1000000ull);
    s_loader_stop = 1;
    pthread_join(s_loader_thread, NULL);
    s_loader_running = 0;
    if (s_loader_ready < 0 || !s_win || !s_ctx)
        return 0;
    printf("[switch] loading screen handed over after %.1f s\n", since_boot());
    *win = s_win;
    *ctx = s_ctx;
    return 1;
}

#else
/* The Vulkan build has no GL context to draw a loading screen on: the
 * renderer takes the display at its first present. */
static void loader_start(void) { }
#endif

static int s_prof_all;          /* RECOMP_NX_PROFILE=2 */
static u64 s_prof_after;        /* RECOMP_NX_PROFILE_AFTER=<s>: ticks since boot */

/* ── Sampling profiler (RECOMP_NX_PROFILE=1) ──────────────────────────
 *
 * The console has no profiler and Eden's costs are not the console's. So:
 * a thread above everything else in the game wakes every millisecond,
 * pauses each busy thread (svcSetThreadActivity), reads its pc, lr and the
 * return address one frame up (x29 chain) with svcGetThreadContext3, and
 * lets it run again. "Busy" is re-decided every 100 ms from the thread tick
 * counts: over 10% of a core. Samples go to sdmc:/switch/nfsu2x/prof.bin
 * every 10 s as 32-byte records (thread slot, pc, lr, 5 return addresses, as offsets
 * into the NRO), and the log names the slots; tools/prof_report.py turns
 * the file into per-function tables with the ELF. Nothing is resolved on
 * the console. Costs a few percent of one core while on. */
int xbox_nx_thread_at(int i, Handle *h, uintptr_t *entry);
/* The NRO load address: _start, the first instruction (ELF address 0).
 * Not __start__, which is an absolute 0 and is not relocated. */
extern void _start(void);

typedef struct { u16 slot, pad; u32 pc, lr, ret[5]; } ProfSample;   /* 32 bytes */
#define PROF_MAX 120000
#define PROF_FRAMES 5
static ProfSample s_prof[PROF_MAX];
static unsigned s_prof_n;

static u32 prof_off(u64 a)
{
    u64 b = (u64)(uintptr_t)&_start;
    return a >= b && a - b < 0xFFFFFFFFull ? (u32)(a - b) : 0xFFFFFFFFu;
}

static void prof_flush(void)
{
    FILE *f;
    if (!s_prof_n)
        return;
    f = fopen("sdmc:/switch/nfsu2x/prof.bin", "ab");
    if (f) {
        fwrite(s_prof, sizeof s_prof[0], s_prof_n, f);
        fclose(f);
    }
    fprintf(stderr, "[prof] %u samples written\n", s_prof_n);
    s_prof_n = 0;
}

static void prof_thread(void *arg)
{
    enum { MAXT = 64 };
    Handle h[MAXT];
    u64 last_ticks[MAXT] = {0};
    int busy[MAXT] = {0}, n = 0, i;
    u64 t_list = 0, t_flush = armGetSystemTick(), freq = armGetSystemTickFreq();
    (void)arg;
    remove("sdmc:/switch/nfsu2x/prof.bin");
    for (;;) {
        u64 now;
        svcSleepThread(1000000ull);
        now = armGetSystemTick();
        if (now - s_t0 < s_prof_after) {             /* RECOMP_NX_PROFILE_AFTER */
            t_flush = now;
            continue;
        }
        if (now - t_list > freq / 10) {             /* who is busy */
            u64 span = now - t_list;
            uintptr_t entry;
            int counted = 0;
            for (n = 0; n < MAXT && xbox_nx_thread_at(n, &h[n], &entry); n++) {
                u64 t = 0;
                if (R_FAILED(svcGetInfo(&t, InfoType_ThreadTickCount, h[n], (u64)-1)))
                    t = 0;
                counted |= t != 0;
                /* thread ticks count at 19.2 MHz, like the system tick */
                /* over 10% of a core; RECOMP_NX_PROFILE=2: over 2% (the DPC
                 * and interrupt threads, which are rarely busy but hold the
                 * dispatch lock and the guest lock when they run) */
                busy[n] = t_list && (t - last_ticks[n]) * (s_prof_all ? 50 : 10) > span;
                last_ticks[n] = t;
            }
            if (!counted)                           /* Eden: no tick counts */
                for (i = 0; i < n; i++)
                    busy[i] = i == 0;       /* the game's main thread only: pausing
                                             * every thread each ms hung Eden's boot */
            t_list = now;
        }
        for (i = 0; i < n && s_prof_n < PROF_MAX; i++) {
            ThreadContext ctx;
            if (!busy[i] || R_FAILED(svcSetThreadActivity(h[i], ThreadActivity_Paused)))
                continue;
            if (R_SUCCEEDED(svcGetThreadContext3(&ctx, h[i]))) {
                ProfSample *o = &s_prof[s_prof_n++];
                MemoryInfo mi;
                u32 pi;
                o->slot = (u16)i;
                /* when: 10 ms units since boot (wraps after 655 s) */
                o->pad = (u16)((armGetSystemTick() - s_t0) / 192000u);
                o->pc = prof_off(ctx.pc.x);
                o->lr = prof_off(ctx.lr);
                memset(o->ret, 0xFF, sizeof o->ret);
                /* The x29 chain, within the stack's own mapping: the thread
                 * is paused, and its stack is our memory. */
                if ((ctx.fp & 7) == 0 && R_SUCCEEDED(svcQueryMemory(&mi, &pi, ctx.fp))
                    && (mi.perm & Perm_R)) {
                    u64 fp = ctx.fp, lo = mi.addr, hi = mi.addr + mi.size;
                    int k;
                    for (k = 0; k < PROF_FRAMES && fp >= lo && fp + 16 <= hi && !(fp & 7); k++) {
                        const u64 *fr = (const u64 *)(uintptr_t)fp;
                        o->ret[k] = prof_off(fr[1]);
                        if (fr[0] <= fp)
                            break;
                        fp = fr[0];
                    }
                }
            }
            svcSetThreadActivity(h[i], ThreadActivity_Runnable);
        }
        if (now - t_flush > freq * 10) {
            uintptr_t entry;
            Handle hh;
            char line[1024];
            int len = snprintf(line, sizeof line, "[prof] slots:");
            for (i = 0; i < MAXT && xbox_nx_thread_at(i, &hh, &entry) && len < 1000; i++)
                len += snprintf(line + len, sizeof line - len, " %d=%x", i,
                                entry ? prof_off(entry) : 0u);
            fprintf(stderr, "%s\n", line);
            prof_flush();
            t_flush = armGetSystemTick();
        }
    }
}

static void prof_start(void)
{
    static Thread t;
    const char *e = getenv("RECOMP_NX_PROFILE");
    if (!e || (*e != '1' && *e != '2'))
        return;
    s_prof_all = *e == '2';
    /* Sample only from <s> seconds after boot: a problem that starts late
     * in a long session (the 1 kHz file grows ~1 MB per 10 s). */
    if ((e = getenv("RECOMP_NX_PROFILE_AFTER")) && atoi(e) > 0)
        s_prof_after = (u64)atoi(e) * armGetSystemTickFreq();
    /* 0x2A: above every game and host thread, so it runs on time. */
    if (R_FAILED(threadCreate(&t, prof_thread, NULL, NULL, 0x4000, 0x2A, -2))
        || R_FAILED(threadStart(&t))) {
        fprintf(stderr, "[prof] could not start the profiler thread\n");
        return;
    }
    fprintf(stderr, "[prof] sampling busy threads at 1 kHz into prof.bin (NRO base %p)\n",
            (void *)&_start);
}

/* ── Clocks ───────────────────────────────────────────────────────
 *
 * NFSU2_CPU_MHZ / NFSU2_GPU_MHZ / NFSU2_MEM_MHZ in nfsu2x_env.txt: run the
 * CPU, GPU and memory at that clock (the closest the hardware lists, never
 * above the request; capped at 1785 / 921.6 / 1600 MHz). Unset = the
 * system's (CPU 1020 MHz in every official profile that has a usable GPU).
 * The CPU is what limits this port: the game thread, the pushbuffer
 * executor and the GL/Vulkan thread are all CPU-bound. Through clkrst
 * (8.0.0+) or pcv, as sys-clk does; the old rates come back on exit. The
 * system resets clocks on dock/undock and after sleep, so a thread applies
 * them again every second while the game has focus. More heat and battery:
 * opt-in. sys-clk, when installed, may override them with its own profile. */
static struct {
    const char *env, *name;
    PcvModule mod;
    PcvModuleId id;
    u32 cap_hz, want_hz, old_hz, set_hz;
    int open;
    ClkrstSession s;
} s_clk[3] = {
    { "NFSU2_CPU_MHZ", "CPU", PcvModule_CpuBus, PcvModuleId_CpuBus, 1785000000u },
    { "NFSU2_GPU_MHZ", "GPU", PcvModule_GPU,    PcvModuleId_GPU,    921600000u },
    { "NFSU2_MEM_MHZ", "memory", PcvModule_EMC, PcvModuleId_EMC,    1600000000u },
};
static int s_clk_rst = -1;                 /* 1 clkrst, 0 pcv, -1 not up */
static volatile int s_clk_stop;
static Thread s_clk_thread;
static int s_clk_thread_up;

static Result clk_get(int i, u32 *hz)
{
    return s_clk_rst ? clkrstGetClockRate(&s_clk[i].s, hz) : pcvGetClockRate(s_clk[i].mod, hz);
}

static Result clk_set(int i, u32 hz)
{
    return s_clk_rst ? clkrstSetClockRate(&s_clk[i].s, hz) : pcvSetClockRate(s_clk[i].mod, hz);
}

/* The highest listed rate not above want (want itself when there is no list). */
static u32 clk_pick(int i, u32 want)
{
    u32 rates[64], best = 0;
    s32 n = 0;
    PcvClockRatesListType type;
    Result rc = s_clk_rst
        ? clkrstGetPossibleClockRates(&s_clk[i].s, rates, 64, &type, &n)
        : pcvGetPossibleClockRates(s_clk[i].mod, rates, 64, &type, &n);
    if (R_FAILED(rc) || n <= 0 || type != PcvClockRatesListType_Discrete)
        return want;
    for (s32 k = 0; k < n; k++)
        if (rates[k] <= want && rates[k] > best)
            best = rates[k];
    return best ? best : want;
}

static void clk_restore(void)
{
    int i;
    if (s_clk_rst < 0)
        return;
    s_clk_stop = 1;
    if (s_clk_thread_up) {
        threadWaitForExit(&s_clk_thread);
        threadClose(&s_clk_thread);
        s_clk_thread_up = 0;
    }
    for (i = 0; i < 3; i++)
        if (s_clk[i].set_hz && s_clk[i].old_hz)
            clk_set(i, s_clk[i].old_hz);
    for (i = 0; i < 3; i++)
        if (s_clk[i].open)
            clkrstCloseSession(&s_clk[i].s);
    if (s_clk_rst)
        clkrstExit();
    else
        pcvExit();
    s_clk_rst = -1;
}

static void clk_keeper(void *arg)
{
    unsigned logged = 0;
    (void)arg;
    while (!s_clk_stop) {
        svcSleepThread(1000000000ull);
        if (s_clk_stop || appletGetFocusState() != AppletFocusState_InFocus)
            continue;
        for (int i = 0; i < 3; i++) {
            u32 hz = 0;
            if (!s_clk[i].set_hz || R_FAILED(clk_get(i, &hz)) || hz == s_clk[i].set_hz)
                continue;
            clk_set(i, s_clk[i].set_hz);
            if (logged++ < 8)
                printf("[clock] %s was reset to %u MHz, back to %u MHz\n", s_clk[i].name,
                       hz / 1000000u, s_clk[i].set_hz / 1000000u);
        }
    }
}

static void clk_apply(void)
{
    int i, any = 0;
    for (i = 0; i < 3; i++) {
        const char *e = getenv(s_clk[i].env);
        double mhz = e ? atof(e) : 0;
        s_clk[i].want_hz = mhz > 0 ? (u32)(mhz * 1e6 + 0.5) : 0;
        if (s_clk[i].want_hz > s_clk[i].cap_hz)
            s_clk[i].want_hz = s_clk[i].cap_hz;
        any |= s_clk[i].want_hz != 0;
    }
    if (!any)
        return;
    if (hosversionAtLeast(8, 0, 0) && R_SUCCEEDED(clkrstInitialize()))
        s_clk_rst = 1;
    else if (R_SUCCEEDED(pcvInitialize()))
        s_clk_rst = 0;
    else {
        printf("[clock] neither clkrst nor pcv is available: clocks unchanged\n");
        return;
    }
    for (i = 0; i < 3; i++) {
        u32 hz = 0, now = 0;
        Result rc;
        if (!s_clk[i].want_hz)
            continue;
        if (s_clk_rst) {
            if (R_FAILED(rc = clkrstOpenSession(&s_clk[i].s, s_clk[i].id, 3))) {
                printf("[clock] %s: no clkrst session (0x%X)\n", s_clk[i].name, rc);
                continue;
            }
            s_clk[i].open = 1;
        }
        clk_get(i, &s_clk[i].old_hz);
        hz = clk_pick(i, s_clk[i].want_hz);
        rc = clk_set(i, hz);
        clk_get(i, &now);
        if (R_SUCCEEDED(rc))
            s_clk[i].set_hz = now ? now : hz;
        printf("[clock] %s %u -> %u MHz (asked %u)%s\n", s_clk[i].name,
               s_clk[i].old_hz / 1000000u, now / 1000000u, s_clk[i].want_hz / 1000000u,
               R_FAILED(rc) ? " -- refused" : "");
    }
    atexit(clk_restore);
    if (R_SUCCEEDED(threadCreate(&s_clk_thread, clk_keeper, NULL, NULL, 0x4000, 0x3F, -2))
        && R_SUCCEEDED(threadStart(&s_clk_thread)))
        s_clk_thread_up = 1;
}

void switch_boot(void)
{
    s_t0 = armGetSystemTick();
    socketInitializeDefault();
    s_nxlink = nxlinkStdio();
    if (s_nxlink < 0 && !log_open())
        fprintf(stderr, "[switch] log device unavailable\n");
    load_env();
    s_log_sync = getenv("NFSU2_LOG_SYNC") && strcmp(getenv("NFSU2_LOG_SYNC"), "1") == 0;
    {
        const char *a = getenv("NFSU2_NO_LOG"), *b = getenv("NFSU2_LOG");
        if ((a && *a == '1') || (b && *b == '0')) {
            printf("[switch] logging off (NFSU2_NO_LOG): no log, [perf] or profiler\n");
            setenv("RECOMP_NO_LOG", "1", 1);     /* the kernel's own log file */
            setenv("RECOMP_QUIET", "1", 1);
            unsetenv("RECOMP_NX_PROFILE");
            unsetenv("NFSU2_LOG_SYNC");
            s_log_sync = 0;
            log_flush(1);
            s_log_off = 1;
        }
    }
    {
        u64 total = 0, used = 0;
        AppletType at = appletGetAppletType();
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        printf("[switch] applet type %d (%s), memory %lu MB total / %lu MB used\n",
               (int)at, (at == AppletType_Application || at == AppletType_SystemApplication)
                        ? "title override, full memory"
                        : "applet mode: launch via a game title (hold R) for full memory",
               (unsigned long)(total >> 20), (unsigned long)(used >> 20));
    }
    clk_apply();
    loader_start();
    prof_start();
}

void switch_error_dialog(const char *msg, const char *details)
{
    AppletType at = appletGetAppletType();
    Result rc;
    log_flush(1);
    if (at == AppletType_Application || at == AppletType_SystemApplication) {
        ErrorApplicationConfig c;
        rc = errorApplicationCreate(&c, msg, details);
        if (R_SUCCEEDED(rc))
            rc = errorApplicationShow(&c);
    } else {
        ErrorSystemConfig c;
        rc = errorSystemCreate(&c, msg, details);
        if (R_SUCCEEDED(rc))
            rc = errorSystemShow(&c);
    }
    if (R_FAILED(rc))
        fprintf(stderr, "[switch] error applet failed (0x%X)\n", rc);
}

void switch_shutdown(void)
{
    clk_restore();
    log_flush(1);
    socketExit();
}

/* libnx calls this for any CPU exception in the application. Print what we
 * can, then let the process die so the log's last lines are the report. */
alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    fprintf(stderr, "[CRASH] exception %u at PC=0x%lx far=0x%lx (guest VA 0x%08X)\n",
            ctx->error_desc, (unsigned long)ctx->pc.x, (unsigned long)ctx->far.x,
            (uint32_t)(ctx->far.x - (uintptr_t)g_xbox_mem_offset));
    fprintf(stderr, "  lr=0x%lx sp=0x%lx  (switch_boot at %p: subtract to get an ELF address)\n",
            (unsigned long)ctx->lr.x, (unsigned long)ctx->sp.x, (void *)switch_boot);
    print_guest_state();
    log_flush(0);
    svcExitProcess();
}
#endif
