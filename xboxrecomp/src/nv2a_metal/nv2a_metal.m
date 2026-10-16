/*
 * nv2a_metal.m -- a Metal renderer for the pushbuffer executor.
 *
 * The same job as nv2a_vk.c, on Metal, and built the same way: registered
 * as the executor's back end (nv2a_backend.h, draw_raw), it turns the method
 * shadow into draws. Apple only; the host chooses it over nv2a_vk at run
 * time (MoltenVK stays as the fallback).
 *
 *   surfaces   one BGRA8 texture per colour-surface address (rows top-first
 *              like guest memory), depth per zeta address and stored size
 *              (Depth32Float_Stencil8). Metal tracks hazards between passes.
 *   textures   nv2a_vk's cache and decoders; uploaded through the frame's
 *              ring with blits. DXT stays compressed where the GPU has BC.
 *   shaders    nv2a_metal_msl.c: gl_vsh.c / gl_psh.c bodies wrapped in MSL,
 *              compiled from source at first use (Metal keeps its own cache
 *              of compiled functions across runs).
 *   pipelines  keyed by the program, blend state, colour mask, whether there
 *              is a depth buffer, points, and the 16 vertex formats; vertex
 *              strides are dynamic (MTLBufferLayoutStrideDynamic) where the
 *              GPU has them (Apple4+), else in the key too. Depth and
 *              stencil are cached MTLDepthStencilStates; cull, winding,
 *              scissor, stencil reference and blend colour are encoder state.
 *   clears     quads (Metal has no mid-pass clear of a rectangle).
 *   uniforms   setVertexBytes / setFragmentBytes; the 192 transform constants
 *              in the ring (instancing appends sets behind them).
 *
 * Three frames in flight, each with its own ring. Everything runs on the
 * executor's thread.
 *
 * Runtime switches are nv2a_vk's where they mean the same: RECOMP_GL_DUMP,
 * RECOMP_GL_TRACE, RECOMP_GL_SCALE, RECOMP_GL_TEX_MB, RECOMP_GL_DXT,
 * RECOMP_TEX_CHANGES, RECOMP_VK_CUBE, RECOMP_VK_INSTANCE, RECOMP_DRAW_STATS,
 * RECOMP_FPS_LOG, RECOMP_PROG_CACHE. RECOMP_MTL_TRACE=<n> names every step
 * of the first n draws, clears and flips.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <TargetConditionals.h>
#include "../platform/apple_present.h"

#include "nv2a_metal.h"
#include "nv2a_metal_msl.h"
#include "../kernel/nv2a_backend.h"

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define LOGE(...) fprintf(stderr, "  [MTL] " __VA_ARGS__)

static int s_state;            /* 0 untried, 1 ready, -1 failed */
static int s_trace;
static int s_vtrace;
#define VT(...) do { if (s_vtrace > 0) { fprintf(stderr, "  [MTLT] " __VA_ARGS__); fflush(stderr); } } while (0)
static uint32_t s_frame;
static int s_tex_changes = -1;
static int s_drew_any;
static const uint32_t *s_regs;
static const Nv2aRawBatch *s_batch;

/* Hitch accounting, as in nv2a_vk. */
static uint32_t s_hz_progs, s_hz_texs, s_hz_draws;
static uint64_t s_hz_prog_ns, s_hz_tex_ns, s_hz_tex_bytes;
static uint64_t hz_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ── Device and host ──────────────────────────────────────────────── */

static id<MTLDevice>       s_dev;
static id<MTLCommandQueue> s_queue;
static CAMetalLayer       *s_layer;
static int                 s_headless;
static int                 s_have_bc;
static uint32_t            s_max_size = 8192;
/* No dynamic vertex strides (the iOS Simulator's GPU): strides go into the
 * pipeline key. */
static int                 s_static_stride;

__attribute__((weak)) void *nv2a_mtl_host_layer(void)
{
    return NULL;
}

__attribute__((weak)) void nv2a_mtl_host_gate(void)
{
}

/* ── Frames in flight ─────────────────────────────────────────────── */

#define MTL_FRAMES 3
#define RING_BYTES (32u << 20)
/* Constant-buffer offsets: 256 covers every Metal GPU. */
#define CONST_ALIGN 256u

typedef struct {
    __strong id<MTLBuffer> ring;
    uint8_t *ptr;
    size_t   off;
} Frame;

static Frame s_fr[MTL_FRAMES];
static Frame *s_f;
static uint32_t s_fi;
static dispatch_semaphore_t s_inflight;
static id<MTLCommandBuffer> s_cb;
static uint32_t s_cb_gen;      /* bumped at every new command buffer */
static id<MTLRenderCommandEncoder> s_enc;
static id<MTLBlitCommandEncoder>   s_blit;
/* Completion handlers run off the executor thread. Publish coherent timing
 * windows only when FPS diagnostics are requested; never wait for the GPU. */
static pthread_mutex_t s_gpu_stats_lock = PTHREAD_MUTEX_INITIALIZER;
static double s_gpu_ms, s_gpu_max_ms;
static uint64_t s_gpu_samples;

static void cb_new(void)
{
    s_cb = [s_queue commandBuffer];
    s_cb_gen++;
}

static void watch_errors(id<MTLCommandBuffer> cb)
{
    static int timing = -1;
    if (timing < 0) {
        const char *e = getenv("RECOMP_FPS_LOG");
        timing = e && *e == '1';
    }
    [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
        static int reported;
        if (b.status == MTLCommandBufferStatusError && reported++ < 10)
            fprintf(stderr, "  [MTL] command buffer failed: %s\n",
                    b.error.localizedDescription.UTF8String);
        if (timing && b.status == MTLCommandBufferStatusCompleted) {
            double ms = (b.GPUEndTime - b.GPUStartTime) * 1000.0;
            if (b.GPUStartTime > 0 && ms > 0) {
                pthread_mutex_lock(&s_gpu_stats_lock);
                s_gpu_ms += ms;
                if (ms > s_gpu_max_ms) s_gpu_max_ms = ms;
                s_gpu_samples++;
                pthread_mutex_unlock(&s_gpu_stats_lock);
            }
        }
    }];
}

static void frame_begin(void)
{
    s_f = &s_fr[s_fi];
    dispatch_semaphore_wait(s_inflight, DISPATCH_TIME_FOREVER);
    s_f->off = 0;
    cb_new();
}

/* ── Rendering passes ─────────────────────────────────────────────── */

/* w, h are the surface in the title's pixels (anti-aliasing included);
 * pw, ph the texture: w, h times the render scale. */
typedef struct {
    uint32_t va, w, h, pw, ph;
    __strong id<MTLTexture> tex;
    uint32_t used;
    uint32_t aa_sx, aa_sy;
    uint32_t gen;              /* bumped by every draw and clear into it */
} MtSurf;

typedef struct {
    uint32_t va, pw, ph;
    __strong id<MTLTexture> tex;
    uint32_t used;
} MtDepth;

#define MTL_MAX_SURF 32
#define MTL_MAX_DEPTH 16

static double s_scale = 1.0;

static uint32_t to_stored(uint32_t v, uint32_t p, uint32_t l)
{
    return (uint32_t)(((uint64_t)v * p + l / 2) / l);
}
static MtSurf  s_surf[MTL_MAX_SURF];
static MtDepth s_depth[MTL_MAX_DEPTH];
static MtSurf *s_last;

static MtSurf  *s_rt;
static MtDepth *s_rt_depth;

/* Encoder state set in this pass (set_* compare against it). */
static MTLScissorRect s_sc_cur;
static id<MTLRenderPipelineState> s_cur_pipe;
static int s_dyn_valid;
static int s_vs_valid, s_fs_valid, s_consts_valid;
static size_t s_consts_bound_at;
static Nv2aMslVs s_vs_cur;
static Nv2aMslFs s_fs_cur;
static id<MTLTexture> s_tex_cur[4];
static id<MTLSamplerState> s_smp_cur[4];
static void bindings_reset(void)
{
    s_vs_valid = s_fs_valid = s_consts_valid = 0;
    for (int i = 0; i < 4; i++) { s_tex_cur[i] = nil; s_smp_cur[i] = nil; }
}

static void end_pass(void)
{
    if (s_enc) {
        [s_enc endEncoding];
        s_enc = nil;
    }
    s_rt = NULL;
    s_rt_depth = NULL;
}

static void end_blit(void)
{
    if (s_blit) {
        [s_blit endEncoding];
        s_blit = nil;
    }
}

static id<MTLBlitCommandEncoder> blit(void)
{
    end_pass();
    if (!s_blit)
        s_blit = [s_cb blitCommandEncoder];
    return s_blit;
}

static void set_viewport(uint32_t pw, uint32_t ph)
{
    MTLViewport vp = { 0, 0, (double)pw, (double)ph, 0.0, 1.0 };
    MTLScissorRect sc = { 0, 0, pw, ph };
    [s_enc setViewport:vp];
    [s_enc setScissorRect:sc];
    s_sc_cur = sc;
}

/* SET_SURFACE_CLIP as the scissor, in s's stored pixels (nv2a_vk.c). */
static void set_surface_scissor(const uint32_t *r, const MtSurf *s)
{
    uint32_t x0 = (r[0x200 / 4] & 0xFFFF) * s->aa_sx, x1 = x0 + (r[0x200 / 4] >> 16) * s->aa_sx;
    uint32_t y0 = (r[0x204 / 4] & 0xFFFF) * s->aa_sy, y1 = y0 + (r[0x204 / 4] >> 16) * s->aa_sy;
    MTLScissorRect sc = { 0, 0, s->pw, s->ph };

    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 > x0 && y1 > y0) {
        sc.x = to_stored(x0, s->pw, s->w);
        sc.y = to_stored(y0, s->ph, s->h);
        sc.width = to_stored(x1, s->pw, s->w) - sc.x;
        sc.height = to_stored(y1, s->ph, s->h) - sc.y;
    }
    if (memcmp(&sc, &s_sc_cur, sizeof sc)) {
        [s_enc setScissorRect:sc];
        s_sc_cur = sc;
    }
}

static void begin_pass_clear(MtSurf *s, MtDepth *d, uint32_t flags,
                             const float *color, double depth, uint32_t stencil)
{
    MTLRenderPassDescriptor *rp;

    if (!flags && s_enc && s_rt == s && s_rt_depth == d)
        return;
    end_pass();
    end_blit();
    rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = s->tex;
    rp.colorAttachments[0].loadAction = (flags & 0xF0) ? MTLLoadActionClear : MTLLoadActionLoad;
    if (flags & 0xF0)
        rp.colorAttachments[0].clearColor = MTLClearColorMake(color[0], color[1], color[2], color[3]);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (d) {
        rp.depthAttachment.texture = d->tex;
        rp.depthAttachment.loadAction = (flags & 1) ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.depthAttachment.clearDepth = depth;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.stencilAttachment.texture = d->tex;
        rp.stencilAttachment.loadAction = (flags & 2) ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.stencilAttachment.clearStencil = stencil;
        rp.stencilAttachment.storeAction = MTLStoreActionStore;
    }
    s_enc = [s_cb renderCommandEncoderWithDescriptor:rp];
    s_rt = s;
    s_rt_depth = d;
    s_cur_pipe = nil;
    s_dyn_valid = 0;
    bindings_reset();
    /* The NV2A clamps depth rather than clipping at the near/far planes. */
    [s_enc setDepthClipMode:MTLDepthClipModeClamp];
    set_viewport(s->pw, s->ph);
}

static void begin_pass(MtSurf *s, MtDepth *d)
{
    begin_pass_clear(s, d, 0, NULL, 1.0, 0);
}

/* A texture cleared by an empty pass (new surfaces and depth buffers). */
static void clear_texture(id<MTLTexture> t, int depth)
{
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    id<MTLRenderCommandEncoder> e;
    end_pass();
    end_blit();
    if (depth) {
        rp.depthAttachment.texture = t;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.stencilAttachment.texture = t;
        rp.stencilAttachment.loadAction = MTLLoadActionClear;
        rp.stencilAttachment.clearStencil = 0;
        rp.stencilAttachment.storeAction = MTLStoreActionStore;
    } else {
        rp.colorAttachments[0].texture = t;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    e = [s_cb renderCommandEncoderWithDescriptor:rp];
    [e endEncoding];
}

/* ── Ring ─────────────────────────────────────────────────────────── */

static void flush_frame_wait(void);
static size_t ring_alloc(size_t size, size_t align, void **ptr)
{
    size_t at = (s_f->off + align - 1) & ~(align - 1);
    if (at + size > RING_BYTES) {
        static int warned;
        if (!warned++)
            LOGE("frame ring (%u MB) full: flushing mid-frame\n", RING_BYTES >> 20);
        flush_frame_wait();
        at = 0;
        if (size > RING_BYTES) {
            *ptr = NULL;
            return 0;
        }
    }
    s_f->off = at + size;
    *ptr = s_f->ptr + at;
    return at;
}

/* Submit what is recorded and wait for it, then keep recording into the
 * same ring (ring full, or a read-back). */
static void flush_frame_wait(void)
{
    id<MTLCommandBuffer> cb;
    end_pass();
    end_blit();
    nv2a_mtl_host_gate();
    cb = s_cb;
    watch_errors(cb);
    [cb commit];
    [cb waitUntilCompleted];
    s_f->off = 0;
    cb_new();
}

/* ── Surfaces ─────────────────────────────────────────────────────── */

static MtSurf *surf_find(uint32_t va)
{
    int i;
    for (i = 0; i < MTL_MAX_SURF; i++)
        if (s_surf[i].tex && s_surf[i].va == va)
            return &s_surf[i];
    return NULL;
}

static id<MTLTexture> make_texture(MTLTextureType type, MTLPixelFormat fmt, uint32_t w, uint32_t h,
                                   MTLTextureUsage usage)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor new];
    td.textureType = type;
    td.pixelFormat = fmt;
    td.width = w;
    td.height = h;
    td.mipmapLevelCount = 1;
    td.usage = usage;
    td.storageMode = MTLStorageModePrivate;
    return [s_dev newTextureWithDescriptor:td];
}

static void surf_reset(MtSurf *s)
{
    s->tex = nil;
    s->va = s->w = s->h = s->pw = s->ph = s->used = s->aa_sx = s->aa_sy = s->gen = 0;
}

