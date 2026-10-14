#ifndef NFSU2_FPS_OVERLAY_H
#define NFSU2_FPS_OVERLAY_H
#include <stdint.h>
#include <stdio.h>
#define FPS_OVERLAY_W 116u
#define FPS_OVERLAY_H 22u
typedef struct {
    uint64_t window_ns;
    uint32_t frames, tenths;
    int started, valid;
} FpsCounter;

/* Count successful presentation calls against monotonic wall time, twice a second.
 * The first presentation starts the window and is not counted twice. */
static void fps_counter_present(FpsCounter *c, uint64_t now)
{
    if (!c->started || now < c->window_ns) {
        c->started = 1; c->window_ns = now; c->frames = 0;
        return;
    }
    ++c->frames;
    uint64_t elapsed = now - c->window_ns;
    if (elapsed >= 500000000ull) {
        uint64_t n = ((uint64_t)c->frames * 10000000000ull + elapsed / 2) / elapsed;
        c->tenths = (uint32_t)(n > 9999 ? 9999 : n);
        c->valid = 1; c->frames = 0; c->window_ns = now;
    }
}

static void fps_overlay_pixels(uint32_t *pixels, uint32_t tenths, int valid)
{
    /* 5x7 font: digits, F, P, S, dot, dash, space. Greys have the same
     * byte order in RGBA8 and BGRA8 swapchains. No external font assets. */
    static const uint8_t rows[16][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
        {31,16,16,30,16,16,16}, {30,17,17,30,16,16,16},
        {15,16,16,14,1,1,30}, {0,0,0,0,0,12,12},
        {0,0,0,31,0,0,0}, {0,0,0,0,0,0,0}
    };
    char text[16];
    if (valid) snprintf(text, sizeof text, "%u.%u FPS", tenths / 10, tenths % 10);
    else snprintf(text, sizeof text, "--.- FPS");
    for (unsigned i = 0; i < FPS_OVERLAY_W * FPS_OVERLAY_H; ++i) pixels[i] = 0xff151515u;
    for (unsigned i = 0; text[i] && i < 9; ++i) {
        unsigned g = text[i] >= '0' && text[i] <= '9' ? (unsigned)(text[i] - '0')
                   : text[i] == 'F' ? 10 : text[i] == 'P' ? 11 : text[i] == 'S' ? 12
                   : text[i] == '.' ? 13 : text[i] == '-' ? 14 : 15;
        for (unsigned y = 0; y < 7; ++y)
            for (unsigned x = 0; x < 5; ++x)
                if (rows[g][y] & (1u << (4 - x)))
                    for (unsigned dy = 0; dy < 2; ++dy)
                        for (unsigned dx = 0; dx < 2; ++dx)
                            pixels[(4 + 2*y + dy) * FPS_OVERLAY_W + 4 + 12*i + 2*x + dx] = 0xffffffffu;
    }
}
#endif
