/*
 * Benchmark the functions selected by a real libswscale context.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#if ARCH_AARCH64
#include <arm_neon.h>
#include "libavutil/aarch64/cpu.h"
#elif !defined(BENCH_HOST_CHECK)
#error "Run this benchmark on AArch64; BENCH_HOST_CHECK is only for host validation."
#endif

#ifndef BENCH_OPTIMIZED
#define BENCH_OPTIMIZED 0
#endif

#define PAD_BYTES 64
#define BATCHES 9
#define BATCH_NS 20000000
#define CANARY 0xA5
#define FRAME_BUFFER_ALIGNMENT 64
#define YUV420P_PLANE_COUNT 3
#define RGB24_BYTES_PER_PIXEL 3
#define NEON_VECTOR_BYTES 16
#define NS_PER_SECOND INT64_C(1000000000)
#define NS_PER_MICROSECOND INT64_C(1000)
#define FNV1A_64_OFFSET_BASIS UINT64_C(14695981039346656037)
#define FNV1A_64_PRIME UINT64_C(1099511628211)
#define ROW_ALIGNMENT 64
#define FILTER_SAMPLE_ALIGNMENT (ROW_ALIGNMENT / sizeof(int16_t))
#define YUV420_CHROMA_DIVISOR 2
#define GENERIC_RGB_TAIL_ROWS 2
#define GENERIC_HFILTER_TAPS 4
#define COLOR_SCALE_ONE (1 << 16)
#define GRADIENT_X_STEP 37
#define GRADIENT_Y_STEP 13
#define GRADIENT_PLANE_STEP 53
#define RANDOM_SEED UINT32_C(0x9E3779B9)
#define RANDOM_LEFT_FIRST 13
#define RANDOM_RIGHT 17
#define RANDOM_LEFT_SECOND 5
#define CALIBRATION_GROWTH_FACTOR 2
#define MAX_CALIBRATION_PASSES (1 << 26)
#define REFERENCE_MAGIC UINT32_C(0x53575342)
#define REFERENCE_VERSION 1
#define REFERENCE_BUFFER_BYTES 4096
#define MEDIAN_INDEX (BATCHES / 2)

enum {
    PLANE_Y,
    PLANE_U,
    PLANE_V
};

enum {
    KIND_RGB = YUV420P_PLANE_COUNT,
    KIND_FRAME,
    KIND_COUNT
};

enum {
    PATTERN_BLACK,
    PATTERN_WHITE,
    PATTERN_GRADIENT,
    PATTERN_RANDOM,
    PATTERN_COUNT
};

enum {
    MODE_ARGUMENT = 1,
    FILE_ARGUMENT,
    BENCH_ARGUMENT_COUNT
};

typedef void (*HScaleFn)(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
                         const int16_t *filter, const int32_t *filterPos, int filterSize);

/* The AArch64 kernels are internal libswscale symbols without a public declaration. */
#if ARCH_AARCH64
void ff_hscale8to15_4_neon(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
    const int16_t *filter, const int32_t *filterPos, int filterSize);
void ff_hscale8to15_X8_neon(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
    const int16_t *filter, const int32_t *filterPos, int filterSize);
#if BENCH_OPTIMIZED
void ff_hscale8to15_3_2_neon(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
    const int16_t *filter, const int32_t *filterPos, int filterSize);
void ff_hscale8to15_5_2_neon(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
    const int16_t *filter, const int32_t *filterPos, int filterSize);
void ff_hscale8to15_3_1_neon(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
    const int16_t *filter, const int32_t *filterPos, int filterSize);
void ff_yuv2rgb24_X_neon(SwsInternal *c, const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
    const int16_t *chrFilter, const int16_t **chrUSrc, const int16_t **chrVSrc,
    int chrFilterSize, const int16_t **alpSrc, uint8_t *dest, int dstW, int y);
#endif
#endif

typedef struct Scenario {
    const char *name;
    int srcW;
    int srcH;
    int dstW;
    int dstH;
    int ratio;
    int hTaps;
    int vYTaps;
    int vUvTaps;
} Scenario;

static const Scenario SCENARIOS[] = {
    { "1920x1080_to_1280x704", 1920, 1080, 1280, 704, 0, 4, 4, 2 },
    { "1920x1280_to_768x512", 1920, 1280, 768, 512, 1, 8, 6, 4 },
    { "3840x2160_to_1280x704", 3840, 2160, 1280, 704, 2, 8, 8, 4 },
};

static const char *const TARGETS[] = {
    "ff_hscale8to15_3_2_neon",
    "ff_hscale8to15_5_2_neon",
    "ff_hscale8to15_3_1_neon",
};

typedef struct HRows {
    int count;
    int capacity;
    int srcW;
    int dstW;
    int srcStride;
    int dstStride;
    uint8_t *src;
    uint8_t *expected;
    uint8_t *output;
} HRows;

typedef struct RGBRows {
    int count;
    int capacity;
    int lumSize;
    int chrSize;
    int lumStride;
    int chrStride;
    int dstStride;
    int *y;
    int16_t *lumFilter;
    int16_t *chrFilter;
    int16_t *lum;
    int16_t *u;
    int16_t *v;
    const int16_t **lumPtr;
    const int16_t **uPtr;
    const int16_t **vPtr;
    uint8_t *expected;
    uint8_t *output;
} RGBRows;

typedef struct RGBInput {
    const int16_t *lumFilter;
    const int16_t **lumSrc;
    int lumSize;
    const int16_t *chrFilter;
    const int16_t **uSrc;
    const int16_t **vSrc;
    int chrSize;
    int dstW;
    int y;
} RGBInput;

typedef struct SourcePattern {
    int kind;
    int plane;
    uint32_t random;
} SourcePattern;

typedef struct Bench {
    const Scenario *scenario;
    SwsContext *sws;
    SwsInternal *c;
    AVFrame *src;
    AVFrame *dst;
    HScaleFn hy;
    HScaleFn hc;
    yuv2packedX_fn rgb;
    const char *hName;
    const char *rgbName;
    int capture;
    int error;
    int calls[KIND_RGB + 1];
    HRows h[YUV420P_PLANE_COUNT];
    RGBRows r;
} Bench;

typedef struct Stats {
    double median;
    double p95;
    double minimum;
    double maximum;
} Stats;

