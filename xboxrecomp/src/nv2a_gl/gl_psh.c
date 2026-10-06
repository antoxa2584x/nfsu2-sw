/*
 * gl_psh.c -- NV2A texture shader + register combiners as a GLSL fragment
 * shader.
 *
 * The register values are the XDK's pixel-shader tokens, which D3D writes to
 * the combiner registers unchanged, so the encoding is the XDK's:
 *
 *   input byte (ICW, final CW0/CW1): reg[3:0] | ALPHA 0x10 | mapping[7:5]
 *   colour/alpha OCW:  CD dst[3:0] AB dst[7:4] SUM dst[11:8]
 *                      CD_DOT 12, AB_DOT 13, MUX 14, scale/bias [17:15],
 *                      CD_BLUE_TO_ALPHA 18, AB_BLUE_TO_ALPHA 19
 *   CONTROL:           stage count [7:0], MUX_MSB 8, UNIQUE_C0 12, UNIQUE_C1 16
 *   final CW1:         E, F, G bytes; CLAMP_SUM 0x80, COMPLEMENT_V1 0x40,
 *                      COMPLEMENT_R0 0x20 in the low byte
 *   SHADER_STAGE_PROGRAM: 5 bits per texture stage (PS_TEXTUREMODES)
 *
 * Registers: 0 zero, 1 C0, 2 C1, 3 fog, 4 V0 (diffuse), 5 V1 (specular),
 * 8-11 T0-T3, 12 R0, 13 R1, and for the final combiner only 14 V1R0_SUM and
 * 15 EF_PROD. R0.a starts as T0.a.
 *
 * ponytail: the bump-map and dot-product texture modes (DOT_ST and friends)
 * sample as plain 2D, and cube maps read black on GL (Vulkan samples them);
 * the dependent AR/GB lookups are done. They are the
 * next thing to add when a title's surfaces need them.
 */
#include "gl_psh.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char  *p;
    size_t left;
    int    overflow;
} Out;

static void emit(Out *o, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (o->overflow)
        return;
    va_start(ap, fmt);
    n = vsnprintf(o->p, o->left, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= o->left) {
        o->overflow = 1;
        return;
    }
    o->p += n;
    o->left -= (size_t)n;
}

static const char *reg_name(uint32_t reg, int stage, const Nv2aPshKey *k,
                            char *buf, size_t n)
{
    switch (reg) {
    case 1:
        snprintf(buf, n, "u_c0[%d]", (k->control & (1u << 12)) ? stage : 0);
        return buf;
    case 2:
        snprintf(buf, n, "u_c1[%d]", (k->control & (1u << 16)) ? stage : 0);
        return buf;
    case 3:  return "FOG";
    case 4:  return "V0";
    case 5:  return "V1";
    case 8:  return "T0";
    case 9:  return "T1";
    case 10: return "T2";
    case 11: return "T3";
    case 12: return "R0";
    case 13: return "R1";
    case 14: return "V1R0";
    case 15: return "EF";
    default: return "ZERO";
    }
}

/* One general-combiner input, mapped. `alpha_portion` selects the
 * float-valued (alpha) path, where the RGB channel bit means blue. */
static void input(Out *o, uint32_t b, int stage, int alpha_portion,
                  const Nv2aPshKey *k)
{
    char nb[16];
    /* V1R0_SUM and EF_PROD exist only in the final combiner. */
    const char *r = (b & 0xF) >= 14 ? "ZERO"
                                    : reg_name(b & 0xF, stage, k, nb, sizeof nb);
    const char *x;
    char val[40];
    int use_alpha = (b >> 4) & 1;
    uint32_t map = (b >> 5) & 7;

    if (alpha_portion)
        snprintf(val, sizeof val, "%s.%s", r, use_alpha ? "a" : "b");
    else if (use_alpha)
        snprintf(val, sizeof val, "vec3(%s.a)", r);
    else
        snprintf(val, sizeof val, "%s.rgb", r);
    x = val;

    switch (map) {
    case 0: emit(o, "max(%s, 0.0)", x); break;                       /* unsigned */
    case 1: emit(o, "(1.0 - clamp(%s, 0.0, 1.0))", x); break;        /* unsigned invert */
    case 2: emit(o, "(2.0 * max(%s, 0.0) - 1.0)", x); break;         /* expand normal */
    case 3: emit(o, "(1.0 - 2.0 * max(%s, 0.0))", x); break;         /* expand negate */
    case 4: emit(o, "(max(%s, 0.0) - 0.5)", x); break;               /* half-bias normal */
    case 5: emit(o, "(0.5 - max(%s, 0.0))", x); break;               /* half-bias negate */
    case 6: emit(o, "(%s)", x); break;                               /* signed identity */
    default: emit(o, "(-(%s))", x); break;                           /* signed negate */
    }
}

