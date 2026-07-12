#include "moo_runtime.h"
#include "moo_surface_core.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

#define MOO_SURFACE_MAGIC UINT32_C(0x53555246)
#define MOO_SURFACE_GUARD_BYTES 64u
#define MOO_SURFACE_PREFIX_BYTE UINT8_C(0xa5)
#define MOO_SURFACE_SUFFIX_BYTE UINT8_C(0x5a)

struct MooSurface {
    int32_t refcount; /* must remain first: generic moo refcount contract */
    uint32_t magic;
    uint8_t* allocation;
    size_t allocation_bytes;
    size_t pixel_bytes;
    MooSurfaceCore core;
};

static bool moo_surface_number_i32(MooValue value, int32_t* out) {
    double number;
    int32_t converted;
    if (!out || value.tag != MOO_NUMBER) return false;
    number = MV_NUM(value);
    if (!isfinite(number) ||
        number < (double)INT32_MIN || number > (double)INT32_MAX) return false;
    converted = (int32_t)number;
    if ((double)converted != number) return false;
    *out = converted;
    return true;
}

static bool moo_surface_color(MooValue r, MooValue g, MooValue b, MooValue a,
                              MooSurfaceColor* out) {
    int32_t values[4];
    if (!out ||
        !moo_surface_number_i32(r, &values[0]) ||
        !moo_surface_number_i32(g, &values[1]) ||
        !moo_surface_number_i32(b, &values[2]) ||
        !moo_surface_number_i32(a, &values[3])) return false;
    if (values[0] < 0 || values[0] > 255 ||
        values[1] < 0 || values[1] > 255 ||
        values[2] < 0 || values[2] > 255 ||
        values[3] < 0 || values[3] > 255) return false;
    out->r = (uint8_t)values[0];
    out->g = (uint8_t)values[1];
    out->b = (uint8_t)values[2];
    out->a = (uint8_t)values[3];
    return true;
}

static bool moo_surface_guards_ok(const MooSurface* surface) {
    size_t i;
    const uint8_t* suffix;
    if (!surface || surface->magic != MOO_SURFACE_MAGIC ||
        !surface->allocation) return false;
    if (surface->pixel_bytes > SIZE_MAX - 2u * MOO_SURFACE_GUARD_BYTES)
        return false;
    if (surface->allocation_bytes !=
        surface->pixel_bytes + 2u * MOO_SURFACE_GUARD_BYTES) return false;
    if (surface->core.pixels !=
        surface->allocation + MOO_SURFACE_GUARD_BYTES ||
        surface->core.buffer_bytes != surface->pixel_bytes ||
        !moo_surface_core_valid(&surface->core)) return false;
    suffix = surface->core.pixels + surface->pixel_bytes;
    for (i = 0u; i < MOO_SURFACE_GUARD_BYTES; ++i) {
        if (surface->allocation[i] != MOO_SURFACE_PREFIX_BYTE ||
            suffix[i] != MOO_SURFACE_SUFFIX_BYTE) return false;
    }
    return true;
}

static MooSurface* moo_surface_get(MooValue value) {
    MooSurface* surface;
    if (value.tag != MOO_SURFACE) return NULL;
    surface = MV_SURFACE(value);
    return moo_surface_guards_ok(surface) ? surface : NULL;
}

static MooValue moo_surface_value(MooSurface* surface) {
    MooValue value;
    value.tag = MOO_SURFACE;
    moo_val_set_ptr(&value, surface);
    return value;
}

