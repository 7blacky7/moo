#include "moo_surface_core.h"

#include <limits.h>

static bool moo_surface_size_ok(int32_t width, int32_t height, size_t stride,
                                size_t buffer_bytes) {
    size_t row_bytes;
    if (width <= 0 || height <= 0) return false;
    if ((size_t)width > SIZE_MAX / 4u) return false;
    row_bytes = (size_t)width * 4u;
    if (stride < row_bytes) return false;
    if ((size_t)height > SIZE_MAX / stride) return false;
    return stride * (size_t)height <= buffer_bytes;
}

static bool moo_surface_clip_valid(const MooSurfaceCore* core,
                                   MooSurfaceClip clip) {
    return clip.x0 >= 0 && clip.y0 >= 0 &&
           clip.x0 <= clip.x1 && clip.y0 <= clip.y1 &&
           clip.x1 <= core->width && clip.y1 <= core->height;
}

bool moo_surface_core_valid(const MooSurfaceCore* core) {
    uint32_t i;
    if (!core || !core->pixels) return false;
    if (!moo_surface_size_ok(core->width, core->height, core->stride,
                             core->buffer_bytes)) return false;
    if (core->clip_depth == 0u ||
        core->clip_depth > MOO_SURFACE_CLIP_CAPACITY) return false;
    for (i = 0u; i < core->clip_depth; ++i) {
        if (!moo_surface_clip_valid(core, core->clips[i])) return false;
    }
    return core->clips[0].x0 == 0 && core->clips[0].y0 == 0 &&
           core->clips[0].x1 == core->width &&
           core->clips[0].y1 == core->height;
}

bool moo_surface_core_init(MooSurfaceCore* core, uint8_t* pixels,
                           size_t buffer_bytes, int32_t width, int32_t height,
                           size_t stride) {
    if (!core || !pixels ||
        !moo_surface_size_ok(width, height, stride, buffer_bytes)) return false;
    core->pixels = pixels;
    core->buffer_bytes = buffer_bytes;
    core->stride = stride;
    core->width = width;
    core->height = height;
    core->clip_depth = 1u;
    core->clips[0].x0 = 0;
    core->clips[0].y0 = 0;
    core->clips[0].x1 = width;
    core->clips[0].y1 = height;
    return true;
}

static MooSurfaceClip moo_surface_intersect(const MooSurfaceCore* core,
                                            int64_t x0, int64_t y0,
                                            int64_t x1, int64_t y1) {
    MooSurfaceClip current = core->clips[core->clip_depth - 1u];
    MooSurfaceClip out;
    int64_t ix0 = x0 > current.x0 ? x0 : current.x0;
    int64_t iy0 = y0 > current.y0 ? y0 : current.y0;
    int64_t ix1 = x1 < current.x1 ? x1 : current.x1;
    int64_t iy1 = y1 < current.y1 ? y1 : current.y1;
    if (ix0 > current.x1) ix0 = current.x1;
    if (iy0 > current.y1) iy0 = current.y1;
    if (ix1 < current.x0) ix1 = current.x0;
    if (iy1 < current.y0) iy1 = current.y0;
    if (ix1 < ix0) ix1 = ix0;
    if (iy1 < iy0) iy1 = iy0;
    out.x0 = (int32_t)ix0;
    out.y0 = (int32_t)iy0;
    out.x1 = (int32_t)ix1;
    out.y1 = (int32_t)iy1;
    return out;
}

static bool moo_surface_rect_bounds(const MooSurfaceCore* core,
                                    int32_t x, int32_t y,
                                    int32_t width, int32_t height,
                                    MooSurfaceClip* out) {
    int64_t x1;
    int64_t y1;
    if (width < 0 || height < 0 || !out) return false;
    x1 = (int64_t)x + (int64_t)width;
    y1 = (int64_t)y + (int64_t)height;
    *out = moo_surface_intersect(core, x, y, x1, y1);
    return true;
}

static uint8_t* moo_surface_pixel(MooSurfaceCore* core, int32_t x, int32_t y) {
    size_t offset = (size_t)y * core->stride + (size_t)x * 4u;
    return core->pixels + offset;
}

static const uint8_t* moo_surface_pixel_const(const MooSurfaceCore* core,
                                              int32_t x, int32_t y) {
    size_t offset = (size_t)y * core->stride + (size_t)x * 4u;
    return core->pixels + offset;
}