static const char *dst_name(uint32_t reg)
{
    switch (reg) {
    case 4:  return "V0";
    case 5:  return "V1";
    case 8:  return "T0";
    case 9:  return "T1";
    case 10: return "T2";
    case 11: return "T3";
    case 12: return "R0";
    case 13: return "R1";
    default: return NULL;           /* 0 discards; others are not writable */
    }
}

static const char *scale_bias(uint32_t op)
{
    switch (op) {
    case 1:  return "(%s - 0.5)";
    case 2:  return "(%s * 2.0)";
    case 3:  return "((%s - 0.5) * 2.0)";
    case 4:  return "(%s * 4.0)";
    case 6:  return "(%s * 0.5)";
    default: return "(%s)";
    }
}

static void stage(Out *o, int i, const Nv2aPshKey *k)
{
    uint32_t cicw = k->color_icw[i], cocw = k->color_ocw[i];
    uint32_t aicw = k->alpha_icw[i], aocw = k->alpha_ocw[i];
    const char *d;
    char e[64];

    emit(o, "    { /* stage %d */\n", i);
    /* Colour inputs */
    emit(o, "        vec3 cA = "); input(o, cicw >> 24, i, 0, k); emit(o, ";\n");
    emit(o, "        vec3 cB = "); input(o, cicw >> 16, i, 0, k); emit(o, ";\n");
    emit(o, "        vec3 cC = "); input(o, cicw >> 8, i, 0, k);  emit(o, ";\n");
    emit(o, "        vec3 cD = "); input(o, cicw, i, 0, k);       emit(o, ";\n");
    /* Alpha inputs */
    emit(o, "        float aA = "); input(o, aicw >> 24, i, 1, k); emit(o, ";\n");
    emit(o, "        float aB = "); input(o, aicw >> 16, i, 1, k); emit(o, ";\n");
    emit(o, "        float aC = "); input(o, aicw >> 8, i, 1, k);  emit(o, ";\n");
    emit(o, "        float aD = "); input(o, aicw, i, 1, k);       emit(o, ";\n");

    emit(o, "        vec3 cAB = %s;\n", (cocw & (1u << 13)) ? "vec3(dot(cA, cB))" : "cA * cB");
    emit(o, "        vec3 cCD = %s;\n", (cocw & (1u << 12)) ? "vec3(dot(cC, cD))" : "cC * cD");
    if (cocw & (1u << 14))
        emit(o, "        vec3 cS = %s ? cCD : cAB;\n",
             (k->control & (1u << 8)) ? "(R0.a >= 0.5)"
                                      : "((int(R0.a * 255.0 + 0.5) & 1) != 0)");
    else
        emit(o, "        vec3 cS = cAB + cCD;\n");
    emit(o, "        float aAB = aA * aB, aCD = aC * aD;\n");
    if (aocw & (1u << 14))
        emit(o, "        float aS = %s ? aCD : aAB;\n",
             (k->control & (1u << 8)) ? "(R0.a >= 0.5)"
                                      : "((int(R0.a * 255.0 + 0.5) & 1) != 0)");
    else
        emit(o, "        float aS = aAB + aCD;\n");

    /* Scale/bias, clamp to the signed range, write. */
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cAB");
    emit(o, "        cAB = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cCD");
    emit(o, "        cCD = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cS");
    emit(o, "        cS = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aAB");
    emit(o, "        aAB = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aCD");
    emit(o, "        aCD = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aS");
    emit(o, "        aS = clamp(%s, -1.0, 1.0);\n", e);

    if ((d = dst_name((cocw >> 4) & 0xF)) != NULL) emit(o, "        %s.rgb = cAB;\n", d);
    if ((d = dst_name(cocw & 0xF)) != NULL)        emit(o, "        %s.rgb = cCD;\n", d);
    if ((d = dst_name((cocw >> 8) & 0xF)) != NULL) emit(o, "        %s.rgb = cS;\n", d);
    if ((d = dst_name((aocw >> 4) & 0xF)) != NULL) emit(o, "        %s.a = aAB;\n", d);
    if ((d = dst_name(aocw & 0xF)) != NULL)        emit(o, "        %s.a = aCD;\n", d);
    if ((d = dst_name((aocw >> 8) & 0xF)) != NULL) emit(o, "        %s.a = aS;\n", d);
    /* Blue-to-alpha replaces the alpha written to the AB / CD destination. */
    if ((cocw & (1u << 19)) && (d = dst_name((cocw >> 4) & 0xF)) != NULL)
        emit(o, "        %s.a = cAB.b;\n", d);
    if ((cocw & (1u << 18)) && (d = dst_name(cocw & 0xF)) != NULL)
        emit(o, "        %s.a = cCD.b;\n", d);
    emit(o, "    }\n");
}

