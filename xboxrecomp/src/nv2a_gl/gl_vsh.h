/* gl_vsh.h -- NV2A vertex transform as GLSL (see gl_vsh.c). */
#ifndef NV2A_GL_VSH_H
#define NV2A_GL_VSH_H

#include <stddef.h>
#include <stdint.h>

/* The body of a vertex program, as `void nv2a_program(void)`, from slot
 * `start` to the FINAL instruction. Returns bytes written, -1 on overflow. */
int nv2a_gl_vsh_program(const uint32_t (*prog)[4], uint32_t slots,
                        uint32_t start, char *buf, size_t cap);

/* Nonzero: emit Vulkan GLSL (nv2a_vk). Set once, before the first shader. */
extern int nv2a_shader_vk;
/* Nonzero: emit GLSL ES 3.00 (nv2a_gl on Android). */
extern int nv2a_shader_es;

/* Declarations and helpers every vertex shader starts with. */
const char *nv2a_gl_vsh_prelude(void);
/* main() for a vertex-program shader (follows the program body). */
const char *nv2a_gl_vsh_main_program(void);
/* main() for fixed-function / pre-transformed batches (no program body). */
const char *nv2a_gl_vsh_fixed(void);

#endif