static void moo_surface_blend(uint8_t* dst, MooSurfaceColor src) {
    uint64_t inv;
    uint64_t alpha_numerator;
    uint64_t red_numerator;
    uint64_t green_numerator;
    uint64_t blue_numerator;
    if (src.a == 0u) return;
    if (src.a == 255u) {
        dst[0] = src.r;
        dst[1] = src.g;
        dst[2] = src.b;
        dst[3] = 255u;
        return;
    }
    inv = 255u - (uint64_t)src.a;
    alpha_numerator = (uint64_t)src.a * 255u + (uint64_t)dst[3] * inv;
    if (alpha_numerator == 0u) {
        dst[0] = 0u;
        dst[1] = 0u;
        dst[2] = 0u;
        dst[3] = 0u;
        return;
    }
    red_numerator = (uint64_t)src.r * (uint64_t)src.a * 255u +
                    (uint64_t)dst[0] * (uint64_t)dst[3] * inv;
    green_numerator = (uint64_t)src.g * (uint64_t)src.a * 255u +
                      (uint64_t)dst[1] * (uint64_t)dst[3] * inv;
    blue_numerator = (uint64_t)src.b * (uint64_t)src.a * 255u +
                     (uint64_t)dst[2] * (uint64_t)dst[3] * inv;
    dst[0] = (uint8_t)((red_numerator + alpha_numerator / 2u) /
                       alpha_numerator);
    dst[1] = (uint8_t)((green_numerator + alpha_numerator / 2u) /
                       alpha_numerator);
    dst[2] = (uint8_t)((blue_numerator + alpha_numerator / 2u) /
                       alpha_numerator);
    dst[3] = (uint8_t)((alpha_numerator + 127u) / 255u);
}

bool moo_surface_core_clear(MooSurfaceCore* core, MooSurfaceColor color) {
    int32_t x;
    int32_t y;
    if (!moo_surface_core_valid(core)) return false;
    for (y = 0; y < core->height; ++y) {
        for (x = 0; x < core->width; ++x) {
            uint8_t* pixel = moo_surface_pixel(core, x, y);
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
            pixel[3] = color.a;
        }
    }
    return true;
}

bool moo_surface_core_clip_push(MooSurfaceCore* core, int32_t x, int32_t y,
                                int32_t width, int32_t height) {
    MooSurfaceClip next;
    if (!moo_surface_core_valid(core) ||
        core->clip_depth >= MOO_SURFACE_CLIP_CAPACITY) return false;
    if (!moo_surface_rect_bounds(core, x, y, width, height, &next)) return false;
    core->clips[core->clip_depth] = next;
    core->clip_depth++;
    return true;
}

bool moo_surface_core_clip_pop(MooSurfaceCore* core) {
    if (!moo_surface_core_valid(core) || core->clip_depth <= 1u) return false;
    core->clip_depth--;
    return true;
}

static void moo_surface_fill_clip(MooSurfaceCore* core, MooSurfaceClip clip,
                                  MooSurfaceColor color) {
    int32_t x;
    int32_t y;
    for (y = clip.y0; y < clip.y1; ++y) {
        for (x = clip.x0; x < clip.x1; ++x) {
            moo_surface_blend(moo_surface_pixel(core, x, y), color);
        }
    }
}

bool moo_surface_core_rect(MooSurfaceCore* core, int32_t x, int32_t y,
                           int32_t width, int32_t height,
                           MooSurfaceColor color) {
    MooSurfaceClip clip;
    if (!moo_surface_core_valid(core) ||
        !moo_surface_rect_bounds(core, x, y, width, height, &clip)) return false;
    moo_surface_fill_clip(core, clip, color);
    return true;
}