/* A final-combiner input: clamped to [0,1], optionally inverted. Its C0 and
 * C1 are its own (SPECULAR_FOG_FACTOR0/1), not a general stage's: read as
 * stage 0's, NFSU2's colour grading added its red channel mask to the whole
 * picture. */
static void final_input(Out *o, uint32_t b, int alpha_only, const Nv2aPshKey *k)
{
    char nb[16];
    const char *r = (b & 0xF) == 1 ? "u_fc0"
                  : (b & 0xF) == 2 ? "u_fc1"
                  : reg_name(b & 0xF, 0, k, nb, sizeof nb);
    int use_alpha = (b >> 4) & 1;
    int invert = (b >> 5) & 1;
    char val[40];

    if (alpha_only)
        snprintf(val, sizeof val, "%s.%s", r, use_alpha ? "a" : "b");
    else if (use_alpha)
        snprintf(val, sizeof val, "vec3(%s.a)", r);
    else
        snprintf(val, sizeof val, "%s.rgb", r);
    if (invert)
        emit(o, "(1.0 - clamp(%s, 0.0, 1.0))", val);
    else
        emit(o, "clamp(%s, 0.0, 1.0)", val);
}

/* The earlier stage a dependent mode reads (SHADER_OTHER_STAGE_INPUT):
 * stage 1 always reads T0; stage 2 selects with bits [19:16], stage 3 with
 * [23:20]. */
static int input_stage(const Nv2aPshKey *k, int t)
{
    int s = t <= 1 ? 0 : (int)((k->other_input >> (t == 2 ? 16 : 20)) & 0xF);
    return s < t ? s : t - 1;
}

static void tex_fetch(Out *o, int t, uint32_t mode, const Nv2aPshKey *k)
{
    switch (mode) {
    case 15:                                         /* DEPENDENT_AR */
    case 16:                                         /* DEPENDENT_GB */
        /* A lookup indexed by an earlier stage's colour -- NFSU2's colour
         * grading does red through one and green/blue through the other.
         * The coordinates are already normalised. Sampled at the stage's own
         * texcoords, the green/blue lookup read black and races were red. */
        if (t == 0) {
            emit(o, "    vec4 T%d = vec4(0.0);\n", t);
            break;
        }
        emit(o, "    vec4 T%d = texture(t%d, T%d.%s);\n", t, t, input_stage(k, t),
             mode == 15 ? "ar" : "gb");
        break;
    case 0:                                          /* NONE */
        emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        break;
    case 4:                                          /* PASS_THROUGH */
        emit(o, "    vec4 T%d = clamp(vT%d, 0.0, 1.0);\n", t, t);
        break;
    case 5:                                          /* CLIP_PLANE */
        emit(o, "    if (any(lessThan(vT%d, vec4(0.0)))) discard;\n"
                "    vec4 T%d = vec4(0.0);\n", t, t);
        break;
    case 3:                                          /* CUBE_MAP */
        /* Car reflections. The direction is (s, t, r), not divided by q
         * (xemu does the same). Vulkan only: nv2a_vk binds cube views,
         * nv2a_gl does not, so GL still reads black. */
        if (nv2a_shader_vk)
            emit(o, "    vec4 T%d = texture(t%d, vT%d.xyz);\n", t, t, t);
        else
            emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        break;
    default: {                                       /* 2D (+ the rest, for now) */
        emit(o, "    vec2 tc%d = (abs(vT%d.w) > 1e-8 && %d == 1) ? vT%d.xy / vT%d.w : vT%d.xy;\n",
             t, t, mode == 1 ? 1 : 0, t, t, t);
        emit(o, "    vec4 T%d = texture(t%d, tc%d * u_tscale[%d]);\n", t, t, t, t);
        break;
    }
    }
}