MooValue moo_surface_new(MooValue width, MooValue height) {
    int32_t w;
    int32_t h;
    size_t stride;
    size_t pixel_bytes;
    size_t allocation_bytes;
    size_t i;
    MooSurface* surface;
    uint8_t* allocation;
    if (!moo_surface_number_i32(width, &w) ||
        !moo_surface_number_i32(height, &h) ||
        w <= 0 || h <= 0 || w > INT_MAX / 4) return moo_none();
    if ((size_t)w > SIZE_MAX / 4u) return moo_none();
    stride = (size_t)w * 4u;
    if ((size_t)h > SIZE_MAX / stride) return moo_none();
    pixel_bytes = stride * (size_t)h;
    if (pixel_bytes > (size_t)MOO_MAX_ALLOC_SIZE ||
        pixel_bytes > SIZE_MAX - 2u * MOO_SURFACE_GUARD_BYTES)
        return moo_none();
    allocation_bytes = pixel_bytes + 2u * MOO_SURFACE_GUARD_BYTES;
    surface = (MooSurface*)malloc(sizeof(MooSurface));
    if (!surface) return moo_none();
    allocation = (uint8_t*)malloc(allocation_bytes);
    if (!allocation) {
        free(surface);
        return moo_none();
    }
    for (i = 0u; i < MOO_SURFACE_GUARD_BYTES; ++i)
        allocation[i] = MOO_SURFACE_PREFIX_BYTE;
    for (i = 0u; i < pixel_bytes; ++i)
        allocation[MOO_SURFACE_GUARD_BYTES + i] = 0u;
    for (i = 0u; i < MOO_SURFACE_GUARD_BYTES; ++i)
        allocation[MOO_SURFACE_GUARD_BYTES + pixel_bytes + i] =
            MOO_SURFACE_SUFFIX_BYTE;
    surface->refcount = 1;
    surface->magic = MOO_SURFACE_MAGIC;
    surface->allocation = allocation;
    surface->allocation_bytes = allocation_bytes;
    surface->pixel_bytes = pixel_bytes;
    if (!moo_surface_core_init(&surface->core,
                               allocation + MOO_SURFACE_GUARD_BYTES,
                               pixel_bytes, w, h, stride)) {
        free(allocation);
        free(surface);
        return moo_none();
    }
    return moo_surface_value(surface);
}

void moo_surface_free(void* ptr) {
    MooSurface* surface = (MooSurface*)ptr;
    if (!surface) return;
    if (surface->magic != MOO_SURFACE_MAGIC) return;
    surface->magic = 0u;
    surface->core.pixels = NULL;
    if (surface->allocation) {
        free(surface->allocation);
        surface->allocation = NULL;
    }
    free(surface);
}

MooValue moo_surface_clear(MooValue value, MooValue r, MooValue g,
                           MooValue b, MooValue a) {
    MooSurface* surface = moo_surface_get(value);
    MooSurfaceColor color;
    bool ok;
    if (!surface || !moo_surface_color(r, g, b, a, &color))
        return moo_bool(false);
    ok = moo_surface_core_clear(&surface->core, color);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}

MooValue moo_surface_clip_push(MooValue value, MooValue x, MooValue y,
                               MooValue width, MooValue height) {
    MooSurface* surface = moo_surface_get(value);
    int32_t ix;
    int32_t iy;
    int32_t iw;
    int32_t ih;
    bool ok;
    if (!surface ||
        !moo_surface_number_i32(x, &ix) ||
        !moo_surface_number_i32(y, &iy) ||
        !moo_surface_number_i32(width, &iw) ||
        !moo_surface_number_i32(height, &ih)) return moo_bool(false);
    ok = moo_surface_core_clip_push(&surface->core, ix, iy, iw, ih);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}

MooValue moo_surface_clip_pop(MooValue value) {
    MooSurface* surface = moo_surface_get(value);
    bool ok;
    if (!surface) return moo_bool(false);
    ok = moo_surface_core_clip_pop(&surface->core);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}

MooValue moo_surface_rect(MooValue value, MooValue x, MooValue y,
                          MooValue width, MooValue height, MooValue r,
                          MooValue g, MooValue b, MooValue a) {
    MooSurface* surface = moo_surface_get(value);
    MooSurfaceColor color;
    int32_t ix;
    int32_t iy;
    int32_t iw;
    int32_t ih;
    bool ok;
    if (!surface ||
        !moo_surface_number_i32(x, &ix) ||
        !moo_surface_number_i32(y, &iy) ||
        !moo_surface_number_i32(width, &iw) ||
        !moo_surface_number_i32(height, &ih) ||
        !moo_surface_color(r, g, b, a, &color)) return moo_bool(false);
    ok = moo_surface_core_rect(&surface->core, ix, iy, iw, ih, color);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}

