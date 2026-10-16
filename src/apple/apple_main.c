/*
 * apple_main.c -- macOS host for the NFSU2 recompilation (iOS later).
 *
 * AppKit (and UIKit) own the main thread: windows are created there and
 * events are pumped there. Linux runs the title on main() and lets the
 * renderer make its window on the pushbuffer executor's thread; AppKit
 * refuses that. So here, as on the Switch, the title runs on a 16 MB thread
 * of its own, and the main thread makes the window, hands it to the renderer
 * (nv2a_gl_adopt_window, the hook the Switch loading screen uses) and pumps
 * events until the title returns.
 *
 * The main thread must not block anywhere but SDL_WaitEvent: SDL's Cocoa GL
 * backend dispatch_sync()s [NSOpenGLContext update]/setView from the render
 * thread to the main queue, which only runs while the main loop does.
 *
 * With NFSU2_METAL built in, the native Metal renderer (nv2a_metal) is used
 * unless NFSU2_RENDERER=vulkan picks Vulkan/MoltenVK; its view and layer are
 * made here on the main thread as the Vulkan surface is.
 */
#if defined(__APPLE__)

#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#if !defined(NFSU2_VULKAN)
#error "iOS renders with Vulkan (MoltenVK): configure with -DNFSU2_VULKAN=ON"
#endif
void ios_host_setup(void);              /* src/ios/ios_host.c */
int ios_attach_window_to_scene(SDL_Window *window);   /* src/ios/ios_scene.m */
void ios_touch_attach(SDL_Window *window);            /* src/ios/ios_touch.m */
void ios_touch_controllers_changed(void);
void ios_touch_update_auto(void);
int ios_import_game(const char *game_dir);            /* src/ios/ios_import.m */
void ios_verify_game(const char *game_dir);
#endif

int nfsu2_game_main(void);              /* main.c */
#if !TARGET_OS_IPHONE
void apple_keyboard_controllers_changed(void);           /* apple_keyboard.c */
void apple_keyboard_event(const SDL_Event *e);
#endif
void apple_perf_start(void);            /* apple_perf.m: [perf] lines */

static SDL_Window   *s_win;
#if !defined(NFSU2_VULKAN)
static SDL_GLContext s_ctx;
#endif
static int           s_rc = 1;
static Uint32        s_done_event;
static int           s_metal;            /* NFSU2_RENDERER=metal */

/* src/main.c: which renderer to install. */
int nfsu2_apple_metal(void)
{
    return s_metal;
}

#if !defined(NFSU2_VULKAN)
/* nv2a_gl.c: a weak default returns 0 and the renderer would create its own
 * window on the executor thread. */
int nv2a_gl_adopt_window(void **win, void **ctx)
{
    if (!s_win || !s_ctx)
        return 0;
    *win = s_win;
    *ctx = s_ctx;
    return 1;
}
#else
#include <SDL_vulkan.h>
#include <dispatch/dispatch.h>

/* nv2a_vk.c: the window, and its surface made on the main thread (SDL adds
 * a Metal view to the window for it). The renderer asks from its own thread;
 * the main thread serves the main queue while it waits for events. */
int nv2a_vk_adopt_window(void **win)
{
    if (!s_win)
        return 0;
    *win = s_win;
    return 1;
}

typedef struct { VkInstance inst; VkSurfaceKHR *surface; SDL_bool ok; } SurfaceReq;

static void make_surface(void *arg)
{
    SurfaceReq *r = (SurfaceReq *)arg;
    r->ok = SDL_Vulkan_CreateSurface(s_win, r->inst, r->surface);
#if TARGET_OS_IPHONE
    if (r->ok)
        ios_touch_attach(s_win);        /* above the Metal view SDL just added */
#endif
}

int nv2a_vk_host_surface(VkInstance inst, VkSurfaceKHR *surface)
{
    SurfaceReq r = { inst, surface, SDL_FALSE };
    if (!s_win)
        return 0;
    if (pthread_main_np())
        make_surface(&r);
    else
        dispatch_sync_f(dispatch_get_main_queue(), &r, make_surface);
    if (!r.ok)
        fprintf(stderr, "[apple] Vulkan surface: %s\n", SDL_GetError());
    return r.ok ? 1 : 0;
}
#endif

#if defined(NFSU2_METAL)
#include <SDL_metal.h>

/* nv2a_metal.m: the window's CAMetalLayer, from a Metal view SDL adds on the
 * main thread (as for the Vulkan surface). */
static void *s_layer;