int nv2a_gl_psh(const Nv2aPshKey *k, char *buf, size_t cap)
{
    Out o = { buf, cap, 0 };
    uint32_t count = k->control & 0xFF;
    uint32_t i;

    if (count > 8)
        count = 8;
    if (nv2a_shader_vk) {
        /* Vulkan (nv2a_vk): std140 block at binding 1 -- vec2 array
         * elements take 16 bytes -- and samplers at bindings 2..5,
         * samplerCube for a CUBE_MAP stage. */
        emit(&o,
            "#version 450\n"
            "layout(location = 0) in vec4 vD0; layout(location = 1) in vec4 vD1;\n"
            "layout(location = 2) in vec4 vT0; layout(location = 3) in vec4 vT1;\n"
            "layout(location = 4) in vec4 vT2; layout(location = 5) in vec4 vT3;\n"
            "layout(location = 6) in float vFog;\n");
        for (i = 0; i < 4; i++)
            emit(&o, "layout(set = 0, binding = %u) uniform %s t%u;\n", i + 2,
                 ((k->shader_program >> (5 * i)) & 0x1F) == 3 ? "samplerCube" : "sampler2D",
                 i);
        emit(&o,
            "layout(std140, set = 0, binding = 1) uniform FsU {\n"
            "    vec2 u_tscale[4];\n"
            "    vec4 u_c0[8]; vec4 u_c1[8];\n"
            "    vec4 u_fc0; vec4 u_fc1;\n"
            "    vec4 u_fogcolor;\n"
            "    int u_alpha_func; float u_alpha_ref;\n"
            "};\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "void main() {\n");
    } else
    emit(&o,
        "#version 330 core\n"
        "in vec4 vD0; in vec4 vD1; in vec4 vT0; in vec4 vT1;\n"
        "in vec4 vT2; in vec4 vT3; in float vFog;\n"
        "uniform sampler2D t0; uniform sampler2D t1;\n"
        "uniform sampler2D t2; uniform sampler2D t3;\n"
        "uniform vec2 u_tscale[4];\n"
        "uniform vec4 u_c0[8]; uniform vec4 u_c1[8];\n"
        "uniform vec4 u_fc0; uniform vec4 u_fc1;\n"
        "uniform vec4 u_fogcolor;\n"
        "uniform int u_alpha_func; uniform float u_alpha_ref;\n"
        "out vec4 fragColor;\n"
        "void main() {\n");
    for (i = 0; i < 4; i++)
        tex_fetch(&o, (int)i, (k->shader_program >> (5 * i)) & 0x1F, k);
    emit(&o,
        "    vec4 ZERO = vec4(0.0);\n"
        "    vec4 V0 = vD0, V1 = vD1;\n"
        "    vec4 R0 = vec4(0.0, 0.0, 0.0, T0.a), R1 = vec4(0.0);\n"
        "    vec4 FOG = vec4(u_fogcolor.rgb, clamp(vFog, 0.0, 1.0));\n");
    for (i = 0; i < count; i++)
        stage(&o, (int)i, k);

    /* Final combiner. */
    {
        uint32_t cw0 = k->final0, cw1 = k->final1;
        emit(&o, "    vec4 V1R0 = vec4(%s + %s, 0.0);\n",
             (cw1 & 0x40) ? "(1.0 - clamp(V1.rgb, 0.0, 1.0))" : "clamp(V1.rgb, 0.0, 1.0)",
             (cw1 & 0x20) ? "(1.0 - clamp(R0.rgb, 0.0, 1.0))" : "clamp(R0.rgb, 0.0, 1.0)");
        if (cw1 & 0x80)
            emit(&o, "    V1R0 = clamp(V1R0, 0.0, 1.0);\n");
        emit(&o, "    vec4 EF = vec4(");
        final_input(&o, cw1 >> 24, 0, k);
        emit(&o, " * ");
        final_input(&o, cw1 >> 16, 0, k);
        emit(&o, ", 0.0);\n");
        emit(&o, "    vec3 fA = "); final_input(&o, cw0 >> 24, 0, k); emit(&o, ";\n");
        emit(&o, "    vec3 fB = "); final_input(&o, cw0 >> 16, 0, k); emit(&o, ";\n");
        emit(&o, "    vec3 fC = "); final_input(&o, cw0 >> 8, 0, k);  emit(&o, ";\n");
        emit(&o, "    vec3 fD = "); final_input(&o, cw0, 0, k);       emit(&o, ";\n");
        emit(&o, "    float fG = "); final_input(&o, cw1 >> 8, 1, k); emit(&o, ";\n");
        emit(&o, "    fragColor = vec4(fA * fB + (1.0 - fA) * fC + fD, fG);\n");
    }
    emit(&o,
        "    fragColor = clamp(fragColor, 0.0, 1.0);\n"
        "    float ar = fragColor.a * 255.0;\n"
        "    bool pass = true;\n"
        "    if (u_alpha_func == 0x200) pass = false;\n"
        "    else if (u_alpha_func == 0x201) pass = ar <  u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x202) pass = ar == u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x203) pass = ar <= u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x204) pass = ar >  u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x205) pass = ar != u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x206) pass = ar >= u_alpha_ref;\n"
        "    if (!pass) discard;\n"
        "}\n");
    return o.overflow ? -1 : (int)(cap - o.left);
}
