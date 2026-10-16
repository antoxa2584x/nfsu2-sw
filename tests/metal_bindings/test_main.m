/* Real GPU regression with synthetic triangles. Include the backend so its
 * encoder cache can be inspected without exposing test hooks in the runtime. */
#include "../../xboxrecomp/src/nv2a_metal/nv2a_metal.m"
#include <assert.h>
static uint8_t dirty[128];
ptrdiff_t xbox_GetMemoryOffset(void) { return 0; }
uint8_t *nv2a_pb_reg_dirty(void) { return dirty; }
int xbox_video_widescreen(void) { return 0; }
void xbox_vblank_report(double dt, char *out, size_t cap) { (void)dt; snprintf(out, cap, "test"); }
void nv2a_backend_register(const Nv2aBackend *b) { (void)b; }
int nv2a_backend_decode_texture(const Nv2aTexture *t, uint32_t *out) { (void)t; (void)out; return 0; }

@interface CountingEncoder : NSObject
@property(nonatomic, strong) id<MTLRenderCommandEncoder> target;
@property(nonatomic) unsigned bindings;
@property(nonatomic) unsigned constantBindings;
@end
@interface CountingBlitEncoder : NSObject
@property(nonatomic, strong) id<MTLBlitCommandEncoder> target;
@property(nonatomic) unsigned copies;
@end
@implementation CountingBlitEncoder
- (id)forwardingTargetForSelector:(SEL)s { return self.target; }
- (void)copyFromTexture:(id<MTLTexture>)source toTexture:(id<MTLTexture>)destination
{ self.copies++; [self.target copyFromTexture:source toTexture:destination]; }
@end
@implementation CountingEncoder
- (id)forwardingTargetForSelector:(SEL)s { return self.target; }
- (void)setVertexBytes:(const void *)bytes length:(NSUInteger)n atIndex:(NSUInteger)i
{ self.bindings++; [self.target setVertexBytes:bytes length:n atIndex:i]; }
- (void)setFragmentBytes:(const void *)bytes length:(NSUInteger)n atIndex:(NSUInteger)i
{ self.bindings++; [self.target setFragmentBytes:bytes length:n atIndex:i]; }
- (void)setFragmentTexture:(id<MTLTexture>)t atIndex:(NSUInteger)i
{ self.bindings++; [self.target setFragmentTexture:t atIndex:i]; }
- (void)setFragmentSamplerState:(id<MTLSamplerState>)s atIndex:(NSUInteger)i
{ self.bindings++; [self.target setFragmentSamplerState:s atIndex:i]; }
- (void)setVertexBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)i
{ if (i == NV2A_MSL_VS_CONSTS) self.constantBindings++; [self.target setVertexBuffer:buffer offset:offset atIndex:i]; }
@end
static CountingEncoder *count_encoder(void)
{
    CountingEncoder *e = [CountingEncoder new];
    e.target = s_enc;
    s_enc = (id<MTLRenderCommandEncoder>)e;
    return e;
}
static void check_texture(id<MTLTexture> texture, uint32_t expected)
{
    NSUInteger w = texture.width, h = texture.height;
    id<MTLBuffer> out = [s_dev newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    [blit() copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(w, h, 1) toBuffer:out destinationOffset:0
        destinationBytesPerRow:w * 4 destinationBytesPerImage:w * h * 4];
    flush_frame_wait();
    uint32_t actual = ((uint32_t *)out.contents)[(h / 4) * w + w / 4];
    if (actual != expected) fprintf(stderr, "pixel %08X, expected %08X\n", actual, expected);
    assert(actual == expected);
}
static void check_pixel(MtSurf *s, uint32_t expected) { check_texture(s->tex, expected); }
static int compare_ms(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void benchmark_constants(Nv2aRawBatch *batch)
{
    if (!getenv("NFSU2_TEST_BENCH_CONSTANTS")) return;
    double ms[2][7];
    uint32_t saved_gen = batch->vp_const_gen;
    for (int trial = 0; trial < 7; trial++)
        for (int order = 0; order < 2; order++) {
            int cached = (order + trial) & 1;
            // Zero requests unconditional uploads, matching the previous path.
            batch->vp_const_gen = cached ? 1 : 0;
            mtl_draw_raw(batch);
            uint64_t start = hz_now();
            for (int i = 0; i < 2000; i++) mtl_draw_raw(batch);
            ms[cached][trial] = (hz_now() - start) / 1e6;
            // Completion and GPU pixel verification stay outside the timer.
            check_pixel(s_last, 0xFFFF0000);
        }
    qsort(ms[0], 7, sizeof(double), compare_ms);
    qsort(ms[1], 7, sizeof(double), compare_ms);
    printf("Paired median, 7 trials of 2000 unchanged draws: unconditional uploads %.2f ms, cached %.2f ms (%.1f%% less submission time)\n",
           ms[0][3], ms[1][3], (1.0 - ms[1][3] / ms[0][3]) * 100);
    batch->vp_const_gen = saved_gen;
}
int main(void)
{
    @autoreleasepool {
        setenv("RECOMP_PROG_CACHE", "0", 1);
        setenv("RECOMP_VK_INSTANCE", "0", 1);
        float attrs[3][16][4] = {0}, constants[192][4] = {0};
        const uint32_t indices[] = {0, 1, 2};
        uint32_t regs[0x2000 / 4] = {0};
        regs[0x1D98 / 4] = 63u << 16; // full clear rectangle
        regs[0x1D9C / 4] = 63u << 16;
        regs[0x358 / 4] = 0x01010101; // all colour channels
        regs[0x288 / 4] = 1;          // final-combiner D = constant C0
        regs[0x28C / 4] = 0x3000;     // alpha = 1 - ZERO.a
        regs[0x1E20 / 4] = 0xFFFF0000;
        for (int i = 0; i < 3; i++) attrs[i][0][3] = 1;
        attrs[1][0][0] = 64; attrs[2][0][1] = 64;
        Nv2aRawBatch batch = { .prim = 5, .vertex_count = 3, .attr_present = 1,
            .attrs = &attrs[0][0][0], .indices = indices, .index_count = 3, .regs = regs,
            .vp_consts = constants, .vp_const_count = 192, .vp_const_gen = 1, .aa_sx = 1, .aa_sy = 1,
            .surface = { .color_va = 0x1000, .width = 64, .height = 64, .pitch = 256,
                         .bytes_per_pixel = 4, .aa_sx = 1, .aa_sy = 1 } };
        memset(dirty, 1, sizeof dirty);
        mtl_draw_raw(&batch);
        assert(s_enc && s_vs_valid && s_fs_valid);
        CountingEncoder *e = count_encoder();
        uint64_t start = hz_now();
        size_t original_constants = s_consts_upload_at;
        for (int i = 0; i < 2000; i++) mtl_draw_raw(&batch);
        printf("2000 repeated draws: %u uniform/texture/sampler bindings (previously 20000), %.2f ms CPU\n",
               e.bindings, (hz_now() - start) / 1e6);
        assert(e.bindings == 0);
        assert(e.constantBindings == 0 && s_consts_upload_at == original_constants);
        printf("2000 unchanged transform sets: 6,144,000 upload bytes and 2000 constant-buffer binds avoided\n");
        check_pixel(s_last, 0xFFFF0000);
        benchmark_constants(&batch);
        // A fresh encoder must bind resources even when contents did not change.
        begin_pass(s_last, NULL); e = count_encoder();
        mtl_draw_raw(&batch); assert(e.bindings == 10);
        check_pixel(s_last, 0xFFFF0000);
        // Changed uniforms must be used; unchanged vertex block stays cached.
        begin_pass(s_last, NULL); mtl_draw_raw(&batch); e = count_encoder();
        regs[0x1E20 / 4] = 0xFF00FF00; memset(dirty, 1, sizeof dirty);
        mtl_draw_raw(&batch); assert(e.bindings == 1);
        check_pixel(s_last, 0xFF00FF00);
        // Reused source pointers may contain a new generation of constants.
        constants[191][3] = 17.0f; batch.vp_const_gen++;
        mtl_draw_raw(&batch);
        assert(!memcmp(s_f->ptr + s_consts_upload_at, constants, sizeof constants));
        size_t changed_constants = s_consts_upload_at;
        begin_pass(s_last, NULL); e = count_encoder();
        mtl_draw_raw(&batch);
        assert(s_consts_upload_at == changed_constants && e.constantBindings == 0);
        // A fresh command buffer must upload again, even for the same generation.
        check_pixel(s_last, 0xFF00FF00);
        uint32_t previous_cb = s_consts_upload_cb;
        mtl_draw_raw(&batch);
        assert(s_consts_upload_cb == s_cb_gen && previous_cb != s_cb_gen);
        // Generation zero carries no identity guarantee: always copy it.
        batch.vp_const_gen = 0;
        mtl_draw_raw(&batch); changed_constants = s_consts_upload_at;
        constants[191][3] = 33.0f; mtl_draw_raw(&batch);
        assert(s_consts_upload_at != changed_constants);
        assert(!memcmp(s_f->ptr + s_consts_upload_at, constants, sizeof constants));
        // Clear overwrites fragment buffer 0; subsequent draws must restore it.
        mtl_draw_raw(&batch);
        mtl_clear(&batch.surface, NULL, 0xF0, 0xFF0000FF, 0);
        assert(!s_fs_valid && !s_vs_valid);
        e = count_encoder(); mtl_draw_raw(&batch); assert(e.bindings == 10);
        check_pixel(s_last, 0xFF00FF00);
        // A full clear at a pass boundary uses attachment clear actions.
        mtl_clear(&batch.surface, NULL, 0xF0, 0xFF0000FF, 0);
        assert(!s_fs_valid && !s_vs_valid);
        check_pixel(s_last, 0xFF0000FF);
        // A partial clear must preserve pixels outside its rectangle.
        s_regs = regs;
        regs[0x1D98 / 4] = 7u << 16;
        regs[0x1D9C / 4] = 7u << 16;
        mtl_clear(&batch.surface, NULL, 0xF0, 0xFFFF0000, 0);
        check_pixel(s_last, 0xFF0000FF);
        regs[0x1D98 / 4] = 63u << 16;
        regs[0x1D9C / 4] = 63u << 16;
        mtl_clear(&batch.surface, NULL, 0xF0, 0xFFFF0000, 0);
        check_pixel(s_last, 0xFFFF0000);
        // Four texture stages sampling this attachment share one snapshot.
        regs[0x1E20 / 4] = 0xFFFF0000;
        for (int i = 0; i < 4; i++) {
            unsigned base = (0x1B00 + i * 0x40) / 4;
            regs[base] = batch.surface.color_va;
            regs[base + 1] = (6u << 20) | (6u << 24) | (6u << 8);
            regs[base + 3] = 0x40000000;
            batch.tex_va[i] = batch.surface.color_va;
        }
        CountingBlitEncoder *copies = [CountingBlitEncoder new];
        copies.target = blit(); s_blit = (id<MTLBlitCommandEncoder>)copies;
        memset(dirty, 1, sizeof dirty);
        mtl_draw_raw(&batch);
        assert(copies.copies == 1);
        for (int i = 0; i < 4; i++) assert(s_tex_cur[i] == s_feedback);
        check_pixel(s_last, 0xFFFF0000);
        check_texture(s_feedback, 0xFFFF0000);
        copies = [CountingBlitEncoder new];
        copies.target = blit(); s_blit = (id<MTLBlitCommandEncoder>)copies;
        regs[0x1E20 / 4] = 0xFF00FF00; memset(dirty, 1, sizeof dirty);
        mtl_draw_raw(&batch); assert(copies.copies == 1);
        check_pixel(s_last, 0xFF00FF00);
        check_texture(s_feedback, 0xFFFF0000); // snapshot precedes this draw's write
        regs[0x1E20 / 4] = 0xFF0000FF; memset(dirty, 1, sizeof dirty);
        mtl_draw_raw(&batch);
        check_pixel(s_last, 0xFF0000FF);
        check_texture(s_feedback, 0xFF00FF00); // following draw sees the new contents
        if (getenv("RECOMP_FPS_LOG")) {
            pthread_mutex_lock(&s_gpu_stats_lock);
            assert(s_gpu_samples > 0 && s_gpu_ms > 0 && s_gpu_max_ms > 0);
            printf("GPU completion timing: %llu command buffers, %.3f ms total\n",
                   (unsigned long long)s_gpu_samples, s_gpu_ms);
            pthread_mutex_unlock(&s_gpu_stats_lock);
        }
        puts("Four feedback stages: one full-surface copy per draw, GPU pixels preserved");
        // ADD oPos, v0, c[0]: changing the reused constants pointer moves
        // the triangle from offscreen to visible in the same command buffer.
        static const uint32_t add_position[1][4] = {
            {0, (3u << 21) | 0x1B, (2u << 26) | (0x1Bu << 2),
             (3u << 28) | (15u << 12) | (1u << 11) | 1u}
        };
        for (int i = 0; i < 4; i++) regs[(0x1B00 + i * 0x40) / 4 + 3] = 0;
        regs[0x1E20 / 4] = 0xFFFF0000; memset(dirty, 1, sizeof dirty);
        batch.xform = 2; batch.vp_program = add_position;
        batch.vp_slots = 1; batch.vp_prog_gen = 1;
        memset(constants, 0, sizeof constants);
        mtl_clear(&batch.surface, NULL, 0xF0, 0xFF0000FF, 0);
        constants[0][0] = 128; batch.vp_const_gen = 3;
        mtl_draw_raw(&batch);
        constants[0][0] = 0; batch.vp_const_gen = 4;
        mtl_draw_raw(&batch);
        check_pixel(s_last, 0xFFFF0000);
        // Instancing requires consecutive sets, even when an earlier
        // non-instanced draw uploaded the same generation.
        mtl_clear(&batch.surface, NULL, 0xF0, 0xFF0000FF, 0);
        constants[0][0] = 128; batch.vp_const_gen = 5;
        mtl_draw_raw(&batch);
        size_t noninstanced_constants = s_consts_upload_at;
        batch.attr_direct = 1; batch.vtx_epoch = batch.attr_const_gen = 1;
        batch.direct[0].ptr = (const uint8_t *)attrs;
        batch.direct[0].stride = sizeof attrs[0];
        batch.direct[0].type = 2; batch.direct[0].size = 4;
        s_inst_on = 1;
        mtl_draw_raw(&batch); assert(s_pend.on && s_pend.count == 1);
        size_t instance_constants = s_pend.cat;
        assert(instance_constants != noninstanced_constants);
        constants[0][0] = 0; batch.vp_const_gen = 6;
        mtl_draw_raw(&batch); assert(s_pend.count == 2);
        assert(*(float *)(s_f->ptr + instance_constants) == 128);
        assert(!memcmp(s_f->ptr + instance_constants + MTL_CONST_SIZE, constants, sizeof constants));
        pend_flush(); check_pixel(s_last, 0xFFFF0000);
        puts("GPU transform constants: changed generations and distinct instanced transforms: passed");
        puts("Metal cache/clear/pass transitions and GPU pixels: passed");
    }
}