static void make_layer(void *arg)
{
    SDL_MetalView view;
    (void)arg;
    view = SDL_Metal_CreateView(s_win);
    if (view)
        s_layer = SDL_Metal_GetLayer(view);
#if TARGET_OS_IPHONE
    if (s_layer)
        ios_touch_attach(s_win);        /* above the Metal view SDL just added */
#endif
}

void *nv2a_mtl_host_layer(void)
{
    if (!s_win)
        return NULL;
    if (pthread_main_np())
        make_layer(NULL);
    else
        dispatch_sync_f(dispatch_get_main_queue(), NULL, make_layer);
    if (!s_layer)
        fprintf(stderr, "[apple] Metal view: %s\n", SDL_GetError());
    return s_layer;
}
#endif

#if TARGET_OS_IPHONE
/* iOS kills an app that submits GPU work from the background (the device
 * is lost: kIOGPUCommandBufferCallbackErrorBackgroundExecutionNotPermitted).
 * SDL reports the transitions on the main thread; the renderer's gate holds
 * its next acquire/submit until the app is in front again.
 *
 * The guest itself is paused too, and the sound: a thread of ours takes the
 * guest lock (kernel_bridge.c) and keeps it while the app is away, so no
 * guest thread runs -- the main thread must not wait for it itself -- and
 * the audio device stops. Otherwise the APU kept looping the title's sound
 * buffers, which nothing refilled, until iOS suspended the app. */
static pthread_mutex_t s_fg_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_fg_cond = PTHREAD_COND_INITIALIZER;
static int             s_background;
static int             s_controls_editor;

void xbox_gil_enter(void);              /* kernel_bridge.c: the guest lock */
void xbox_gil_leave(void);
void xa2_pause(int on);                 /* apu_xaudio2.c */

static void *pause_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&s_fg_lock);
    for (;;) {
        while (!(s_background || s_controls_editor))
            pthread_cond_wait(&s_fg_cond, &s_fg_lock);
        pthread_mutex_unlock(&s_fg_lock);
        xbox_gil_enter();               /* each guest thread stops at its next yield */
        pthread_mutex_lock(&s_fg_lock);
        if (s_background || s_controls_editor)
            fprintf(stderr, "[ios] guest paused\n");
        while (s_background || s_controls_editor)
            pthread_cond_wait(&s_fg_cond, &s_fg_lock);
        pthread_mutex_unlock(&s_fg_lock);
        xbox_gil_leave();
        pthread_mutex_lock(&s_fg_lock);
    }
    return NULL;
}

/* Main-thread UI request; pause ownership composes with backgrounding. */
void ios_controls_editor_pause(int on)
{
    pthread_mutex_lock(&s_fg_lock);
    s_controls_editor = on != 0;
    int paused = s_background || s_controls_editor;
    pthread_cond_broadcast(&s_fg_cond);
    pthread_mutex_unlock(&s_fg_lock);
    xa2_pause(paused);
}

static int app_events(void *userdata, SDL_Event *e)
{
    (void)userdata;
    if (e->type == SDL_APP_WILLENTERBACKGROUND || e->type == SDL_APP_DIDENTERFOREGROUND) {
        pthread_mutex_lock(&s_fg_lock);
        s_background = e->type == SDL_APP_WILLENTERBACKGROUND;
        int paused = s_background || s_controls_editor;
        pthread_cond_broadcast(&s_fg_cond);
        pthread_mutex_unlock(&s_fg_lock);
        xa2_pause(paused);
        fprintf(stderr, "[ios] %s\n", e->type == SDL_APP_WILLENTERBACKGROUND
                ? "background: GPU work, guest and audio paused" : "foreground");
    }
    return 1;
}

void nv2a_vk_host_gate(void)
{
    pthread_mutex_lock(&s_fg_lock);
    while (s_background || s_controls_editor)
        pthread_cond_wait(&s_fg_cond, &s_fg_lock);
    pthread_mutex_unlock(&s_fg_lock);
}

#if defined(NFSU2_METAL)
void nv2a_mtl_host_gate(void)
{
    nv2a_vk_host_gate();
}
#endif
#endif