static bool moo_surface_round_inside(int32_t px, int32_t py,
                                     int64_t x0, int64_t y0,
                                     int64_t x1, int64_t y1,
                                     int32_t radius) {
    int64_t sx;
    int64_t sy;
    int64_t cx;
    int64_t cy;
    int64_t dx;
    int64_t dy;
    int64_t rr;
    uint64_t ux;
    uint64_t uy;
    uint64_t ur;
    if (radius <= 0) return true;
    sx = (int64_t)px * 2 + 1;
    sy = (int64_t)py * 2 + 1;
    if (sx >= 2 * (x0 + radius) && sx < 2 * (x1 - radius)) return true;
    if (sy >= 2 * (y0 + radius) && sy < 2 * (y1 - radius)) return true;
    cx = sx < 2 * (x0 + radius) ? 2 * (x0 + radius) :
         2 * (x1 - radius);
    cy = sy < 2 * (y0 + radius) ? 2 * (y0 + radius) :
         2 * (y1 - radius);
    dx = sx - cx;
    dy = sy - cy;
    rr = (int64_t)radius * 2;
    if (dx < -rr || dx > rr || dy < -rr || dy > rr) return false;
    ux = (uint64_t)(dx < 0 ? -dx : dx);
    uy = (uint64_t)(dy < 0 ? -dy : dy);
    ur = (uint64_t)rr;
    return ux * ux + uy * uy <= ur * ur;
}

bool moo_surface_core_roundrect(MooSurfaceCore* core, int32_t x, int32_t y,
                                int32_t width, int32_t height, int32_t radius,
                                MooSurfaceColor color) {
    MooSurfaceClip clip;
    int32_t px;
    int32_t py;
    int32_t max_radius;
    int64_t x1;
    int64_t y1;
    if (!moo_surface_core_valid(core) || width < 0 || height < 0 ||
        radius < 0) return false;
    max_radius = (width < height ? width : height) / 2;
    if (radius > max_radius) return false;
    if (!moo_surface_rect_bounds(core, x, y, width, height, &clip)) return false;
    x1 = (int64_t)x + (int64_t)width;
    y1 = (int64_t)y + (int64_t)height;
    for (py = clip.y0; py < clip.y1; ++py) {
        for (px = clip.x0; px < clip.x1; ++px) {
            if (moo_surface_round_inside(px, py, x, y, x1, y1, radius)) {
                moo_surface_blend(moo_surface_pixel(core, px, py), color);
            }
        }
    }
    return true;
}

static uint64_t moo_surface_circle_extent(uint64_t radius, uint64_t dy) {
    uint64_t target = radius * radius - dy * dy;
    uint64_t low = 0u;
    uint64_t high = radius;
    while (low < high) {
        uint64_t mid = low + (high - low + 1u) / 2u;
        if (mid <= target / mid) low = mid;
        else high = mid - 1u;
    }
    return low;
}

bool moo_surface_core_circle(MooSurfaceCore* core, int32_t cx, int32_t cy,
                             int32_t radius, MooSurfaceColor color) {
    MooSurfaceClip clip;
    MooSurfaceClip current;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int32_t y;
    if (!moo_surface_core_valid(core) || radius < 0) return false;
    left = (int64_t)cx - radius;
    top = (int64_t)cy - radius;
    right = (int64_t)cx + radius + 1;
    bottom = (int64_t)cy + radius + 1;
    clip = moo_surface_intersect(core, left, top, right, bottom);
    current = core->clips[core->clip_depth - 1u];
    for (y = clip.y0; y < clip.y1; ++y) {
        int64_t dy64 = (int64_t)y - cy;
        uint64_t dy = (uint64_t)(dy64 < 0 ? -dy64 : dy64);
        uint64_t extent;
        int64_t row_left;
        int64_t row_right;
        int32_t x0;
        int32_t x1;
        int32_t x;
        if (dy > (uint64_t)radius) continue;
        extent = moo_surface_circle_extent((uint64_t)radius, dy);
        row_left = (int64_t)cx - (int64_t)extent;
        row_right = (int64_t)cx + (int64_t)extent + 1;
        if (row_left < current.x0) row_left = current.x0;
        if (row_right > current.x1) row_right = current.x1;
        if (row_right <= row_left) continue;
        x0 = (int32_t)row_left;
        x1 = (int32_t)row_right;
        for (x = x0; x < x1; ++x) {
            moo_surface_blend(moo_surface_pixel(core, x, y), color);
        }
    }
    return true;
}

static bool moo_surface_clip_test(double p, double q,
                                  double* t0, double* t1) {
    double r;
    if (p == 0.0) return q >= 0.0;
    r = q / p;
    if (p < 0.0) {
        if (r > *t1) return false;
        if (r > *t0) *t0 = r;
    } else {
        if (r < *t0) return false;
        if (r < *t1) *t1 = r;
    }
    return true;
}