static MtSurf *surf_get(uint32_t va, uint32_t w, uint32_t h, uint32_t aa_sx, uint32_t aa_sy)
{
    MtSurf *s = surf_find(va), *victim = NULL;
    int i;

    /* h is the surface clip, not the allocation (nv2a_vk.c): keep the
     * surface while the clip fits, grow it when it does not. */
    if (s && s->w == w && s->h >= h) {
        s->used = s_frame;
        return s;
    }
    if (!s) {
        for (i = 0; i < MTL_MAX_SURF; i++) {
            if (!s_surf[i].tex) { victim = &s_surf[i]; break; }
            if (!victim || s_surf[i].used < victim->used) victim = &s_surf[i];
        }
        s = victim;
    }
    if (s->tex) {
        if (s_rt == s) end_pass();
        if (s_last == s) s_last = NULL;
    }
    surf_reset(s);
    s->va = va; s->w = w; s->h = h; s->used = s_frame;
    s->aa_sx = aa_sx; s->aa_sy = aa_sy;
    {
        double k = s_scale, m = (double)(w > h ? w : h);
        if (m * k > (double)s_max_size)
            k = (double)s_max_size / m;
        s->pw = (uint32_t)(w * k + 0.5); s->ph = (uint32_t)(h * k + 0.5);
        if (!s->pw) s->pw = 1;
        if (!s->ph) s->ph = 1;
    }
    s->tex = make_texture(MTLTextureType2D, MTLPixelFormatBGRA8Unorm, s->pw, s->ph,
                          MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    if (!s->tex) {
        LOGE("surface 0x%08X %ux%u: texture creation failed\n", va, w, h);
        surf_reset(s);
        return NULL;
    }
    clear_texture(s->tex, 0);
    if (s_trace)
        LOGE("surface 0x%08X %ux%u (aa %ux%u, stored %ux%u)\n", va, w, h, aa_sx, aa_sy,
             s->pw, s->ph);
    return s;
}

static MtDepth *depth_get(uint32_t va, uint32_t pw, uint32_t ph)
{
    MtDepth *d, *victim = NULL;
    int i;

    for (i = 0; i < MTL_MAX_DEPTH; i++) {
        if (s_depth[i].tex && s_depth[i].va == va && s_depth[i].pw == pw && s_depth[i].ph == ph) {
            s_depth[i].used = s_frame;
            return &s_depth[i];
        }
        if (!victim || (victim->tex && (!s_depth[i].tex || s_depth[i].used < victim->used)))
            victim = &s_depth[i];
    }
    d = victim;
    if (d->tex && s_rt_depth == d)
        end_pass();
    d->tex = make_texture(MTLTextureType2D, MTLPixelFormatDepth32Float_Stencil8, pw, ph,
                          MTLTextureUsageRenderTarget);
    d->va = va; d->pw = pw; d->ph = ph; d->used = s_frame;
    if (!d->tex) {
        LOGE("depth 0x%08X %ux%u: texture creation failed\n", va, pw, ph);
        d->va = d->pw = d->ph = 0;
        return NULL;
    }
    clear_texture(d->tex, 1);
    if (s_trace)
        LOGE("depth 0x%08X %ux%u\n", va, pw, ph);
    return d;
}

/* The target of a draw or clear: surface (created on first use) and the
 * depth buffer of its zeta address. */
static MtSurf *target(const Nv2aSurface *sf, uint32_t zeta_va, MtDepth **dout)
{
    uint32_t bpp = sf->bytes_per_pixel ? sf->bytes_per_pixel : 4;
    uint32_t w = sf->pitch ? sf->pitch / bpp : sf->width;
    uint32_t h = sf->clip_y + sf->height;
    MtSurf *s;

    *dout = NULL;
    if (w < sf->clip_x + sf->width) w = sf->clip_x + sf->width;
    if (!w || !h || !sf->color_va || w > 4096 || h > 4096)
        return NULL;
    s = surf_get(sf->color_va, w, h, sf->aa_sx ? sf->aa_sx : 1, sf->aa_sy ? sf->aa_sy : 1);
    if (s && zeta_va)
        *dout = depth_get(zeta_va, s->pw, s->ph);
    if (s)
        s_last = s;
    return s;
}

/* ── Textures ─────────────────────────────────────────────────────── */

typedef struct {
    uint32_t va, color, w, h, pitch, hash, bytes;
    uint32_t face;              /* cube map: bytes per face; 0 = 2D */
    __strong id<MTLTexture> tex;
    MTLPixelFormat fmt;
    uint32_t used, checked;
} MtTex;

#define MTL_MAX_TEX 1024
#define TEX_BUCKETS 2048
static MtTex    s_tex[MTL_MAX_TEX];
static uint16_t s_tex_head[TEX_BUCKETS], s_tex_next[MTL_MAX_TEX];
static uint64_t s_tex_bytes;
static uint32_t *s_decode;
static size_t    s_decode_cap;
static uint32_t s_palette_reg, s_palette_va;
static id<MTLTexture> s_dummy, s_dummy_cube;

static uint32_t tex_bucket(uint32_t va, uint32_t color, uint32_t w, uint32_t h)
{
    uint32_t k = va ^ (color << 24) ^ (w << 12) ^ (h << 4);
    k ^= k >> 15; k *= 0x2C1B3C6Du; k ^= k >> 12;
    return k & (TEX_BUCKETS - 1);
}
static void tex_index_add(MtTex *t)
{
    uint32_t b = tex_bucket(t->va, t->color, t->w, t->h);
    uint16_t i = (uint16_t)(t - s_tex);
    s_tex_next[i] = s_tex_head[b];
    s_tex_head[b] = (uint16_t)(i + 1);
}
static void tex_index_del(MtTex *t)
{
    uint16_t *at = &s_tex_head[tex_bucket(t->va, t->color, t->w, t->h)];
    uint16_t i = (uint16_t)(t - s_tex);
    while (*at) {
        if (*at == i + 1) { *at = s_tex_next[i]; return; }
        at = &s_tex_next[*at - 1];
    }
}

static int tex_size_from_format(uint32_t color)
{
    if (color >= 0x0C && color <= 0x0F)
        return 1;
    switch (color) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
    case 0x16: case 0x17: case 0x18: case 0x1B: case 0x1C: case 0x1D:
    case 0x1E: case 0x1F: case 0x20: case 0x24: case 0x25: case 0x26:
    case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x34: case 0x35:
    case 0x36: case 0x37: case 0x40: case 0x41: case 0x43: case 0x44:
    case 0x45: case 0x46:
        return 0;
    default:
        return 1;
    }
}

static uint32_t bytes_hash(uint32_t va, uint32_t bytes)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset() + va;
    uint32_t h = 2166136261u ^ bytes, step, i;
    if (!bytes)
        return h;
    step = bytes / 256;
    if (step < 4) step = 4;
    step &= ~3u;
    for (i = 0; i + 4 <= bytes; i += step) {
        uint32_t w;
        memcpy(&w, mem + i, 4);
        h = (h ^ w) * 16777619u;
    }
    memcpy(&i, mem + bytes - 4, 4);
    return (h ^ i) * 16777619u;
}

static uint32_t tex_bytes(uint32_t color, uint32_t w, uint32_t h, uint32_t pitch)
{
    if (color == 0x0C) return ((w + 3) / 4) * ((h + 3) / 4) * 8;
    if (color == 0x0E || color == 0x0F) return ((w + 3) / 4) * ((h + 3) / 4) * 16;
    if (pitch) return pitch * h;
    return w * h * 4;
}

static uint32_t swizzle_index(uint32_t u, uint32_t v, uint32_t w, uint32_t h)
{
    uint32_t out = 0, bit = 0, mw = w - 1, mh = h - 1, m;
    for (m = 1; mw >= m || mh >= m; m <<= 1) {
        if (mw >= m) { if (u & m) out |= 1u << bit; bit++; }
        if (mh >= m) { if (v & m) out |= 1u << bit; bit++; }
    }
    return out;
}

static int decode_indexed(uint32_t va, uint32_t w, uint32_t h, uint32_t *out)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t entries = 256u >> ((s_palette_reg >> 2) & 3);
    const uint8_t *idx = mem + va;
    uint32_t x, y;
    if (!(s_palette_reg & ~0x3Fu))
        return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t i = idx[swizzle_index(x, y, w, h)], c;
            if (i >= entries) i = 0;
            memcpy(&c, mem + s_palette_va + i * 4, 4);
            out[y * w + x] = c;
        }
    return 1;
}

static void tex_free(MtTex *t)
{
    s_tex_bytes -= t->bytes;
    tex_index_del(t);
    t->tex = nil;               /* a command buffer in flight keeps its own reference */
    t->va = t->color = t->w = t->h = t->pitch = t->hash = t->bytes = t->face = 0;
    t->fmt = MTLPixelFormatInvalid;
    t->used = t->checked = 0;
}

static void tex_account(MtTex *t, uint32_t bytes)
{
    static uint64_t budget;
    s_hz_texs++;
    s_hz_tex_bytes += bytes;
    if (!budget) {
        const char *e = getenv("RECOMP_GL_TEX_MB");
        budget = (uint64_t)(e && atoi(e) > 0 ? atoi(e) : 256) << 20;
    }
    s_tex_bytes += (uint64_t)bytes - t->bytes;
    t->bytes = bytes;
    while (s_tex_bytes > budget) {
        MtTex *lru = NULL;
        uint32_t i;
        for (i = 0; i < MTL_MAX_TEX; i++) {
            MtTex *c = &s_tex[i];
            if (c->tex && c != t && c->used != s_frame && (!lru || c->used < lru->used))
                lru = c;
        }
        if (!lru)
            break;
        tex_free(lru);
    }
}

static int is_bc(MTLPixelFormat f)
{
    return f == MTLPixelFormatBC1_RGBA || f == MTLPixelFormatBC2_RGBA || f == MTLPixelFormatBC3_RGBA;
}