/* The contexts are explicitly single threaded. Hooks never enter a timed region. */
static Bench *g_active;
static FILE *g_reference;
static int g_referenceRead;
static const char *const VERSION_NAME = BENCH_OPTIMIZED ? "opt" : "base";

/* Return a status while retaining compile-time checking of printf arguments. */
static int av_printf_format(2, 3) Print(FILE *stream, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = vfprintf(stream, format, arguments);
    va_end(arguments);
    return written < 0 ? AVERROR(EIO) : 0;
}

static int Fail(const char *message)
{
    const int result = Print(stderr, "ERROR: %s\n", message);
    return result < 0 ? result : AVERROR_INVALIDDATA;
}

static int FillBytes(uint8_t *dst, size_t capacity, uint8_t value, size_t count)
{
    if (count > capacity || (!dst && count)) {
        return AVERROR(EINVAL);
    }
    size_t offset = 0;
#if ARCH_AARCH64
    const uint8x16_t values = vdupq_n_u8(value);
    for (; count - offset >= NEON_VECTOR_BYTES; offset += NEON_VECTOR_BYTES) {
        vst1q_u8(dst + offset, values);
    }
#endif
    for (; offset < count; offset++) {
        dst[offset] = value;
    }
    return 0;
}

/* Source and destination must describe separate, live byte ranges. */
static int CopyBytes(uint8_t *dst, size_t capacity, const uint8_t *src, size_t count)
{
    if (count > capacity || ((!dst || !src) && count)) {
        return AVERROR(EINVAL);
    }
    if (!count) {
        return 0;
    }
    const uintptr_t destination = (uintptr_t)dst;
    const uintptr_t source = (uintptr_t)src;
    const uintptr_t distance = destination > source ? destination - source : source - destination;
    if (distance < count) {
        return AVERROR(EINVAL);
    }
    size_t offset = 0;
#if ARCH_AARCH64
    for (; count - offset >= NEON_VECTOR_BYTES; offset += NEON_VECTOR_BYTES) {
        const uint8x16_t values = vld1q_u8(src + offset);
        vst1q_u8(dst + offset, values);
    }
#endif
    for (; offset < count; offset++) {
        dst[offset] = src[offset];
    }
    return 0;
}

static int ReferenceBytes(const char *what, const uint8_t *data, size_t size)
{
    if (!g_reference) {
        return 0;
    }
    if (!g_referenceRead) {
        if (fwrite(data, 1, size, g_reference) != size) {
            return Fail("cannot write reference data");
        }
        return 0;
    }
    uint8_t buffer[REFERENCE_BUFFER_BYTES];
    for (size_t offset = 0; offset < size;) {
        const size_t part = FFMIN(sizeof(buffer), size - offset);
        if (fread(buffer, 1, part, g_reference) != part) {
            return Fail("reference is truncated or call counts differ");
        }
        if (memcmp(buffer, data + offset, part)) {
            const char *scenario = g_active ? g_active->scenario->name : "header";
            const int result = Print(stderr, "ERROR: %s differs: scenario=%s, item=%s, byte>=%zu\n",
                                     VERSION_NAME, scenario, what, offset);
            return result < 0 ? result : AVERROR_INVALIDDATA;
        }
        offset += part;
    }
    return 0;
}

static uint64_t Checksum(const uint8_t *bytes, size_t size)
{
    uint64_t hash = FNV1A_64_OFFSET_BASIS;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * FNV1A_64_PRIME;
    }
    return hash;
}

static int64_t TimeNs(void)
{
#if defined(CLOCK_MONOTONIC_RAW) || defined(CLOCK_MONOTONIC)
    /*
     * Prefer the raw monotonic clock when the platform exposes it without a
     * reserved GNU feature-test macro; otherwise use the POSIX monotonic
     * clock. NTP slew is negligible over the >=20 ms measurement batches.
     */
    struct timespec now;
#ifdef CLOCK_MONOTONIC_RAW
    const clockid_t clockId = CLOCK_MONOTONIC_RAW;
#else
    const clockid_t clockId = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(clockId, &now)) {
        return Fail("clock_gettime failed");
    }
    return (int64_t)now.tv_sec * NS_PER_SECOND + now.tv_nsec;
#else
    const int64_t microseconds = av_gettime_relative();
    if (microseconds < 0) {
        return Fail("av_gettime_relative failed");
    }
    return microseconds * NS_PER_MICROSECOND;
#endif
}

static AVFrame *MakeFrame(int width, int height, enum AVPixelFormat format)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        return NULL;
    }
    frame->width = width;
    frame->height = height;
    frame->format = format;
    if (av_frame_get_buffer(frame, FRAME_BUFFER_ALIGNMENT) < 0) {
        av_frame_free(&frame);
        return NULL;
    }
    return frame;
}

static uint8_t PatternValue(SourcePattern *pattern, int x, int y)
{
    if (pattern->kind == PATTERN_BLACK) {
        return 0;
    }
    if (pattern->kind == PATTERN_WHITE) {
        return UINT8_MAX;
    }
    if (pattern->kind == PATTERN_GRADIENT) {
        return (x * GRADIENT_X_STEP + y * GRADIENT_Y_STEP + pattern->plane * GRADIENT_PLANE_STEP) & UINT8_MAX;
    }
    pattern->random ^= pattern->random << RANDOM_LEFT_FIRST;
    pattern->random ^= pattern->random >> RANDOM_RIGHT;
    pattern->random ^= pattern->random << RANDOM_LEFT_SECOND;
    return pattern->random & UINT8_MAX;
}

static int FillPlane(AVFrame *frame, SourcePattern *pattern)
{
    const int plane = pattern->plane;
    const int width = plane ? frame->width / YUV420_CHROMA_DIVISOR : frame->width;
    const int height = plane ? frame->height / YUV420_CHROMA_DIVISOR : frame->height;
    AVBufferRef *buffer = av_frame_get_plane_buffer(frame, plane);
    if (!buffer || !frame->data[plane] || frame->linesize[plane] < width) {
        return Fail("invalid input plane storage");
    }
    const uintptr_t address = (uintptr_t)frame->data[plane];
    const uintptr_t base = (uintptr_t)buffer->data;
    if (address < base || address - base > buffer->size) {
        return Fail("input plane is outside its buffer");
    }
    const size_t capacity = buffer->size - (address - base);
    const size_t stride = frame->linesize[plane];
    if ((size_t)height > capacity / stride) {
        return Fail("input plane exceeds its buffer");
    }
    for (int y = 0; y < height; y++) {
        const size_t offset = (size_t)y * stride;
        uint8_t *row = frame->data[plane] + offset;
        if (FillBytes(row, capacity - offset, 0, stride) < 0) {
            return Fail("cannot initialize input row");
        }
        for (int x = 0; x < width; x++) {
            row[x] = PatternValue(pattern, x, y);
        }
    }
    return 0;
}