static void *game_thread(void *arg)
{
    SDL_Event e;

    (void)arg;
    pthread_setname_np("game");         /* the title's main thread, in [perf] lines */
    {
        /* A game thread in the user-interactive class, below audio (apple_qos.c). */
        extern void xbox_apple_thread_qos(int priority);
        xbox_apple_thread_qos(0);
    }
    s_rc = nfsu2_game_main();
    SDL_zero(e);
    e.type = s_done_event;
    SDL_PushEvent(&e);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_attr_t attr;
    pthread_t th;

    (void)argc;
    (void)argv;
#if TARGET_OS_IPHONE
    ios_host_setup();                   /* paths, settings file, log */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    SDL_SetHint(SDL_HINT_IDLE_TIMER_DISABLED, "1");   /* no auto-lock during movies */
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#endif
    /* Controllers too: macOS delivers their arrival and input through the
     * main run loop, which only this thread pumps. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[apple] SDL video: %s\n", SDL_GetError());
        return 1;
    }
#if TARGET_OS_IPHONE
    SDL_AddEventWatch(app_events, NULL);
    if (getenv("NFSU2_GAME_DIR"))       /* the user's disc, picked in the app if missing */
        ios_import_game(getenv("NFSU2_GAME_DIR"));
    if (getenv("NFSU2_VERIFY") && *getenv("NFSU2_VERIFY") == '1' && getenv("NFSU2_GAME_DIR"))
        ios_verify_game(getenv("NFSU2_GAME_DIR"));      /* checksums of the disc files */
#endif
#if defined(NFSU2_METAL)
    {
        /* Metal by default; NFSU2_RENDERER=vulkan keeps MoltenVK. */
        const char *r = getenv("NFSU2_RENDERER");
        if (!(r && !strcmp(r, "vulkan"))) {
            if (__builtin_available(macOS 14.0, iOS 17.0, *))
                s_metal = 1;
            else
                fprintf(stderr, "[apple] the Metal renderer needs macOS 14 / iOS 17\n");
        }
        fprintf(stderr, "[apple] renderer: %s\n", s_metal ? "Metal" : "Vulkan (MoltenVK)");
    }
#endif
#if !defined(NFSU2_VULKAN)
    /* The attributes nv2a_gl.c asks for when it makes the window itself. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    s_win = SDL_CreateWindow("NV2A", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (s_win)
        s_ctx = SDL_GL_CreateContext(s_win);
    if (!s_ctx) {
        fprintf(stderr, "[apple] GL window/context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(s_win, NULL);    /* the executor thread takes it */
#else
    {
        const char *h = getenv("RECOMP_VK_HEADLESS");
        if (!(h && *h == '1')) {
#if TARGET_OS_IPHONE
            s_win = SDL_CreateWindow("NFSU2", 0, 0, 0, 0,
                                     (s_metal ? SDL_WINDOW_METAL : SDL_WINDOW_VULKAN) |
                                     SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI |
                                     SDL_WINDOW_BORDERLESS);
            if (s_win)
                ios_attach_window_to_scene(s_win);
#else
            s_win = SDL_CreateWindow(s_metal ? "NV2A (Metal)" : "NV2A (Vulkan)",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                     (s_metal ? SDL_WINDOW_METAL : SDL_WINDOW_VULKAN) |
                                     SDL_WINDOW_RESIZABLE);
#endif
            if (!s_win)
                fprintf(stderr, "[apple] Vulkan window: %s\n", SDL_GetError());
        }
    }
#endif
#if !TARGET_OS_IPHONE
    {
        const char *e = getenv("NFSU2_FULLSCREEN");
        if (s_win && !(e && !strcmp(e, "0")))
            SDL_SetWindowFullscreen(s_win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
#endif
    s_done_event = SDL_RegisterEvents(1);

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u * 1024 * 1024);
    if (pthread_create(&th, &attr, game_thread, NULL) != 0) {
        fprintf(stderr, "[FATAL] cannot start the game thread\n");
        return 1;
    }
    pthread_detach(th);
#if TARGET_OS_IPHONE
    if (pthread_create(&th, NULL, pause_thread, NULL) == 0)
        pthread_detach(th);
#endif
    pthread_attr_destroy(&attr);
    apple_perf_start();

#if !TARGET_OS_IPHONE
    apple_keyboard_controllers_changed();   /* the keyboard as a pad (NFSU2_KEYBOARD=0: off) */
#endif
    for (;;) {
        SDL_Event e;
#if TARGET_OS_IPHONE
        int got_event = SDL_WaitEventTimeout(&e, 50);
        ios_touch_update_auto();
        if (!got_event) continue;
#else
        if (!SDL_WaitEvent(&e)) continue;
#endif
        if (e.type == SDL_QUIT) {
            fprintf(stderr, "[apple] window closed\n");
            fflush(stdout);
            fflush(stderr);
            _exit(0);
        }
        if (e.type == s_done_event)
            return s_rc;
#if TARGET_OS_IPHONE
        if (e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED)
            ios_touch_controllers_changed();
#else
        if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F11 && !e.key.repeat && s_win)
            SDL_SetWindowFullscreen(s_win, (SDL_GetWindowFlags(s_win) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                           ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        apple_keyboard_event(&e);
        if (e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED)
            apple_keyboard_controllers_changed();
#endif
    }
}

#endif /* __APPLE__ */
