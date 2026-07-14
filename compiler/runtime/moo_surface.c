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

/* =========================================================================
 * Glass-Farbpass-Fastpath (P016-O5-AERO)
 *
 * Semantisch exakter C-Port des Farb-Passes aus
 * stdlib/ui_moo_effects.moo::uim_effekt_vorschau_zeichnen. Die Moo-Schleife
 * bleibt dort als Referenz/Fallback (_uime_farbpass_langsam); die Effekt-
 * Goldens gelten fuer beide Pfade und dienen als Paritaetsgate.
 * Two-Phase wie die Referenz: erst ALLE Reads (scratch), dann die Writes.
 * ========================================================================= */

static MooValue glass_dget(MooValue dict, const char* name) {
    /* moo_dict_get konsumiert den Key (Transfer-Semantik) — kein release. */
    return moo_dict_get(dict, moo_string_new(name));
}

static double glass_dnum(MooValue dict, const char* name, double fallback) {
    MooValue v = glass_dget(dict, name);
    double r = (v.tag == MOO_NUMBER) ? MV_NUM(v) : fallback;
    moo_release(v);
    return r;
}

/* _uime_bit_an: boden(mask / bit) % 2 == 1 */
static bool glass_bit_an(double mask, double bit) {
    if (bit <= 0.0) return false;
    return ((int64_t)floor(mask / bit)) % 2 == 1;
}

/* _uime_klemme_kanal: <0 -> 0, >255 -> 255, sonst runde() */
static uint8_t glass_klemme(double wert) {
    if (wert < 0.0) return 0u;
    if (wert > 255.0) return 255u;
    return (uint8_t)round(wert);
}

/* _uime_im_rundclip; ecken = [oben_links, oben_rechts, unten_rechts,
 * unten_links], Vergleiche wie die Moo-Referenz in double. */
static bool glass_im_rundclip(const double ecken[4], int32_t lx, int32_t ly,
                              int32_t b, int32_t h) {
    double radius;
    double cx;
    double cy;
    double bx = (double)b / 2.0;
    double by = (double)h / 2.0;
    bool in_ecke;
    double dx;
    double dy;
    if ((double)lx < bx && (double)ly < by) {
        radius = ecken[0]; cx = radius; cy = radius;
    } else if ((double)lx >= bx && (double)ly < by) {
        radius = ecken[1]; cx = (double)b - radius; cy = radius;
    } else if ((double)lx >= bx && (double)ly >= by) {
        radius = ecken[2]; cx = (double)b - radius; cy = (double)h - radius;
    } else {
        radius = ecken[3]; cx = radius; cy = (double)h - radius;
    }
    if (radius <= 0.0) return true;
    in_ecke = ((double)lx < radius || (double)lx >= (double)b - radius) &&
              ((double)ly < radius || (double)ly >= (double)h - radius);
    if (!in_ecke) return true;
    dx = (double)lx + 0.5 - cx;
    dy = (double)ly + 0.5 - cy;
    return dx * dx + dy * dy <= radius * radius;
}

typedef struct {
    int32_t x;
    int32_t y;
    MooSurfaceColor color;
} GlassScratch;

