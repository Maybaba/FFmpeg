/*
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"

#include "libswscale/rgb2rgb.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        for (j = 0; j < size; j+=4)       \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static const uint8_t width[] = {12, 16, 20, 32, 36, 128};
static const struct {uint8_t w, h, s;} planes[] = {
    {12,16,12}, {16,16,16}, {20,23,25}, {32,18,48}, {8,128,16}, {128,128,128}
};

#define MAX_STRIDE 128
#define MAX_HEIGHT 128

static void check_shuffle_bytes(void * func, const char * report)
{
    int i;
    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [MAX_STRIDE]);

    declare_func(void, const uint8_t *src, uint8_t *dst, int src_size);

    memset(dst0, 0, MAX_STRIDE);
    memset(dst1, 0, MAX_STRIDE);
    randomize_buffers(src0, MAX_STRIDE);
    memcpy(src1, src0, MAX_STRIDE);

    if (check_func(func, "%s", report)) {
        for (i = 0; i < 6; i ++) {
            call_ref(src0, dst0, width[i]);
            call_new(src1, dst1, width[i]);
            if (memcmp(dst0, dst1, MAX_STRIDE))
                fail();
        }
        bench_new(src0, dst0, width[5]);
    }
}

static void check_uyvy_to_422p(void)
{
    int i;

    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE * MAX_HEIGHT * 2]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE * MAX_HEIGHT * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst_y_0, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_y_1, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_u_0, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_u_1, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_v_0, [(MAX_STRIDE/2) * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst_v_1, [(MAX_STRIDE/2) * MAX_HEIGHT]);

    declare_func(void, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                 const uint8_t *src, int width, int height,
                 int lumStride, int chromStride, int srcStride);

    randomize_buffers(src0, MAX_STRIDE * MAX_HEIGHT * 2);
    memcpy(src1, src0, MAX_STRIDE * MAX_HEIGHT * 2);

    if (check_func(uyvytoyuv422, "uyvytoyuv422")) {
        for (i = 0; i < 6; i ++) {
            memset(dst_y_0, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst_y_1, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst_u_0, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_u_1, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_v_0, 0, (MAX_STRIDE/2) * MAX_HEIGHT);
            memset(dst_v_1, 0, (MAX_STRIDE/2) * MAX_HEIGHT);

            call_ref(dst_y_0, dst_u_0, dst_v_0, src0, planes[i].w, planes[i].h,
                     MAX_STRIDE, MAX_STRIDE / 2, planes[i].s);
            call_new(dst_y_1, dst_u_1, dst_v_1, src1, planes[i].w, planes[i].h,
                     MAX_STRIDE, MAX_STRIDE / 2, planes[i].s);
            if (memcmp(dst_y_0, dst_y_1, MAX_STRIDE * MAX_HEIGHT) ||
                memcmp(dst_u_0, dst_u_1, (MAX_STRIDE/2) * MAX_HEIGHT) ||
                memcmp(dst_v_0, dst_v_1, (MAX_STRIDE/2) * MAX_HEIGHT))
                fail();
        }
        bench_new(dst_y_1, dst_u_1, dst_v_1, src1, planes[5].w, planes[5].h,
                  MAX_STRIDE, MAX_STRIDE / 2, planes[5].s);
    }
}

#define NUM_LINES 5
#define MAX_LINE_SIZE 1920
#define BUFSIZE (NUM_LINES * MAX_LINE_SIZE)

static int cmp_off_by_n(const uint8_t *ref, const uint8_t *test, size_t n, int accuracy)
{
    for (size_t i = 0; i < n; i++) {
        if (abs(ref[i] - test[i]) > accuracy)
            return 1;
    }
    return 0;
}

static void check_rgb24toyv12(SwsContext *sws)
{
    static const int input_sizes[] = {16, 128, 512, MAX_LINE_SIZE, -MAX_LINE_SIZE};
    SwsInternal *ctx = sws_internal(sws);

    LOCAL_ALIGNED_32(uint8_t, src, [BUFSIZE * 3]);
    LOCAL_ALIGNED_32(uint8_t, buf_y_0, [BUFSIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf_y_1, [BUFSIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf_u_0, [BUFSIZE / 4]);
    LOCAL_ALIGNED_32(uint8_t, buf_u_1, [BUFSIZE / 4]);
    LOCAL_ALIGNED_32(uint8_t, buf_v_0, [BUFSIZE / 4]);
    LOCAL_ALIGNED_32(uint8_t, buf_v_1, [BUFSIZE / 4]);

    declare_func(void, const uint8_t *src, uint8_t *ydst, uint8_t *udst,
                       uint8_t *vdst, int width, int height, int lumStride,
                       int chromStride, int srcStride, const int32_t *rgb2yuv);

    randomize_buffers(src, BUFSIZE * 3);

    for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
        int input_size = input_sizes[isi];
        int negstride = input_size < 0;
        const char *negstride_str = negstride ? "_negstride" : "";
        int width = FFABS(input_size);
        int linesize = width + 32;
        /* calculate height based on specified width to use the entire buffer. */
        int height = (BUFSIZE / linesize) & ~1;
        uint8_t *src0 = src;
        uint8_t *src1 = src;
        uint8_t *dst_y_0 = buf_y_0;
        uint8_t *dst_y_1 = buf_y_1;
        uint8_t *dst_u_0 = buf_u_0;
        uint8_t *dst_u_1 = buf_u_1;
        uint8_t *dst_v_0 = buf_v_0;
        uint8_t *dst_v_1 = buf_v_1;

        if (negstride) {
            src0    += (height - 1) * (linesize * 3);
            src1    += (height - 1) * (linesize * 3);
            dst_y_0 += (height - 1) * linesize;
            dst_y_1 += (height - 1) * linesize;
            dst_u_0 += ((height / 2) - 1) * (linesize / 2);
            dst_u_1 += ((height / 2) - 1) * (linesize / 2);
            dst_v_0 += ((height / 2) - 1) * (linesize / 2);
            dst_v_1 += ((height / 2) - 1) * (linesize / 2);
            linesize *= -1;
        }

        if (check_func(ff_rgb24toyv12, "rgb24toyv12_%d_%d%s", width, height, negstride_str)) {
            memset(buf_y_0, 0xFF, BUFSIZE);
            memset(buf_y_1, 0xFF, BUFSIZE);
            memset(buf_u_0, 0xFF, BUFSIZE / 4);
            memset(buf_u_1, 0xFF, BUFSIZE / 4);
            memset(buf_v_0, 0xFF, BUFSIZE / 4);
            memset(buf_v_1, 0xFF, BUFSIZE / 4);

            call_ref(src0, dst_y_0, dst_u_0, dst_v_0, width, height,
                     linesize, linesize / 2, linesize * 3, ctx->input_rgb2yuv_table);
            call_new(src1, dst_y_1, dst_u_1, dst_v_1, width, height,
                     linesize, linesize / 2, linesize * 3, ctx->input_rgb2yuv_table);
            if (cmp_off_by_n(buf_y_0, buf_y_1, BUFSIZE, 1) ||
                cmp_off_by_n(buf_u_0, buf_u_1, BUFSIZE / 4, 1) ||
                cmp_off_by_n(buf_v_0, buf_v_1, BUFSIZE / 4, 1))
                fail();
            bench_new(src1, dst_y_1, dst_u_1, dst_v_1, width, height,
                      linesize, linesize / 2, linesize * 3, ctx->input_rgb2yuv_table);
        }
    }
}