/* `bytes` at `src`, rows `row_bytes` apart, into slice `layer` of t. */
static void tex_upload(MtTex *t, const void *src, uint32_t bytes, uint32_t row_bytes,
                       uint32_t rows, uint32_t layer)
{
    void *dst;
    size_t at = ring_alloc(bytes, 16, &dst);
    if (!dst)
        return;
    memcpy(dst, src, bytes);
    [blit() copyFromBuffer:s_f->ring
              sourceOffset:at
         sourceBytesPerRow:row_bytes
       sourceBytesPerImage:(NSUInteger)row_bytes * rows
                sourceSize:MTLSizeMake(t->w, t->h, 1)
                 toTexture:t->tex
          destinationSlice:layer
          destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
}

/* Cube map faces follow each other, each with its mip chain, padded to 128
 * bytes; +X -X +Y -Y +Z -Z, Metal's slice order too. */
static uint32_t cube_face_bytes(uint32_t color, uint32_t w, uint32_t h, uint32_t levels)
{
    uint32_t n = 0, l;

    if (!levels)
        levels = 1;
    for (l = 0; l < levels; l++) {
        n += tex_bytes(color, w, h, 0);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return (n + 127) & ~127u;
}

/* One 2D texture (layer 0) or cube face from guest memory at va. */
static void tex_fill(MtTex *t, uint32_t va, uint32_t layer)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t color = t->color, w = t->w, h = t->h, pitch = t->pitch;
    Nv2aTexture nt;

    if (is_bc(t->fmt)) {                                    /* DXT as it is */
        uint32_t block = color == 0x0C ? 8 : 16;
        tex_upload(t, mem + va, tex_bytes(color, w, h, 0), ((w + 3) / 4) * block, (h + 3) / 4,
                   layer);
        return;
    }
    if (color == 0x12 && pitch && !(pitch & 3) && pitch / 4 >= w) {
        /* Linear A8R8G8B8 (movie frames): the bytes are BGRA8 already. */
        tex_upload(t, mem + va, pitch * (h - 1) + w * 4, pitch, h, layer);
        return;
    }
    if ((size_t)w * h > s_decode_cap) {
        free(s_decode);
        s_decode_cap = (size_t)w * h;
        s_decode = (uint32_t *)malloc(s_decode_cap * 4);
    }
    if (!s_decode)
        return;
    if (color == 0x06 || color == 0x07) {
        const uint32_t *src = (const uint32_t *)(mem + va);
        uint32_t x, y, fill = color == 0x07 ? 0xFF000000u : 0;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                s_decode[(size_t)y * w + x] = src[swizzle_index(x, y, w, h)] | fill;
    } else {
        int ok;
        memset(&nt, 0, sizeof nt);
        nt.offset = va; nt.width = w; nt.height = h; nt.pitch = pitch; nt.color = color;
        nt.addr_u = nt.addr_v = 1;
        ok = color == 0x0B ? decode_indexed(va, w, h, s_decode)
                           : nv2a_backend_decode_texture(&nt, s_decode);
        if (!ok) {
            uint32_t k;
            for (k = 0; k < w * h; k++)
                s_decode[k] = 0xFFFF00FFu;
            if (s_trace)
                LOGE("texture 0x%08X format 0x%02X not decodable\n", va, color);
        }
    }
    tex_upload(t, s_decode, w * h * 4, w * 4, h, layer);
}

/* face: bytes per cube face (cube_face_bytes), 0 for a 2D texture. */
static id<MTLTexture> tex_get(uint32_t va, uint32_t color, uint32_t w, uint32_t h, uint32_t pitch,
                              uint32_t face)
{
    MtTex *t = NULL, *victim = NULL;
    uint32_t i, hash;
    MTLPixelFormat fmt = MTLPixelFormatBGRA8Unorm;
    uint32_t layers = face ? 6 : 1, f, size;

    for (i = s_tex_head[tex_bucket(va, color, w, h)]; i; i = s_tex_next[i - 1]) {
        MtTex *c = &s_tex[i - 1];
        if (c->tex && c->va == va && c->color == color && c->w == w && c->h == h
            && c->face == face) {
            t = c;
            break;
        }
    }
    if (t && t->checked == s_frame) {
        t->used = s_frame;
        return t->tex;
    }
    hash = bytes_hash(va, face ? face * 6 : tex_bytes(color, w, h, pitch));
    if (color == 0x0B)
        hash ^= bytes_hash(s_palette_va, 1024) * 31u;
    if (t && t->hash == hash) {
        t->used = t->checked = s_frame;
        return t->tex;
    }
    uint64_t upload_start = hz_now();
    if (s_tex_changes < 0) {
        const char *e = getenv("RECOMP_TEX_CHANGES");
        s_tex_changes = e && atoi(e) > 0;
    }
    if (t && s_tex_changes)
        fprintf(stderr, "[tex] frame %u: %08X format %02X %ux%u changed\n",
                s_frame, va, color, w, h);
    if (s_have_bc && color == 0x0C) fmt = MTLPixelFormatBC1_RGBA;
    if (s_have_bc && color == 0x0E) fmt = MTLPixelFormatBC2_RGBA;
    if (s_have_bc && color == 0x0F) fmt = MTLPixelFormatBC3_RGBA;
    if (t && (t->fmt != fmt || t->pitch != pitch)) {
        tex_free(t);
        t = NULL;
    }
    if (!t) {
        if (face && w != h)
            return s_dummy_cube;                /* Metal cubes are square */
        for (i = 0; i < MTL_MAX_TEX; i++) {
            MtTex *c = &s_tex[i];
            if (!c->tex) { if (!victim || victim->tex) victim = c; }
            else if (!victim || (victim->tex && c->used < victim->used)) victim = c;
        }
        t = victim;
        if (t->tex)
            tex_free(t);
        t->va = va; t->color = color; t->w = w; t->h = h; t->pitch = pitch; t->fmt = fmt;
        t->face = face;
        VT("texture %08X fmt %02X %ux%u pitch %u%s\n", va, color, w, h, pitch, face ? " cube" : "");
        t->tex = make_texture(face ? MTLTextureTypeCube : MTLTextureType2D, fmt, w, h,
                              MTLTextureUsageShaderRead);
        if (!t->tex) {
            t->va = t->color = t->w = t->h = t->pitch = t->face = 0;
            return face ? s_dummy_cube : s_dummy;
        }
        tex_index_add(t);
        if (s_trace)
            LOGE("texture 0x%08X format 0x%02X %ux%u pitch %u%s\n", va, color, w, h, pitch,
                 face ? " cube" : "");
    }
    t->hash = hash;
    t->used = t->checked = s_frame;

    for (f = 0; f < layers; f++)
        tex_fill(t, va + f * face, f);
    size = is_bc(fmt) ? tex_bytes(color, w, h, 0) : w * h * 4;
    tex_account(t, size * layers);
    s_hz_tex_ns += hz_now() - upload_start;
    return t->tex;
}

/* ── Copies by drawing ──────────────────────────────────────────────
 *
 * Presenting and cube faces scale a surface into another texture; Metal's
 * blits do not scale, so a textured quad does it (linear filtering, as
 * vkCmdBlitImage did). */
static id<MTLRenderPipelineState> s_copy_pipe;          /* into BGRA8 */
static id<MTLRenderPipelineState> s_clear_pipe[2][2];   /* [colour][depth attachment] */
static id<MTLDepthStencilState>   s_clear_ds[2][2];     /* [depth][stencil] */
static id<MTLSamplerState>        s_linear;

/* src's texels (0,0)-(sw,sh) into dst's rectangle (dx,dy,dw,dh) of slice.
 * clear: 0 keeps dst, 1 clears it to opaque black first, 2 to transparent. */
static void draw_copy(id<MTLTexture> src, uint32_t sw, uint32_t sh, id<MTLTexture> dst, uint32_t slice,
                      int clear, double dx, double dy, double dw, double dh)
{
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    id<MTLRenderCommandEncoder> e;
    float uv[4] = { 0, 0, (float)sw / (float)src.width, (float)sh / (float)src.height };
    MTLViewport vp = { dx, dy, dw, dh, 0.0, 1.0 };

    end_pass();
    end_blit();
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].slice = slice;
    rp.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, clear == 2 ? 0 : 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    e = [s_cb renderCommandEncoderWithDescriptor:rp];
    if (src) {
        [e setRenderPipelineState:s_copy_pipe];
        [e setViewport:vp];
        [e setVertexBytes:uv length:sizeof uv atIndex:0];
        [e setFragmentTexture:src atIndex:0];
        [e setFragmentSamplerState:s_linear atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
    [e endEncoding];
}

/* A cube map whose faces the title renders (race car reflections); the
 * faces exist only as surfaces, so the cube is assembled from them again
 * whenever a face was drawn into since the last copy (nv2a_vk.c). */
typedef struct {
    uint32_t va, w, h, face, size;
    __strong id<MTLTexture> tex;
    MtSurf  *src[6];
    uint32_t gen[6];
    uint32_t used;
} MtCube;
#define MTL_MAX_CUBE 4
static MtCube s_cube[MTL_MAX_CUBE];

static id<MTLTexture> cube_from_surfaces(uint32_t va, uint32_t w, uint32_t h, uint32_t face,
                                         MtSurf *faces[6])
{
    MtCube *c = NULL;
    uint32_t f, size = 0;
    int i;

    for (f = 0; f < 6; f++)
        if (faces[f] && to_stored(w, faces[f]->pw, faces[f]->w) > size)
            size = to_stored(w, faces[f]->pw, faces[f]->w);
    for (i = 0; i < MTL_MAX_CUBE; i++)
        if (s_cube[i].tex && s_cube[i].va == va && s_cube[i].w == w && s_cube[i].h == h
            && s_cube[i].face == face && s_cube[i].size == size) {
            c = &s_cube[i];
            break;
        }
    if (!c) {
        for (i = 0; i < MTL_MAX_CUBE; i++)
            if (!c || !s_cube[i].tex || (c->tex && s_cube[i].used < c->used))
                c = &s_cube[i];
        c->tex = make_texture(MTLTextureTypeCube, MTLPixelFormatBGRA8Unorm, size, size,
                              MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        memset(c->src, 0, sizeof c->src);
        memset(c->gen, 0, sizeof c->gen);
        c->va = c->w = c->h = c->face = c->size = 0;
        if (!c->tex)
            return s_dummy_cube;
        for (f = 0; f < 6; f++)
            draw_copy(nil, 0, 0, c->tex, f, 2, 0, 0, size, size);   /* transparent black */
        c->va = va; c->w = w; c->h = h; c->face = face; c->size = size;
        if (s_trace)
            LOGE("cube 0x%08X %ux%u from surfaces (stored %u)\n", va, w, h, size);
    }
    c->used = s_frame;
    for (f = 0; f < 6; f++) {
        MtSurf *sf = faces[f];
        if (!sf || (c->src[f] == sf && c->gen[f] == sf->gen))
            continue;
        draw_copy(sf->tex, to_stored(w, sf->pw, sf->w), to_stored(h, sf->ph, sf->h), c->tex, f, 0,
                  0, 0, size, size);
        c->src[f] = sf;
        c->gen[f] = sf->gen;
    }
    return c->tex;
}

/* Samplers: wrap u, wrap v, mag, min. */
typedef struct { uint32_t key; __strong id<MTLSamplerState> s; } SamplerEnt;
static SamplerEnt s_samplers[64];
static int s_nsamplers;

static MTLSamplerAddressMode wrap_mode(uint32_t m)
{
    switch (m) {
    case 2:  return MTLSamplerAddressModeMirrorRepeat;
    case 3:  return MTLSamplerAddressModeClampToEdge;
    case 4:  return MTLSamplerAddressModeClampToZero;      /* border: transparent black */
    case 5:  return MTLSamplerAddressModeClampToEdge;
    default: return MTLSamplerAddressModeRepeat;
    }
}

static id<MTLSamplerState> sampler_get(uint32_t key)
{
    MTLSamplerDescriptor *d;
    int i;
    for (i = 0; i < s_nsamplers; i++)
        if (s_samplers[i].key == key)
            return s_samplers[i].s;
    d = [MTLSamplerDescriptor new];
    d.sAddressMode = wrap_mode(key & 0xF);
    d.tAddressMode = wrap_mode((key >> 8) & 0xF);
    d.rAddressMode = MTLSamplerAddressModeRepeat;
    d.magFilter = (key & 0x10000) ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    d.minFilter = (key & 0x20000) ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    d.mipFilter = MTLSamplerMipFilterNotMipmapped;
    if (s_nsamplers == 64)
        s_nsamplers = 0;                        /* never in practice: 4 x 4 x 4 keys */
    s_samplers[s_nsamplers].key = key;
    s_samplers[s_nsamplers].s = [s_dev newSamplerStateWithDescriptor:d];
    return s_samplers[s_nsamplers++].s;
}

static void rt_scale(uint32_t w, uint32_t h, uint32_t sw, uint32_t sh,
                     uint32_t aa_sx, uint32_t aa_sy, float scale[2])
{
    uint32_t lw = sw / (aa_sx ? aa_sx : 1), lh = sh / (aa_sy ? aa_sy : 1);

    if (w < lw) scale[0] *= (float)w / (float)lw;
    if (h < lh) scale[1] *= (float)h / (float)lh;
}

/* Stage i: its texture (a surface or a cached texture), sampler and
 * texcoord scale. Records uploads, so call it before the pass begins. */
static void bind_stage(const uint32_t *regs, int i, float scale[2], __strong id<MTLTexture> *tex_out,
                       __strong id<MTLSamplerState> *smp_out, MtSurf **sampled)
{
    int cube = ((regs[0x1E70 / 4] >> (5 * i)) & 0x1F) == 3;
    static int cube_on = -1;
    uint32_t base = (0x1B00u + (uint32_t)i * 0x40u) / 4;
    uint32_t control0 = regs[base + 3];
    uint32_t format = regs[base + 1];
    uint32_t color = (format >> 8) & 0xFF;
    uint32_t va, w, h, pitch = 0, addr, filter, key;
    MtSurf *rt;

    if (cube_on < 0) {
        const char *e = getenv("RECOMP_VK_CUBE");
        cube_on = !(e && *e == '0');
    }
    scale[0] = scale[1] = 1.0f;
    *tex_out = cube ? s_dummy_cube : s_dummy;
    *smp_out = sampler_get(0);
    *sampled = NULL;
    if (!(control0 & 0x40000000u) || !regs[base])
        return;
    va = s_batch->tex_va[i];
    if (tex_size_from_format(color)) {
        w = 1u << ((format >> 20) & 0xF);
        h = 1u << ((format >> 24) & 0xF);
    } else {
        uint32_t rect = regs[base + 7];
        w = rect >> 16;
        h = rect & 0xFFFF;
        pitch = regs[base + 4] >> 16;
        if (w) scale[0] = 1.0f / (float)w;
        if (h) scale[1] = 1.0f / (float)h;
    }
    if (!w || !h || w > 4096 || h > 4096)
        return;
    s_palette_reg = regs[base + 8];
    s_palette_va = s_batch->pal_va[i];
    rt = cube ? NULL : surf_find(va);
    if (cube) {
        if (cube_on && (format & 4)) {
            uint32_t face = cube_face_bytes(color, w, h, (format >> 16) & 0xF), f;
            MtSurf *faces[6];
            int any = 0;
            for (f = 0; f < 6; f++)
                any |= (faces[f] = surf_find(va + f * face)) != NULL;
            if (any) {
                *tex_out = cube_from_surfaces(va, w, h, face, faces);
            } else {
                *tex_out = tex_get(va, color, w, h, 0, face);
            }
        }
    } else if (rt) {
        *tex_out = rt->tex;
        *sampled = rt;
        rt_scale(w, h, rt->w, rt->h, rt->aa_sx, rt->aa_sy, scale);
    } else {
        *tex_out = tex_get(va, color, w, h, pitch, 0);
    }
    addr = regs[base + 2];
    filter = regs[base + 5];
    key = (addr & 0xF0F);
    if (((filter >> 24) & 0xF) == 1) key |= 0x10000;
    if (((filter >> 16) & 0xFF) == 1) key |= 0x20000;
    *smp_out = sampler_get(key);
}

/* A draw that samples the surface it renders into reads a copy taken just
 * before it: Metal does not allow sampling a pass's own attachment. */
static __strong id<MTLTexture> s_feedback;

static id<MTLTexture> feedback_copy(MtSurf *s)
{
    if (!s_feedback || s_feedback.width != s->pw || s_feedback.height != s->ph)
        s_feedback = make_texture(MTLTextureType2D, MTLPixelFormatBGRA8Unorm, s->pw, s->ph,
                                  MTLTextureUsageShaderRead);
    if (!s_feedback)
        return s_dummy;
    [blit() copyFromTexture:s->tex toTexture:s_feedback];
    return s_feedback;
}

/* ── Shaders and pipelines ────────────────────────────────────────── */

typedef struct {
    uint64_t   vkey;
    Nv2aPshKey pkey;
    __strong id<MTLLibrary> vlib;
    __strong id<MTLFunction> vs, vsp, fs;     /* vsp: points variant, made on demand */
    uint32_t   used;
    uint32_t   hash;
    int        next;
} MtProg;

#define MTL_MAX_PROG 1024
#define PROG_BUCKETS 1024
static MtProg s_prog[MTL_MAX_PROG];
static int s_prog_head[PROG_BUCKETS];
static uint32_t s_prog_made, s_prog_live;

/* Libraries by hash of their MSL: programs that share a vertex program or a
 * combiner state share the compiled half. */
static NSMutableDictionary<NSNumber *, id<MTLLibrary>> *s_libs;
static MTLCompileOptions *s_copts;

static uint64_t text_hash(const char *t)
{
    uint64_t h = 1469598103934665603ull;
    while (*t)
        h = (h ^ (uint8_t)*t++) * 1099511628211ull;
    return h;
}

static id<MTLLibrary> compile(const char *src, const char *what)
{
    NSNumber *k = @(text_hash(src));
    id<MTLLibrary> lib = s_libs[k];
    NSError *err = nil;

    if (lib)
        return lib;
    VT("compile %s (%u bytes of MSL)\n", what, (unsigned)strlen(src));
    lib = [s_dev newLibraryWithSource:[NSString stringWithUTF8String:src] options:s_copts error:&err];
    if (!lib) {
        LOGE("%s shader failed: %s\n", what, err.localizedDescription.UTF8String);
        if (s_trace)
            fprintf(stderr, "%s\n", src);
        return nil;
    }
    s_libs[k] = lib;
    return lib;
}

static id<MTLFunction> function(id<MTLLibrary> lib, const char *name, BOOL points)
{
    MTLFunctionConstantValues *cv = [MTLFunctionConstantValues new];
    NSError *err = nil;
    id<MTLFunction> fn;
    [cv setConstantValue:&points type:MTLDataTypeBool atIndex:NV2A_MSL_FC_POINTS];
    fn = [lib newFunctionWithName:[NSString stringWithUTF8String:name] constantValues:cv error:&err];
    if (!fn)
        LOGE("function %s: %s\n", name, err.localizedDescription.UTF8String);
    return fn;
}

static uint64_t prog_hash(const Nv2aRawBatch *b)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t pc, k;
    for (pc = b->vp_start; pc < b->vp_slots; pc++) {
        for (k = 0; k < 4; k++)
            h = (h ^ b->vp_program[pc][k]) * 1099511628211ull;
        if (b->vp_program[pc][3] & 1)
            break;
    }
    return h ? h : 1;
}

static uint32_t prog_bucket(uint64_t vkey, const Nv2aPshKey *pk)
{
    const uint32_t *w = (const uint32_t *)pk;
    uint64_t h = vkey ^ 1469598103934665603ull;
    size_t i;
    for (i = 0; i < sizeof *pk / 4; i++)
        h = (h ^ w[i]) * 1099511628211ull;
    return (uint32_t)(h ^ (h >> 32)) & (PROG_BUCKETS - 1);
}

/* ── Caches across runs ───────────────────────────────────────────
 *
 *   progcache.bin  what each program was built from: the GL and Vulkan
 *                  renderers' records, shared
 *   mtlpipes.bin   the pipeline variants seen (program + fixed state +
 *                  vertex formats)
 * Both are replayed at start-up, before the title draws. Compiled shaders
 * themselves are cached by Metal. RECOMP_PROG_CACHE=0 disables both. */
static int s_prewarming;
static int s_cache_off = -1;

static int cache_path(char *out, size_t cap, const char *name)
{
    if (s_cache_off < 0) {
        const char *e = getenv("RECOMP_PROG_CACHE");
        s_cache_off = e && e[0] == '0' && !e[1];
    }
    if (s_cache_off)
        return 0;
    if (!strcmp(name, "progcache.bin")) {
        const char *e = getenv("RECOMP_PROG_CACHE");
        if (e && *e) {
            snprintf(out, cap, "%s", e);
            return 1;
        }
    }
    snprintf(out, cap, "%s", name);             /* the current directory */
    return 1;
}

#define PROG_REC_MAGIC 0x4350564Eu              /* "NVPC" */
typedef struct {
    uint32_t   magic, xform, vp_start, slots;
    Nv2aPshKey pk;
    uint32_t   prog[136][4];
} ProgRec;

static void prog_rec_append(const Nv2aRawBatch *b, const Nv2aPshKey *pk)
{
    char path[256];
    ProgRec rec;
    FILE *f;
    if (s_prewarming || !cache_path(path, sizeof path, "progcache.bin"))
        return;
    memset(&rec, 0, sizeof rec);
    rec.magic = PROG_REC_MAGIC;
    rec.xform = b->xform == 2 ? 2 : 1;
    rec.pk = *pk;
    if (rec.xform == 2 && b->vp_program && b->vp_slots <= 136) {
        rec.vp_start = b->vp_start;
        rec.slots = b->vp_slots;
        memcpy(rec.prog, b->vp_program, (size_t)b->vp_slots * 16);
    }
    if ((f = fopen(path, "ab"))) {
        fwrite(&rec, sizeof rec, 1, f);
        fclose(f);
    }
}

static void prog_rec_batch(const ProgRec *rec, Nv2aRawBatch *bb, uint32_t n)
{
    memset(bb, 0, sizeof *bb);
    bb->xform = rec->xform;
    bb->vp_program = (const uint32_t (*)[4])rec->prog;
    bb->vp_slots = rec->slots ? rec->slots : 136;
    bb->vp_start = rec->vp_start;
    bb->vp_prog_gen = 0x80000000u + n;          /* hash it: not an executor version */
}

static void pipes_forget_prog(int prog);

static MtProg *prog_get(const Nv2aRawBatch *b, const Nv2aPshKey *pk)
{
    static uint32_t last_gen;
    static uint64_t last_vkey;
    static MtProg *last;
    static char vsrc[512 * 1024], fsrc[128 * 1024];
    uint64_t vkey = 0;
    uint32_t bucket;
    MtProg *p, *victim = NULL;
    id<MTLLibrary> vl, fl;
    int i;

    if (b->xform == 2) {
        if (b->vp_prog_gen != last_gen || !last_vkey) {
            last_vkey = prog_hash(b);
            last_gen = b->vp_prog_gen;
        }
        vkey = last_vkey;
    }
    if (last && last->vs && last->vkey == vkey && !memcmp(&last->pkey, pk, sizeof *pk)) {
        last->used = s_frame;
        return last;
    }
    bucket = prog_bucket(vkey, pk);
    for (i = s_prog_head[bucket]; i; i = s_prog[i - 1].next) {
        MtProg *c = &s_prog[i - 1];
        if (c->vs && c->vkey == vkey && !memcmp(&c->pkey, pk, sizeof *pk)) {
            c->used = s_frame;
            last = c;
            return c;
        }
    }
    uint64_t compile_start = hz_now();
    for (i = 0; i < MTL_MAX_PROG; i++) {
        MtProg *c = &s_prog[i];
        if (!c->vs) { if (!victim || victim->vs) victim = c; }
        else if (!victim || (victim->vs && c->used < victim->used)) victim = c;
    }
    p = victim;
    last = NULL;
    if (p->vs) {
        int *at = &s_prog_head[p->hash], me = (int)(p - s_prog) + 1;
        while (*at) {
            if (*at == me) { *at = p->next; break; }
            at = &s_prog[*at - 1].next;
        }
        pipes_forget_prog((int)(p - s_prog));
        s_prog_live--;
    }
    p->vlib = nil;
    p->vs = p->vsp = p->fs = nil;

    if (nv2a_msl_vertex(vkey ? b->vp_program : NULL, b->vp_slots, b->vp_start, vsrc, sizeof vsrc) < 0
     || nv2a_msl_fragment(pk, fsrc, sizeof fsrc) < 0) {
        LOGE("shader generation failed (vertex program %016llX)\n", (unsigned long long)vkey);
        return NULL;
    }
    vl = compile(vsrc, "vertex");
    fl = compile(fsrc, "fragment");
    if (!vl || !fl)
        return NULL;
    p->vs = function(vl, NV2A_MSL_VS_NAME, NO);
    p->fs = function(fl, NV2A_MSL_FS_NAME, NO);
    if (!p->vs || !p->fs) {
        p->vs = p->fs = nil;
        return NULL;
    }
    p->vlib = vl;
    s_prog_live++;
    if ((++s_prog_made % 100) == 0)
        LOGE("%u shader programs compiled (%u live)\n", s_prog_made, s_prog_live);
    p->vkey = vkey;
    p->pkey = *pk;
    p->used = s_frame;
    p->hash = bucket;
    p->next = s_prog_head[bucket];
    s_prog_head[bucket] = (int)(p - s_prog) + 1;
    last = p;
    prog_rec_append(b, pk);
    s_hz_progs++;
    s_hz_prog_ns += hz_now() - compile_start;
    return p;
}

/* The points variant of a program's vertex function (writes point_size). */
static id<MTLFunction> prog_vs_points(MtProg *p)
{
    if (!p->vsp && p->vlib)
        p->vsp = function(p->vlib, NV2A_MSL_VS_NAME, YES);
    return p->vsp;
}

/* Vertex formats in a pipeline key: the MTLVertexFormat, or this bit for an
 * absent attribute (a constant: the title's SET_VERTEX_DATA* value). */
#define VFMT_CONST 0x10000u

typedef struct {
    int      prog;
    uint32_t blend, bsrc, bdst, beq, cmask, depth, points;
    uint32_t vfmt[NV2A_RAW_ATTRS];
    uint32_t vstride[NV2A_RAW_ATTRS];   /* s_static_stride only, else 0 */
} PipeKey;

typedef struct {
    PipeKey key;
    __strong id<MTLRenderPipelineState> pipe;
    uint32_t hash;
    int next;
} PipeEnt;

#define MTL_MAX_PIPE 4096
#define PIPE_BUCKETS 4096
static PipeEnt s_pipes[MTL_MAX_PIPE];
static int s_pipe_head[PIPE_BUCKETS];
static int s_npipes;

static void pipes_forget_prog(int prog)
{
    int i;
    for (i = 0; i < s_npipes; i++) {
        PipeEnt *e = &s_pipes[i];
        if (e->pipe && e->key.prog == prog) {
            int *at = &s_pipe_head[e->hash], me = i + 1;
            while (*at) {
                if (*at == me) { *at = e->next; break; }
                at = &s_pipes[*at - 1].next;
            }
            e->pipe = nil;                      /* in-flight buffers keep theirs */
        }
    }
}

static MTLBlendFactor blend_factor(uint32_t v)
{
    switch (v) {
    case 0: return MTLBlendFactorZero;
    case 1: return MTLBlendFactorOne;
    case 0x300: return MTLBlendFactorSourceColor;
    case 0x301: return MTLBlendFactorOneMinusSourceColor;
    case 0x302: return MTLBlendFactorSourceAlpha;
    case 0x303: return MTLBlendFactorOneMinusSourceAlpha;
    case 0x304: return MTLBlendFactorDestinationAlpha;
    case 0x305: return MTLBlendFactorOneMinusDestinationAlpha;
    case 0x306: return MTLBlendFactorDestinationColor;
    case 0x307: return MTLBlendFactorOneMinusDestinationColor;
    case 0x308: return MTLBlendFactorSourceAlphaSaturated;
    case 0x8001: return MTLBlendFactorBlendColor;
    case 0x8002: return MTLBlendFactorOneMinusBlendColor;
    case 0x8003: return MTLBlendFactorBlendAlpha;
    case 0x8004: return MTLBlendFactorOneMinusBlendAlpha;
    default: return MTLBlendFactorOne;
    }
}

static MTLBlendOperation blend_op(uint32_t v)
{
    switch (v) {
    case 0x800A: return MTLBlendOperationSubtract;
    case 0x800B: return MTLBlendOperationReverseSubtract;
    case 0x8007: return MTLBlendOperationMin;
    case 0x8008: return MTLBlendOperationMax;
    default: return MTLBlendOperationAdd;
    }
}

/* mtlpipes.bin: the program by what generated it, and the pipeline's own
 * state. */
#define PIPE_REC_MAGIC 0x4C50544Du              /* "MTPL" */
typedef struct {
    uint32_t   magic, pad;
    uint64_t   vkey;
    Nv2aPshKey pk;
    uint32_t   blend, bsrc, bdst, beq, cmask, depth, points;
    uint32_t   vfmt[NV2A_RAW_ATTRS];
} PipeRec;

static void pipe_rec_append(const PipeKey *k)
{
    char path[256];
    PipeRec rec;
    FILE *f;
    if (s_prewarming || s_static_stride || !cache_path(path, sizeof path, "mtlpipes.bin"))
        return;                             /* static strides: not recorded */
    memset(&rec, 0, sizeof rec);
    rec.magic = PIPE_REC_MAGIC;
    rec.vkey = s_prog[k->prog].vkey;
    rec.pk = s_prog[k->prog].pkey;
    rec.blend = k->blend; rec.bsrc = k->bsrc; rec.bdst = k->bdst; rec.beq = k->beq;
    rec.cmask = k->cmask; rec.depth = k->depth; rec.points = k->points;
    memcpy(rec.vfmt, k->vfmt, sizeof rec.vfmt);
    if ((f = fopen(path, "ab"))) {
        fwrite(&rec, sizeof rec, 1, f);
        fclose(f);
    }
}

static uint32_t pipe_hash(const PipeKey *k)
{
    const uint32_t *w = (const uint32_t *)k;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *k / 4; i++)
        h = (h ^ w[i]) * 16777619u;
    return h & (PIPE_BUCKETS - 1);
}

static id<MTLRenderPipelineState> pipe_find(const PipeKey *k, uint32_t h)
{
    int e;
    for (e = s_pipe_head[h]; e; e = s_pipes[e - 1].next)
        if (s_pipes[e - 1].pipe && !memcmp(&s_pipes[e - 1].key, k, sizeof *k))
            return s_pipes[e - 1].pipe;
    return nil;
}

/* What the pipeline for k is made from; nil when its program has no
 * vertex function for it. */
static MTLRenderPipelineDescriptor *pipe_desc(const PipeKey *k)
{
    MtProg *p = &s_prog[k->prog];
    MTLRenderPipelineDescriptor *d;
    MTLVertexDescriptor *vd;
    id<MTLFunction> vs = k->points ? prog_vs_points(p) : p->vs;
    uint32_t i;

    if (!vs)
        return nil;
    d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = vs;
    d.fragmentFunction = p->fs;
    vd = [MTLVertexDescriptor vertexDescriptor];
    for (i = 0; i < NV2A_RAW_ATTRS; i++) {
        vd.attributes[i].format = (MTLVertexFormat)(k->vfmt[i] & 0xFFFF);
        vd.attributes[i].offset = 0;
        vd.attributes[i].bufferIndex = i;
        if (k->vfmt[i] & VFMT_CONST) {
            vd.layouts[i].stepFunction = MTLVertexStepFunctionConstant;
            vd.layouts[i].stepRate = 0;
            vd.layouts[i].stride = 16;
        } else {
            vd.layouts[i].stepFunction = MTLVertexStepFunctionPerVertex;
            vd.layouts[i].stride = s_static_stride ? k->vstride[i] : MTLBufferLayoutStrideDynamic;
        }
    }
    d.vertexDescriptor = vd;
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    d.colorAttachments[0].blendingEnabled = k->blend ? YES : NO;
    d.colorAttachments[0].sourceRGBBlendFactor = d.colorAttachments[0].sourceAlphaBlendFactor =
        blend_factor(k->bsrc);
    d.colorAttachments[0].destinationRGBBlendFactor = d.colorAttachments[0].destinationAlphaBlendFactor =
        blend_factor(k->bdst);
    d.colorAttachments[0].rgbBlendOperation = d.colorAttachments[0].alphaBlendOperation =
        blend_op(k->beq);
    d.colorAttachments[0].writeMask = k->cmask;
    if (k->depth) {
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    }
    if (k->points)
        d.inputPrimitiveTopology = MTLPrimitiveTopologyClassPoint;
    return d;
}

static void pipe_insert(const PipeKey *k, uint32_t h, id<MTLRenderPipelineState> pipe)
{
    PipeEnt *pe;
    if (s_npipes == MTL_MAX_PIPE) {
        static int warned;
        if (!warned++)
            LOGE("pipeline table full\n");
        return;
    }
    pe = &s_pipes[s_npipes];
    pe->key = *k;
    pe->pipe = pipe;
    pe->hash = h;
    pe->next = s_pipe_head[h];
    s_pipe_head[h] = ++s_npipes;
    pipe_rec_append(k);
}

static id<MTLRenderPipelineState> pipe_get(const PipeKey *k)
{
    uint32_t h = pipe_hash(k);
    MTLRenderPipelineDescriptor *d;
    NSError *err = nil;
    id<MTLRenderPipelineState> pipe = pipe_find(k, h);

    if (pipe)
        return pipe;
    if (!(d = pipe_desc(k)))
        return nil;
    VT("newRenderPipelineState prog %d blend %u cmask %X depth %u points %u\n", k->prog, k->blend,
       k->cmask, k->depth, k->points);
    pipe = [s_dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!pipe) {
        LOGE("pipeline creation failed: %s\n", err.localizedDescription.UTF8String);
        return nil;
    }
    pipe_insert(k, h, pipe);
    return pipe;
}

static MtProg *prog_find(uint64_t vkey, const Nv2aPshKey *pk)
{
    int i;
    for (i = s_prog_head[prog_bucket(vkey, pk)]; i; i = s_prog[i - 1].next) {
        MtProg *c = &s_prog[i - 1];
        if (c->vs && c->vkey == vkey && !memcmp(&c->pkey, pk, sizeof *pk))
            return c;
    }
    return NULL;
}

/* Compile what the recorded programs need, all at once: Metal compiles
 * asynchronous requests in parallel (its compiler service), so prewarm takes
 * about the time of the slowest library instead of the sum. The libraries
 * land in s_libs, where prog_get then finds them. */
static uint32_t prewarm_libraries(const char *path)
{
    static char vsrc[512 * 1024], fsrc[128 * 1024];
    NSMutableDictionary<NSNumber *, NSString *> *todo = [NSMutableDictionary new];
    NSMutableDictionary<NSNumber *, id<MTLLibrary>> *done = [NSMutableDictionary new];
    dispatch_group_t g = dispatch_group_create();
    __block uint32_t failed = 0;
    ProgRec rec;
    FILE *f = fopen(path, "rb");

    if (!f)
        return 0;
    while (fread(&rec, sizeof rec, 1, f) == 1) {
        const char *srcs[2] = { vsrc, fsrc };
        int i;
        if (rec.magic != PROG_REC_MAGIC || rec.slots > 136)
            break;
        if (nv2a_msl_vertex(rec.xform == 2 ? (const uint32_t (*)[4])rec.prog : NULL,
                            rec.slots ? rec.slots : 136, rec.vp_start, vsrc, sizeof vsrc) < 0
         || nv2a_msl_fragment(&rec.pk, fsrc, sizeof fsrc) < 0)
            continue;
        for (i = 0; i < 2; i++) {
            NSNumber *k = @(text_hash(srcs[i]));
            if (!s_libs[k] && !todo[k])
                todo[k] = [NSString stringWithUTF8String:srcs[i]];
        }
    }
    fclose(f);
    for (NSNumber *k in todo) {
        dispatch_group_enter(g);
        [s_dev newLibraryWithSource:todo[k] options:s_copts
                  completionHandler:^(id<MTLLibrary> lib, NSError *err) {
            @synchronized (done) {
                if (lib)
                    done[k] = lib;
                else
                    failed++;
            }
            dispatch_group_leave(g);
        }];
    }
    dispatch_group_wait(g, DISPATCH_TIME_FOREVER);
    [s_libs addEntriesFromDictionary:done];
    if (failed)
        LOGE("prewarm: %u shader libraries failed to compile\n", failed);
    return (uint32_t)done.count;
}

static void prewarm(void)
{
    char path[256];
    FILE *f;
    uint64_t t0 = hz_now(), t1;
    uint32_t nprog = 0, npipe = 0, nlib = 0, bad = 0;
    ProgRec rec;
    PipeRec pr;
    PipeKey *keys = NULL;
    uint32_t nkeys = 0, capkeys = 0, i;

    s_prewarming = 1;
    if (cache_path(path, sizeof path, "progcache.bin")) {
        @autoreleasepool {
            nlib = prewarm_libraries(path);
        }
    }
    if (cache_path(path, sizeof path, "progcache.bin") && (f = fopen(path, "rb"))) {
        while (fread(&rec, sizeof rec, 1, f) == 1) {
            Nv2aRawBatch bb;
            if (rec.magic != PROG_REC_MAGIC || rec.slots > 136) { bad++; break; }
            prog_rec_batch(&rec, &bb, nprog);
            @autoreleasepool {
                if (prog_get(&bb, &rec.pk))
                    nprog++;
            }
        }
        fclose(f);
    }
    t1 = hz_now();
    if (cache_path(path, sizeof path, "mtlpipes.bin") && (f = fopen(path, "rb"))) {
        while (fread(&pr, sizeof pr, 1, f) == 1) {
            MtProg *p;
            PipeKey k;
            if (pr.magic != PIPE_REC_MAGIC) { bad++; break; }
            if (!(p = prog_find(pr.vkey, &pr.pk)))
                continue;
            memset(&k, 0, sizeof k);
            k.prog = (int)(p - s_prog);
            k.blend = pr.blend; k.bsrc = pr.bsrc; k.bdst = pr.bdst; k.beq = pr.beq;
            k.cmask = pr.cmask; k.depth = pr.depth; k.points = pr.points;
            memcpy(k.vfmt, pr.vfmt, sizeof k.vfmt);
            if (nkeys == capkeys) {
                capkeys = capkeys ? capkeys * 2 : 256;
                keys = (PipeKey *)realloc(keys, capkeys * sizeof *keys);
                if (!keys) { nkeys = capkeys = 0; break; }
            }
            keys[nkeys++] = k;
        }
        fclose(f);
    }
    if (nkeys) {
        /* Pipelines in parallel too; inserted here, on this thread. */
        __strong id<MTLRenderPipelineState> *made =
            (__strong id<MTLRenderPipelineState> *)calloc(nkeys, sizeof *made);
        dispatch_group_t g = dispatch_group_create();
        @autoreleasepool {
            for (i = 0; i < nkeys && made; i++) {
                MTLRenderPipelineDescriptor *d;
                if (pipe_find(&keys[i], pipe_hash(&keys[i])) || !(d = pipe_desc(&keys[i])))
                    continue;
                dispatch_group_enter(g);
                [s_dev newRenderPipelineStateWithDescriptor:d
                                          completionHandler:^(id<MTLRenderPipelineState> ps, NSError *err) {
                    made[i] = ps;              /* each request owns its slot */
                    dispatch_group_leave(g);
                }];
            }
            dispatch_group_wait(g, DISPATCH_TIME_FOREVER);
        }
        for (i = 0; i < nkeys && made; i++) {
            uint32_t h = pipe_hash(&keys[i]);
            if (made[i] && !pipe_find(&keys[i], h)) {
                pipe_insert(&keys[i], h, made[i]);
                npipe++;
            }
            made[i] = nil;
        }
        free(made);
    }
    free(keys);
    s_prewarming = 0;
    fprintf(stderr, "  [MTL] prewarm: %u programs (%u libraries compiled), %u pipelines in %.0f ms"
            " (programs %.0f ms)%s\n", nprog, nlib, npipe, (hz_now() - t0) / 1e6, (t1 - t0) / 1e6,
            bad ? " (a file ends in a bad record)" : "");
}

/* ── Fixed shaders: copies and clears ─────────────────────────────── */

static int fixed_shaders(void)
{
    NSError *err = nil;
    id<MTLLibrary> lib = [s_dev newLibraryWithSource:[NSString stringWithUTF8String:nv2a_msl_fixed()]
                                             options:s_copts error:&err];
    MTLRenderPipelineDescriptor *d;
    MTLDepthStencilDescriptor *dd;
    MTLSamplerDescriptor *sd;
    int c, z, st;

    if (!lib) {
        LOGE("fixed shaders: %s\n", err.localizedDescription.UTF8String);
        return 0;
    }
    d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@"copy_vs"];
    d.fragmentFunction = [lib newFunctionWithName:@"copy_fs"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (!(s_copy_pipe = [s_dev newRenderPipelineStateWithDescriptor:d error:&err])) {
        LOGE("copy pipeline: %s\n", err.localizedDescription.UTF8String);
        return 0;
    }
    for (c = 0; c < 2; c++)
        for (z = 0; z < 2; z++) {
            d = [MTLRenderPipelineDescriptor new];
            d.vertexFunction = [lib newFunctionWithName:@"clear_vs"];
            d.fragmentFunction = [lib newFunctionWithName:@"clear_fs"];
            d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            d.colorAttachments[0].writeMask = c ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
            if (z) {
                d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
                d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
            }
            if (!(s_clear_pipe[c][z] = [s_dev newRenderPipelineStateWithDescriptor:d error:&err])) {
                LOGE("clear pipeline: %s\n", err.localizedDescription.UTF8String);
                return 0;
            }
        }
    for (z = 0; z < 2; z++)
        for (st = 0; st < 2; st++) {
            dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = MTLCompareFunctionAlways;
            dd.depthWriteEnabled = z ? YES : NO;
            if (st) {
                MTLStencilDescriptor *sdsc = [MTLStencilDescriptor new];
                sdsc.stencilCompareFunction = MTLCompareFunctionAlways;
                sdsc.stencilFailureOperation = MTLStencilOperationReplace;
                sdsc.depthFailureOperation = MTLStencilOperationReplace;
                sdsc.depthStencilPassOperation = MTLStencilOperationReplace;
                sdsc.readMask = 0xFF;
                sdsc.writeMask = 0xFF;
                dd.frontFaceStencil = sdsc;
                dd.backFaceStencil = sdsc;
            }
            s_clear_ds[z][st] = [s_dev newDepthStencilStateWithDescriptor:dd];
        }
    sd = [MTLSamplerDescriptor new];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    s_linear = [s_dev newSamplerStateWithDescriptor:sd];
    return 1;
}

/* ── Context ──────────────────────────────────────────────────────── */

static int ready(void)
{
    uint32_t i;

    if (s_state)
        return s_state > 0;
    s_state = -1;
    s_trace = getenv("RECOMP_GL_TRACE") != NULL;
    {
        const char *e = getenv("RECOMP_MTL_TRACE");
        s_vtrace = e ? atoi(e) : 0;
    }
    s_layer = (__bridge CAMetalLayer *)nv2a_mtl_host_layer();
    s_dev = s_layer ? s_layer.device : nil;
    if (!s_dev)
        s_dev = MTLCreateSystemDefaultDevice();
    if (!s_dev) {
        LOGE("no Metal device\n");
        return 0;
    }
    s_headless = s_layer == nil;
    if (s_layer) {
        s_layer.device = s_dev;
        s_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        s_layer.framebufferOnly = YES;
        s_layer.maximumDrawableCount = 3;
#if TARGET_OS_OSX
        /* The game paces itself (vblank emulation): presenting must not
         * block it (nv2a_vk asks for immediate/mailbox for the same reason). */
        s_layer.displaySyncEnabled = NO;
#endif
    }
    s_queue = [s_dev newCommandQueue];
    s_inflight = dispatch_semaphore_create(MTL_FRAMES);
    s_libs = [NSMutableDictionary new];
    s_copts = [MTLCompileOptions new];
    if (@available(macOS 15.0, iOS 18.0, *))
        s_copts.mathMode = MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        s_copts.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    s_max_size = [s_dev supportsFamily:MTLGPUFamilyApple3] ? 16384 : 8192;
    /* Apple4+ (every Apple-silicon Mac and A11+ device; the builds are
     * arm64 only). */
    s_static_stride = ![s_dev supportsFamily:MTLGPUFamilyApple4];
    s_have_bc = s_dev.supportsBCTextureCompression;
    {
        const char *e = getenv("RECOMP_GL_DXT");
        if (e && *e == '0')
            s_have_bc = 0;
    }
    {
        const char *e = getenv("RECOMP_GL_SCALE");
        double k = e ? strtod(e, NULL) : 1.0;
        if (!(k > 0.0)) k = 1.0;
        s_scale = k < 0.5 ? 0.5 : k > 4.0 ? 4.0 : k;
        if (s_scale != 1.0)
            fprintf(stderr, "  [MTL] rendering at %gx (RECOMP_GL_SCALE), surfaces up to %u\n",
                    s_scale, s_max_size);
    }
    fprintf(stderr, "  [MTL] %s\n", s_dev.name.UTF8String);
    if (!fixed_shaders())
        return 0;
    for (i = 0; i < MTL_FRAMES; i++) {
        s_fr[i].ring = [s_dev newBufferWithLength:RING_BYTES
                                          options:MTLResourceStorageModeShared |
                                                  MTLResourceCPUCacheModeWriteCombined];
        if (!s_fr[i].ring) {
            LOGE("frame ring allocation failed\n");
            return 0;
        }
        s_fr[i].ptr = (uint8_t *)s_fr[i].ring.contents;
    }
    s_fi = 0;
    frame_begin();
    s_dummy = make_texture(MTLTextureType2D, MTLPixelFormatBGRA8Unorm, 1, 1,
                           MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    s_dummy_cube = make_texture(MTLTextureTypeCube, MTLPixelFormatBGRA8Unorm, 1, 1,
                                MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    if (!s_dummy || !s_dummy_cube)
        return 0;
    {
        /* Transparent black, as nv2a_vk's dummies. */
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].texture = s_dummy;
        [[s_cb renderCommandEncoderWithDescriptor:rp] endEncoding];
        for (i = 0; i < 6; i++) {
            rp.colorAttachments[0].texture = s_dummy_cube;
            rp.colorAttachments[0].slice = i;
            [[s_cb renderCommandEncoderWithDescriptor:rp] endEncoding];
        }
    }
    s_state = 1;
    fprintf(stderr, "  [MTL] ready%s: BC %s, surfaces up to %u, vertex strides %s\n",
            s_headless ? " (headless)" : "", s_have_bc ? "yes" : "no", s_max_size,
            s_static_stride ? "in the pipelines" : "dynamic");
    prewarm();
    return 1;
}

/* ── Render state ─────────────────────────────────────────────────── */

static float byte_f(uint32_t v, int shift) { return (float)((v >> shift) & 0xFF) / 255.0f; }
static void argb_vec4(uint32_t v, float out[4])
{
    out[0] = byte_f(v, 16); out[1] = byte_f(v, 8);
    out[2] = byte_f(v, 0);  out[3] = byte_f(v, 24);
}

/* NV097 compare (GL_NEVER 0x200 .. GL_ALWAYS 0x207) = MTLCompareFunction + 0x200. */
static MTLCompareFunction cmp_op(uint32_t v, uint32_t dflt)
{
    if (!v) v = dflt;
    if (v < 0x200 || v > 0x207) v = dflt;
    return (MTLCompareFunction)(v - 0x200);
}

static MTLStencilOperation stencil_op(uint32_t v)
{
    switch (v) {
    case 0x1E01: return MTLStencilOperationReplace;
    case 0x1E02: return MTLStencilOperationIncrementClamp;
    case 0x1E03: return MTLStencilOperationDecrementClamp;
    case 0x150A: return MTLStencilOperationInvert;
    case 0x8507: return MTLStencilOperationIncrementWrap;
    case 0x8508: return MTLStencilOperationDecrementWrap;
    default: return MTLStencilOperationKeep;    /* KEEP, and 0 (never set) */
    }
}

typedef struct {
    uint32_t dtest, dwrite, dop;
    uint32_t stest, sfail, szfail, spass, sop, smask, swrite;
} DsKey;
typedef struct { DsKey key; __strong id<MTLDepthStencilState> ds; } DsEnt;
static DsEnt s_ds[256];
static int s_nds;

static id<MTLDepthStencilState> ds_get(const DsKey *k)
{
    MTLDepthStencilDescriptor *d;
    int i;
    for (i = 0; i < s_nds; i++)
        if (!memcmp(&s_ds[i].key, k, sizeof *k))
            return s_ds[i].ds;
    d = [MTLDepthStencilDescriptor new];
    d.depthCompareFunction = k->dtest ? cmp_op(k->dop, 0x203) : MTLCompareFunctionAlways;
    d.depthWriteEnabled = k->dwrite ? YES : NO;
    if (k->stest) {
        MTLStencilDescriptor *s = [MTLStencilDescriptor new];
        s.stencilCompareFunction = cmp_op(k->sop, 0x207);
        s.stencilFailureOperation = stencil_op(k->sfail);
        s.depthFailureOperation = stencil_op(k->szfail);
        s.depthStencilPassOperation = stencil_op(k->spass);
        s.readMask = k->smask;
        s.writeMask = k->swrite;
        d.frontFaceStencil = s;
        d.backFaceStencil = s;
    }
    if (s_nds == 256)
        s_nds = 0;                              /* never in practice */
    s_ds[s_nds].key = *k;
    s_ds[s_nds].ds = [s_dev newDepthStencilStateWithDescriptor:d];
    return s_ds[s_nds++].ds;
}

/* Encoder state for a draw, set only when it changed. */
typedef struct {
    uint32_t cull, cullmode, front;
    DsKey    ds;
    uint32_t sref, bcolor;
} DynState;
static DynState s_dyn;

/* Returns 0 when the draw shows nothing (front-and-back culled triangles:
 * Metal has no such cull mode). */
static int apply_dynamic(const uint32_t *r, int has_depth, uint32_t cls)
{
    DynState d;
    memset(&d, 0, sizeof d);
    d.cull = r[0x308 / 4] != 0;
    d.cullmode = r[0x39C / 4];
    d.front = r[0x3A0 / 4];
    d.ds.dtest = has_depth && r[0x30C / 4];
    d.ds.dwrite = has_depth && r[0x35C / 4];
    d.ds.dop = d.ds.dtest ? r[0x354 / 4] : 0;
    d.ds.stest = has_depth && r[0x32C / 4];
    if (d.ds.stest) {
        d.ds.sfail = r[0x370 / 4]; d.ds.szfail = r[0x374 / 4]; d.ds.spass = r[0x378 / 4];
        d.ds.sop = r[0x364 / 4]; d.ds.smask = r[0x36C / 4] & 0xFF; d.ds.swrite = r[0x360 / 4] & 0xFF;
        d.sref = r[0x368 / 4] & 0xFF;
    }
    d.bcolor = r[0x34C / 4];
    if (d.cull && d.cullmode == 0x408 && cls == 2)
        return 0;
    if (s_dyn_valid && !memcmp(&d, &s_dyn, sizeof d))
        return 1;
    if (!s_dyn_valid || d.cull != s_dyn.cull || d.cullmode != s_dyn.cullmode)
        [s_enc setCullMode:!d.cull ? MTLCullModeNone
                          : d.cullmode == 0x404 ? MTLCullModeFront : MTLCullModeBack];
    if (!s_dyn_valid || d.front != s_dyn.front)
        /* On-screen winding, rows top-first: CW is CW (nv2a_vk.c; the
         * nv2a_metal_msl test checks Metal agrees). */
        [s_enc setFrontFacingWinding:d.front == 0x901 ? MTLWindingCounterClockwise
                                                      : MTLWindingClockwise];
    if (!s_dyn_valid || memcmp(&d.ds, &s_dyn.ds, sizeof d.ds))
        [s_enc setDepthStencilState:ds_get(&d.ds)];
    if (!s_dyn_valid || d.sref != s_dyn.sref)
        [s_enc setStencilReferenceValue:d.sref];
    if (!s_dyn_valid || d.bcolor != s_dyn.bcolor) {
        float bc[4];
        argb_vec4(d.bcolor, bc);
        [s_enc setBlendColorRed:bc[0] green:bc[1] blue:bc[2] alpha:bc[3]];
    }
    s_dyn = d;
    s_dyn_valid = 1;
    return 1;
}

static void psh_key(const uint32_t *r, Nv2aPshKey *k)
{
    int i;
    memset(k, 0, sizeof *k);
    for (i = 0; i < 8; i++) {
        k->color_icw[i] = r[0xAC0 / 4 + i];
        k->color_ocw[i] = r[0x1E40 / 4 + i];
        k->alpha_icw[i] = r[0x260 / 4 + i];
        k->alpha_ocw[i] = r[0xAA0 / 4 + i];
    }
    k->control = r[0x1E60 / 4];
    k->final0 = r[0x288 / 4];
    k->final1 = r[0x28C / 4];
    k->shader_program = r[0x1E70 / 4];
    k->other_input = r[0x1E78 / 4];
}

static uint32_t zmax_of(const uint32_t *r)
{
    return (((r[0x208 / 4] >> 4) & 0xF) == 1) ? 0xFFFFu : 0xFFFFFFu;
}

/* NV097 primitive -> Metal primitive, class (0 points, 1 lines, 2
 * triangles) and (possibly rewritten) indices. Metal has no fans, loops or
 * quads. */
static uint32_t *s_prim_idx;
static size_t    s_prim_cap;

static int prim_indices(const Nv2aRawBatch *b, const uint32_t **idx, uint32_t *n,
                        MTLPrimitiveType *type, uint32_t *cls)
{
    uint32_t i, m = 0;
    size_t need;
    *idx = b->indices;
    *n = b->index_count;
    switch (b->prim) {
    case 1: *type = MTLPrimitiveTypePoint; *cls = 0; return 1;
    case 2: *type = MTLPrimitiveTypeLine; *cls = 1; return 1;
    case 4: *type = MTLPrimitiveTypeLineStrip; *cls = 1; return 1;
    case 5: *type = MTLPrimitiveTypeTriangle; *cls = 2; return 1;
    case 6: case 9: *type = MTLPrimitiveTypeTriangleStrip; *cls = 2; return 1;
    case 3:  need = (size_t)b->index_count + 1; break;                     /* line loop */
    case 7: case 10:                                                       /* fan, polygon */
             need = b->index_count >= 3 ? ((size_t)b->index_count - 2) * 3 : 0; break;
    case 8:  need = (size_t)b->index_count / 4 * 6; break;                 /* quads */
    default: *n = 0; return 0;
    }
    if (need > s_prim_cap) {
        free(s_prim_idx);
        s_prim_cap = need + 1024;
        s_prim_idx = (uint32_t *)malloc(s_prim_cap * 4);
    }
    if (!need || !s_prim_idx) { *n = 0; return 0; }
    if (b->prim == 3) {
        memcpy(s_prim_idx, b->indices, (size_t)b->index_count * 4);
        s_prim_idx[b->index_count] = b->indices[0];
        m = b->index_count + 1;
        *type = MTLPrimitiveTypeLineStrip; *cls = 1;
    } else if (b->prim == 8) {
        for (i = 0; i + 3 < b->index_count; i += 4) {
            const uint32_t *q = b->indices + i;
            s_prim_idx[m++] = q[0]; s_prim_idx[m++] = q[1]; s_prim_idx[m++] = q[2];
            s_prim_idx[m++] = q[0]; s_prim_idx[m++] = q[2]; s_prim_idx[m++] = q[3];
        }
        *type = MTLPrimitiveTypeTriangle; *cls = 2;
    } else {
        for (i = 1; i + 1 < b->index_count; i++) {
            s_prim_idx[m++] = b->indices[0];
            s_prim_idx[m++] = b->indices[i];
            s_prim_idx[m++] = b->indices[i + 1];
        }
        *type = MTLPrimitiveTypeTriangle; *cls = 2;
    }
    *idx = s_prim_idx;
    *n = m;
    return 1;
}

/* ── Vertices ─────────────────────────────────────────────────────── */

typedef struct { const uint8_t *lo, *hi; size_t at; } VRun;

static uint32_t elem_bytes(uint32_t type, uint32_t size)
{
    switch (type) {
    case 0: return 4;
    case 2: return 4 * size;
    case 4: return size;
    default: return 2 * size;
    }
}

/* The Metal format of a direct attribute uploaded as stored, or Invalid
 * when it is converted to float4 here: S32K (no scaled formats in Metal),
 * and data the vertex fetch cannot address (offsets and strides must be
 * multiples of 4). */
static MTLVertexFormat vertex_format(const Nv2aRawBatch *b, uint32_t a)
{
    static const MTLVertexFormat f_float[5] = { MTLVertexFormatInvalid, MTLVertexFormatFloat,
        MTLVertexFormatFloat2, MTLVertexFormatFloat3, MTLVertexFormatFloat4 };
    static const MTLVertexFormat f_snorm16[5] = { MTLVertexFormatInvalid, MTLVertexFormatShortNormalized,
        MTLVertexFormatShort2Normalized, MTLVertexFormatShort3Normalized, MTLVertexFormatShort4Normalized };
    static const MTLVertexFormat f_unorm8[5] = { MTLVertexFormatInvalid, MTLVertexFormatUCharNormalized,
        MTLVertexFormatUChar2Normalized, MTLVertexFormatUChar3Normalized, MTLVertexFormatUChar4Normalized };
    uint32_t type = b->direct[a].type, size = b->direct[a].size, stride = b->direct[a].stride;

    if (size < 1 || size > 4 || !stride || (stride & 3) || ((uintptr_t)b->direct[a].ptr & 3))
        return MTLVertexFormatInvalid;
    switch (type) {
    case 0: return MTLVertexFormatUChar4Normalized_BGRA;
    case 1: return f_snorm16[size];
    case 2: return f_float[size];
    case 4: return f_unorm8[size];
    default: return MTLVertexFormatInvalid;
    }
}

static uint32_t vertex_runs(const Nv2aRawBatch *b, uint16_t direct, VRun *run, int *of_run)
{
    uint32_t nv = b->vertex_count, nrun = 0, a, i;
    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        const uint8_t *lo, *hi;
        if (!(direct & (1u << a)))
            continue;
        lo = (const uint8_t *)((uintptr_t)b->direct[a].ptr & ~(uintptr_t)3);
        hi = b->direct[a].ptr + (size_t)(nv - 1) * b->direct[a].stride
           + elem_bytes(b->direct[a].type, b->direct[a].size);
        for (i = 0; i < nrun; i++)
            if (lo <= run[i].hi && run[i].lo <= hi)
                break;
        if (i == nrun) {
            run[nrun].lo = lo;
            run[nrun].hi = hi;
            nrun++;
        } else {
            if (lo < run[i].lo) run[i].lo = lo;
            if (hi > run[i].hi) run[i].hi = hi;
        }
        of_run[a] = (int)i;
    }
    for (i = 0; i + 1 < nrun; ) {
        uint32_t j;
        for (j = i + 1; j < nrun; j++)
            if (run[j].lo <= run[i].hi && run[i].lo <= run[j].hi)
                break;
        if (j == nrun) { i++; continue; }
        if (run[j].lo < run[i].lo) run[i].lo = run[j].lo;
        if (run[j].hi > run[i].hi) run[i].hi = run[j].hi;
        for (a = 0; a < NV2A_RAW_ATTRS; a++) {
            if (!(direct & (1u << a)))
                continue;
            if (of_run[a] == (int)j) of_run[a] = (int)i;
            else if (of_run[a] == (int)(nrun - 1)) of_run[a] = (int)j;
        }
        run[j] = run[nrun - 1];
        nrun--;
    }
    return nrun;
}

/* One attribute of vertex v, as the NV2A reads it, into float4. */
static void attr_to_float4(const Nv2aRawBatch *b, uint32_t a, uint32_t v, float out[4])
{
    const uint8_t *p = b->direct[a].ptr + (size_t)v * b->direct[a].stride;
    uint32_t size = b->direct[a].size, c;
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (b->direct[a].type) {
    case 0:
        out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f;
        break;
    case 1:
        for (c = 0; c < size && c < 4; c++) {
            int16_t s; memcpy(&s, p + 2 * c, 2);
            out[c] = s / 32767.0f < -1.0f ? -1.0f : s / 32767.0f;
        }
        break;
    case 2:
        for (c = 0; c < size && c < 4; c++) memcpy(&out[c], p + 4 * c, 4);
        break;
    case 4:
        for (c = 0; c < size && c < 4; c++) out[c] = p[c] / 255.0f;
        break;
    default:
        for (c = 0; c < size && c < 4; c++) { int16_t s; memcpy(&s, p + 2 * c, 2); out[c] = (float)s; }
        break;
    }
}

/* The stride each attribute gets in upload_vertices (s_static_stride): the
 * title's own for attributes uploaded as stored, the packed float4 block for
 * converted ones, 16 for absent ones (constant step: unused). */
static void vertex_strides_key(const Nv2aRawBatch *b, uint32_t *vstride)
{
    uint32_t a, np = 0, direct = 0;
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if ((b->attr_direct & (1u << a)) && vertex_format(b, a) != MTLVertexFormatInvalid)
            direct |= 1u << a;
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if ((b->attr_present & ~direct) & (1u << a))
            np++;
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        vstride[a] = (direct & (1u << a)) ? b->direct[a].stride
                   : (b->attr_present & (1u << a)) ? np * 16 : 16;
}

/* The format each attribute gets in upload_vertices: the title's own for
 * attributes uploaded as stored, float4 for converted ones, a float4
 * constant for absent ones. */
static void vertex_formats_key(const Nv2aRawBatch *b, uint32_t *vfmt)
{
    uint32_t a;
    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        MTLVertexFormat f = MTLVertexFormatInvalid;
        if (b->attr_direct & (1u << a))
            f = vertex_format(b, a);
        if (f != MTLVertexFormatInvalid)
            vfmt[a] = (uint32_t)f;
        else if (b->attr_present & (1u << a))
            vfmt[a] = (uint32_t)MTLVertexFormatFloat4;
        else
            vfmt[a] = (uint32_t)MTLVertexFormatFloat4 | VFMT_CONST;
    }
}

