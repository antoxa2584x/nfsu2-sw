/*
 * NV2A render back-end interface.
 *
 * The pushbuffer executor (nv2a_pb_exec.c) decodes what a title asks the GPU
 * to do. By default it carries that out itself, on the CPU, into guest memory.
 * A game project can instead register a back end -- a D3D11 renderer, say --
 * and the executor hands it the work in already-decoded form:
 *
 *   surface state + clears, triangles (transformed to surface pixels, with
 *   per-vertex colour and texel-space UVs), the texture each batch samples,
 *   and the flip that ends a frame.
 *
 * Everything is called on the NV2A poll thread, one call at a time, so a back
 * end may create its window and device lazily on the first call and pump its
 * window messages from flip().
 *
 * Fixed-function, pre-transformed and vertex-program batches all arrive the
 * same way: the executor transforms fixed-function vertices itself and runs
 * vertex programs on the CPU (vp_run), so a back end only ever sees surface
 * pixels. Blend, depth and alpha state arrive in Nv2aRenderState.
 *
 * ponytail: register-combiner state is not passed on. Extend Nv2aBatch when
 * it lands rather than adding callbacks.
 */
#ifndef NV2A_BACKEND_H
#define NV2A_BACKEND_H

#include <stdint.h>

/* The colour surface being drawn into, in real (anti-aliased) pixels.
 * aa_sx/aa_sy give the anti-aliasing factor, so width/aa_sx is the logical
 * size the title thinks it renders at (e.g. 640x480). */
/* Nonzero when the title was told the TV is 16:9 (kernel_xbox.c): present
 * the logical surface at 16:9 instead of 4:3. */
int xbox_video_widescreen(void);

typedef struct {
    uint32_t color_va;          /* guest address of pixel (0,0) */
    uint32_t width, height;     /* clip rectangle size, real pixels */
    uint32_t pitch;             /* bytes per row */
    uint32_t bytes_per_pixel;   /* 2 or 4 */
    uint32_t aa_sx, aa_sy;      /* 1 or 2 each */
    uint32_t clip_x, clip_y;    /* clip rectangle origin, real pixels */
} Nv2aSurface;

/* A texture as the title programmed it. uv in Nv2aVertex are in texels. */
typedef struct {
    uint32_t offset;            /* guest address of texel (0,0) */
    uint32_t width, height;
    uint32_t pitch;             /* linear formats only */
    uint32_t color;             /* NV097 colour-format code */
    uint32_t addr_u, addr_v;    /* NV097 wrap mode per axis (1 wrap, 3 clamp) */
} Nv2aTexture;

typedef struct {
    float    x, y, z;           /* surface pixels; z as the title produced it */
    float    rhw;               /* 1/w, 1 for pre-transformed batches */
    uint32_t diffuse;           /* 0xAARRGGBB */
    float    u, v;              /* texels */
} Nv2aVertex;

/* Render state as the title set it, in NV2A's own (OpenGL) enum values:
 * blend factors GL_ZERO/GL_ONE/0x300..0x308/0x8001..0x8004, equations
 * GL_FUNC_ADD 0x8006 etc., compare functions GL_NEVER 0x200 .. GL_ALWAYS 0x207.
 * color_mask uses NV2A's bytes: A 0x01000000, R 0x00010000, G 0x100, B 0x1. */
typedef struct {
    uint32_t blend_enable, blend_src, blend_dst, blend_eq, blend_color;
    uint32_t alpha_test_enable, alpha_func, alpha_ref;   /* ref 0..255 */
    uint32_t depth_test_enable, depth_func, depth_write;
    uint32_t color_mask;
    uint32_t cull_enable, cull_face, front_face;
    uint32_t zeta_va;                                    /* 0: no depth surface */
    float    depth_min, depth_max;                       /* SET_CLIP_MIN/MAX */
} Nv2aRenderState;

typedef struct {
    const Nv2aVertex  *vertices;    /* triangle list: count is a multiple of 3 */
    uint32_t           count;
    const Nv2aTexture *texture;     /* NULL: untextured */
    const Nv2aRenderState *state;
} Nv2aBatch;

/* A batch as the NV2A itself sees it, for a back end that runs the whole
 * pipeline on a GPU (vertex programs, register combiners, multi-texture)
 * rather than taking the executor's CPU-transformed triangles.
 *
 * The executor still decodes the pushbuffer and gathers vertex data -- every
 * attribute type, inline arrays and immediate vertices alike become float4
 * attributes -- and everything else comes straight from the method shadow:
 * regs[m / 4] is the last value the title wrote to 3D-class method m, the
 * same register file the hardware draws from. */