#undef NUM_LINES
#undef MAX_LINE_SIZE
#undef BUFSIZE

static void check_interleave_bytes(void)
{
    LOCAL_ALIGNED_16(uint8_t, src0_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, src1_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst0_buf, [2*MAX_STRIDE*MAX_HEIGHT+2]);
    LOCAL_ALIGNED_16(uint8_t, dst1_buf, [2*MAX_STRIDE*MAX_HEIGHT+2]);
    // Intentionally using unaligned buffers, as this function doesn't have
    // any alignment requirements.
    uint8_t *src0 = src0_buf + 1;
    uint8_t *src1 = src1_buf + 1;
    uint8_t *dst0 = dst0_buf + 2;
    uint8_t *dst1 = dst1_buf + 2;

    declare_func(void, const uint8_t *, const uint8_t *,
                 uint8_t *, int, int, int, int, int);

    randomize_buffers(src0, MAX_STRIDE * MAX_HEIGHT);
    randomize_buffers(src1, MAX_STRIDE * MAX_HEIGHT);

    if (check_func(interleaveBytes, "interleave_bytes")) {
        for (int i = 0; i <= 16; i++) {
            // Try all widths [1,16], and try one random width.

            int w = i > 0 ? i : (1 + (rnd() % (MAX_STRIDE-2)));
            int h = 1 + (rnd() % (MAX_HEIGHT-2));

            int src0_offset = 0, src0_stride = MAX_STRIDE;
            int src1_offset = 0, src1_stride = MAX_STRIDE;
            int dst_offset  = 0, dst_stride  = 2 * MAX_STRIDE;

            memset(dst0, 0, 2 * MAX_STRIDE * MAX_HEIGHT);
            memset(dst1, 0, 2 * MAX_STRIDE * MAX_HEIGHT);

            // Try different combinations of negative strides
            if (i & 1) {
                src0_offset = (h-1)*src0_stride;
                src0_stride = -src0_stride;
            }
            if (i & 2) {
                src1_offset = (h-1)*src1_stride;
                src1_stride = -src1_stride;
            }
            if (i & 4) {
                dst_offset = (h-1)*dst_stride;
                dst_stride = -dst_stride;
            }

            call_ref(src0 + src0_offset, src1 + src1_offset, dst0 + dst_offset,
                     w, h, src0_stride, src1_stride, dst_stride);
            call_new(src0 + src0_offset, src1 + src1_offset, dst1 + dst_offset,
                     w, h, src0_stride, src1_stride, dst_stride);
            // Check a one pixel-pair edge around the destination area,
            // to catch overwrites past the end.
            checkasm_check(uint8_t, dst0, 2*MAX_STRIDE, dst1, 2*MAX_STRIDE,
                           2 * w + 2, h + 1, "dst");
        }

        bench_new(src0, src1, dst1, 127, MAX_HEIGHT,
                  MAX_STRIDE, MAX_STRIDE, 2*MAX_STRIDE);
    }
    if (check_func(interleaveBytes, "interleave_bytes_aligned")) {
        // Bench the function in a more typical case, with aligned
        // buffers and widths.
        bench_new(src0_buf, src1_buf, dst1_buf, 128, MAX_HEIGHT,
                  MAX_STRIDE, MAX_STRIDE, 2*MAX_STRIDE);
    }
}