static int upload_vertices(const Nv2aRawBatch *b)
{
    VRun run[NV2A_RAW_ATTRS];
    int of_run[NV2A_RAW_ATTRS];
    uint32_t slot[NV2A_RAW_ATTRS], np = 0, nrun, a, i, v;
    uint32_t nv = b->vertex_count;
    uint16_t direct = 0, conv, cdirect = 0;
    size_t bytes = 0, pack_at, const_at, at;
    uint8_t *dst;
    void *map;
    id<MTLBuffer> bufs[NV2A_RAW_ATTRS];
    NSUInteger offs[NV2A_RAW_ATTRS], strides[NV2A_RAW_ATTRS];

    if (!nv)
        return 0;
    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        if (!(b->attr_direct & (1u << a)))
            continue;
        if (vertex_format(b, a) != MTLVertexFormatInvalid)
            direct |= (uint16_t)(1u << a);
        else
            cdirect |= (uint16_t)(1u << a);          /* direct data, converted here */
    }
    conv = b->attr_present & (uint16_t)~direct;
    nrun = vertex_runs(b, direct, run, of_run);
    for (i = 0; i < nrun; i++) {
        run[i].at = bytes;
        bytes += ((size_t)(run[i].hi - run[i].lo) + 15) & ~(size_t)15;
    }
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if (conv & (1u << a))
            slot[np++] = a;
    pack_at = bytes;
    bytes += (size_t)nv * np * 16;
    const_at = bytes;
    bytes += NV2A_RAW_ATTRS * 16;
    at = ring_alloc(bytes, 16, &map);
    dst = (uint8_t *)map;
    if (!dst)
        return 0;
    for (i = 0; i < nrun; i++)
        memcpy(dst + run[i].at, run[i].lo, (size_t)(run[i].hi - run[i].lo));
    if (np) {
        float *out = (float *)(dst + pack_at);
        for (v = 0; v < nv; v++)
            for (i = 0; i < np; i++) {
                float *o = out + ((size_t)v * np + i) * 4;
                if (cdirect & (1u << slot[i]))
                    attr_to_float4(b, slot[i], v, o);
                else
                    memcpy(o, b->attrs + ((size_t)v * NV2A_RAW_ATTRS + slot[i]) * 4, 16);
            }
    }
    memcpy(dst + const_at, b->attr_const, NV2A_RAW_ATTRS * 16);

    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        bufs[a] = s_f->ring;
        if (direct & (1u << a)) {
            strides[a] = b->direct[a].stride;
            offs[a] = at + run[of_run[a]].at + (size_t)(b->direct[a].ptr - run[of_run[a]].lo);
        } else if (conv & (1u << a)) {
            for (i = 0; i < np && slot[i] != a; i++) { }
            strides[a] = np * 16;
            offs[a] = at + pack_at + i * 16;
        } else {
            strides[a] = MTLAttributeStrideStatic;   /* constant step (pipeline) */
            offs[a] = at + const_at + a * 16;
        }
    }
    if (s_static_stride)
        [s_enc setVertexBuffers:bufs offsets:offs withRange:NSMakeRange(0, NV2A_RAW_ATTRS)];
    else
        [s_enc setVertexBuffers:bufs offsets:offs attributeStrides:strides
                      withRange:NSMakeRange(0, NV2A_RAW_ATTRS)];
    return 1;
}