#define NV2A_RAW_ATTRS 16
typedef struct {
    uint32_t        prim;           /* SET_BEGIN_END op: 1 points .. 10 polygon */
    uint32_t        vertex_count;   /* gathered vertices */
    uint16_t        attr_present;   /* bit a: attribute a carries data */
    const float    *attrs;          /* vertex_count x 16 x float4, [v][a][c];
                                     * absent attributes read (0,0,0,1) */
    const uint32_t *indices;        /* into the gathered vertices */
    uint32_t        index_count;
    const uint32_t *regs;           /* method shadow, 0x2000 bytes */
    const uint32_t (*vp_program)[4];/* transform program slots */
    uint32_t        vp_slots;
    const float    (*vp_consts)[4]; /* transform constants (raw index) */
    uint32_t        vp_const_count;
    Nv2aSurface     surface;
    uint32_t        color_va, zeta_va;  /* resolved guest addresses */
    /* Which transform applies: 0 pre-transformed (attribute 0 is a surface
     * position in real pixels, w = 1/rhw), 1 fixed-function (composite
     * matrix, then divide, then viewport offset, all in logical pixels),
     * 2 vertex program (the XDK tail leaves oPos in logical screen space with
     * w still the clip w). */
    uint32_t        xform;
    float           composite[16];      /* clip[i] = sum_j m[4i+j] * pos[j] */
    float           vp_offset[4];
    float           aa_sx, aa_sy;       /* logical -> real pixels */
    uint32_t        vp_start;           /* first program slot */
    /* Only for a back end registered with NV2A_BACKEND_RAW_DIRECT: the
     * attributes in attr_direct are not converted into attrs[] (their
     * entries hold garbage) but described as the title stored them, for the
     * back end to upload as they are. direct[a].ptr is attribute a of
     * gathered vertex 0; vertex i is at ptr + i * stride, for vertex_count
     * vertices. type is the NV097 code: 0 D3DCOLOR (size 4, bytes B,G,R,A),
     * 1 S1 (normalised short), 2 float, 4 normalised unsigned byte, 5 S32K
     * (short as-is). With the flag, attrs[] entries of absent attributes are
     * not filled either. */
    /* Bumped whenever the transform program (slots or start) / constants
     * change; equal values mean identical contents. Never 0. */
    uint32_t        vp_prog_gen, vp_const_gen;
    /* Texture stage i's image and palette as guest VAs (0: none), resolved
     * by the executor so a back end never reads the executor's DMA state
     * (a threaded one runs behind it). */
    uint32_t        tex_va[4], pal_va[4];
    uint16_t        attr_direct;
    struct {
        const uint8_t *ptr;
        uint32_t       type, size, stride;
    } direct[NV2A_RAW_ATTRS];
    /* What an attribute without an array reads: the last SET_VERTEX_DATA*
     * value the title gave it, as the hardware does. D3D sets them for the
     * FVF components a draw leaves out -- NFSU2's lens drops have no
     * specular, and their fog comes from its alpha, which D3D sets to 0.
     * Held by value so a queued copy of the batch keeps them. attr_const_gen
     * changes whenever any of them does (never 0). */
    float           attr_const[NV2A_RAW_ATTRS][4];
    uint32_t        attr_const_gen;
    /* Vertex-memory epoch: changes at every semaphore release and trap
     * (after which the title may reuse buffers the GPU has finished with)
     * and around every inline-array batch. Two batches with equal epochs
     * and equal direct[] pointers read the same vertex bytes, so a back end
     * may reuse what it uploaded for the first (instancing). */
    uint32_t        vtx_epoch;
} Nv2aRawBatch;

/* Resolve a DMA offset the title programmed (texture, surface) to a guest VA. */
uint32_t nv2a_pb_resolve(uint32_t dma_offset);

typedef struct {
    /* flags: CLEAR_SURFACE bits (Z 0x1, stencil 0x2, colour 0xF0).
     * zstencil: SET_ZSTENCIL_CLEAR_VALUE (depth in the top 24 bits for Z24S8). */
    void (*clear)(const Nv2aSurface *s, const Nv2aRenderState *rs,
                  uint32_t flags, uint32_t argb, uint32_t zstencil);
    void (*draw)(const Nv2aSurface *s, const Nv2aBatch *b);
    void (*flip)(void);
    /* Optional. When set, every batch goes here instead of draw(), before
     * any CPU transform, clipping or screen-space test. */
    void (*draw_raw)(const Nv2aRawBatch *b);
    /* NV2A_BACKEND_* bits. */
    uint32_t flags;
} Nv2aBackend;

/* draw_raw takes attributes in their stored format (Nv2aRawBatch.attr_direct).
 * RECOMP_GL_DIRECT=0 turns it off at run time. */
#define NV2A_BACKEND_RAW_DIRECT 0x1u

/* Optional, set by the game project: sees every raw batch just before the
 * back end gets it and may change it. attrs is rb->attrs, writable; an
 * attribute moved from attr_direct to attrs[] must have its bit cleared. */
extern void (*nv2a_raw_batch_hook)(Nv2aRawBatch *rb, float *attrs);

/* Register (or, with NULL, remove) the back end. Call before the title starts
 * submitting work; the executor must also be enabled (RECOMP_PB_EXEC). */
void nv2a_backend_register(const Nv2aBackend *backend);

/* Decode a whole texture to 0xAARRGGBB, row-major, width*height entries.
 * Handles every format the executor can sample (swizzled, linear, DXT).
 * Returns 0 if the format is not supported. */
int nv2a_backend_decode_texture(const Nv2aTexture *tex, uint32_t *argb_out);

/* Where BACK_END_WRITE_SEMAPHORE_RELEASE (0x1D70) values land: the guest VA
 * of the semaphore the title reads GPU progress from (for XDK D3D, the
 * pointer at device+0x30). The executor writes each release there once it
 * has executed everything before it -- which is
 * what lets D3D's fence waits and ring-space checks see real progress.
 *
 * ponytail: the title supplies the address instead of the executor resolving
 * the semaphore context DMA object through RAMIN. */
void nv2a_pb_set_semaphore_target(uint32_t guest_va);

#endif /* NV2A_BACKEND_H */
