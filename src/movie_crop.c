/**
 * Movie crop to 16:9
 *
 * Draws that sample the movie texture (the A8R8G8B8 picture sub_0025F0B7
 * fills from the VP6 frame) go through nv2a_raw_batch_hook.
 *
 * In 16:9 the game's movie player draws every movie as a 4:3 quad at 90%
 * (clip x +-1 through a vertex program: 864 of the 1280 surface columns),
 * pillarboxed. Scaling that quad's positions by 1/0.675 makes it as wide as
 * the screen. The intro trailer (MOVIES\FMVOpening.vp6) is a ~1.85:1 film
 * letterboxed into its 640x480 picture and then crops ~6 rows top and
 * bottom; every other movie on the disc (logos, PSA, fly-overs, tutorials,
 * the career story scenes) is full 4:3 and loses ~65 rows top and bottom,
 * centred. Only in widescreen, only the player's quad (prim 7, 4 vertices,
 * x = +-1); movies drawn into a smaller window keep their size.
 * NFSU2_MOVIE_CROP=0 off, NFSU2_MOVIE_TRACE=1 logs the movie draws.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../xboxrecomp/src/kernel/nv2a_backend.h"

extern uint32_t nfsu2_movie_row;
extern char nfsu2_movie_name[64];

#define MOVIE_BYTES (640u * 480u * 4u)

/* Attribute a of vertex i as float4. */
static void attr_get(const Nv2aRawBatch *rb, const float *attrs, uint32_t a,
                     uint32_t i, float o[4])
{
    if (rb->attr_direct & (1u << a)) {
        const uint8_t *p = rb->direct[a].ptr + (size_t)i * rb->direct[a].stride;
        uint32_t k;
        o[0] = o[1] = o[2] = 0.0f;
        o[3] = 1.0f;
        if (rb->direct[a].type == 2)
            for (k = 0; k < rb->direct[a].size; k++)
                memcpy(&o[k], p + 4 * k, 4);
        return;
    }
    memmove(o, attrs + ((size_t)i * NV2A_RAW_ATTRS + a) * 4, 16);  /* o may be that entry */
}

/* 1 / (0.75 * 0.9): the 4:3 box at 90% to the full 16:9 width */
#define CROP_SCALE (1.0f / 0.675f)

static int crop_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("NFSU2_MOVIE_CROP");
        on = !(e && *e == '0');
    }
    return on && xbox_video_widescreen();
}

/* The movie player's full-screen quad: every vertex at clip x = +-1. */
static int player_quad(const Nv2aRawBatch *rb, const float *attrs)
{
    uint32_t i;
    for (i = 0; i < 4; i++) {
        float v[4];
        attr_get(rb, attrs, 0, i, v);
        if (v[0] < 0.0f) v[0] = -v[0];
        if (v[0] < 0.99f || v[0] > 1.01f)
            return 0;
    }
    return 1;
}

static void movie_hook(Nv2aRawBatch *rb, float *attrs)
{
    static int trace = -1;
    static unsigned long seen;
    uint32_t t = rb->tex_va[0], i, a;

    if (trace < 0) {
        const char *e = getenv("NFSU2_MOVIE_TRACE");
        trace = e && *e == '1';
    }
    if (!t || nfsu2_movie_row < t || nfsu2_movie_row >= t + MOVIE_BYTES)
        return;
    if (crop_on() && rb->prim == 7 && rb->vertex_count == 4 && (rb->attr_present & 1u)
        && player_quad(rb, attrs)) {
        for (i = 0; i < 4; i++) {
            float *p = attrs + (size_t)i * NV2A_RAW_ATTRS * 4;
            attr_get(rb, attrs, 0, i, p);
            p[0] *= CROP_SCALE;
            p[1] *= CROP_SCALE;
        }
        rb->attr_direct &= (uint16_t)~1u;
    }
    if (trace && (seen++ % 120) < 3) {
        fprintf(stderr, "[movie] draw %s: tex %08X xform %u prim %u nv %u present %04X direct %04X surf %ux%u aa %ux%u\n",
                nfsu2_movie_name, t, rb->xform, rb->prim, rb->vertex_count,
                rb->attr_present, rb->attr_direct, rb->surface.width, rb->surface.height,
                rb->surface.aa_sx, rb->surface.aa_sy);
        for (i = 0; i < rb->vertex_count && i < 6; i++)
            for (a = 0; a < NV2A_RAW_ATTRS; a++) {
                float v[4];
                if (!(rb->attr_present & (1u << a)))
                    continue;
                attr_get(rb, attrs, a, i, v);
                fprintf(stderr, "[movie]   v%u a%u type %u: %g %g %g %g\n", i, a,
                        (rb->attr_direct & (1u << a)) ? rb->direct[a].type : 99,
                        v[0], v[1], v[2], v[3]);
            }
    }
}

__attribute__((constructor)) static void movie_crop_init(void)
{
    nv2a_raw_batch_hook = movie_hook;
}