MooValue moo_surface_roundrect(MooValue value, MooValue x, MooValue y,
                               MooValue width, MooValue height, MooValue radius,
                               MooValue r, MooValue g, MooValue b, MooValue a) {
    MooSurface* surface = moo_surface_get(value);
    MooSurfaceColor color;
    int32_t ix;
    int32_t iy;
    int32_t iw;
    int32_t ih;
    int32_t iradius;
    bool ok;
    if (!surface ||
        !moo_surface_number_i32(x, &ix) ||
        !moo_surface_number_i32(y, &iy) ||
        !moo_surface_number_i32(width, &iw) ||
        !moo_surface_number_i32(height, &ih) ||
        !moo_surface_number_i32(radius, &iradius) ||
        !moo_surface_color(r, g, b, a, &color)) return moo_bool(false);
    ok = moo_surface_core_roundrect(&surface->core, ix, iy, iw, ih,
                                    iradius, color);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}

MooValue moo_surface_circle(MooValue value, MooValue cx, MooValue cy,
                            MooValue radius, MooValue r, MooValue g,
                            MooValue b, MooValue a) {
    MooSurface* surface = moo_surface_get(value);
    MooSurfaceColor color;
    int32_t icx;
    int32_t icy;
    int32_t iradius;
    bool ok;
    if (!surface ||
        !moo_surface_number_i32(cx, &icx) ||
        !moo_surface_number_i32(cy, &icy) ||
        !moo_surface_number_i32(radius, &iradius) ||
        !moo_surface_color(r, g, b, a, &color)) return moo_bool(false);
    ok = moo_surface_core_circle(&surface->core, icx, icy, iradius, color);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}

MooValue moo_surface_line(MooValue value, MooValue x0, MooValue y0,
                          MooValue x1, MooValue y1, MooValue r, MooValue g,
                          MooValue b, MooValue a) {
    MooSurface* surface = moo_surface_get(value);
    MooSurfaceColor color;
    int32_t ix0;
    int32_t iy0;
    int32_t ix1;
    int32_t iy1;
    bool ok;
    if (!surface ||
        !moo_surface_number_i32(x0, &ix0) ||
        !moo_surface_number_i32(y0, &iy0) ||
        !moo_surface_number_i32(x1, &ix1) ||
        !moo_surface_number_i32(y1, &iy1) ||
        !moo_surface_color(r, g, b, a, &color)) return moo_bool(false);
    ok = moo_surface_core_line(&surface->core, ix0, iy0, ix1, iy1, color);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}

MooValue moo_surface_read_pixel(MooValue value, MooValue x, MooValue y) {
    MooSurface* surface = moo_surface_get(value);
    MooSurfaceColor color;
    int32_t ix;
    int32_t iy;
    MooValue dict;
    if (!surface ||
        !moo_surface_number_i32(x, &ix) ||
        !moo_surface_number_i32(y, &iy) ||
        !moo_surface_core_read_pixel(&surface->core, ix, iy, &color) ||
        !moo_surface_guards_ok(surface)) return moo_none();
    dict = moo_dict_new();
    moo_dict_set(dict, moo_string_new("rot"), moo_number((double)color.r));
    moo_dict_set(dict, moo_string_new("gruen"), moo_number((double)color.g));
    moo_dict_set(dict, moo_string_new("blau"), moo_number((double)color.b));
    moo_dict_set(dict, moo_string_new("alpha"), moo_number((double)color.a));
    return dict;
}

MooValue moo_surface_hash(MooValue value) {
    static const char hex[] = "0123456789abcdef";
    MooSurface* surface = moo_surface_get(value);
    uint64_t hash;
    char text[17];
    int i;
    if (!surface) return moo_none();
    hash = moo_surface_core_hash(&surface->core);
    if (!moo_surface_guards_ok(surface)) return moo_none();
    for (i = 15; i >= 0; --i) {
        text[i] = hex[hash & 0x0fu];
        hash >>= 4;
    }
    text[16] = '\0';
    return moo_string_new(text);
}

MooValue moo_surface_snapshot_to_frame(MooValue value) {
    MooSurface* surface = moo_surface_get(value);
    uint8_t* pixels;
    size_t i;
    if (!surface) return moo_none();
    pixels = (uint8_t*)malloc(surface->pixel_bytes);
    if (!pixels) return moo_none();
    for (i = 0u; i < surface->pixel_bytes; ++i)
        pixels[i] = surface->core.pixels[i];
    if (!moo_surface_guards_ok(surface)) {
        free(pixels);
        return moo_none();
    }
    return moo_frame_new_take(surface->core.width, surface->core.height, pixels);
}