static int32_t moo_surface_round_clamped(double value,
                                         int32_t low, int32_t high) {
    int64_t rounded;
    if (value <= (double)low) return low;
    if (value >= (double)high) return high;
    rounded = (int64_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
    if (rounded < low) return low;
    if (rounded > high) return high;
    return (int32_t)rounded;
}

bool moo_surface_core_line(MooSurfaceCore* core, int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1, MooSurfaceColor color) {
    MooSurfaceClip clip;
    double dx;
    double dy;
    double t0 = 0.0;
    double t1 = 1.0;
    int32_t ax;
    int32_t ay;
    int32_t bx;
    int32_t by;
    int64_t ddx;
    int64_t ddy;
    int64_t sx;
    int64_t sy;
    int64_t err;
    if (!moo_surface_core_valid(core)) return false;
    clip = core->clips[core->clip_depth - 1u];
    if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1) return true;
    dx = (double)x1 - (double)x0;
    dy = (double)y1 - (double)y0;
    if (!moo_surface_clip_test(-dx, (double)x0 - clip.x0, &t0, &t1) ||
        !moo_surface_clip_test(dx, (double)(clip.x1 - 1) - x0, &t0, &t1) ||
        !moo_surface_clip_test(-dy, (double)y0 - clip.y0, &t0, &t1) ||
        !moo_surface_clip_test(dy, (double)(clip.y1 - 1) - y0, &t0, &t1)) {
        return true;
    }
    ax = moo_surface_round_clamped((double)x0 + t0 * dx,
                                   clip.x0, clip.x1 - 1);
    ay = moo_surface_round_clamped((double)y0 + t0 * dy,
                                   clip.y0, clip.y1 - 1);
    bx = moo_surface_round_clamped((double)x0 + t1 * dx,
                                   clip.x0, clip.x1 - 1);
    by = moo_surface_round_clamped((double)y0 + t1 * dy,
                                   clip.y0, clip.y1 - 1);
    ddx = (int64_t)bx - ax;
    if (ddx < 0) ddx = -ddx;
    ddy = (int64_t)by - ay;
    if (ddy < 0) ddy = -ddy;
    ddy = -ddy;
    sx = ax < bx ? 1 : -1;
    sy = ay < by ? 1 : -1;
    err = ddx + ddy;
    for (;;) {
        int64_t twice;
        moo_surface_blend(moo_surface_pixel(core, ax, ay), color);
        if (ax == bx && ay == by) break;
        twice = err * 2;
        if (twice >= ddy) {
            err += ddy;
            ax = (int32_t)((int64_t)ax + sx);
        }
        if (twice <= ddx) {
            err += ddx;
            ay = (int32_t)((int64_t)ay + sy);
        }
    }
    return true;
}