/* ── Instancing (nv2a_vk.c) ───────────────────────────────────────────
 *
 * A run of draws that repeat the previous one with only the transform
 * constants changed becomes one instanced draw: the first is set up and
 * held (s_pend); every repeat appends its 192 constants right behind the
 * previous set in the ring (the vertex shader indexes them by instance);
 * anything else issues the held draw with the instance count first. */
#define MTL_INST_MAX   16
#define MTL_CONST_SIZE (192 * 16)
/* Ring contents remain immutable until a new command buffer starts. A
 * nonzero executor generation identifies all 192 constants, even when the
 * source pointer is reused. Instancing still allocates contiguous sets. */
static uint32_t s_consts_upload_cb, s_consts_upload_gen;
static size_t s_consts_upload_at;

extern uint8_t *nv2a_pb_reg_dirty(void);
static uint32_t s_regs_seen[0x2000 / 4];

static int regs_changed(const uint32_t *r)
{
    uint8_t *dirty = nv2a_pb_reg_dirty();
    uint32_t blk, w;
    int changed = 0;

    for (blk = 0; blk < 0x2000 / 64; blk++) {
        if (!dirty[blk])
            continue;
        dirty[blk] = 0;
        if (blk >= 0xB00 / 64 && blk < 0xC00 / 64)
            continue;                           /* program / constant uploads */
        for (w = blk * 16; w < blk * 16 + 16; w++) {
            uint32_t m = w * 4;
            if (r[w] == s_regs_seen[w])
                continue;
            s_regs_seen[w] = r[w];
            if (m == 0x0100 || m == 0x17FC || (m >= 0x1800 && m < 0x1820)
             || m == 0x1D6C || m == 0x1D70 || m == 0x1E9C || m == 0x1EA4)
                continue;                       /* NOP, BEGIN_END, draw data, semaphore, load slots */
            changed = 1;
        }
    }
    return changed;
}

