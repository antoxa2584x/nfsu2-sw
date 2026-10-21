/* Shared native movie-row conversion and guest postconditions. */
#ifndef NFSU2_NATIVE_MOVIE_ROW_H
#define NFSU2_NATIVE_MOVIE_ROW_H


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

static void yuv_row_native(uint32_t y, uint32_t u, uint32_t v, uint8_t *out, uint32_t n, uint32_t ty_va, uint32_t tu_va, uint32_t tv_va)
{
    const int16_t *ty = (const int16_t *)XBOX_PTR(ty_va);
    const int16_t *tu = (const int16_t *)XBOX_PTR(tu_va);
    const int16_t *tv = (const int16_t *)XBOX_PTR(tv_va);
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

static void nfsu2_movie_row_override(void (*generated)(void), uint32_t ty_va, uint32_t tu_va, uint32_t tv_va)
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
        generated();
        return;
    }
    if (mode == 2) {
        static unsigned long rows, bad;
        static uint8_t buf[4096 * 8];
        yuv_row_native(y, u, v, buf, n, ty_va, tu_va, tv_va);
        generated();
        rows++;
        if (memcmp(buf, (const void *)XBOX_PTR(dst), n * 8u) && bad++ < 20)
            fprintf(stderr, "[native] movie conversion mismatch: row %lu at %08X\n", rows, dst);
        if ((rows & 0xFFFF) == 0)
            fprintf(stderr, "[native] movie conversion: %lu rows, %lu mismatches\n", rows, bad);
        return;
    }
    nfsu2_movie_row = dst;
    yuv_row_native(y, u, v, (uint8_t *)XBOX_PTR(dst), n, ty_va, tu_va, tv_va);

    /* What the last step leaves in the registers. */
    last = n - 1u;
    {
        const int16_t *ty = (const int16_t *)XBOX_PTR(ty_va);
        const int16_t *tu = (const int16_t *)XBOX_PTR(tu_va);
        const int16_t *tv = (const int16_t *)XBOX_PTR(tv_va);
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


#endif
