#include "moo_compositor_core.h"

#include <stdint.h>

static int moo_comp_output_valid(const MooCompOutput *output) {
    size_t row_bytes;
    if (!output || !output->pixels ||
        output->width <= 0 || output->height <= 0) return 0;
    if ((size_t)output->width > SIZE_MAX / 4u) return 0;
    row_bytes = (size_t)output->width * 4u;
    if (output->stride < row_bytes) return 0;
    if ((size_t)output->height > SIZE_MAX / output->stride) return 0;
    return output->stride * (size_t)output->height <= output->buffer_bytes;
}

static int moo_comp_source_valid(const MooCompBufferView *source) {
    size_t row_bytes;
    if (!source || !source->pixels ||
        source->format != MOO_COMP_FORMAT_RGBA8888 ||
        source->width <= 0 || source->height <= 0) return 0;
    if ((size_t)source->width > SIZE_MAX / 4u) return 0;
    row_bytes = (size_t)source->width * 4u;
    if (source->stride < row_bytes) return 0;
    if ((size_t)source->height > SIZE_MAX / source->stride) return 0;
    return source->stride * (size_t)source->height <= source->buffer_bytes;
}
static int moo_comp_ranges_overlap(const void *left, size_t left_size,
                                   const void *right, size_t right_size) {
    uintptr_t left_begin;
    uintptr_t right_begin;
    if (!left || !right || left_size == 0u || right_size == 0u) return 0;
    left_begin = (uintptr_t)left;
    right_begin = (uintptr_t)right;
    if (left_size > UINTPTR_MAX - left_begin ||
        right_size > UINTPTR_MAX - right_begin)
        return 1;
    return left_begin < right_begin + right_size &&
           right_begin < left_begin + left_size;
}


static int32_t moo_comp_i64_to_low(int64_t value, int32_t low) {
    return value < (int64_t)low ? low : (int32_t)value;
}

static int32_t moo_comp_i64_to_high(int64_t value, int32_t high) {
    return value > (int64_t)high ? high : (int32_t)value;
}

static int moo_comp_clip_output(const MooCompOutput *output, MooCompRect clip,
                                int32_t *x0, int32_t *y0,
                                int32_t *x1, int32_t *y1) {
    int64_t right;
    int64_t bottom;
    if (!moo_comp_output_valid(output) || !x0 || !y0 || !x1 || !y1 ||
        clip.width < 0 || clip.height < 0) return 0;
    right = (int64_t)clip.x + (int64_t)clip.width;
    bottom = (int64_t)clip.y + (int64_t)clip.height;
    *x0 = moo_comp_i64_to_low(clip.x, 0);
    *y0 = moo_comp_i64_to_low(clip.y, 0);
    *x1 = moo_comp_i64_to_high(right, output->width);
    *y1 = moo_comp_i64_to_high(bottom, output->height);
    if (*x0 > output->width) *x0 = output->width;
    if (*y0 > output->height) *y0 = output->height;
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x1 < *x0) *x1 = *x0;
    if (*y1 < *y0) *y1 = *y0;
    return 1;
}

static uint8_t *moo_comp_output_pixel(const MooCompOutput *output,
                                      int32_t x, int32_t y) {
    return output->pixels + (size_t)y * output->stride + (size_t)x * 4u;
}

static const uint8_t *moo_comp_source_pixel(const MooCompBufferView *source,
                                            int32_t x, int32_t y) {
    return source->pixels + (size_t)y * source->stride + (size_t)x * 4u;
}

static void moo_comp_blend(uint8_t *dst, const uint8_t *source,
                           uint32_t opacity) {
    uint64_t src_alpha;
    uint64_t inverse;
    uint64_t alpha_numerator;
    uint64_t red_numerator;
    uint64_t green_numerator;
    uint64_t blue_numerator;
    src_alpha = ((uint64_t)source[3] * opacity + 127u) / 255u;
    if (src_alpha == 0u) return;
    if (src_alpha == 255u) {
        dst[0] = source[0];
        dst[1] = source[1];
        dst[2] = source[2];
        dst[3] = 255u;
        return;
    }
    inverse = 255u - src_alpha;
    alpha_numerator = src_alpha * 255u + (uint64_t)dst[3] * inverse;
    if (alpha_numerator == 0u) {
        dst[0] = 0u;
        dst[1] = 0u;
        dst[2] = 0u;
        dst[3] = 0u;
        return;
    }
    red_numerator = (uint64_t)source[0] * src_alpha * 255u +
                    (uint64_t)dst[0] * (uint64_t)dst[3] * inverse;
    green_numerator = (uint64_t)source[1] * src_alpha * 255u +
                      (uint64_t)dst[1] * (uint64_t)dst[3] * inverse;
    blue_numerator = (uint64_t)source[2] * src_alpha * 255u +
                     (uint64_t)dst[2] * (uint64_t)dst[3] * inverse;
    dst[0] = (uint8_t)((red_numerator + alpha_numerator / 2u) /
                       alpha_numerator);
    dst[1] = (uint8_t)((green_numerator + alpha_numerator / 2u) /
                       alpha_numerator);
    dst[2] = (uint8_t)((blue_numerator + alpha_numerator / 2u) /
                       alpha_numerator);
    dst[3] = (uint8_t)((alpha_numerator + 127u) / 255u);
}