static struct {
    int          on;
    uint32_t     n, count, max;
    size_t       cat;                       /* first instance's constants */
    uint32_t     cb_gen;
    MTLPrimitiveType type;
    MTLIndexType itype;
    size_t       iat;
    uint32_t     prim, vertex_count, index_count;
    uint16_t     attr_present, attr_direct;
    uint32_t     vp_prog_gen, attr_const_gen, vtx_epoch, color_va, zeta_va;
    uint32_t     tex_va[4], pal_va[4];
    float        aa_sx, aa_sy;
    Nv2aSurface  surface;
    struct { const uint8_t *ptr; uint32_t type, size, stride; } direct[NV2A_RAW_ATTRS];
    uint32_t    *idx;
    size_t       idx_cap;
} s_pend;
static int s_inst_on = -1, s_inst_alt;
static uint32_t s_st_draws, s_st_calls, s_st_inst;

static void draw_indexed(MTLPrimitiveType type, uint32_t n, MTLIndexType itype, size_t iat,
                         uint32_t instances)
{
    [s_enc drawIndexedPrimitives:type indexCount:n indexType:itype indexBuffer:s_f->ring
               indexBufferOffset:iat instanceCount:instances];
    s_st_calls++;
}

static void pend_flush(void)
{
    if (!s_pend.on)
        return;
    s_pend.on = 0;
    if (s_pend.cb_gen != s_cb_gen || !s_enc)
        return;                                 /* cannot happen: flushes go first */
    draw_indexed(s_pend.type, s_pend.n, s_pend.itype, s_pend.iat, s_pend.count);
}

