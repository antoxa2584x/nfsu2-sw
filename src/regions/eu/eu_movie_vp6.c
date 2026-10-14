/* EU mapping of the existing US v0.5 native VP6 movie wrapper.
 * Upstream: antoxa2584x/nfsu2-sw, commit
 * 48b1b2798d60f5deb80a189781d78643ac8f7f8a, src/recomp_manual.c.
 * EU wrapper 00261E90 -> decode 00261D31; decoder frame counter 0046A444.
 * The original EU decoder init/output code verifies the shared plane layout.
 * This replaces the codec implementation, preserving the guest cdecl ABI,
 * buffer swap and the planes that the movie player's output call consumes.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "movie_vp6.h"

extern void sub_00261E90_gen(void);

#ifdef NFSU2_NATIVE_VP6
typedef struct {
    uint32_t y, u, v, y_stride, uv_stride, w, h;
} EuVp6Planes;

/* Identical 48/24 pixel borders are written/read by EU 00263A48/002619ED. */
static EuVp6Planes vp6_planes(uint32_t dec, uint32_t buf)
{
    EuVp6Planes p;
    p.y_stride = MEM32(dec + 0x1B8);
    p.uv_stride = MEM32(dec + 0x1BC);
    p.y = buf + MEM32(dec + 0x21C) + (p.y_stride + 1u) * 48u;
    p.u = buf + MEM32(dec + 0x220) + (p.uv_stride + 1u) * 24u;
    p.v = buf + MEM32(dec + 0x224) + (p.uv_stride + 1u) * 24u;
    p.w = MEM32(dec + 0x1B0);
    p.h = MEM32(dec + 0x1B4);
    return p;
}

static int valid_planes(const EuVp6Planes *p, uint32_t size)
{
    return size > 0 && size < (uint32_t)(INT_MAX - 131072)
        && p->w && p->h && p->w <= 4096 && p->h <= 4096
        && !(p->w & 1u) && !(p->h & 1u)
        && p->y_stride >= p->w && p->y_stride <= 8192
        && p->uv_stride >= p->w / 2u && p->uv_stride <= 4096;
}

/* Comparison uses generated output and a separate FFmpeg picture. It must
 * start at the movie's key frame; changing decoder mode mid-stream is invalid.
 * The generated decoder alone maintains its own motion compensation borders.
 */
static void vp6_check(uint32_t dec, uint32_t data, uint32_t size)
{
    static unsigned long frames, bad;
    static uint8_t *pic;
    EuVp6Planes p = vp6_planes(dec, MEM32(dec + 0x254));
    uint32_t cw = p.w / 2u, row, diff = 0;
    if (!valid_planes(&p, size)) return;
    if (!pic && !(pic = malloc(4096u * 4096u * 3u / 2u))) return;
    if (nfsu2_vp6_decode(dec, (const uint8_t *)XBOX_PTR(data), (int)size,
            pic, pic + p.w * p.h, pic + p.w * p.h + cw * (p.h / 2u),
            (int)p.w, (int)cw, (int)p.w, (int)p.h) == 0) {
        for (row = 0; row < p.h; row++)
            diff += memcmp(pic + row * p.w,
                (const void *)XBOX_PTR(p.y + row * p.y_stride), p.w) != 0;
        for (row = 0; row < p.h / 2u; row++) {
            diff += memcmp(pic + p.w * p.h + row * cw,
                (const void *)XBOX_PTR(p.u + row * p.uv_stride), cw) != 0;
            diff += memcmp(pic + p.w * p.h + cw * (p.h / 2u) + row * cw,
                (const void *)XBOX_PTR(p.v + row * p.uv_stride), cw) != 0;
        }
    } else diff = 1;
    frames++;
    if (diff && bad++ < 20)
        fprintf(stderr, "[movie] EU VP6 check: frame %lu (%u bytes) differs in %u rows\n",
            frames, size, diff);
    if (frames % 300 == 0)
        fprintf(stderr, "[movie] EU VP6 check: %lu frames, %lu differ\n", frames, bad);
}

void sub_00261E90(void)
{
    int mode = nfsu2_vp6_mode();
    uint32_t dec = MEM32(esp + 4), data = MEM32(esp + 8), size = MEM32(esp + 12);
    if (mode != 1) {
        sub_00261E90_gen();
        if (mode == 2 && eax == 0) vp6_check(dec, data, size);
        return;
    }
    RECOMP_EU_ENTER(0x00261E90u);
    RECOMP_PREEMPT();
    uint32_t cur = MEM32(dec + 0x244);
    EuVp6Planes p = vp6_planes(dec, cur);
    int decoded = valid_planes(&p, size) && nfsu2_vp6_decode(dec,
        (const uint8_t *)XBOX_PTR(data), (int)size,
        (uint8_t *)XBOX_PTR(p.y), (uint8_t *)XBOX_PTR(p.u), (uint8_t *)XBOX_PTR(p.v),
        (int)p.y_stride, (int)p.uv_stride, (int)p.w, (int)p.h) == 0;
    if (decoded) {
        MEM32(dec + 0x244) = MEM32(dec + 0x254);
        MEM32(dec + 0x254) = cur;
    } else {
        static unsigned long errors;
        if (errors++ < 20)
            fprintf(stderr, "[movie] EU VP6 rejected frame: %u bytes, %ux%u stride %u/%u; retaining last picture\n",
                size, p.w, p.h, p.y_stride, p.uv_stride);
    }
    MEM32(dec + 0x1E8) = size;
    MEM32(0x0046A444u) += 1;
    /* EU 00261E90 pops the saved data argument into ECX, restores EBX and
     * returns cdecl. The movie caller overwrites EAX/ECX before their next use.
     * Internal On2-only bookkeeping is owned by FFmpeg in native mode.
     */
    eax = 0;
    ecx = data;
    esp += 4;
}
#else
void sub_00261E90(void) { sub_00261E90_gen(); }
#endif