static const uint8_t* moo_surface_glyph(unsigned char c) {
    static const uint8_t glyphs[40][5] = {
        {7,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},
        {7,4,6,4,7},{7,4,6,4,4},{7,4,5,5,7},{5,5,7,5,5},
        {7,2,2,2,7},{1,1,1,5,7},{5,5,6,5,5},{4,4,4,4,7},
        {5,7,7,5,5},{5,7,7,7,5},{7,5,5,5,7},{7,5,7,4,4},
        {7,5,5,7,1},{7,5,6,5,5},{7,4,7,1,7},{7,2,2,2,2},
        {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
        {5,5,2,2,2},{7,1,2,4,7},
        {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},
        {5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},
        {7,5,7,5,7},{7,5,7,1,7},
        {0,0,0,0,0},{0,0,7,0,0},{0,0,0,0,2},{2,0,2,2,2}
    };
    static const uint8_t question[5] = {7,1,3,0,2};
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return glyphs[c - 'A'];
    if (c >= '0' && c <= '9') return glyphs[26u + c - '0'];
    if (c == ' ') return glyphs[36];
    if (c == '-') return glyphs[37];
    if (c == '.') return glyphs[38];
    if (c == '!') return glyphs[39];
    return question;
}

bool moo_surface_core_text_width_3x5(const char* text, size_t text_len,
                                     int32_t scale, int32_t* out_width) {
    size_t i;
    size_t line_chars = 0u;
    size_t max_chars = 0u;
    uint64_t advance;
    uint64_t scaled;
    if (!text || !out_width || scale <= 0) return false;
    for (i = 0u; i < text_len; ++i) {
        if (text[i] == '\n') {
            if (line_chars > max_chars) max_chars = line_chars;
            line_chars = 0u;
        } else {
            if (line_chars == SIZE_MAX) return false;
            line_chars++;
        }
    }
    if (line_chars > max_chars) max_chars = line_chars;
    if (max_chars == 0u) {
        *out_width = 0;
        return true;
    }
    advance = (uint64_t)(uint32_t)scale * 4u;
    if ((uint64_t)max_chars >
        ((uint64_t)INT32_MAX + (uint64_t)(uint32_t)scale) / advance)
        return false;
    scaled = (uint64_t)max_chars * advance - (uint64_t)(uint32_t)scale;
    if (scaled > (uint64_t)INT32_MAX) return false;
    *out_width = (int32_t)scaled;
    return true;
}

bool moo_surface_core_text_3x5(MooSurfaceCore* core, int32_t x, int32_t y,
                               int32_t scale, const char* text,
                               size_t text_len, MooSurfaceColor color) {
    size_t i;
    int64_t pen_x = x;
    int64_t pen_y = y;
    int64_t origin_x = x;
    int64_t glyph_advance;
    int64_t line_advance;
    if (!moo_surface_core_valid(core) || !text || scale <= 0) return false;
    glyph_advance = (int64_t)scale * 4;
    line_advance = (int64_t)scale * 6;
    for (i = 0u; i < text_len; ++i) {
        const uint8_t* glyph;
        int32_t row;
        if (text[i] == '\n') {
            if (pen_y > INT64_MAX - line_advance) return false;
            pen_x = origin_x;
            pen_y += line_advance;
            continue;
        }
        if (pen_x > INT64_MAX - glyph_advance ||
            pen_y > INT64_MAX - line_advance) return false;
        glyph = moo_surface_glyph((unsigned char)text[i]);
        for (row = 0; row < 5; ++row) {
            int32_t col;
            for (col = 0; col < 3; ++col) {
                int64_t gx;
                int64_t gy;
                if ((glyph[row] & (uint8_t)(1u << (2 - col))) == 0u) continue;
                gx = pen_x + (int64_t)col * scale;
                gy = pen_y + (int64_t)row * scale;
                if (gx >= INT32_MIN && gx <= INT32_MAX &&
                    gy >= INT32_MIN && gy <= INT32_MAX) {
                    if (!moo_surface_core_rect(core, (int32_t)gx, (int32_t)gy,
                                               scale, scale, color)) return false;
                }
            }
        }
        pen_x += glyph_advance;
    }
    return true;
}

bool moo_surface_core_read_pixel(const MooSurfaceCore* core, int32_t x,
                                 int32_t y, MooSurfaceColor* out_color) {
    const uint8_t* pixel;
    if (!moo_surface_core_valid(core) || !out_color ||
        x < 0 || y < 0 || x >= core->width || y >= core->height) return false;
    pixel = moo_surface_pixel_const(core, x, y);
    out_color->r = pixel[0];
    out_color->g = pixel[1];
    out_color->b = pixel[2];
    out_color->a = pixel[3];
    return true;
}

static uint64_t moo_surface_hash_byte(uint64_t hash, uint8_t byte) {
    return (hash ^ byte) * UINT64_C(1099511628211);
}

uint64_t moo_surface_core_hash(const MooSurfaceCore* core) {
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t width;
    uint32_t height;
    int32_t x;
    int32_t y;
    int i;
    if (!moo_surface_core_valid(core)) return 0u;
    width = (uint32_t)core->width;
    height = (uint32_t)core->height;
    for (i = 0; i < 4; ++i) {
        hash = moo_surface_hash_byte(hash, (uint8_t)(width & 0xffu));
        width >>= 8;
    }
    for (i = 0; i < 4; ++i) {
        hash = moo_surface_hash_byte(hash, (uint8_t)(height & 0xffu));
        height >>= 8;
    }
    for (y = 0; y < core->height; ++y) {
        for (x = 0; x < core->width; ++x) {
            const uint8_t* pixel = moo_surface_pixel_const(core, x, y);
            hash = moo_surface_hash_byte(hash, pixel[0]);
            hash = moo_surface_hash_byte(hash, pixel[1]);
            hash = moo_surface_hash_byte(hash, pixel[2]);
            hash = moo_surface_hash_byte(hash, pixel[3]);
        }
    }
    return hash;
}