MooCompResult moo_comp_raster_clear(const MooCompOutput *output,
                                    MooCompRect clip,
                                    uint8_t r, uint8_t g,
                                    uint8_t b, uint8_t a) {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t x;
    int32_t y;
    if (!moo_comp_clip_output(output, clip, &x0, &y0, &x1, &y1))
        return MOO_COMP_INVALID;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            uint8_t *pixel = moo_comp_output_pixel(output, x, y);
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
            pixel[3] = a;
        }
    }
    return MOO_COMP_OK;
}

MooCompResult moo_comp_raster_blit(const MooCompOutput *output,
                                   const MooCompBufferView *source,
                                   int32_t dst_x, int32_t dst_y,
                                   uint32_t scale, uint32_t opacity,
                                   MooCompRect clip) {
    int32_t clip_x0;
    int32_t clip_y0;
    int32_t clip_x1;
    int32_t clip_y1;
    int32_t logical_width;
    int32_t logical_height;
    int64_t dst_right;
    int64_t dst_bottom;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t x;
    int32_t y;
    if (!moo_comp_output_valid(output) || !moo_comp_source_valid(source) ||
        scale < MOO_COMP_SCALE_MIN || scale > MOO_COMP_SCALE_MAX ||
        opacity > 255u ||
        source->width % (int32_t)scale != 0 ||
        source->height % (int32_t)scale != 0)
        return MOO_COMP_BAD_BUFFER;
    if (!moo_comp_clip_output(output, clip, &clip_x0, &clip_y0,
                              &clip_x1, &clip_y1))
        return MOO_COMP_INVALID;
    logical_width = source->width / (int32_t)scale;
    logical_height = source->height / (int32_t)scale;
    dst_right = (int64_t)dst_x + logical_width;
    dst_bottom = (int64_t)dst_y + logical_height;
    x0 = dst_x > clip_x0 ? dst_x : clip_x0;
    y0 = dst_y > clip_y0 ? dst_y : clip_y0;
    x1 = moo_comp_i64_to_high(dst_right, clip_x1);
    y1 = moo_comp_i64_to_high(dst_bottom, clip_y1);
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    for (y = y0; y < y1; ++y) {
        int32_t source_y = (y - dst_y) * (int32_t)scale;
        for (x = x0; x < x1; ++x) {
            int32_t source_x = (x - dst_x) * (int32_t)scale;
            moo_comp_blend(moo_comp_output_pixel(output, x, y),
                           moo_comp_source_pixel(source, source_x, source_y),
                           opacity);
        }
    }
    return MOO_COMP_OK;
}
MooCompResult moo_comp_raster_copy_rgba(
    const MooCompOutput *output, uint8_t *destination,
    size_t destination_bytes, size_t destination_stride) {
    size_t active_bytes;
    size_t destination_total;
    size_t source_total;
    int32_t y;
    if (!moo_comp_output_valid(output) || !destination)
        return MOO_COMP_INVALID;
    active_bytes = (size_t)output->width * 4u;
    if (destination_stride < active_bytes ||
        (size_t)output->height > SIZE_MAX / destination_stride)
        return MOO_COMP_INVALID;
    destination_total = destination_stride * (size_t)output->height;
    source_total = output->stride * (size_t)output->height;
    if (destination_total > destination_bytes) return MOO_COMP_INVALID;
    if (moo_comp_ranges_overlap(output->pixels, source_total,
                                destination, destination_total))
        return MOO_COMP_INVALID;
    for (y = 0; y < output->height; ++y) {
        size_t x;
        const uint8_t *source =
            output->pixels + (size_t)y * output->stride;
        uint8_t *target = destination + (size_t)y * destination_stride;
        for (x = 0u; x < active_bytes; ++x) target[x] = source[x];
    }
    return MOO_COMP_OK;
}
