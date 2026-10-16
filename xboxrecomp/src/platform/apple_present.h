/* Apple presentation policy shared by Metal and MoltenVK. */
#ifndef XBOX_APPLE_PRESENT_H
#define XBOX_APPLE_PRESENT_H
#include <TargetConditionals.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline uint64_t xbox_apple_present_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

/* Zero (the default) removes host pacing. An explicit positive cap remains
 * available without touching the audio or simulation clock. */
static inline unsigned xbox_apple_present_fps_limit(void)
{
    static int limit = -1;
    if (limit < 0) {
        const char *e = getenv("NFSU2_FPS_LIMIT");
        char *end = NULL;
        long fps = e ? strtol(e, &end, 10) : 0;
        if (e && (end == e || *end || fps < 0 || fps > 1000)) fps = 0;
        limit = (int)fps;
    }
    return (unsigned)limit;
}

/* XDK software flip command: low five bits are the operation; bits 5..7
 * specify the vblank interval and bit 8 selects immediate retirement. Other
 * bits carry the surface/flags and must be preserved. */
static inline uint32_t xbox_apple_immediate_flip(uint32_t command)
{
    return xbox_apple_present_fps_limit() == 0 && (command & 31u) == 1u
        ? (command & ~0xE0u) | 0x100u : command;
}

static inline void xbox_apple_limit_frame(void)
{
    static uint64_t last;
    unsigned fps = xbox_apple_present_fps_limit();
    if (!fps) return;
    uint64_t interval = (1000000000ull + fps - 1) / fps;
    uint64_t now;
    now = xbox_apple_present_now();
    while (last && now - last < interval) {
        uint64_t remain = interval - (now - last);
        struct timespec t = { (time_t)(remain / 1000000000ull), (long)(remain % 1000000000ull) };
        if (nanosleep(&t, NULL) != 0 && errno != EINTR) break;
        now = xbox_apple_present_now();
    }
    last = now;
}

/* Fill uses the entire display without cutting off the HUD. On wider phones
 * this stretches the 16:9 picture; NFSU2_SCREEN=fit preserves its aspect. */
static inline int xbox_apple_screen_fill(void)
{
    static int fill = -1;
    if (fill < 0) {
        const char *e = getenv("NFSU2_SCREEN");
        fill = e ? !strcmp(e, "fill") : TARGET_OS_IPHONE;
    }
    return fill;
}
#endif