static int pend_matches(const Nv2aRawBatch *b)
{
    uint32_t a;
    if (b->xform != 2 || b->prim != s_pend.prim || b->vertex_count != s_pend.vertex_count
     || b->index_count != s_pend.index_count || b->attr_present != s_pend.attr_present
     || b->attr_direct != s_pend.attr_direct || b->vp_prog_gen != s_pend.vp_prog_gen
     || b->attr_const_gen != s_pend.attr_const_gen || b->vtx_epoch != s_pend.vtx_epoch
     || b->color_va != s_pend.color_va || b->zeta_va != s_pend.zeta_va
     || b->aa_sx != s_pend.aa_sx || b->aa_sy != s_pend.aa_sy
     || memcmp(b->tex_va, s_pend.tex_va, sizeof s_pend.tex_va)
     || memcmp(b->pal_va, s_pend.pal_va, sizeof s_pend.pal_va)
     || memcmp(&b->surface, &s_pend.surface, sizeof s_pend.surface))
        return 0;
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if ((b->attr_direct & (1u << a))
         && (b->direct[a].ptr != s_pend.direct[a].ptr || b->direct[a].type != s_pend.direct[a].type
          || b->direct[a].size != s_pend.direct[a].size || b->direct[a].stride != s_pend.direct[a].stride))
            return 0;
    return !memcmp(b->indices, s_pend.idx, (size_t)b->index_count * 4);
}

static int pend_eligible(const Nv2aRawBatch *b)
{
    uint32_t a;
    if (!s_inst_on || b->xform != 2 || (b->attr_present & ~b->attr_direct))
        return 0;
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if ((b->attr_direct & (1u << a)) && vertex_format(b, a) == MTLVertexFormatInvalid)
            return 0;
    return 1;
}

static void pend_hold(const Nv2aRawBatch *b, MTLPrimitiveType type, uint32_t n, MTLIndexType itype,
                      size_t iat, size_t cat, uint32_t max)
{
    uint32_t a;
    if (b->index_count > s_pend.idx_cap) {
        free(s_pend.idx);
        s_pend.idx_cap = b->index_count + 1024;
        s_pend.idx = (uint32_t *)malloc(s_pend.idx_cap * 4);
        if (!s_pend.idx) {
            s_pend.idx_cap = 0;
            draw_indexed(type, n, itype, iat, 1);
            return;
        }
    }
    s_pend.on = 1;
    s_pend.n = n;
    s_pend.count = 1;
    s_pend.max = max;
    s_pend.cat = cat;
    s_pend.cb_gen = s_cb_gen;
    s_pend.type = type;
    s_pend.itype = itype;
    s_pend.iat = iat;
    s_pend.prim = b->prim;
    s_pend.vertex_count = b->vertex_count;
    s_pend.index_count = b->index_count;
    s_pend.attr_present = b->attr_present;
    s_pend.attr_direct = b->attr_direct;
    s_pend.vp_prog_gen = b->vp_prog_gen;
    s_pend.attr_const_gen = b->attr_const_gen;
    s_pend.vtx_epoch = b->vtx_epoch;
    s_pend.color_va = b->color_va;
    s_pend.zeta_va = b->zeta_va;
    s_pend.aa_sx = b->aa_sx;
    s_pend.aa_sy = b->aa_sy;
    s_pend.surface = b->surface;
    memcpy(s_pend.tex_va, b->tex_va, sizeof s_pend.tex_va);
    memcpy(s_pend.pal_va, b->pal_va, sizeof s_pend.pal_va);
    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        s_pend.direct[a].ptr = b->direct[a].ptr;
        s_pend.direct[a].type = b->direct[a].type;
        s_pend.direct[a].size = b->direct[a].size;
        s_pend.direct[a].stride = b->direct[a].stride;
    }
    memcpy(s_pend.idx, b->indices, (size_t)b->index_count * 4);
}

static int pend_append(const Nv2aRawBatch *b, int changed)
{
    size_t at;
    void *map;

    if (!s_pend.on)
        return 0;
    if (changed || s_pend.count >= s_pend.max || s_pend.cb_gen != s_cb_gen || !pend_matches(b))
        return 0;
    at = (s_f->off + CONST_ALIGN - 1) & ~(size_t)(CONST_ALIGN - 1);
    if (at != s_pend.cat + (size_t)s_pend.count * MTL_CONST_SIZE || at + MTL_CONST_SIZE > RING_BYTES)
        return 0;
    at = ring_alloc(MTL_CONST_SIZE, CONST_ALIGN, &map);
    if (!map)
        return 0;
    memcpy(map, b->vp_consts, MTL_CONST_SIZE);
    s_pend.count++;
    s_st_inst++;
    return 1;
}

/* ── Back end entry points ────────────────────────────────────────── */

static void mtl_draw_raw_inner(const Nv2aRawBatch *b)
{
    MtSurf *s, *sampled[4];
    MtDepth *d;
    MtProg *p;
    Nv2aPshKey pk;
    const uint32_t *r = b->regs;
    const uint32_t *idx;
    uint32_t n, i, cls;
    MTLPrimitiveType type;
    id<MTLTexture> tex[4];
    id<MTLSamplerState> smp[4];
    float tscale[4][2];
    PipeKey key;
    id<MTLRenderPipelineState> pipe;
    uint32_t inst_room;
    void *map;
    size_t iat, cat;
    MTLIndexType itype;

    if (!ready())
        return;
    s_regs = r;
    s_batch = b;
    if (s_inst_on < 0) {
        const char *e = getenv("RECOMP_VK_INSTANCE");
        s_inst_on = !(e && *e == '0');
        if (s_inst_on && e && *e == '2')
            s_inst_alt = 1;                     /* debug: every other frame */
        fprintf(stderr, "  [MTL] instancing %s\n", s_inst_on ? "on" : "off");
    }
    s_st_draws++;
    {
        int changed = regs_changed(r);
        if (pend_append(b, changed))
            return;
    }
    pend_flush();
    if (!prim_indices(b, &idx, &n, &type, &cls) || !n)
        return;
    VT("draw: prim %u idx %u verts %u attrs %04X direct %04X xform %u surf %08X zeta %08X\n", b->prim, n,
       b->vertex_count, b->attr_present, b->attr_direct, b->xform, b->surface.color_va, b->zeta_va);
    s = target(&b->surface, b->zeta_va, &d);
    if (!s)
        return;
    s_drew_any = 1;
    psh_key(r, &pk);
    p = prog_get(b, &pk);
    if (!p)
        return;
    s_hz_draws++;

    /* Textures first: uploads and copies end the pass. */
    for (i = 0; i < 4; i++)
        bind_stage(r, (int)i, tscale[i], &tex[i], &smp[i], &sampled[i]);
    {
        /* All stages in this draw read the same pre-draw contents. A 4×
         * surface is large: copy it once even if several stages sample it.
         * Keep this local to the draw so later writes always get a new copy. */
        id<MTLTexture> feedback = nil;
        for (i = 0; i < 4; i++)
            if (sampled[i] == s) {
                if (!feedback) feedback = feedback_copy(s);
                tex[i] = feedback;
            }
    }

    memset(&key, 0, sizeof key);
    key.prog = (int)(p - s_prog);
    if (r[0x304 / 4]) {
        key.blend = 1; key.bsrc = r[0x344 / 4]; key.bdst = r[0x348 / 4]; key.beq = r[0x350 / 4];
    }
    {
        uint32_t cm = r[0x358 / 4], m = 0;
        if (cm & 0x00010000u) m |= MTLColorWriteMaskRed;
        if (cm & 0x00000100u) m |= MTLColorWriteMaskGreen;
        if (cm & 0x00000001u) m |= MTLColorWriteMaskBlue;
        if (cm & 0x01000000u) m |= MTLColorWriteMaskAlpha;
        key.cmask = m;
    }
    key.depth = d != NULL;
    key.points = cls == 0;
    vertex_formats_key(b, key.vfmt);
    if (s_static_stride)
        vertex_strides_key(b, key.vstride);
    pipe = pipe_get(&key);
    if (!pipe)
        return;

    {
        /* Everything this draw puts in the ring, reserved now: a flush
         * later in the draw would reuse the ring under its own data. */
        size_t need = 2 * 4096 + MTL_CONST_SIZE + CONST_ALIGN + (size_t)n * 4
                    + NV2A_RAW_ATTRS * 16 + 16 * 16;
        uint32_t a;
        for (a = 0; a < NV2A_RAW_ATTRS; a++)
            if (b->attr_present & (1u << a)) {
                uint32_t st = (b->attr_direct & (1u << a)) ? b->direct[a].stride : 0;
                need += (size_t)b->vertex_count * (st > 16 ? st : 16) + 16;
            }
        if (s_f->off + need > RING_BYTES) {
            if (need > RING_BYTES) return;
            flush_frame_wait();
        }
    }
    begin_pass(s, d);
    if (!apply_dynamic(r, d != NULL, cls))
        return;
    s->gen++;
    if (pipe != s_cur_pipe) {
        [s_enc setRenderPipelineState:pipe];
        s_cur_pipe = pipe;
    }
    set_surface_scissor(r, s);

    /* Uniforms. */
    {
        Nv2aMslVs vb;
        Nv2aMslFs fb;
        memset(&vb, 0, sizeof vb);
        vb.surf[0] = 2.0f / (float)s->w;
        vb.surf[1] = 2.0f / (float)s->h;
        vb.surf[2] = 1.0f / (float)zmax_of(r);
        vb.surf[3] = 0.5f - 0.5f * (float)s->w / (float)s->pw;      /* see nv2a_snap */
        memcpy(vb.m, b->composite, sizeof vb.m);
        memcpy(vb.vpoff, b->vp_offset, sizeof vb.vpoff);
        vb.aa[0] = b->aa_sx > 0 ? b->aa_sx : 1.0f;
        vb.aa[1] = b->aa_sy > 0 ? b->aa_sy : 1.0f;
        vb.xform = b->xform == 1 ? 1 : 0;
        if (!s_vs_valid || memcmp(&vb, &s_vs_cur, sizeof vb)) {
            [s_enc setVertexBytes:&vb length:sizeof vb atIndex:NV2A_MSL_VS_BLOCK];
            s_vs_cur = vb;
            s_vs_valid = 1;
        }

        memset(&fb, 0, sizeof fb);
        for (i = 0; i < 4; i++) {
            fb.tscale[i][0] = tscale[i][0];
            fb.tscale[i][1] = tscale[i][1];
        }
        for (i = 0; i < 8; i++) {
            argb_vec4(r[0xA60 / 4 + i], fb.c0[i]);
            argb_vec4(r[0xA80 / 4 + i], fb.c1[i]);
        }
        argb_vec4(r[0x1E20 / 4], fb.fc0);
        argb_vec4(r[0x1E24 / 4], fb.fc1);
        {
            uint32_t fc = r[0x2A8 / 4];
            fb.fog[0] = byte_f(fc, 0); fb.fog[1] = byte_f(fc, 8);
            fb.fog[2] = byte_f(fc, 16); fb.fog[3] = byte_f(fc, 24);
        }
        fb.afunc = r[0x300 / 4] ? (int32_t)r[0x33C / 4] : 0x207;
        fb.aref = (float)(r[0x340 / 4] & 0xFF);
        if (!s_fs_valid || memcmp(&fb, &s_fs_cur, sizeof fb)) {
            [s_enc setFragmentBytes:&fb length:sizeof fb atIndex:NV2A_MSL_FS_BLOCK];
            s_fs_cur = fb;
            s_fs_valid = 1;
        }
    }
    for (i = 0; i < 4; i++) {
        if (tex[i] != s_tex_cur[i]) {
            [s_enc setFragmentTexture:tex[i] atIndex:i];
            s_tex_cur[i] = tex[i];
        }
        if (smp[i] != s_smp_cur[i]) {
            [s_enc setFragmentSamplerState:smp[i] atIndex:i];
            s_smp_cur[i] = smp[i];
        }
    }
    if (!upload_vertices(b))
        return;
    if (b->vertex_count <= 65536) {
        uint16_t *o;
        iat = ring_alloc((size_t)n * 2, 4, &map);
        o = (uint16_t *)map;
        if (!o) return;
        for (i = 0; i < n; i++)
            o[i] = (uint16_t)idx[i];
        itype = MTLIndexTypeUInt16;
    } else {
        iat = ring_alloc((size_t)n * 4, 4, &map);
        if (!map) return;
        memcpy(map, idx, (size_t)n * 4);
        itype = MTLIndexTypeUInt32;
    }
    /* Constants last: instanced repeats append right behind (pend_append).
     * Other draws may reuse an immutable upload in this command buffer. */
    int instanced = pend_eligible(b);
    if (!instanced && b->vp_const_gen && s_consts_upload_cb == s_cb_gen
        && s_consts_upload_gen == b->vp_const_gen) {
        cat = s_consts_upload_at;
    } else {
        cat = ring_alloc(MTL_CONST_SIZE, CONST_ALIGN, &map);
        if (!map) return;
        memcpy(map, b->vp_consts, MTL_CONST_SIZE);
        s_consts_upload_cb = s_cb_gen;
        s_consts_upload_gen = b->vp_const_gen;
        s_consts_upload_at = cat;
    }
    inst_room = (uint32_t)((RING_BYTES - cat) / MTL_CONST_SIZE);
    if (inst_room > MTL_INST_MAX) inst_room = MTL_INST_MAX;
    if (!s_consts_valid || s_consts_bound_at != cat) {
        [s_enc setVertexBuffer:s_f->ring offset:cat atIndex:NV2A_MSL_VS_CONSTS];
        s_consts_bound_at = cat;
        s_consts_valid = 1;
    }

    VT("  drawIndexedPrimitives %u\n", n);
    if (instanced && inst_room > 1)
        pend_hold(b, type, n, itype, iat, cat, inst_room);
    else
        draw_indexed(type, n, itype, iat, 1);
    if (s_vtrace > 0)
        s_vtrace--;
}

