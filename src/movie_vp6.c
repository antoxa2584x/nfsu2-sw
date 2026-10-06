/**
 * Native VP6 movie decoding (FFmpeg's vp6 decoder)
 *
 * NFSU2's movies are EA "MVhd" streams in the ZZDATA*.BIN archives: VP6
 * video, 640x480, ~30 fps, "MV0K" (key) and "MV0F" chunks. The title links
 * On2's VP6 decoder (0x0024xxxx-0x0027xxxx); the player hands each chunk's
 * payload to sub_002618F0, which decodes it into the decoder's own YUV 4:2:0
 * buffers. Lifted, that decoder (MMX IDCT, motion compensation, loop filter)
 * is too slow on the Switch, so the payload goes to FFmpeg instead and the
 * picture is written where the title expects it (recomp_manual.c,
 * sub_002618F0). The output is bit-exact with the title's decoder (checked
 * with NFSU2_NATIVE_VP6=2).
 *
 * The FFmpeg used is a minimal LGPL 2.1 build: only the vp6 decoder, no
 * GPL parts, no threads (tools/build_ffmpeg_vp6.sh).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>

#include "movie_vp6.h"

/* One FFmpeg decoder per title decoder handle; a movie that reuses a handle
 * starts with a key frame, which resets VP6's reference frames. */
#define VP6_SLOTS 4

typedef struct {
    uint32_t handle;
    unsigned long used;
    AVCodecContext *ctx;
} Vp6Slot;

static Vp6Slot s_slots[VP6_SLOTS];
static AVPacket *s_pkt;
static AVFrame *s_frame;
static uint8_t *s_buf;
static int s_buf_size;
static unsigned long s_tick;

/* Stats, logged every 300 frames (~10 s of movie). */
static unsigned long s_frames, s_errors;
static double s_ms_sum, s_ms_worst;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int nfsu2_vp6_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *e = getenv("NFSU2_NATIVE_VP6");
        mode = e && *e ? atoi(e) : 1;
        if (mode < 0 || mode > 2)
            mode = 1;
        fprintf(stderr, "[movie] VP6 decoder: %s\n",
                mode == 0 ? "lifted (NFSU2_NATIVE_VP6=0)"
                : mode == 2 ? "FFmpeg, checked against the lifted one" : "FFmpeg");
    }
    return mode;
}

static AVCodecContext *slot_ctx(uint32_t handle)
{
    const AVCodec *codec;
    Vp6Slot *s = NULL;
    int i;

    for (i = 0; i < VP6_SLOTS; i++)
        if (s_slots[i].ctx && s_slots[i].handle == handle) {
            s = &s_slots[i];
            break;
        }
    if (!s) {
        s = &s_slots[0];
        for (i = 1; i < VP6_SLOTS; i++)
            if (s_slots[i].used < s->used)
                s = &s_slots[i];
        avcodec_free_context(&s->ctx);
        codec = avcodec_find_decoder(AV_CODEC_ID_VP6);
        if (!codec || !(s->ctx = avcodec_alloc_context3(codec)))
            return NULL;
        s->ctx->thread_count = 1;
        if (avcodec_open2(s->ctx, codec, NULL) < 0) {
            avcodec_free_context(&s->ctx);
            return NULL;
        }
        s->handle = handle;
    }
    s->used = ++s_tick;
    return s->ctx;
}

/* A flat near-white frame: every sample of the middle half (rows and
 * columns, so letterbox bars do not count) has luma >= 200. FMVOpening
 * cuts between its clips with 1-3 of them, a white flash on screen. */
static int flat_white(const AVFrame *f)
{
    int x, y, w = f->width, h = f->height;

    for (y = h / 4; y < h * 3 / 4; y += 8)
        for (x = w / 4; x < w * 3 / 4; x += 8)
            if (f->data[0][(ptrdiff_t)y * f->linesize[0] + x] < 200)
                return 0;
    return w > 0 && h > 0;
}

/* NFSU2_MOVIE_FLASH=1 keeps the white flash frames; by default they are
 * shown as the movie's own black (luma 16). */
static int flash_to_black(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("NFSU2_MOVIE_FLASH");
        /* Not in the compare mode: the lifted decoder keeps them. */
        on = !(e && *e == '1') && nfsu2_vp6_mode() != 2;
    }
    return on;
}

int nfsu2_vp6_decode(uint32_t handle, const uint8_t *data, int size,
                     uint8_t *y, uint8_t *u, uint8_t *v,
                     int y_stride, int uv_stride, int width, int height)
{
    AVCodecContext *ctx;
    double t0 = now_ms(), ms;
    int r, row, w, h;

    if (!s_pkt) {
        av_log_set_level(AV_LOG_ERROR);
        s_pkt = av_packet_alloc();
        s_frame = av_frame_alloc();
        if (!s_pkt || !s_frame)
            return -1;
    }
    if (size <= 0 || !(ctx = slot_ctx(handle)))
        return -1;
    /* FFmpeg reads up to AV_INPUT_BUFFER_PADDING_SIZE past the end. */
    if (size + AV_INPUT_BUFFER_PADDING_SIZE > s_buf_size) {
        free(s_buf);
        s_buf_size = size + AV_INPUT_BUFFER_PADDING_SIZE + 65536;
        if (!(s_buf = malloc(s_buf_size))) {
            s_buf_size = 0;
            return -1;
        }
    }
    memcpy(s_buf, data, size);
    memset(s_buf + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    s_pkt->data = s_buf;
    s_pkt->size = size;

    r = avcodec_send_packet(ctx, s_pkt);
    if (r >= 0)
        r = avcodec_receive_frame(ctx, s_frame);
    if (r < 0) {
        if (s_errors++ < 20)
            fprintf(stderr, "[movie] VP6 frame of %d bytes not decoded (%d)\n", size, r);
        return -1;
    }

    /* The title's buffers are bottom-up (VP6 codes the picture flipped;
     * FFmpeg returns it upright). */
    w = s_frame->width < width ? s_frame->width : width;
    h = s_frame->height < height ? s_frame->height : height;
    if (flash_to_black() && flat_white(s_frame)) {
        /* Into the title's buffers only: FFmpeg's own frame stays the
         * reference for the next ones. */
        for (row = 0; row < h; row++)
            memset(y + (size_t)row * y_stride, 16, w);
        for (row = 0; row < h / 2; row++) {
            memset(u + (size_t)row * uv_stride, 128, w / 2);
            memset(v + (size_t)row * uv_stride, 128, w / 2);
        }
        av_frame_unref(s_frame);
        return 0;
    }
    for (row = 0; row < h; row++)
        memcpy(y + (size_t)(height - 1 - row) * y_stride,
               s_frame->data[0] + (ptrdiff_t)row * s_frame->linesize[0], w);
    for (row = 0; row < h / 2; row++) {
        size_t dst = (size_t)(height / 2 - 1 - row) * uv_stride;
        memcpy(u + dst, s_frame->data[1] + (ptrdiff_t)row * s_frame->linesize[1], w / 2);
        memcpy(v + dst, s_frame->data[2] + (ptrdiff_t)row * s_frame->linesize[2], w / 2);
    }
    av_frame_unref(s_frame);

    ms = now_ms() - t0;
    s_ms_sum += ms;
    if (ms > s_ms_worst)
        s_ms_worst = ms;
    if (++s_frames % 300 == 0) {
        fprintf(stderr, "[movie] VP6: %lu frames, %.2f ms average, %.2f ms worst, %lu errors\n",
                s_frames, s_ms_sum / 300, s_ms_worst, s_errors);
        s_ms_sum = s_ms_worst = 0;
    }
    return 0;
}