MooValue moo_surface_glass_farbpass(MooValue value, MooValue x, MooValue y,
                                    MooValue b, MooValue h, MooValue effekt) {
    MooSurface* surface = moo_surface_get(value);
    int32_t px;
    int32_t py;
    int32_t pb;
    int32_t ph;
    if (!surface || effekt.tag != MOO_DICT ||
        !moo_surface_number_i32(x, &px) ||
        !moo_surface_number_i32(y, &py) ||
        !moo_surface_number_i32(b, &pb) ||
        !moo_surface_number_i32(h, &ph) ||
        pb < 1 || ph < 1 || pb > 4096 || ph > 4096) return moo_bool(false);

    double aktiv = glass_dnum(effekt, "aktiv", 0.0);
    bool sat_an = glass_bit_an(aktiv, 8.0);
    bool tint_an = glass_bit_an(aktiv, 16.0);
    bool noise_an = glass_bit_an(aktiv, 32.0);

    MooValue hg = glass_dget(effekt, "hintergrund");
    MooValue eckd = glass_dget(effekt, "ecken");
    if (hg.tag != MOO_DICT || eckd.tag != MOO_DICT) {
        moo_release(hg);
        moo_release(eckd);
        return moo_bool(false);
    }

    double blur = glass_dnum(hg, "unschaerfe", 0.0);
    if (blur > 24.0) blur = 24.0;
    int32_t iblur = (int32_t)blur;
    double sat = glass_dnum(hg, "saettigung", 256.0) / 256.0;
    double mix = glass_dnum(hg, "toenung_mix", 0.0) / 255.0;
    double tr = 0.0;
    double tg = 0.0;
    double tb = 0.0;
    if (tint_an) {
        MooValue tf = glass_dget(hg, "toenung");
        if (tf.tag == MOO_DICT) {
            tr = glass_dnum(tf, "r", 0.0);
            tg = glass_dnum(tf, "g", 0.0);
            tb = glass_dnum(tf, "b", 0.0);
        }
        moo_release(tf);
    }
    double staerke = glass_dnum(hg, "rauschen", 0.0);
    double seed = glass_dnum(hg, "rauschen_seed", 0.0);
    double ecken[4];
    ecken[0] = glass_dnum(eckd, "oben_links", 0.0);
    ecken[1] = glass_dnum(eckd, "oben_rechts", 0.0);
    ecken[2] = glass_dnum(eckd, "unten_rechts", 0.0);
    ecken[3] = glass_dnum(eckd, "unten_links", 0.0);
    moo_release(hg);
    moo_release(eckd);

    GlassScratch* scratch =
        (GlassScratch*)malloc((size_t)pb * (size_t)ph * sizeof(GlassScratch));
    if (!scratch) return moo_bool(false);
    size_t n = 0u;
    for (int32_t ly = 0; ly < ph; ++ly) {
        for (int32_t lx = 0; lx < pb; ++lx) {
            double sum_r = 0.0;
            double sum_g = 0.0;
            double sum_b = 0.0;
            double sum_a = 0.0;
            double anzahl = 0.0;
            double rr;
            double gg;
            double bb;
            double aa;
            if (!glass_im_rundclip(ecken, lx, ly, pb, ph)) continue;
            for (int32_t sy = -iblur; sy <= iblur; ++sy) {
                for (int32_t sx = -iblur; sx <= iblur; ++sx) {
                    MooSurfaceColor q;
                    if (moo_surface_core_read_pixel(&surface->core,
                                                    px + lx + sx,
                                                    py + ly + sy, &q)) {
                        sum_r += (double)q.r;
                        sum_g += (double)q.g;
                        sum_b += (double)q.b;
                        sum_a += (double)q.a;
                        anzahl += 1.0;
                    }
                }
            }
            if (anzahl <= 0.0) continue;
            rr = sum_r / anzahl;
            gg = sum_g / anzahl;
            bb = sum_b / anzahl;
            aa = sum_a / anzahl;
            if (sat_an) {
                double lum = (rr + gg + bb) / 3.0;
                rr = lum + (rr - lum) * sat;
                gg = lum + (gg - lum) * sat;
                bb = lum + (bb - lum) * sat;
            }
            if (tint_an) {
                rr = rr * (1.0 - mix) + tr * mix;
                gg = gg * (1.0 - mix) + tg * mix;
                bb = bb * (1.0 - mix) + tb * mix;
            }
            if (noise_an) {
                double nx = (double)(px + lx);
                double ny = (double)(py + ly);
                double hx = fmod(nx * 73856093.0, 65521.0);
                double hy = fmod(ny * 19349663.0, 65521.0);
                double nv = fmod(hx * 31.0 + hy * 17.0 +
                                 fmod(seed, 65521.0), 251.0);
                double nn = (nv - 125.0) * staerke / 1020.0;
                rr += nn;
                gg += nn;
                bb += nn;
            }
            scratch[n].x = px + lx;
            scratch[n].y = py + ly;
            scratch[n].color.r = glass_klemme(rr);
            scratch[n].color.g = glass_klemme(gg);
            scratch[n].color.b = glass_klemme(bb);
            scratch[n].color.a = glass_klemme(aa);
            n++;
        }
    }
    bool ok = true;
    for (size_t i = 0u; i < n; ++i) {
        if (!moo_surface_core_rect(&surface->core, scratch[i].x, scratch[i].y,
                                   1, 1, scratch[i].color)) ok = false;
    }
    free(scratch);
    return moo_bool(ok && moo_surface_guards_ok(surface));
}
