/* Same SSE multiply/add order for both executable regions. Aliasing is safe. */
#ifndef NFSU2_NATIVE_MATRIX_H
#define NFSU2_NATIVE_MATRIX_H
#include <string.h>
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("fp-contract=off")))
#endif
static void mat4_mul(float *o, const float *a, const float *b)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) {
            float s = a[4 * i] * b[k];
            s = s + a[4 * i + 1] * b[4 + k];
            s = s + a[4 * i + 2] * b[8 + k];
            s = s + a[4 * i + 3] * b[12 + k];
            r[4 * i + k] = s;
        }
    memcpy(o, r, sizeof r);
}
#endif
