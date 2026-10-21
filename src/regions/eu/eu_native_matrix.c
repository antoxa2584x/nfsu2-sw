/* Existing US v0.5 native matrix arithmetic, mapped to the EU executable.
 * Source: antoxa2584x/nfsu2-sw, 48b1b2798d60f5deb80a189781d78643ac8f7f8a.
 * EU wrappers preserve all six SSE results and both EAX return conventions.
 * Do not fuse the four products: the original SSE rounds each add/multiply.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

extern void sub_0022199A_gen(void);
extern void sub_002EF0A0_gen(void);

/* The sum order and temporary result array are from the existing US helper. */
#include "native_matrix.h"

/* Snapshot both inputs before writing, including out==a, out==b and partial
 * overlap. The last products left in xmm0/1 are part of the guest ABI. */
static void d3d_mat_mul(uint32_t a, uint32_t b, RecompXmm x[6])
{
    float ma[16], mb[16], o[16];
    memcpy(ma, (const void *)XBOX_PTR(a), 64);
    memcpy(mb, (const void *)XBOX_PTR(b), 64);
    mat4_mul(o, ma, mb);
    for (int k = 0; k < 4; k++) {
        x[0].f[k] = ma[14] * mb[8 + k];
        x[1].f[k] = ma[15] * mb[12 + k];
    }
    memcpy(&x[2], o, 64);
}

static int native_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE");
        const char *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
        fprintf(stderr, "[native] EU matrices: %s\n",
                mode == 0 ? "generated" : mode == 2 ? "compare (generated output)" : "native");
    }
    return mode;
}

static void matrix(uint32_t va, int return_output, void (*generated)(void))
{
    int mode = native_mode();
    if (mode == 0) {
        generated();
        return;
    }
    if (mode == 1) {
        RECOMP_EU_ENTER(va);
        RECOMP_PREEMPT();
    }
    uint32_t e0 = esp, out = MEM32(esp + 4);
    uint32_t a = MEM32(esp + 8), b = MEM32(esp + 12);
    RecompXmm x[6];
    d3d_mat_mul(a, b, x);
    if (mode == 2) {
        static unsigned long calls, bad;
        generated();
        int ok = !memcmp(&x[2], (const void *)XBOX_PTR(out), 64)
            && eax == (return_output ? out : a) && ecx == out && esp == e0 + 16u
            && !memcmp(&x[0], &g_xmm0, 16) && !memcmp(&x[1], &g_xmm1, 16)
            && !memcmp(&x[2], &g_xmm2, 16) && !memcmp(&x[3], &g_xmm3, 16)
            && !memcmp(&x[4], &g_xmm4, 16) && !memcmp(&x[5], &g_xmm5, 16);
        calls++;
        if (!ok && bad++ < 20)
            fprintf(stderr, "[native] EU %08X matrix mismatch out=%08X a=%08X b=%08X\n", va, out, a, b);
        if ((calls & 0xFFFFFu) == 0)
            fprintf(stderr, "[native] EU matrices: %lu comparisons, %lu mismatches\n", calls, bad);
        return;
    }
    memcpy((void *)XBOX_PTR(out), &x[2], 64);
    g_xmm0 = x[0]; g_xmm1 = x[1]; g_xmm2 = x[2];
    g_xmm3 = x[3]; g_xmm4 = x[4]; g_xmm5 = x[5];
    eax = return_output ? out : a;
    ecx = out;
    esp += 16;
}

void sub_0022199A(void) { matrix(0x0022199Au, 1, sub_0022199A_gen); }
void sub_002EF0A0(void) { matrix(0x002EF0A0u, 0, sub_002EF0A0_gen); }