static void mtl_draw_raw(const Nv2aRawBatch *b)
{
    @autoreleasepool {
        mtl_draw_raw_inner(b);
    }
}

static void mtl_clear(const Nv2aSurface *sf, const Nv2aRenderState *rs,
                      uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    @autoreleasepool {
        MtSurf *s;
        MtDepth *d;
        MTLScissorRect rect;
        int color, dz;
        float c[4], z = 1.0f;

        if (!ready())
            return;
        pend_flush();
        s = target(sf, rs ? rs->zeta_va : 0, &d);
        if (!s)
            return;
        rect.x = rect.y = 0;
        rect.width = s->pw;
        rect.height = s->ph;
        if (s_regs) {
            uint32_t hz = s_regs[0x1D98 / 4], vt = s_regs[0x1D9C / 4];
            uint32_t x0 = (hz & 0xFFFF) * s->aa_sx, x1 = ((hz >> 16) + 1) * s->aa_sx;
            uint32_t y0 = (vt & 0xFFFF) * s->aa_sy, y1 = ((vt >> 16) + 1) * s->aa_sy;
            if (x1 > s->w) x1 = s->w;
            if (y1 > s->h) y1 = s->h;
            if (x1 > x0 && y1 > y0) {
                uint32_t px0 = to_stored(x0, s->pw, s->w), py0 = to_stored(y0, s->ph, s->h);
                rect.x = px0;
                rect.y = py0;
                rect.width = to_stored(x1, s->pw, s->w) - px0;
                rect.height = to_stored(y1, s->ph, s->h) - py0;
            }
        }
        if (d && rect.width > d->pw - rect.x)
            rect.width = d->pw - rect.x;
        if (d && rect.height > d->ph - rect.y)
            rect.height = d->ph - rect.y;
        if (!rect.width || !rect.height)
            return;
        color = (flags & 0xF0) == 0xF0;
        if ((flags & 0xF0) && !color) {
            static int warned;
            if (!warned++)
                LOGE("clear with a partial colour mask (0x%X) not done yet\n", flags & 0xF0);
        }
        dz = d && (flags & 0x3);
        if (!color && !dz)
            return;
        argb_vec4(argb, c);
        if (dz) {
            uint32_t zmax = s_regs ? zmax_of(s_regs) : 0xFFFFFFu;
            uint32_t zv = zmax == 0xFFFFu ? (zstencil & 0xFFFF) : (zstencil >> 8);
            z = (float)((double)zv / (double)zmax);
        }
        VT("clear %08X %ux%u flags %X rect %lu,%lu %lux%lu depth %p\n", s->va, s->pw, s->ph, flags,
           (unsigned long)rect.x, (unsigned long)rect.y, (unsigned long)rect.width,
           (unsigned long)rect.height, (void *)d);
        /* Tile GPUs can discard old attachment contents on a full clear.
         * Only use this when a new pass is needed anyway: restarting an
         * active pass would add a tile store/load. Partial rectangles and
         * colour masks retain the drawing path below. */
        if (!rect.x && !rect.y && rect.width == s->pw && rect.height == s->ph
            && (!d || (d->pw == s->pw && d->ph == s->ph))
            && !(s_enc && s_rt == s && s_rt_depth == d)) {
            begin_pass_clear(s, d, (color ? 0xF0u : 0) | (dz ? flags & 3u : 0),
                             c, z, zstencil & 0xFF);
            s->gen++;
            return;
        }
        begin_pass(s, d);
        {
            MTLViewport vp = { (double)rect.x, (double)rect.y, (double)rect.width,
                               (double)rect.height, 0.0, 1.0 };
            [s_enc setRenderPipelineState:s_clear_pipe[color][d != NULL]];
            [s_enc setViewport:vp];
            [s_enc setScissorRect:rect];
            [s_enc setCullMode:MTLCullModeNone];
            if (d) {
                [s_enc setDepthStencilState:s_clear_ds[(flags & 1) != 0][(flags & 2) != 0]];
                [s_enc setStencilReferenceValue:zstencil & 0xFF];
            }
            [s_enc setVertexBytes:&z length:sizeof z atIndex:0];
            [s_enc setFragmentBytes:c length:sizeof c atIndex:0];
            [s_enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            /* What the draws expect again. */
            vp.originX = vp.originY = 0;
            vp.width = s->pw;
            vp.height = s->ph;
            [s_enc setViewport:vp];
            s_sc_cur = rect;
            s_cur_pipe = nil;
            s_dyn_valid = 0;
            bindings_reset();
        }
        s->gen++;
    }
}

static void dump_bmp(const char *path, const uint8_t *bgra, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb");
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t row = w * 3, pad = (4 - (row & 3)) & 3, size = 54 + (row + pad) * h, y, x;
    if (!f)
        return;
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (y = h; y-- > 0; ) {
        const uint8_t *p = bgra + (size_t)y * w * 4;
        for (x = 0; x < w; x++)
            fwrite(p + x * 4, 1, 3, f);
        fwrite("\0\0\0", 1, pad, f);
    }
    fclose(f);
}

/* RECOMP_GL_DUMP: read the surface back (waits for the GPU). */
static void dump_surface(MtSurf *s, const char *path)
{
    id<MTLBuffer> buf = [s_dev newBufferWithLength:(NSUInteger)s->pw * s->ph * 4
                                           options:MTLResourceStorageModeShared];
    if (!buf)
        return;
    [blit() copyFromTexture:s->tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                 sourceSize:MTLSizeMake(s->pw, s->ph, 1) toBuffer:buf destinationOffset:0
        destinationBytesPerRow:(NSUInteger)s->pw * 4 destinationBytesPerImage:(NSUInteger)s->pw * s->ph * 4];
    flush_frame_wait();
    dump_bmp(path, (const uint8_t *)buf.contents, s->pw, s->ph);
}

static void present(MtSurf *s)
{
    id<CAMetalDrawable> drawable;
    CGSize ds;
    nv2a_mtl_host_gate();
    ds = s_layer.drawableSize;
    if (ds.width < 1 || ds.height < 1)
        return;
    drawable = [s_layer nextDrawable];
    if (!drawable)
        return;
    if (s && s_drew_any) {
        /* The logical aspect (16:9 for a widescreen title), letterboxed. */
        double lw = (double)s->w / s->aa_sx, lh = (double)s->h / s->aa_sy;
        double ww = ds.width, wh = ds.height, scale, dw, dh;
        if (xbox_video_widescreen() && lw < lh * 1.5)
            lw = lh * 16.0 / 9.0;
        scale = ww / lw < wh / lh ? ww / lw : wh / lh;
        dw = (double)(int)(lw * scale); dh = (double)(int)(lh * scale);
        if (xbox_apple_screen_fill()) { dw = ww; dh = wh; }
        draw_copy(s->tex, s->pw, s->ph, drawable.texture, 0, 1,
                  (double)(((int)ww - (int)dw) / 2), (double)(((int)wh - (int)dh) / 2), dw, dh);
    } else {
        draw_copy(nil, 0, 0, drawable.texture, 0, 1, 0, 0, ds.width, ds.height);
    }
    [s_cb presentDrawable:drawable];
}

static void mtl_flip_inner(void)
{
    static int dump_every = -1;
    static char dump_prefix[256];
    MtSurf *s = s_last;
    id<MTLCommandBuffer> cb;

    if (!ready())
        return;
    xbox_apple_limit_frame();
    pend_flush();
    s_frame++;
    if (s_inst_alt)
        s_inst_on = s_frame & 1;
    {
        static int st = -1;
        static uint32_t st_frame;
        if (st < 0) {
            const char *e = getenv("RECOMP_DRAW_STATS");
            st = e && *e == '1';
        }
        if (st && s_frame - st_frame >= 300) {
            fprintf(stderr, "[mtl] per frame: %u draws -> %u draw calls (%u instances appended)\n",
                    s_st_draws / (s_frame - st_frame), s_st_calls / (s_frame - st_frame),
                    s_st_inst / (s_frame - st_frame));
            s_st_draws = s_st_calls = s_st_inst = 0;
            st_frame = s_frame;
        }
    }
    {
        static uint64_t last_ns, window_ns;
        static int lines;
        uint64_t now = hz_now();
        if (last_ns) {
            uint64_t dt = now - last_ns;
            if (now - window_ns > 10000000000ull) { window_ns = now; lines = 0; }
            if (dt > 50000000ull && lines++ < 30)
                fprintf(stderr, "[hitch] frame %u: %.0f ms -- %u programs compiled (%.0f ms),"
                        " %u textures %.1f MB (%.0f ms), %u draws\n", s_frame, dt / 1e6,
                        s_hz_progs, s_hz_prog_ns / 1e6, s_hz_texs,
                        s_hz_tex_bytes / 1048576.0, s_hz_tex_ns / 1e6, s_hz_draws);
        }
        last_ns = now;
        s_hz_progs = s_hz_texs = s_hz_draws = 0;
        s_hz_prog_ns = s_hz_tex_ns = s_hz_tex_bytes = 0;
    }
    {
        static int on = -1;
        static uint32_t last_frame;
        static struct timespec last;
        struct timespec now;
        if (on < 0) {
            const char *e = getenv("RECOMP_FPS_LOG");
            on = e && *e == '1';
            clock_gettime(CLOCK_MONOTONIC, &last);
        }
        if (on) {
            double dt;
            clock_gettime(CLOCK_MONOTONIC, &now);
            dt = (double)(now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9;
            if (dt >= 10.0) {
                extern void xbox_vblank_report(double, char *, size_t);
                char vb[128];
                xbox_vblank_report(dt, vb, sizeof vb);
                fprintf(stderr, "[fps] %.1f fps (frame %u), %s\n",
                        (s_frame - last_frame) / dt, s_frame, vb);
                pthread_mutex_lock(&s_gpu_stats_lock);
                double gpu_ms = s_gpu_ms, gpu_max_ms = s_gpu_max_ms;
                uint64_t gpu_samples = s_gpu_samples;
                s_gpu_ms = s_gpu_max_ms = 0; s_gpu_samples = 0;
                pthread_mutex_unlock(&s_gpu_stats_lock);
                if (gpu_samples)
                    fprintf(stderr, "[gpu] command buffers: %.2f ms average, %.2f ms maximum, %llu completed (GPU execution, excludes queue wait)\n",
                            gpu_ms / gpu_samples, gpu_max_ms, (unsigned long long)gpu_samples);
                last_frame = s_frame;
                last = now;
            }
        }
    }
    if (dump_every < 0) {
        const char *dd = getenv("RECOMP_GL_DUMP");
        dump_every = 0;
        if (dd && *dd) {
            const char *comma = strchr(dd, ',');
            size_t nn = comma ? (size_t)(comma - dd) : strlen(dd);
            if (nn >= sizeof dump_prefix) nn = sizeof dump_prefix - 1;
            memcpy(dump_prefix, dd, nn);
            dump_prefix[nn] = 0;
            dump_every = comma ? atoi(comma + 1) : 60;
            if (dump_every <= 0) dump_every = 60;
        }
    }
    end_pass();
    if (s && s_drew_any && dump_every && ((s_frame % (uint32_t)dump_every) == 0
                                          || (s_inst_alt && s_frame % (uint32_t)dump_every == 1))) {
        char path[320];
        snprintf(path, sizeof path, "%s%05u.bmp", dump_prefix, s_frame);
        dump_surface(s, path);
    }
    VT("flip %u: surface %08X drew %d\n", s_frame, s ? s->va : 0, s_drew_any);
    if (!s_headless)
        present(s);
    end_pass();
    end_blit();
    nv2a_mtl_host_gate();
    cb = s_cb;
    watch_errors(cb);
    {
        dispatch_semaphore_t sem = s_inflight;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
            (void)b;
            dispatch_semaphore_signal(sem);
        }];
    }
    [cb commit];
    s_cb = nil;
    VT("flip %u: committed, waiting for frame slot\n", s_frame);
    s_fi = (s_fi + 1) % MTL_FRAMES;
    frame_begin();
}

static void mtl_flip(void)
{
    @autoreleasepool {
        mtl_flip_inner();
    }
}

static void mtl_draw(const Nv2aSurface *s, const Nv2aBatch *b)
{
    (void)s; (void)b;                        /* draw_raw handles every batch */
}

static const Nv2aBackend s_backend = {
    mtl_clear,
    mtl_draw,
    mtl_flip,
    mtl_draw_raw,
    NV2A_BACKEND_RAW_DIRECT,
};

void nv2a_mtl_install(void)
{
    nv2a_backend_register(&s_backend);
    fprintf(stderr, "[BOOT] NV2A Metal renderer registered\n");
}