static int FillFrame(AVFrame *frame, int kind)
{
    if (frame->format != AV_PIX_FMT_YUV420P || frame->width <= 0 || frame->height <= 0 ||
        (frame->width & 1) || (frame->height & 1) || kind < PATTERN_BLACK || kind >= PATTERN_COUNT) {
        return Fail("input must be an even-sized YUV420P frame with a supported pattern");
    }
    SourcePattern pattern = { .kind = kind, .random = RANDOM_SEED };
    for (int plane = PLANE_Y; plane < YUV420P_PLANE_COUNT; plane++) {
        pattern.plane = plane;
        if (FillPlane(frame, &pattern) < 0) {
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

static int InitHRows(HRows *rows, int srcW, int dstW, int capacity)
{
    rows->srcW = srcW;
    rows->dstW = dstW;
    rows->capacity = capacity;
    rows->srcStride = FFALIGN(srcW + PAD_BYTES, ROW_ALIGNMENT);
    rows->dstStride = FFALIGN(dstW * sizeof(int16_t) + PAD_BYTES, ROW_ALIGNMENT);
    rows->src = av_calloc(capacity, rows->srcStride);
    rows->expected = av_calloc(capacity, rows->dstStride);
    rows->output = av_calloc(capacity, rows->dstStride);
    if (!rows->src || !rows->expected || !rows->output) {
        return Fail("cannot allocate horizontal rows");
    }
    return 0;
}

static int InitRGBRows(RGBRows *rows, const Bench *bench)
{
    rows->capacity = bench->scenario->dstH;
    rows->lumSize = bench->c->vLumFilterSize;
    rows->chrSize = bench->c->vChrFilterSize;
    rows->lumStride = FFALIGN(bench->scenario->dstW + PAD_BYTES / sizeof(*rows->lum), FILTER_SAMPLE_ALIGNMENT);
    rows->chrStride = FFALIGN(bench->c->chrDstW + PAD_BYTES / sizeof(*rows->u), FILTER_SAMPLE_ALIGNMENT);
    rows->dstStride = FFALIGN(bench->scenario->dstW * RGB24_BYTES_PER_PIXEL + PAD_BYTES, ROW_ALIGNMENT);
    rows->y = av_calloc(rows->capacity, sizeof(*rows->y));
    rows->lumFilter = av_calloc((size_t)rows->capacity * rows->lumSize, sizeof(*rows->lumFilter));
    rows->chrFilter = av_calloc((size_t)rows->capacity * rows->chrSize, sizeof(*rows->chrFilter));
    rows->lum = av_calloc((size_t)rows->capacity * rows->lumSize, rows->lumStride * sizeof(*rows->lum));
    rows->u = av_calloc((size_t)rows->capacity * rows->chrSize, rows->chrStride * sizeof(*rows->u));
    rows->v = av_calloc((size_t)rows->capacity * rows->chrSize, rows->chrStride * sizeof(*rows->v));
    rows->lumPtr = av_calloc((size_t)rows->capacity * rows->lumSize, sizeof(*rows->lumPtr));
    rows->uPtr = av_calloc((size_t)rows->capacity * rows->chrSize, sizeof(*rows->uPtr));
    rows->vPtr = av_calloc((size_t)rows->capacity * rows->chrSize, sizeof(*rows->vPtr));
    rows->expected = av_calloc(rows->capacity, rows->dstStride);
    rows->output = av_calloc(rows->capacity, rows->dstStride);
    if (!rows->y || !rows->lumFilter || !rows->chrFilter || !rows->lum || !rows->u || !rows->v ||
        !rows->lumPtr || !rows->uPtr || !rows->vPtr || !rows->expected || !rows->output) {
        return Fail("cannot allocate RGB24 rows");
    }
    return 0;
}

static int StoreHCall(HRows *rows, const uint8_t *src, const int16_t *dst)
{
    if (rows->count < 0 || rows->count >= rows->capacity) {
        return Fail("too many horizontal calls");
    }
    uint8_t *input = rows->src + (size_t)rows->count * rows->srcStride;
    uint8_t *output = rows->expected + (size_t)rows->count * rows->dstStride;
    if (CopyBytes(input, rows->srcStride, src, rows->srcW) < 0 ||
        CopyBytes(output, rows->dstStride, (const uint8_t *)dst, rows->dstW * sizeof(*dst)) < 0) {
        return Fail("cannot capture horizontal row");
    }
    rows->count++;
    return 0;
}

static void CaptureH(int plane, SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
                     const int16_t *filter, const int32_t *filterPos, int filterSize)
{
    Bench *bench = g_active;
    if (!bench->error && (c != bench->c || dstW != bench->h[plane].dstW)) {
        bench->error = Fail("unexpected horizontal scaling context");
    }
    const int parameters[] = { plane, dstW, filterSize };
    if (!bench->error) {
        bench->error = ReferenceBytes("horizontal call parameters", (const uint8_t *)parameters, sizeof(parameters));
    }
    if (!bench->error) {
        bench->error = ReferenceBytes("horizontal input", src, bench->h[plane].srcW);
    }
    /* The scaler still needs valid output when capture or comparison has failed. */
    HScaleFn function = plane ? bench->hc : bench->hy;
    function(c, dst, dstW, src, filter, filterPos, filterSize);
    if (bench->error) {
        return;
    }
    bench->error = ReferenceBytes("horizontal output", (const uint8_t *)dst, dstW * sizeof(*dst));
    if (bench->error) {
        return;
    }
    bench->calls[plane]++;
    if (bench->capture) {
        bench->error = StoreHCall(&bench->h[plane], src, dst);
    }
}

static void CaptureY(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
                     const int16_t *filter, const int32_t *filterPos, int filterSize)
{
    CaptureH(PLANE_Y, c, dst, dstW, src, filter, filterPos, filterSize);
}

static void CaptureUV(SwsInternal *c, int16_t *dst, int dstW, const uint8_t *src,
                      const int16_t *filter, const int32_t *filterPos, int filterSize)
{
    const uintptr_t address = (uintptr_t)src;
    const uintptr_t u = (uintptr_t)g_active->src->data[PLANE_U];
    const uintptr_t v = (uintptr_t)g_active->src->data[PLANE_V];
    const size_t size = (size_t)g_active->src->linesize[PLANE_U] *
                        (g_active->scenario->srcH / YUV420_CHROMA_DIVISOR);
    if (address >= u && address - u < size) {
        CaptureH(PLANE_U, c, dst, dstW, src, filter, filterPos, filterSize);
    } else if (address >= v && address - v < size) {
        CaptureH(PLANE_V, c, dst, dstW, src, filter, filterPos, filterSize);
    } else {
        if (!g_active->error) {
            g_active->error = Fail("chroma input does not belong to the source frame");
        }
        g_active->hc(c, dst, dstW, src, filter, filterPos, filterSize);
    }
}

static int ReferenceRGBInputs(const Bench *bench, const RGBInput *input)
{
    const int parameters[] = { KIND_RGB, input->dstW, input->lumSize, input->chrSize, input->y };
    if (ReferenceBytes("RGB24 call parameters", (const uint8_t *)parameters, sizeof(parameters)) < 0 ||
        ReferenceBytes("RGB24 luma coefficients", (const uint8_t *)input->lumFilter,
                       input->lumSize * sizeof(*input->lumFilter)) < 0 ||
        ReferenceBytes("RGB24 chroma coefficients", (const uint8_t *)input->chrFilter,
                       input->chrSize * sizeof(*input->chrFilter)) < 0) {
        return AVERROR_INVALIDDATA;
    }
    for (int i = 0; i < input->lumSize; i++) {
        if (ReferenceBytes("RGB24 luma input", (const uint8_t *)input->lumSrc[i],
                           input->dstW * sizeof(**input->lumSrc)) < 0) {
            return AVERROR_INVALIDDATA;
        }
    }
    for (int i = 0; i < input->chrSize; i++) {
        if (ReferenceBytes("RGB24 U input", (const uint8_t *)input->uSrc[i],
                           bench->c->chrDstW * sizeof(**input->uSrc)) < 0 ||
            ReferenceBytes("RGB24 V input", (const uint8_t *)input->vSrc[i],
                           bench->c->chrDstW * sizeof(**input->vSrc)) < 0) {
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

static int StoreRGBLuma(RGBRows *rows, const RGBInput *input, int row)
{
    const size_t filterBytes = (size_t)rows->lumSize * sizeof(*rows->lumFilter);
    if (CopyBytes((uint8_t *)(rows->lumFilter + (size_t)row * rows->lumSize), filterBytes,
                  (const uint8_t *)input->lumFilter, input->lumSize * sizeof(*input->lumFilter)) < 0) {
        return Fail("cannot capture RGB24 luma coefficients");
    }
    for (int i = 0; i < input->lumSize; i++) {
        const size_t index = (size_t)row * rows->lumSize + i;
        int16_t *copy = rows->lum + index * rows->lumStride;
        if (CopyBytes((uint8_t *)copy, rows->lumStride * sizeof(*copy),
                      (const uint8_t *)input->lumSrc[i], input->dstW * sizeof(*copy)) < 0) {
            return Fail("cannot capture RGB24 luma input");
        }
        rows->lumPtr[index] = copy;
    }
    return 0;
}

static int StoreRGBChroma(RGBRows *rows, const RGBInput *input, int row, int width)
{
    const size_t filterBytes = (size_t)rows->chrSize * sizeof(*rows->chrFilter);
    if (CopyBytes((uint8_t *)(rows->chrFilter + (size_t)row * rows->chrSize), filterBytes,
                  (const uint8_t *)input->chrFilter, input->chrSize * sizeof(*input->chrFilter)) < 0) {
        return Fail("cannot capture RGB24 chroma coefficients");
    }
    for (int i = 0; i < input->chrSize; i++) {
        const size_t index = (size_t)row * rows->chrSize + i;
        int16_t *u = rows->u + index * rows->chrStride;
        int16_t *v = rows->v + index * rows->chrStride;
        if (CopyBytes((uint8_t *)u, rows->chrStride * sizeof(*u),
                      (const uint8_t *)input->uSrc[i], width * sizeof(*u)) < 0 ||
            CopyBytes((uint8_t *)v, rows->chrStride * sizeof(*v),
                      (const uint8_t *)input->vSrc[i], width * sizeof(*v)) < 0) {
            return Fail("cannot capture RGB24 chroma input");
        }
        rows->uPtr[index] = u;
        rows->vPtr[index] = v;
    }
    return 0;
}

static int StoreRGBCall(Bench *bench, const RGBInput *input, const uint8_t *dest)
{
    RGBRows *rows = &bench->r;
    const int row = rows->count;
    if (row < 0 || row >= rows->capacity) {
        return Fail("too many RGB24 calls");
    }
    rows->y[row] = input->y;
    if (StoreRGBLuma(rows, input, row) < 0 || StoreRGBChroma(rows, input, row, bench->c->chrDstW) < 0 ||
        CopyBytes(rows->expected + (size_t)row * rows->dstStride, rows->dstStride,
                  dest, input->dstW * RGB24_BYTES_PER_PIXEL) < 0) {
        return Fail("cannot capture RGB24 call");
    }
    rows->count++;
    return 0;
}

static void CaptureRGB(SwsInternal *c, const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                       const int16_t *chrFilter, const int16_t **chrUSrc, const int16_t **chrVSrc,
                       int chrFilterSize, const int16_t **alpSrc, uint8_t *dest, int dstW, int y)
{
    Bench *bench = g_active;
    if (!bench->error && (c != bench->c || dstW != bench->scenario->dstW ||
        lumFilterSize != bench->r.lumSize || chrFilterSize != bench->r.chrSize || alpSrc)) {
        bench->error = Fail("unexpected RGB24 scaling context");
    }
    const RGBInput input = { lumFilter, lumSrc, lumFilterSize, chrFilter, chrUSrc, chrVSrc, chrFilterSize, dstW, y };
    if (!bench->error) {
        bench->error = ReferenceRGBInputs(bench, &input);
    }
    bench->rgb(c, lumFilter, lumSrc, lumFilterSize, chrFilter, chrUSrc, chrVSrc, chrFilterSize,
               alpSrc, dest, dstW, y);
    if (bench->error) {
        return;
    }
    bench->error = ReferenceBytes("RGB24 output", dest, dstW * RGB24_BYTES_PER_PIXEL);
    if (bench->error) {
        return;
    }
    bench->calls[KIND_RGB]++;
    if (bench->capture) {
        bench->error = StoreRGBCall(bench, &input, dest);
    }
}

static void SetHooks(Bench *bench, int enabled)
{
    bench->c->hyScale = enabled ? CaptureY : bench->hy;
    bench->c->hcScale = enabled ? CaptureUV : bench->hc;
    bench->c->yuv2packedX = enabled ? CaptureRGB : bench->rgb;
    ff_init_vscale_pfn(bench->c, bench->c->yuv2plane1, bench->c->yuv2planeX, bench->c->yuv2nv12cX,
                      bench->c->yuv2packed1, bench->c->yuv2packed2, bench->c->yuv2packedX,
                      bench->c->yuv2anyX, bench->c->use_mmx_vfilter);
}

static int ScaleFrame(Bench *bench)
{
    const uint8_t *src[] = {
        bench->src->data[PLANE_Y], bench->src->data[PLANE_U], bench->src->data[PLANE_V], NULL
    };
    const int lines = sws_scale(bench->sws, src, bench->src->linesize, 0, bench->scenario->srcH,
                                bench->dst->data, bench->dst->linesize);
    if (lines != bench->scenario->dstH) {
        return Fail("sws_scale did not produce a complete frame");
    }
    return bench->error;
}

static int CheckDispatch(Bench *bench)
{
#if ARCH_AARCH64
    const Scenario *scenario = bench->scenario;
    if (bench->c->hLumFilterSize != scenario->hTaps || bench->c->hChrFilterSize != scenario->hTaps ||
        bench->c->vLumFilterSize != scenario->vYTaps || bench->c->vChrFilterSize != scenario->vUvTaps) {
        return Fail("unexpected filter sizes; these are not the requested BILINEAR cases");
    }
#if BENCH_OPTIMIZED
    const HScaleFn kernels[] = {
        ff_hscale8to15_3_2_neon, ff_hscale8to15_5_2_neon, ff_hscale8to15_3_1_neon,
    };
    if (bench->hy != kernels[scenario->ratio] || bench->hc != kernels[scenario->ratio]) {
        return Fail("optimized horizontal dispatch fell back; no performance result will be reported");
    }
    if (bench->rgb != ff_yuv2rgb24_X_neon || !bench->c->yuv2rgb_lut32_neon_ok) {
        return Fail("RGB24 NEON dispatch or its arithmetic fast path is unavailable");
    }
    bench->hName = TARGETS[scenario->ratio];
    bench->rgbName = "ff_yuv2rgb24_X_neon";
#else
    const int fourTaps = scenario->hTaps == GENERIC_HFILTER_TAPS;
    const HScaleFn expected = fourTaps ? ff_hscale8to15_4_neon : ff_hscale8to15_X8_neon;
    if (bench->hy != expected || bench->hc != expected) {
        return Fail("baseline horizontal dispatch is not the expected generic NEON implementation");
    }
    yuv2planar1_fn plane1;
    yuv2planarX_fn planeX;
    yuv2interleavedX_fn nv12;
    yuv2packed1_fn packed1;
    yuv2packed2_fn packed2;
    yuv2packedX_fn packedX;
    yuv2anyX_fn anyX;
    ff_sws_init_output_funcs(bench->c, &plane1, &planeX, &nv12, &packed1, &packed2, &packedX, &anyX);
    if (bench->rgb != packedX) {
        return Fail("baseline RGB24 dispatch is not the generic C output implementation");
    }
    bench->hName = fourTaps ? "ff_hscale8to15_4_neon" : "ff_hscale8to15_X8_neon";
    bench->rgbName = "yuv2rgb24_X_c";
#endif
#else
    bench->hName = "host_validation_only";
    bench->rgbName = "host_validation_only";
#endif
    if (!bench->hy || !bench->hc || !bench->rgb || bench->c->hyscale_fast || bench->c->hcscale_fast ||
        bench->c->nb_slice_ctx || (bench->c->opts.flags & SWS_FULL_CHR_H_INT)) {
        return Fail("unsupported scaling pipeline");
    }
    return 0;
}

static int InitBench(Bench *bench, const Scenario *scenario)
{
    bench->scenario = scenario;
    bench->sws = sws_alloc_context();
    if (!bench->sws) {
        return Fail("cannot allocate scaling context");
    }
    bench->sws->src_w = scenario->srcW;
    bench->sws->src_h = scenario->srcH;
    bench->sws->src_format = AV_PIX_FMT_YUV420P;
    bench->sws->dst_w = scenario->dstW;
    bench->sws->dst_h = scenario->dstH;
    bench->sws->dst_format = AV_PIX_FMT_RGB24;
    bench->sws->flags = SWS_BILINEAR;
    bench->sws->threads = 1;
    if (sws_init_context(bench->sws, NULL, NULL) < 0) {
        return Fail("cannot initialize BILINEAR context");
    }
    const int *matrix = sws_getCoefficients(SWS_CS_ITU709);
    if (sws_setColorspaceDetails(bench->sws, matrix, 0, matrix, 1, 0, COLOR_SCALE_ONE, COLOR_SCALE_ONE) < 0) {
        return Fail("cannot configure BT.709 limited YUV to full-range RGB");
    }
    bench->c = sws_internal(bench->sws);
    bench->hy = bench->c->hyScale;
    bench->hc = bench->c->hcScale;
    bench->rgb = bench->c->yuv2packedX;
    if (CheckDispatch(bench) < 0) {
        return AVERROR_INVALIDDATA;
    }
    bench->src = MakeFrame(scenario->srcW, scenario->srcH, AV_PIX_FMT_YUV420P);
    bench->dst = MakeFrame(scenario->dstW, scenario->dstH, AV_PIX_FMT_RGB24);
    if (!bench->src || !bench->dst) {
        return Fail("cannot allocate frame storage");
    }
    if (InitHRows(&bench->h[PLANE_Y], scenario->srcW, scenario->dstW, scenario->srcH) < 0 ||
        InitHRows(&bench->h[PLANE_U], bench->c->chrSrcW, bench->c->chrDstW,
                  scenario->srcH / YUV420_CHROMA_DIVISOR) < 0 ||
        InitHRows(&bench->h[PLANE_V], bench->c->chrSrcW, bench->c->chrDstW,
                  scenario->srcH / YUV420_CHROMA_DIVISOR) < 0 ||
        InitRGBRows(&bench->r, bench) < 0) {
        return AVERROR(ENOMEM);
    }
    return 0;
}

static int CheckContext(Bench *bench)
{
    SwsInternal *c = bench->c;
    const int parameters[] = {
        bench->scenario->srcW, bench->scenario->srcH, bench->scenario->dstW, bench->scenario->dstH,
        c->opts.src_format, c->opts.dst_format, c->opts.flags, c->opts.src_range, c->opts.dst_range,
        c->chrSrcW, c->chrDstW, c->hLumFilterSize, c->hChrFilterSize, c->vLumFilterSize, c->vChrFilterSize,
        c->lumXInc, c->chrXInc, c->srcBpc, c->dstBpc,
    };
    if (ReferenceBytes("context parameters", (const uint8_t *)parameters, sizeof(parameters)) < 0 ||
        ReferenceBytes("horizontal Y filter", (const uint8_t *)c->hLumFilter,
                       c->opts.dst_w * c->hLumFilterSize * sizeof(int16_t)) < 0 ||
        ReferenceBytes("horizontal UV filter", (const uint8_t *)c->hChrFilter,
                       c->chrDstW * c->hChrFilterSize * sizeof(int16_t)) < 0 ||
        ReferenceBytes("horizontal Y positions", (const uint8_t *)c->hLumFilterPos,
                       c->opts.dst_w * sizeof(int32_t)) < 0 ||
        ReferenceBytes("horizontal UV positions", (const uint8_t *)c->hChrFilterPos,
                       c->chrDstW * sizeof(int32_t)) < 0) {
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int PrintDispatch(const Bench *bench)
{
    if (!g_reference) {
        return 0;
    }
    if (Print(stderr, "\n  [%s] %s\n", VERSION_NAME, bench->scenario->name) < 0 ||
        Print(stderr, "    Horizontal : %s\n", bench->hName) < 0 ||
        Print(stderr, "    RGB24      : %s\n", bench->rgbName) < 0 ||
        Print(stderr, "    Calls/frame: Y=%d U=%d V=%d RGB24=%d; generic RGB24 tail=%d\n",
              bench->calls[PLANE_Y], bench->calls[PLANE_U], bench->calls[PLANE_V],
              bench->calls[KIND_RGB], GENERIC_RGB_TAIL_ROWS) < 0 ||
        Print(stderr, "    Filter taps: H(Y/UV)=%d/%d V(Y/UV)=%d/%d\n", bench->c->hLumFilterSize,
              bench->c->hChrFilterSize, bench->c->vLumFilterSize, bench->c->vChrFilterSize) < 0) {
        return AVERROR(EIO);
    }
    return 0;
}

static int CollectCalls(Bench *bench, int allPatterns)
{
    g_active = bench;
    if (CheckContext(bench) < 0) {
        return AVERROR_INVALIDDATA;
    }
    for (int pattern = allPatterns ? PATTERN_BLACK : PATTERN_RANDOM; pattern < PATTERN_COUNT; pattern++) {
        if (ReferenceBytes("source pattern", (const uint8_t *)&pattern, sizeof(pattern)) < 0 ||
            FillFrame(bench->src, pattern) < 0) {
            return AVERROR_INVALIDDATA;
        }
        for (size_t i = 0; i < FF_ARRAY_ELEMS(bench->calls); i++) {
            bench->calls[i] = 0;
        }
        bench->capture = pattern == PATTERN_RANDOM;
        SetHooks(bench, 1);
        const int result = ScaleFrame(bench);
        SetHooks(bench, 0);
        if (result < 0) {
            return result;
        }
        /* swscale.c switches the final two output rows to generic C on every architecture. */
        if (bench->calls[PLANE_Y] <= 0 || bench->calls[PLANE_U] <= 0 ||
            bench->calls[PLANE_V] != bench->calls[PLANE_U] ||
            bench->calls[KIND_RGB] != bench->scenario->dstH - GENERIC_RGB_TAIL_ROWS) {
            return Fail("actual target call counts do not match this pipeline");
        }
        if (ReferenceBytes("actual call counts", (const uint8_t *)bench->calls, sizeof(bench->calls)) < 0) {
            return AVERROR_INVALIDDATA;
        }
        for (int y = 0; y < bench->scenario->dstH; y++) {
            if (ReferenceBytes("complete RGB24 frame", bench->dst->data[0] + y * bench->dst->linesize[0],
                               bench->scenario->dstW * RGB24_BYTES_PER_PIXEL) < 0) {
                return AVERROR_INVALIDDATA;
            }
        }
    }
    return PrintDispatch(bench);
}

static void InvokeH(Bench *bench, int plane, int row)
{
    HRows *rows = &bench->h[plane];
    HScaleFn function = plane ? bench->hc : bench->hy;
    const int16_t *filter = plane ? bench->c->hChrFilter : bench->c->hLumFilter;
    const int32_t *positions = plane ? bench->c->hChrFilterPos : bench->c->hLumFilterPos;
    const int size = plane ? bench->c->hChrFilterSize : bench->c->hLumFilterSize;
    function(bench->c, (int16_t *)(rows->output + (size_t)row * rows->dstStride), rows->dstW,
             rows->src + (size_t)row * rows->srcStride, filter, positions, size);
}

static void InvokeRGB(Bench *bench, int row)
{
    RGBRows *rows = &bench->r;
    bench->rgb(bench->c, rows->lumFilter + row * rows->lumSize, rows->lumPtr + row * rows->lumSize,
               rows->lumSize, rows->chrFilter + row * rows->chrSize, rows->uPtr + row * rows->chrSize,
               rows->vPtr + row * rows->chrSize, rows->chrSize, NULL,
               rows->output + (size_t)row * rows->dstStride, bench->scenario->dstW, rows->y[row]);
}

static int CheckOutput(const uint8_t *output, const uint8_t *expected, int bytes, int stride)
{
    if (memcmp(output, expected, bytes)) {
        return Fail("captured call replay differs from the real pipeline");
    }
    for (int i = bytes; i < stride; i++) {
        if (output[i] != CANARY) {
            return Fail("function overwrote destination padding");
        }
    }
    return 0;
}

static int CheckReplay(Bench *bench)
{
    for (int plane = 0; plane < YUV420P_PLANE_COUNT; plane++) {
        HRows *rows = &bench->h[plane];
        if (rows->count < 0 || rows->count > rows->capacity ||
            FillBytes(rows->output, (size_t)rows->capacity * rows->dstStride, CANARY,
                      (size_t)rows->count * rows->dstStride) < 0) {
            return Fail("cannot initialize horizontal replay output");
        }
        for (int row = 0; row < rows->count; row++) {
            InvokeH(bench, plane, row);
            if (CheckOutput(rows->output + (size_t)row * rows->dstStride,
                            rows->expected + (size_t)row * rows->dstStride,
                            rows->dstW * sizeof(int16_t), rows->dstStride) < 0) {
                return AVERROR_INVALIDDATA;
            }
        }
    }
    RGBRows *rows = &bench->r;
    if (rows->count < 0 || rows->count > rows->capacity ||
        FillBytes(rows->output, (size_t)rows->capacity * rows->dstStride, CANARY,
                  (size_t)rows->count * rows->dstStride) < 0) {
        return Fail("cannot initialize RGB24 replay output");
    }
    for (int row = 0; row < rows->count; row++) {
        InvokeRGB(bench, row);
        if (CheckOutput(rows->output + (size_t)row * rows->dstStride,
                        rows->expected + (size_t)row * rows->dstStride,
                        bench->scenario->dstW * RGB24_BYTES_PER_PIXEL, rows->dstStride) < 0) {
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

/* Horizontal kinds use PLANE_*; RGB24 and complete frames use KIND_*. */
static int RunPass(Bench *bench, int kind)
{
    int result = 0;
    if (kind == KIND_FRAME) {
        result = ScaleFrame(bench);
    } else if (kind == KIND_RGB) {
        for (int i = 0; i < bench->r.count; i++) {
            InvokeRGB(bench, i);
        }
    } else {
        for (int i = 0; i < bench->h[kind].count; i++) {
            InvokeH(bench, kind, i);
        }
    }
    atomic_signal_fence(memory_order_seq_cst);
    return result;
}

/* qsort requires untyped arguments for its comparison callback. */
static int CompareDouble(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static int MeasureBatch(Bench *bench, int kind, int passes, int64_t *elapsed)
{
    const int64_t start = TimeNs();
    if (start < 0) {
        return AVERROR(EIO);
    }
    for (int pass = 0; pass < passes; pass++) {
        if (RunPass(bench, kind) < 0) {
            return AVERROR_INVALIDDATA;
        }
    }
    const int64_t end = TimeNs();
    if (end < start) {
        return Fail("invalid measurement clock reading");
    }
    *elapsed = end - start;
    return 0;
}

static int Calibrate(Bench *bench, int kind)
{
    for (int passes = 1; passes <= MAX_CALIBRATION_PASSES; passes *= CALIBRATION_GROWTH_FACTOR) {
        int64_t elapsed = 0;
        if (MeasureBatch(bench, kind, passes, &elapsed) < 0) {
            return AVERROR_INVALIDDATA;
        }
        if (elapsed >= BATCH_NS) {
            return passes;
        }
    }
    return Fail("timer calibration failed");
}

static int Measure(Bench *bench, int kind, Stats *stats)
{
    if (RunPass(bench, kind) < 0 || RunPass(bench, kind) < 0) {
        return AVERROR_INVALIDDATA;
    }
    const int passes = Calibrate(bench, kind);
    if (passes < 0) {
        return passes;
    }
    const int calls = kind == KIND_FRAME ? 1 : kind == KIND_RGB ? bench->r.count : bench->h[kind].count;
    if (calls <= 0) {
        return Fail("cannot measure an empty call sequence");
    }
    double samples[BATCHES];
    for (int batch = 0; batch < BATCHES; batch++) {
        int64_t elapsed = 0;
        if (MeasureBatch(bench, kind, passes, &elapsed) < 0) {
            return AVERROR_INVALIDDATA;
        }
        samples[batch] = (double)elapsed / ((double)passes * calls);
    }
    qsort(samples, BATCHES, sizeof(*samples), CompareDouble);
    *stats = (Stats){ samples[MEDIAN_INDEX], samples[BATCHES - 1], samples[0], samples[BATCHES - 1] };
    return 0;
}

static int Report(FILE *csv, Bench *bench, int kind)
{
    static const char *const PLANES[] = { "Y", "U", "V", "RGB", "RGB" };
    const char *target = kind < KIND_RGB ? TARGETS[bench->scenario->ratio] :
                         kind == KIND_RGB ? "ff_yuv2rgb24_X_neon" : "sws_scale";
    const char *actual = kind < KIND_RGB ? bench->hName : kind == KIND_RGB ? bench->rgbName : "sws_scale";
    const char *workload = kind == KIND_FRAME ? "sws_scale" : "function_replay";
    const int width = kind < KIND_RGB ? bench->h[kind].dstW : bench->scenario->dstW;
    const int hTaps = kind < KIND_RGB ? (kind ? bench->c->hChrFilterSize : bench->c->hLumFilterSize) : 0;
    const int vY = kind == KIND_RGB ? bench->r.lumSize : 0;
    const int vUv = kind == KIND_RGB ? bench->r.chrSize : 0;
    const int calls = kind == KIND_FRAME ? 1 : bench->calls[kind];
    Stats stats;
    if (Measure(bench, kind, &stats) < 0) {
        return AVERROR_INVALIDDATA;
    }
    uint64_t hash = 0;
    if (kind < KIND_RGB) {
        hash = Checksum(bench->h[kind].output, (size_t)bench->h[kind].count * bench->h[kind].dstStride);
    } else if (kind == KIND_RGB) {
        hash = Checksum(bench->r.output, (size_t)bench->r.count * bench->r.dstStride);
    } else {
        for (int y = 0; y < bench->scenario->dstH; y++) {
            hash ^= Checksum(bench->dst->data[0] + y * bench->dst->linesize[0], width * RGB24_BYTES_PER_PIXEL);
        }
    }
    return Print(csv, "%s,%s,%s,%s,%s,%s,OK,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%016" PRIx64 "\n",
                 VERSION_NAME, bench->scenario->name, target, actual, PLANES[kind], workload,
                 bench->scenario->srcW, bench->scenario->srcH, bench->scenario->dstW, bench->scenario->dstH,
                 width, hTaps, vY, vUv, calls, stats.median, stats.p95, stats.minimum, stats.maximum,
                 hash);
}

static void FreeBench(Bench *bench)
{
    for (int plane = 0; plane < YUV420P_PLANE_COUNT; plane++) {
        av_freep(&bench->h[plane].src);
        av_freep(&bench->h[plane].expected);
        av_freep(&bench->h[plane].output);
    }
    av_freep(&bench->r.y);
    av_freep(&bench->r.lumFilter);
    av_freep(&bench->r.chrFilter);
    av_freep(&bench->r.lum);
    av_freep(&bench->r.u);
    av_freep(&bench->r.v);
    av_freep(&bench->r.lumPtr);
    av_freep(&bench->r.uPtr);
    av_freep(&bench->r.vPtr);
    av_freep(&bench->r.expected);
    av_freep(&bench->r.output);
    av_frame_free(&bench->src);
    av_frame_free(&bench->dst);
    sws_freeContext(bench->sws);
    bench->sws = NULL;
    g_active = NULL;
}

static int ReportScenario(FILE *csv, Bench *bench)
{
    for (int kind = PLANE_Y; kind < KIND_COUNT; kind++) {
        const int result = Report(csv, bench, kind);
        if (result < 0) {
            return result;
        }
    }
    if (CheckReplay(bench) < 0) {
        return AVERROR_INVALIDDATA;
    }
    if (fflush(csv)) {
        return Fail("cannot flush CSV output");
    }
    return 0;
}

static int RunScenario(const Scenario *scenario, FILE *csv)
{
    Bench bench = { 0 };
    int result = InitBench(&bench, scenario);
    if (result >= 0) {
        result = CollectCalls(&bench, !csv);
    }
    if (result >= 0) {
        result = CheckReplay(&bench);
    }
    if (result >= 0 && csv) {
        result = ReportScenario(csv, &bench);
    }
    FreeBench(&bench);
    return result;
}

static int RunScenarios(FILE *csv)
{
    for (size_t index = 0; index < FF_ARRAY_ELEMS(SCENARIOS); index++) {
        const int result = RunScenario(&SCENARIOS[index], csv);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static int CheckArguments(int argc, char **argv)
{
    if (argc == BENCH_ARGUMENT_COUNT) {
        const char *mode = argv[MODE_ARGUMENT];
        if (!strcmp(mode, "--reference-write") || !strcmp(mode, "--reference-read") || !strcmp(mode, "--csv")) {
            return 0;
        }
    }
    return Fail("usage: bench --reference-write|--reference-read|--csv FILE");
}

static int CheckPlatform(void)
{
    av_log_set_level(AV_LOG_ERROR);
#if ARCH_AARCH64
    if (!have_neon(av_get_cpu_flags())) {
        return Fail("NEON is unavailable");
    }
#else
    /* This branch exists only to validate capture/replay on a development host. */
    if (Print(stderr, "HOST CHECK ONLY: these timings are not AArch64 performance measurements.\n") < 0) {
        return AVERROR(EIO);
    }
    av_force_cpu_flags(0);
#endif
    return 0;
}

static int OpenOutput(const char *mode, const char *filename, FILE **csv)
{
    if (!strcmp(mode, "--csv")) {
        *csv = fopen(filename, "w");
        if (!*csv) {
            return Fail("cannot open CSV output");
        }
        /* Keep the CSV column names stable for existing consumers. */
        return Print(*csv, "version,scenario,target,actual,plane,workload,status,src_w,src_h,dst_w,dst_h,"
                           "row_width,h_taps,v_y_taps,v_uv_taps,calls_per_frame,median_ns,p95_ns,min_ns,max_ns,"
                           "checksum\n");
    }
    g_referenceRead = !strcmp(mode, "--reference-read");
    g_reference = fopen(filename, g_referenceRead ? "rb" : "wb");
    return g_reference ? 0 : Fail("cannot open reference file");
}

static int CheckReferenceHeader(void)
{
    if (!g_reference) {
        return 0;
    }
    const uint32_t signature[] = { REFERENCE_MAGIC, REFERENCE_VERSION, FF_ARRAY_ELEMS(SCENARIOS) };
    if (!g_referenceRead) {
        return ReferenceBytes("reference header", (const uint8_t *)signature, sizeof(signature));
    }
    uint32_t header[FF_ARRAY_ELEMS(signature)];
    if (fread(header, 1, sizeof(header), g_reference) != sizeof(header) || memcmp(header, signature, sizeof(header))) {
        return Fail("invalid reference header");
    }
    return 0;
}

static int CheckReferenceEnd(void)
{
    if (g_reference && g_referenceRead && (fgetc(g_reference) != EOF || ferror(g_reference))) {
        return Fail("reference contains extra calls or a read error occurred");
    }
    return 0;
}

static int CloseOutput(FILE *csv, int status, int referenceMode)
{
    if (g_reference) {
        const int result = fclose(g_reference);
        g_reference = NULL;
        if (result) {
            status = Fail("cannot close reference file");
        }
    }
    if (csv && fclose(csv)) {
        status = Fail("cannot close CSV output");
    }
    if (status >= 0 && referenceMode &&
        Print(stderr, "\n  [%s] PASS: four source patterns, call inputs/outputs, full frames and replay checked.\n",
              VERSION_NAME) < 0) {
        status = AVERROR(EIO);
    }
    return status;
}

int main(int argc, char **argv)
{
    if (CheckArguments(argc, argv) < 0 || CheckPlatform() < 0) {
        return EXIT_FAILURE;
    }
    FILE *csv = NULL;
    int result = OpenOutput(argv[MODE_ARGUMENT], argv[FILE_ARGUMENT], &csv);
    if (result >= 0) {
        result = CheckReferenceHeader();
    }
    if (result >= 0) {
        result = RunScenarios(csv);
    }
    if (result >= 0) {
        result = CheckReferenceEnd();
    }
    result = CloseOutput(csv, result, strcmp(argv[MODE_ARGUMENT], "--csv") != 0);
    return result < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