static void check_deinterleave_bytes(void)
{
    LOCAL_ALIGNED_16(uint8_t, src_buf,  [2*MAX_STRIDE*MAX_HEIGHT+2]);
    LOCAL_ALIGNED_16(uint8_t, dst0_u_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst0_v_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst1_u_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    LOCAL_ALIGNED_16(uint8_t, dst1_v_buf, [MAX_STRIDE*MAX_HEIGHT+1]);
    // Intentionally using unaligned buffers, as this function doesn't have
    // any alignment requirements.
    uint8_t *src = src_buf + 2;
    uint8_t *dst0_u = dst0_u_buf + 1;
    uint8_t *dst0_v = dst0_v_buf + 1;
    uint8_t *dst1_u = dst1_u_buf + 1;
    uint8_t *dst1_v = dst1_v_buf + 1;

    declare_func(void, const uint8_t *src, uint8_t *dst1, uint8_t *dst2,
                       int width, int height, int srcStride,
                       int dst1Stride, int dst2Stride);

    randomize_buffers(src, 2*MAX_STRIDE*MAX_HEIGHT);

    if (check_func(deinterleaveBytes, "deinterleave_bytes")) {
        for (int i = 0; i <= 16; i++) {
            // Try all widths [1,16], and try one random width.

            int w = i > 0 ? i : (1 + (rnd() % (MAX_STRIDE-2)));
            int h = 1 + (rnd() % (MAX_HEIGHT-2));

            int src_offset   = 0, src_stride    = 2 * MAX_STRIDE;
            int dst_u_offset = 0, dst_u_stride  = MAX_STRIDE;
            int dst_v_offset = 0, dst_v_stride  = MAX_STRIDE;

            memset(dst0_u, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst0_v, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst1_u, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst1_v, 0, MAX_STRIDE * MAX_HEIGHT);

            // Try different combinations of negative strides
            if (i & 1) {
                src_offset = (h-1)*src_stride;
                src_stride = -src_stride;
            }
            if (i & 2) {
                dst_u_offset = (h-1)*dst_u_stride;
                dst_u_stride = -dst_u_stride;
            }
            if (i & 4) {
                dst_v_offset = (h-1)*dst_v_stride;
                dst_v_stride = -dst_v_stride;
            }

            call_ref(src + src_offset, dst0_u + dst_u_offset, dst0_v + dst_v_offset,
                     w, h, src_stride, dst_u_stride, dst_v_stride);
            call_new(src + src_offset, dst1_u + dst_u_offset, dst1_v + dst_v_offset,
                     w, h, src_stride, dst_u_stride, dst_v_stride);
            // Check a one pixel-pair edge around the destination area,
            // to catch overwrites past the end.
            checkasm_check(uint8_t, dst0_u, MAX_STRIDE, dst1_u, MAX_STRIDE,
                           w + 1, h + 1, "dst_u");
            checkasm_check(uint8_t, dst0_v, MAX_STRIDE, dst1_v, MAX_STRIDE,
                           w + 1, h + 1, "dst_v");
        }

        bench_new(src, dst1_u, dst1_v, 127, MAX_HEIGHT,
                  2*MAX_STRIDE, MAX_STRIDE, MAX_STRIDE);
    }
    if (check_func(deinterleaveBytes, "deinterleave_bytes_aligned")) {
        // Bench the function in a more typical case, with aligned
        // buffers and widths.
        bench_new(src_buf, dst1_u_buf, dst1_v_buf, 128, MAX_HEIGHT,
                  2*MAX_STRIDE, MAX_STRIDE, MAX_STRIDE);
    }
}

#define MAX_LINE_SIZE 1920
static const int input_sizes[] = {8, 128, 1080, MAX_LINE_SIZE};
static const enum AVPixelFormat rgb_formats[] = {
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ABGR,
        AV_PIX_FMT_ARGB,
};

static void check_rgb_to_y(SwsContext *sws)
{
    SwsInternal *ctx = sws_internal(sws);

    LOCAL_ALIGNED_16(uint8_t, src24,  [MAX_LINE_SIZE * 3]);
    LOCAL_ALIGNED_16(uint8_t, src32,  [MAX_LINE_SIZE * 4]);
    LOCAL_ALIGNED_32(uint8_t, dst0_y, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_y, [MAX_LINE_SIZE * 2]);

    declare_func(void, uint8_t *dst, const uint8_t *src,
                 const uint8_t *unused1, const uint8_t *unused2, int width,
                 uint32_t *rgb2yuv, void *opq);

    randomize_buffers(src24, MAX_LINE_SIZE * 3);
    randomize_buffers(src32, MAX_LINE_SIZE * 4);

    for (int i = 0; i < FF_ARRAY_ELEMS(rgb_formats); i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(rgb_formats[i]);

        sws->src_format = rgb_formats[i];
        ff_sws_init_scale(ctx);

        for (int j = 0; j < FF_ARRAY_ELEMS(input_sizes); j++) {
            int w = input_sizes[j];

            if (check_func(ctx->lumToYV12, "%s_to_y_%d", desc->name, w)) {
                const uint8_t *src = desc->nb_components == 3 ? src24 : src32;
                memset(dst0_y, 0xFA, MAX_LINE_SIZE * 2);
                memset(dst1_y, 0xFA, MAX_LINE_SIZE * 2);

                call_ref(dst0_y, src, NULL, NULL, w, ctx->input_rgb2yuv_table, NULL);
                call_new(dst1_y, src, NULL, NULL, w, ctx->input_rgb2yuv_table, NULL);

                if (memcmp(dst0_y, dst1_y, w * 2))
                    fail();

                if (desc->nb_components == 3 ||
                    // only bench native endian formats
                    (sws->src_format == AV_PIX_FMT_RGB32 || sws->src_format == AV_PIX_FMT_RGB32_1))
                    bench_new(dst1_y, src, NULL, NULL, w, ctx->input_rgb2yuv_table, NULL);
            }
        }
    }
}

static void check_rgb_to_uv(SwsContext *sws)
{
    SwsInternal *ctx = sws_internal(sws);

    LOCAL_ALIGNED_16(uint8_t, src24,  [MAX_LINE_SIZE * 3]);
    LOCAL_ALIGNED_16(uint8_t, src32,  [MAX_LINE_SIZE * 4]);
    LOCAL_ALIGNED_16(uint8_t, dst0_u, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst0_v, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst1_u, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_16(uint8_t, dst1_v, [MAX_LINE_SIZE * 2]);

    declare_func(void, uint8_t *dstU, uint8_t *dstV,
                 const uint8_t *src1, const uint8_t *src2, const uint8_t *src3,
                 int width, uint32_t *pal, void *opq);

    randomize_buffers(src24, MAX_LINE_SIZE * 3);
    randomize_buffers(src32, MAX_LINE_SIZE * 4);

    for (int i = 0; i < 2 * FF_ARRAY_ELEMS(rgb_formats); i++) {
        enum AVPixelFormat src_fmt = rgb_formats[i / 2];
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src_fmt);

        ctx->chrSrcHSubSample = (i % 2) ? 0 : 1;
        sws->src_format = src_fmt;
        sws->dst_format = ctx->chrSrcHSubSample ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV444P;
        ff_sws_init_scale(ctx);

        for (int j = 0; j < FF_ARRAY_ELEMS(input_sizes); j++) {
            int w = input_sizes[j] >> ctx->chrSrcHSubSample;

            if (check_func(ctx->chrToYV12, "%s_to_uv%s_%d", desc->name,
                           ctx->chrSrcHSubSample ? "_half" : "",
                           input_sizes[j])) {
                const uint8_t *src = desc->nb_components == 3 ? src24 : src32;
                memset(dst0_u, 0xFF, MAX_LINE_SIZE * 2);
                memset(dst0_v, 0xFF, MAX_LINE_SIZE * 2);
                memset(dst1_u, 0xFF, MAX_LINE_SIZE * 2);
                memset(dst1_v, 0xFF, MAX_LINE_SIZE * 2);

                call_ref(dst0_u, dst0_v, NULL, src, src, w, ctx->input_rgb2yuv_table, NULL);
                call_new(dst1_u, dst1_v, NULL, src, src, w, ctx->input_rgb2yuv_table, NULL);

                if (memcmp(dst0_u, dst1_u, w * 2) || memcmp(dst0_v, dst1_v, w * 2))
                    fail();

                if (desc->nb_components == 3 ||
                    // only bench native endian formats
                    (sws->src_format == AV_PIX_FMT_RGB32 || sws->src_format == AV_PIX_FMT_RGB32_1))
                    bench_new(dst1_u, dst1_v, NULL, src, src, w, ctx->input_rgb2yuv_table, NULL);
            }
        }
    }
}

static void check_rgba_to_a(SwsContext *sws)
{
    SwsInternal *ctx = sws_internal(sws);

    LOCAL_ALIGNED_16(uint8_t, src,  [MAX_LINE_SIZE * 4]);
    LOCAL_ALIGNED_32(uint8_t, dst0_y, [MAX_LINE_SIZE * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_y, [MAX_LINE_SIZE * 2]);

    declare_func(void, uint8_t *dst, const uint8_t *src1,
                 const uint8_t *src2, const uint8_t *src3, int width,
                 uint32_t *rgb2yuv, void *opq);

    randomize_buffers(src, MAX_LINE_SIZE * 4);

    for (int i = 0; i < FF_ARRAY_ELEMS(rgb_formats); i++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(rgb_formats[i]);
        if (desc->nb_components < 4)
            continue;

        sws->src_format = rgb_formats[i];
        ff_sws_init_scale(ctx);

        for (int j = 0; j < FF_ARRAY_ELEMS(input_sizes); j++) {
            int w = input_sizes[j];

            if (check_func(ctx->alpToYV12, "%s_to_y_%d", desc->name, w)) {
                memset(dst0_y, 0xFA, MAX_LINE_SIZE * 2);
                memset(dst1_y, 0xFA, MAX_LINE_SIZE * 2);

                call_ref(dst0_y, NULL, NULL, src, w, ctx->input_rgb2yuv_table, NULL);
                call_new(dst1_y, NULL, NULL, src, w, ctx->input_rgb2yuv_table, NULL);

                if (memcmp(dst0_y, dst1_y, w * 2))
                    fail();

                // only bench native endian formats
                if (sws->src_format == AV_PIX_FMT_RGB32 || sws->src_format == AV_PIX_FMT_RGB32_1)
                    bench_new(dst1_y, NULL, NULL, src, w, ctx->input_rgb2yuv_table, NULL);
            }
        }
    }
}


static const int packed_rgb_fmts[] = {
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_ARGB,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_ABGR,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_RGB48BE,
    AV_PIX_FMT_RGB48LE,
    AV_PIX_FMT_RGB565BE,
    AV_PIX_FMT_RGB565LE,
    AV_PIX_FMT_RGB555BE,
    AV_PIX_FMT_RGB555LE,
    AV_PIX_FMT_BGR565BE,
    AV_PIX_FMT_BGR565LE,
    AV_PIX_FMT_BGR555BE,
    AV_PIX_FMT_BGR555LE,
    AV_PIX_FMT_RGB444LE,
    AV_PIX_FMT_RGB444BE,
    AV_PIX_FMT_BGR444LE,
    AV_PIX_FMT_BGR444BE,
    AV_PIX_FMT_BGR48BE,
    AV_PIX_FMT_BGR48LE,
    AV_PIX_FMT_RGBA64BE,
    AV_PIX_FMT_RGBA64LE,
    AV_PIX_FMT_BGRA64BE,
    AV_PIX_FMT_BGRA64LE,
    AV_PIX_FMT_RGB8,
    AV_PIX_FMT_BGR8,
    AV_PIX_FMT_RGB4,
    AV_PIX_FMT_BGR4,
    AV_PIX_FMT_RGB4_BYTE,
    AV_PIX_FMT_BGR4_BYTE,
};

#define INPUT_SIZE 512

static void check_yuv2packed1(void)
{
    static const int alpha_values[] = {0, 2048, 4096};

    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT,
                      void, SwsInternal *c, const int16_t *lumSrc,
                      const int16_t *chrUSrc[2], const int16_t *chrVSrc[2],
                      const int16_t *alpSrc, uint8_t *dest,
                      int dstW, int uvalpha, int y);

    const int16_t *luma;
    const int16_t *chru[2];
    const int16_t *chrv[2];
    const int16_t *alpha;

    LOCAL_ALIGNED_8(int32_t, src_y, [2 * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_u, [2 * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_v, [2 * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_a, [2 * INPUT_SIZE]);

    LOCAL_ALIGNED_8(uint8_t, dst0, [INPUT_SIZE * sizeof(int32_t[4])]);
    LOCAL_ALIGNED_8(uint8_t, dst1, [INPUT_SIZE * sizeof(int32_t[4])]);

    randomize_buffers((uint8_t*)src_y, 2 * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_u, 2 * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_v, 2 * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_a, 2 * INPUT_SIZE * sizeof(int32_t));

    /* Limit to 14 bit input range */
    for (int i = 0; i < 2 * INPUT_SIZE; i++) {
        src_y[i] &= 0x3FFF3FFF;
        src_a[i] &= 0x3FFF3FFF;
        src_u[i] &= 0x3FFF3FFF;
        src_v[i] &= 0x3FFF3FFF;
    }

    luma  = (int16_t *)src_y;
    alpha = (int16_t *)src_a;
    for (int i = 0; i < 2; i++) {
        chru[i] =  (int16_t *)(src_u + i*INPUT_SIZE);
        chrv[i] =  (int16_t *)(src_v + i*INPUT_SIZE);
    }

    for (int fmi = 0; fmi < FF_ARRAY_ELEMS(packed_rgb_fmts); fmi++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(packed_rgb_fmts[fmi]);
        int line_size = INPUT_SIZE * desc->comp[0].step;
        SwsContext *sws;
        SwsInternal *c;

        if (desc->flags & AV_PIX_FMT_FLAG_BITSTREAM)
            line_size = AV_CEIL_RSHIFT(line_size, 3);

        sws = sws_getContext(MAX_LINE_SIZE, MAX_LINE_SIZE, AV_PIX_FMT_YUV420P,
                             MAX_LINE_SIZE, MAX_LINE_SIZE, packed_rgb_fmts[fmi],
                             SWS_ACCURATE_RND | SWS_BITEXACT, NULL, NULL, NULL);
        if (!sws)
            fail();

        c = sws_internal(sws);

        for (int ai = 0; ai < FF_ARRAY_ELEMS(alpha_values); ai++) {
            const int chr_alpha = alpha_values[ai];
            if (check_func(c->yuv2packed1, "yuv2%s_1_%d_%d", desc->name, chr_alpha, INPUT_SIZE)) {
                memset(dst0, 0xFF, INPUT_SIZE * sizeof(int32_t[4]));
                memset(dst1, 0xFF, INPUT_SIZE * sizeof(int32_t[4]));

                call_ref(c, luma, chru, chrv, alpha, dst0, INPUT_SIZE, chr_alpha, 0);
                call_new(c, luma, chru, chrv, alpha, dst1, INPUT_SIZE, chr_alpha, 0);

                if (memcmp(dst0, dst1, line_size))
                    fail();

                bench_new(c, luma, chru, chrv, alpha, dst1, INPUT_SIZE, chr_alpha, 0);
            }
        }

        sws_freeContext(sws);
    }
}

static void check_yuv2packed2(void)
{
    static const int alpha_values[] = {0, 2048, 4096};

    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT,
                      void, SwsInternal *c, const int16_t *lumSrc[2],
                      const int16_t *chrUSrc[2], const int16_t *chrVSrc[2],
                      const int16_t *alpSrc[2], uint8_t *dest,
                      int dstW, int yalpha, int uvalpha, int y);

    const int16_t *luma[2];
    const int16_t *chru[2];
    const int16_t *chrv[2];
    const int16_t *alpha[2];

    LOCAL_ALIGNED_8(int32_t, src_y, [2 * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_u, [2 * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_v, [2 * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_a, [2 * INPUT_SIZE]);

    LOCAL_ALIGNED_8(uint8_t, dst0, [INPUT_SIZE * sizeof(int32_t[4])]);
    LOCAL_ALIGNED_8(uint8_t, dst1, [INPUT_SIZE * sizeof(int32_t[4])]);

    randomize_buffers((uint8_t*)src_y, 2 * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_u, 2 * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_v, 2 * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_a, 2 * INPUT_SIZE * sizeof(int32_t));

    /* Limit to 14 bit input range */
    for (int i = 0; i < 2 * INPUT_SIZE; i++) {
        src_y[i] &= 0x3FFF3FFF;
        src_u[i] &= 0x3FFF3FFF;
        src_v[i] &= 0x3FFF3FFF;
        src_a[i] &= 0x3FFF3FFF;
    }

    for (int i = 0; i < 2; i++) {
        luma[i] =  (int16_t *)(src_y + i*INPUT_SIZE);
        chru[i] =  (int16_t *)(src_u + i*INPUT_SIZE);
        chrv[i] =  (int16_t *)(src_v + i*INPUT_SIZE);
        alpha[i] = (int16_t *)(src_a + i*INPUT_SIZE);
    }

    for (int fmi = 0; fmi < FF_ARRAY_ELEMS(packed_rgb_fmts); fmi++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(packed_rgb_fmts[fmi]);
        int line_size = INPUT_SIZE * desc->comp[0].step;
        SwsContext *sws;
        SwsInternal *c;

        if (desc->flags & AV_PIX_FMT_FLAG_BITSTREAM)
            line_size = AV_CEIL_RSHIFT(line_size, 3);

        sws = sws_getContext(MAX_LINE_SIZE, MAX_LINE_SIZE, AV_PIX_FMT_YUV420P,
                             MAX_LINE_SIZE, MAX_LINE_SIZE, packed_rgb_fmts[fmi],
                             SWS_ACCURATE_RND | SWS_BITEXACT, NULL, NULL, NULL);
        if (!sws)
            fail();

        c = sws_internal(sws);

        for (int ai = 0; ai < FF_ARRAY_ELEMS(alpha_values); ai++) {
            const int lum_alpha = alpha_values[ai];
            const int chr_alpha  = alpha_values[ai];
            if (check_func(c->yuv2packed2, "yuv2%s_2_%d_%d", desc->name, lum_alpha, INPUT_SIZE)) {
                memset(dst0, 0xFF, INPUT_SIZE * sizeof(int32_t[4]));
                memset(dst1, 0xFF, INPUT_SIZE * sizeof(int32_t[4]));

                call_ref(c, luma, chru, chrv, alpha, dst0, INPUT_SIZE, lum_alpha, chr_alpha, 0);
                call_new(c, luma, chru, chrv, alpha, dst1, INPUT_SIZE, lum_alpha, chr_alpha, 0);

                if (memcmp(dst0, dst1, line_size))
                    fail();

                bench_new(c, luma, chru, chrv, alpha, dst1, INPUT_SIZE, lum_alpha, chr_alpha, 0);
            }
        }

        sws_freeContext(sws);
    }
}

static void check_yuv2packedX(void)
{
#define LARGEST_FILTER 16
    static const int filter_sizes[] = {2, 16};

    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT,
                      void, SwsInternal *c, const int16_t *lumFilter,
                      const int16_t **lumSrcx, int lumFilterSize,
                      const int16_t *chrFilter, const int16_t **chrUSrcx,
                      const int16_t **chrVSrcx, int chrFilterSize,
                      const int16_t **alpSrcx, uint8_t *dest,
                      int dstW, int y);

    const int16_t *luma[LARGEST_FILTER];
    const int16_t *chru[LARGEST_FILTER];
    const int16_t *chrv[LARGEST_FILTER];
    const int16_t *alpha[LARGEST_FILTER];

    LOCAL_ALIGNED_8(int16_t, luma_filter, [LARGEST_FILTER]);
    LOCAL_ALIGNED_8(int16_t, chr_filter, [LARGEST_FILTER]);

    LOCAL_ALIGNED_8(int32_t, src_y, [LARGEST_FILTER * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_u, [LARGEST_FILTER * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_v, [LARGEST_FILTER * INPUT_SIZE]);
    LOCAL_ALIGNED_8(int32_t, src_a, [LARGEST_FILTER * INPUT_SIZE]);

    LOCAL_ALIGNED_8(uint8_t, dst0, [INPUT_SIZE * sizeof(int32_t[4])]);
    LOCAL_ALIGNED_8(uint8_t, dst1, [INPUT_SIZE * sizeof(int32_t[4])]);

    randomize_buffers((uint8_t*)src_y, LARGEST_FILTER * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_u, LARGEST_FILTER * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_v, LARGEST_FILTER * INPUT_SIZE * sizeof(int32_t));
    randomize_buffers((uint8_t*)src_a, LARGEST_FILTER * INPUT_SIZE * sizeof(int32_t));

    /* Limit to 14 bit input range */
    for (int i = 0; i < LARGEST_FILTER * INPUT_SIZE; i++) {
        src_y[i] &= 0x3FFF3FFF;
        src_u[i] &= 0x3FFF3FFF;
        src_v[i] &= 0x3FFF3FFF;
        src_a[i] &= 0x3FFF3FFF;
    }

    for (int i = 0; i < LARGEST_FILTER; i++) {
        luma[i] =  (int16_t *)(src_y + i*INPUT_SIZE);
        chru[i] =  (int16_t *)(src_u + i*INPUT_SIZE);
        chrv[i] =  (int16_t *)(src_v + i*INPUT_SIZE);
        alpha[i] = (int16_t *)(src_a + i*INPUT_SIZE);
    }

    for (int fmi = 0; fmi < FF_ARRAY_ELEMS(packed_rgb_fmts); fmi++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(packed_rgb_fmts[fmi]);
        int line_size = INPUT_SIZE * desc->comp[0].step;
        SwsContext *sws;
        SwsInternal *c;

        if (desc->flags & AV_PIX_FMT_FLAG_BITSTREAM)
            line_size = AV_CEIL_RSHIFT(line_size, 3);

        sws = sws_getContext(MAX_LINE_SIZE, MAX_LINE_SIZE, AV_PIX_FMT_YUV420P,
                                MAX_LINE_SIZE, MAX_LINE_SIZE, packed_rgb_fmts[fmi],
                                SWS_ACCURATE_RND | SWS_BITEXACT, NULL, NULL, NULL);
        if (!sws)
            fail();

        c = sws_internal(sws);

        for (int fsi = 0; fsi < FF_ARRAY_ELEMS(filter_sizes); fsi++) {
            const int luma_filter_size = filter_sizes[fsi];
            const int chr_filter_size = filter_sizes[fsi];

            for (int i = 0; i < luma_filter_size; i++)
                luma_filter[i] = -((1 << 12) / (luma_filter_size - 1));
            luma_filter[rnd() % luma_filter_size] = (1 << 13) - 1;

            for (int i = 0; i < chr_filter_size; i++)
                chr_filter[i] = -((1 << 12) / (chr_filter_size - 1));
            chr_filter[rnd() % chr_filter_size] = (1 << 13) - 1;

            if (check_func(c->yuv2packedX, "yuv2%s_X_%d_%d", desc->name, luma_filter_size, INPUT_SIZE)) {
                memset(dst0, 0xFF, INPUT_SIZE * sizeof(int32_t[4]));
                memset(dst1, 0xFF, INPUT_SIZE * sizeof(int32_t[4]));

                call_ref(c, luma_filter, luma, luma_filter_size,
                            chr_filter, chru, chrv, chr_filter_size,
                            alpha, dst0, INPUT_SIZE, 0);

                call_new(c, luma_filter, luma, luma_filter_size,
                            chr_filter, chru, chrv, chr_filter_size,
                            alpha, dst1, INPUT_SIZE, 0);

                if (memcmp(dst0, dst1, line_size))
                    fail();

                bench_new(c, luma_filter, luma, luma_filter_size,
                            chr_filter, chru, chrv, chr_filter_size,
                            alpha, dst1, INPUT_SIZE, 0);
            }
        }

        sws_freeContext(sws);
    }
}

#undef INPUT_SIZE
#undef LARGEST_FILTER

void checkasm_check_sw_rgb(void)
{
    SwsContext *sws;

    ff_sws_rgb2rgb_init();

    check_shuffle_bytes(shuffle_bytes_2103, "shuffle_bytes_2103");
    report("shuffle_bytes_2103");

    check_shuffle_bytes(shuffle_bytes_0321, "shuffle_bytes_0321");
    report("shuffle_bytes_0321");

    check_shuffle_bytes(shuffle_bytes_1230, "shuffle_bytes_1230");
    report("shuffle_bytes_1230");

    check_shuffle_bytes(shuffle_bytes_3012, "shuffle_bytes_3012");
    report("shuffle_bytes_3012");

    check_shuffle_bytes(shuffle_bytes_3210, "shuffle_bytes_3210");
    report("shuffle_bytes_3210");

    check_shuffle_bytes(shuffle_bytes_3102, "shuffle_bytes_3102");
    report("shuffle_bytes_3102");

    check_shuffle_bytes(shuffle_bytes_2013, "shuffle_bytes_2013");
    report("shuffle_bytes_2013");

    check_shuffle_bytes(shuffle_bytes_1203, "shuffle_bytes_1203");
    report("shuffle_bytes_1203");

    check_shuffle_bytes(shuffle_bytes_2130, "shuffle_bytes_2130");
    report("shuffle_bytes_2130");

    check_uyvy_to_422p();
    report("uyvytoyuv422");

    check_interleave_bytes();
    report("interleave_bytes");

    check_deinterleave_bytes();
    report("deinterleave_bytes");

    sws = sws_getContext(MAX_LINE_SIZE, MAX_LINE_SIZE, AV_PIX_FMT_RGB24,
                         MAX_LINE_SIZE, MAX_LINE_SIZE, AV_PIX_FMT_YUV420P,
                         SWS_ACCURATE_RND | SWS_BITEXACT, NULL, NULL, NULL);
    if (!sws)
        fail();

    check_rgb_to_y(sws);
    report("rgb_to_y");

    check_rgb_to_uv(sws);
    report("rgb_to_uv");

    check_rgba_to_a(sws);
    report("rgba_to_a");

    check_rgb24toyv12(sws);
    report("rgb24toyv12");

    sws_freeContext(sws);

    check_yuv2packed1();
    report("yuv2packed1");

    check_yuv2packed2();
    report("yuv2packed2");

    check_yuv2packedX();
    report("yuv2packedX");
}
