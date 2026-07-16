#include "moo_compositor_effects_cpu.h"

#define MOO_FX_HASH_OFFSET UINT64_C(1469598103934665603)
#define MOO_FX_HASH_PRIME UINT64_C(1099511628211)
#define MOO_FX_BACKDROP_MASK \
    (MOO_COMP_EFFECT_BACKDROP_BLUR | MOO_COMP_EFFECT_SATURATION | \
     MOO_COMP_EFFECT_TINT | MOO_COMP_EFFECT_NOISE)

static MooCompResult moo_fx_geometry_preflight(
    const MooCompEffectCpuJob *job);

#define MOO_FX_EFFECT_MASK MOO_COMP_EFFECTS_V2
static uint8_t moo_fx_clamp_u8(int64_t v) {
    if (v < 0) return 0u;
    if (v > 255) return 255u;
    return (uint8_t)v;
}

static uint64_t moo_fx_round_u64(uint64_t n, uint64_t d) {
    return (n + d / UINT64_C(2)) / d;
}

static int64_t moo_fx_round_signed(int64_t n, int64_t d) {
    uint64_t mag;
    uint64_t q;
    if (n >= 0) return (int64_t)moo_fx_round_u64((uint64_t)n, (uint64_t)d);
    mag = (uint64_t)(-(n + 1)) + UINT64_C(1);
    q = moo_fx_round_u64(mag, (uint64_t)d);
    return -(int64_t)q;
}

static int moo_fx_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0u && b > UINT64_MAX / a) return 0;
    *out = a * b;
    return 1;
}

static int moo_fx_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) return 0;
    *out = a + b;
    return 1;
}

static int moo_fx_view_valid(const uint8_t *pixels, size_t bytes,
                             size_t stride, int32_t width, int32_t height) {
    size_t row_bytes;
    if (!pixels || width <= 0 || height <= 0) return 0;
    if ((size_t)width > SIZE_MAX / 4u) return 0;
    row_bytes = (size_t)width * 4u;
    if (stride < row_bytes) return 0;
    if ((size_t)height > SIZE_MAX / stride) return 0;
    return stride * (size_t)height <= bytes;
}

static int moo_fx_ranges_overlap(const void *a, uint64_t a_bytes,
                                 const void *b, uint64_t b_bytes) {
    uintptr_t ap = (uintptr_t)a;
    uintptr_t bp = (uintptr_t)b;
    uintptr_t ae;
    uintptr_t be;
    if (!a || !b || a_bytes == 0u || b_bytes == 0u) return 0;
    if (a_bytes > (uint64_t)UINTPTR_MAX - ap ||
        b_bytes > (uint64_t)UINTPTR_MAX - bp) return 1;
    ae = ap + (uintptr_t)a_bytes;
    be = bp + (uintptr_t)b_bytes;
    return ap < be && bp < ae;
}

static int moo_fx_rect_valid(MooCompRect r) {
    int64_t x1;
    int64_t y1;
    if (r.width <= 0 || r.height <= 0) return 0;
    x1 = (int64_t)r.x + (int64_t)r.width;
    y1 = (int64_t)r.y + (int64_t)r.height;
    return x1 >= INT32_MIN && x1 <= INT32_MAX &&
           y1 >= INT32_MIN && y1 <= INT32_MAX;
}

static int moo_fx_magnitude_ok(int32_t value, int32_t limit) {
    int64_t v = value;
    return v >= -(int64_t)limit && v <= (int64_t)limit;
}

static int moo_fx_effect_valid(const MooCompEffectState *e) {
    if ((e->enabled_mask & ~MOO_FX_EFFECT_MASK) != 0u ||
        (e->required_mask & ~e->enabled_mask) != 0u ||
        e->fallback_policy > MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE ||
        e->reserved != 0u || e->backdrop.reserved != 0u)
        return 0;
    if (e->corners.top_left > MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS ||
        e->corners.top_right > MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS ||
        e->corners.bottom_right > MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS ||
        e->corners.bottom_left > MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS ||
        e->shadow.blur_radius >
            MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_BLUR_RADIUS ||
        e->shadow.spread_radius >
            MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_SPREAD_RADIUS ||
        e->backdrop.blur_radius >
            MOO_COMP_EFFECT_DEFAULT_MAX_BACKDROP_BLUR_RADIUS ||
        e->backdrop.saturation_q8_8 >
            MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8 ||
        !moo_fx_magnitude_ok(e->shadow.offset_x,
            MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET) ||
        !moo_fx_magnitude_ok(e->shadow.offset_y,
            MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET))
        return 0;
    if ((e->enabled_mask & MOO_COMP_EFFECT_AFFINE_2D) != 0u &&
        (!moo_fx_magnitude_ok(e->affine.m11,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT) ||
         !moo_fx_magnitude_ok(e->affine.m12,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT) ||
         !moo_fx_magnitude_ok(e->affine.m21,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT) ||
         !moo_fx_magnitude_ok(e->affine.m22,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT) ||
         !moo_fx_magnitude_ok(e->affine.tx,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION) ||
         !moo_fx_magnitude_ok(e->affine.ty,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION) ||
         !moo_fx_magnitude_ok(e->affine.origin_x,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN) ||
         !moo_fx_magnitude_ok(e->affine.origin_y,
             MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN)))
        return 0;
    return 1;
}


static MooCompAffine2D moo_fx_identity(void) {
    MooCompAffine2D a;
    a.m11 = MOO_COMP_Q16_ONE; a.m12 = 0;
    a.m21 = 0; a.m22 = MOO_COMP_Q16_ONE;
    a.tx = 0; a.ty = 0; a.origin_x = 0; a.origin_y = 0;
    return a;
}

static uint8_t *moo_fx_target_pixel(const MooCompEffectCpuTarget *v,
                                    int32_t x, int32_t y) {
    return v->pixels + (size_t)y * v->stride + (size_t)x * 4u;
}

static const uint8_t *moo_fx_source_pixel(const MooCompEffectCpuSource *v,
                                          int32_t x, int32_t y) {
    return v->pixels + (size_t)y * v->stride + (size_t)x * 4u;
}

static void moo_fx_over(uint8_t *dst, const uint8_t src[4]) {
    uint64_t sa = src[3];
    uint64_t da = dst[3];
    uint64_t inv = UINT64_C(255) - sa;
    uint64_t a = sa * UINT64_C(255) + da * inv;
    uint32_t c;
    uint8_t out[4];
    out[3] = (uint8_t)moo_fx_round_u64(a, UINT64_C(255));
    for (c = 0u; c < 3u; ++c) {
        uint64_t u = (uint64_t)src[c] * sa * UINT64_C(255) +
                     (uint64_t)dst[c] * da * inv;
        out[c] = a == 0u ? 0u : (uint8_t)moo_fx_round_u64(u, a);
    }
    dst[0] = out[0]; dst[1] = out[1]; dst[2] = out[2]; dst[3] = out[3];
}

uint32_t moo_comp_effect_cpu_noise_hash(
    uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = seed ^ x * UINT32_C(0x9e3779b1) ^
                 y * UINT32_C(0x85ebca77);
    h ^= h >> 16;
    h *= UINT32_C(0x7feb352d);
    h ^= h >> 15;
    h *= UINT32_C(0x846ca68b);
    h ^= h >> 16;
    return h;
}

static uint64_t moo_fx_hash_byte(uint64_t h, uint8_t byte) {
    return (h ^ (uint64_t)byte) * MOO_FX_HASH_PRIME;
}

static uint64_t moo_fx_hash_u32(uint64_t h, uint32_t v) {
    h = moo_fx_hash_byte(h, (uint8_t)(v & 255u));
    h = moo_fx_hash_byte(h, (uint8_t)((v >> 8) & 255u));
    h = moo_fx_hash_byte(h, (uint8_t)((v >> 16) & 255u));
    return moo_fx_hash_byte(h, (uint8_t)(v >> 24));
}

MooCompResult moo_comp_effect_cpu_hash_rgba(
    const MooCompEffectCpuSource *image, uint64_t *out_hash) {
    uint64_t h = MOO_FX_HASH_OFFSET;
    int32_t y;
    if (!image || !out_hash ||
        !moo_fx_view_valid(image->pixels, image->buffer_bytes, image->stride,
                           image->width, image->height))
        return MOO_COMP_INVALID;
    h = moo_fx_hash_u32(h, (uint32_t)image->width);
    h = moo_fx_hash_u32(h, (uint32_t)image->height);
    for (y = 0; y < image->height; ++y) {
        const uint8_t *row = image->pixels + (size_t)y * image->stride;
        size_t x;
        for (x = 0u; x < (size_t)image->width * 4u; ++x)
            h = moo_fx_hash_byte(h, row[x]);
    }
    *out_hash = h;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_cpu_requirements(
    const MooCompEffectCpuJob *job, MooCompEffectCpuStats *out_stats) {
    MooCompEffectCpuStats s = {0u, 0u, 0u, 0u, 0u};
    uint64_t pixels;
    uint64_t term;
    uint64_t mask;
    uint64_t content_bytes;
    uint64_t lower_z_bytes;
    MooCompResult geometry_result;
    if (!job || !out_stats ||
        !moo_fx_view_valid(job->content.pixels, job->content.buffer_bytes,
                           job->content.stride, job->content.width,
                           job->content.height) ||
        !moo_fx_view_valid(job->target.pixels, job->target.buffer_bytes,
                           job->target.stride, job->target.width,
                           job->target.height) ||
        (job->lower_z.pixels != NULL &&
         (!moo_fx_view_valid(job->lower_z.pixels, job->lower_z.buffer_bytes,
                            job->lower_z.stride, job->lower_z.width,
                            job->lower_z.height) ||
          job->lower_z.width != job->target.width ||
          job->lower_z.height != job->target.height)) ||
        !moo_fx_rect_valid(job->content_rect) ||
        job->content_scale == 0u || job->reserved_scale != 0u ||
        (uint64_t)(uint32_t)job->content_rect.width *
            (uint64_t)job->content_scale !=
            (uint64_t)(uint32_t)job->content.width ||
        (uint64_t)(uint32_t)job->content_rect.height *
            (uint64_t)job->content_scale !=
            (uint64_t)(uint32_t)job->content.height)
        return MOO_COMP_INVALID;
    if (job->reserved[0] || job->reserved[1] || job->reserved[2] ||
        job->reserved[3] || job->reserved[4] || job->reserved[5] ||
        job->reserved[6])
        return MOO_COMP_INVALID;
    mask = job->effect.enabled_mask;
    if (!moo_fx_effect_valid(&job->effect))
        return MOO_COMP_INVALID;
    content_bytes = (uint64_t)job->content.stride *
                    (uint64_t)(uint32_t)job->content.height;
    lower_z_bytes = job->lower_z.pixels == NULL ? 0u :
        (uint64_t)job->lower_z.stride *
        (uint64_t)(uint32_t)job->lower_z.height;
    if ((mask & MOO_FX_BACKDROP_MASK) != 0u &&
        job->lower_z.pixels != NULL &&
        moo_fx_ranges_overlap(job->lower_z.pixels, lower_z_bytes,
                              job->content.pixels, content_bytes))
        return MOO_COMP_INVALID;
    if (job->target.width > 32767 || job->target.height > 32767)
        return MOO_COMP_LIMIT;
    geometry_result = moo_fx_geometry_preflight(job);
    if (geometry_result != MOO_COMP_OK)
        return geometry_result;
    if (!moo_fx_mul_u64((uint64_t)(uint32_t)job->content_rect.width,
                        (uint64_t)(uint32_t)job->content_rect.height, &pixels))
        return MOO_COMP_LIMIT;
    s.affected_pixels = pixels;
#define MOO_FX_ADD_WORK(bit, cost) do {                                      \
    if ((mask & (bit)) != 0u) {                                              \
        if (!moo_fx_mul_u64(pixels, (cost), &term) ||                         \
            !moo_fx_add_u64(s.work_units, term, &s.work_units))              \
            return MOO_COMP_LIMIT;                                           \
    }                                                                        \
} while (0)
    MOO_FX_ADD_WORK(MOO_COMP_EFFECT_CORNER_CLIP, UINT64_C(1));
    MOO_FX_ADD_WORK(MOO_COMP_EFFECT_SHADOW, UINT64_C(4));
    MOO_FX_ADD_WORK(MOO_COMP_EFFECT_BACKDROP_BLUR, UINT64_C(4));
    /* S1 charges every enabled color stage independently. */
    MOO_FX_ADD_WORK(MOO_COMP_EFFECT_SATURATION, UINT64_C(1));
    MOO_FX_ADD_WORK(MOO_COMP_EFFECT_TINT, UINT64_C(1));
    MOO_FX_ADD_WORK(MOO_COMP_EFFECT_NOISE, UINT64_C(1));
    MOO_FX_ADD_WORK(MOO_COMP_EFFECT_AFFINE_2D, UINT64_C(2));
#undef MOO_FX_ADD_WORK
    if ((mask & MOO_FX_BACKDROP_MASK) != 0u &&
        (mask & MOO_COMP_EFFECT_SHADOW) != 0u &&
        job->lower_z.pixels == NULL)
        return MOO_COMP_INVALID;
    if ((mask & MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u) {
        if (!moo_fx_mul_u64(pixels, UINT64_C(4),
                            &s.rgba_words_per_buffer))
            return MOO_COMP_LIMIT;
    } else if ((mask & MOO_COMP_EFFECT_SHADOW) != 0u &&
               job->effect.shadow.blur_radius != 0u) {
        s.rgba_words_per_buffer = pixels;
    }
    if (!moo_fx_mul_u64(s.rgba_words_per_buffer, UINT64_C(8),
                        &s.scratch_bytes))
        return MOO_COMP_LIMIT;
    s.mask_words_per_buffer = 0u;
    *out_stats = s;
    return MOO_COMP_OK;
}

static int32_t moo_fx_q16_floor(MooCompQ16 q) {
    int64_t v = q;
    if (v >= 0) return (int32_t)(v / INT64_C(65536));
    return (int32_t)(-(((-v) + INT64_C(65535)) / INT64_C(65536)));
}

static int moo_fx_output_point(int32_t x, int32_t y,
                               MooCompEffectPointQ16 *point) {
    int64_t px = (int64_t)x * INT64_C(65536) + INT64_C(32768);
    int64_t py = (int64_t)y * INT64_C(65536) + INT64_C(32768);
    if (px > INT32_MAX || py > INT32_MAX) return 0;
    point->x = (MooCompQ16)px;
    point->y = (MooCompQ16)py;
    return 1;
}

static MooCompResult moo_fx_local_sample_checked(
                               const MooCompAffine2D *inverse,
                               MooCompRect rect, int32_t x, int32_t y,
                               int32_t offset_x, int32_t offset_y,
                               int32_t *sx, int32_t *sy) {
    MooCompEffectPointQ16 p;
    MooCompEffectPointQ16 local;
    MooCompResult result;
    int64_t px;
    int64_t py;
    int64_t local_x;
    int64_t local_y;
    if (!moo_fx_output_point(x, y, &p)) return MOO_COMP_LIMIT;
    px = (int64_t)p.x - (int64_t)offset_x * INT64_C(65536);
    py = (int64_t)p.y - (int64_t)offset_y * INT64_C(65536);
    if (px < INT32_MIN || px > INT32_MAX ||
        py < INT32_MIN || py > INT32_MAX)
        return MOO_COMP_LIMIT;
    p.x = (MooCompQ16)px;
    p.y = (MooCompQ16)py;
    result = moo_comp_effect_affine_transform_point(inverse, p, &local);
    if (result != MOO_COMP_OK) return result;
    local_x = (int64_t)moo_fx_q16_floor(local.x) - (int64_t)rect.x;
    local_y = (int64_t)moo_fx_q16_floor(local.y) - (int64_t)rect.y;
    if (local_x < INT32_MIN || local_x > INT32_MAX ||
        local_y < INT32_MIN || local_y > INT32_MAX)
        return MOO_COMP_LIMIT;
    *sx = (int32_t)local_x;
    *sy = (int32_t)local_y;
    return MOO_COMP_OK;
}

static int moo_fx_local_sample(const MooCompAffine2D *inverse,
                               MooCompRect rect, int32_t x, int32_t y,
                               int32_t offset_x, int32_t offset_y,
                               int32_t *sx, int32_t *sy) {
    return moo_fx_local_sample_checked(
        inverse, rect, x, y, offset_x, offset_y, sx, sy) == MOO_COMP_OK;
}

static MooCompResult moo_fx_geometry_preflight(
    const MooCompEffectCpuJob *job) {
    MooCompAffine2D affine =
        (job->effect.enabled_mask & MOO_COMP_EFFECT_AFFINE_2D) != 0u ?
        job->effect.affine : moo_fx_identity();
    MooCompAffine2D inverse;
    MooCompCorners normalized;
    MooCompCorners spread_corners;
    MooCompRect spread_rect = job->content_rect;
    MooCompResult result;
    int32_t xs[2] = {0, job->target.width - 1};
    int32_t ys[2] = {0, job->target.height - 1};
    uint32_t ix;
    uint32_t iy;
    int32_t sx;
    int32_t sy;

    result = moo_comp_effect_affine_inverse(&affine, &inverse);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_effect_corners_normalize(
        job->content_rect.width, job->content_rect.height,
        &job->effect.corners, &normalized);
    if (result != MOO_COMP_OK) return result;
    for (iy = 0u; iy < 2u; ++iy)
        for (ix = 0u; ix < 2u; ++ix) {
            result = moo_fx_local_sample_checked(
                &inverse, job->content_rect, xs[ix], ys[iy], 0, 0, &sx, &sy);
            if (result != MOO_COMP_OK) return result;
        }

    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_SHADOW) != 0u) {
        uint32_t spread = job->effect.shadow.spread_radius;
        uint32_t radius;
        result = moo_comp_effect_rect_expand(
            job->content_rect, (int32_t)spread, &spread_rect);
        if (result != MOO_COMP_OK) return result;
        spread_corners = job->effect.corners;
#define MOO_FX_SPREAD_CORNER(field) do {                                     \
    radius = (uint32_t)spread_corners.field + spread;                         \
    if (radius > UINT16_MAX) return MOO_COMP_LIMIT;                           \
    spread_corners.field = (uint16_t)radius;                                  \
} while (0)
        MOO_FX_SPREAD_CORNER(top_left);
        MOO_FX_SPREAD_CORNER(top_right);
        MOO_FX_SPREAD_CORNER(bottom_right);
        MOO_FX_SPREAD_CORNER(bottom_left);
#undef MOO_FX_SPREAD_CORNER
        result = moo_comp_effect_corners_normalize(
            spread_rect.width, spread_rect.height,
            &spread_corners, &normalized);
        if (result != MOO_COMP_OK) return result;
        for (iy = 0u; iy < 2u; ++iy)
            for (ix = 0u; ix < 2u; ++ix) {
                result = moo_fx_local_sample_checked(
                    &inverse, spread_rect, xs[ix], ys[iy],
                    job->effect.shadow.offset_x,
                    job->effect.shadow.offset_y, &sx, &sy);
                if (result != MOO_COMP_OK) return result;
            }
    }
    return MOO_COMP_OK;
}


static void moo_fx_box_mask_h(const uint32_t *src, uint32_t *dst,
                              int32_t w, int32_t h, uint32_t radius) {
    int32_t y;
    uint64_t d = (uint64_t)radius * UINT64_C(2) + UINT64_C(1);
    for (y = 0; y < h; ++y) {
        uint64_t sum = 0u;
        int64_t k;
        int32_t x;
        for (k = -(int64_t)radius; k <= (int64_t)radius; ++k)
            if (k >= 0 && k < w) sum += src[(uint64_t)y * (uint64_t)w +
                                               (uint64_t)k];
        for (x = 0; x < w; ++x) {
            int64_t outgoing = (int64_t)x - (int64_t)radius;
            int64_t incoming = (int64_t)x + (int64_t)radius + 1;
            dst[(uint64_t)y * (uint64_t)w + (uint64_t)x] =
                (uint32_t)moo_fx_round_u64(sum, d);
            if (outgoing >= 0 && outgoing < w)
                sum -= src[(uint64_t)y * (uint64_t)w +
                           (uint64_t)outgoing];
            if (incoming >= 0 && incoming < w)
                sum += src[(uint64_t)y * (uint64_t)w +
                           (uint64_t)incoming];
        }
    }
}

static void moo_fx_box_mask_v(const uint32_t *src, uint32_t *dst,
                              int32_t w, int32_t h, uint32_t radius) {
    int32_t x;
    uint64_t d = (uint64_t)radius * UINT64_C(2) + UINT64_C(1);
    for (x = 0; x < w; ++x) {
        uint64_t sum = 0u;
        int64_t k;
        int32_t y;
        for (k = -(int64_t)radius; k <= (int64_t)radius; ++k)
            if (k >= 0 && k < h) sum += src[(uint64_t)k * (uint64_t)w +
                                               (uint64_t)x];
        for (y = 0; y < h; ++y) {
            int64_t outgoing = (int64_t)y - (int64_t)radius;
            int64_t incoming = (int64_t)y + (int64_t)radius + 1;
            dst[(uint64_t)y * (uint64_t)w + (uint64_t)x] =
                (uint32_t)moo_fx_round_u64(sum, d);
            if (outgoing >= 0 && outgoing < h)
                sum -= src[(uint64_t)outgoing * (uint64_t)w +
                           (uint64_t)x];
            if (incoming >= 0 && incoming < h)
                sum += src[(uint64_t)incoming * (uint64_t)w +
                           (uint64_t)x];
        }
    }
}

static int32_t moo_fx_clamp_coord(int64_t v, int32_t upper) {
    if (v < 0) return 0;
    if (v >= upper) return upper - 1;
    return (int32_t)v;
}

static void moo_fx_box_rgba_h(const uint32_t *src, uint32_t *dst,
                              int32_t w, int32_t h, uint32_t radius) {
    int32_t y;
    uint64_t d = (uint64_t)radius * UINT64_C(2) + UINT64_C(1);
    for (y = 0; y < h; ++y) {
        uint64_t sum[4] = {0u, 0u, 0u, 0u};
        int64_t k;
        int32_t x;
        uint32_t c;
        for (k = -(int64_t)radius; k <= (int64_t)radius; ++k) {
            int32_t sx = moo_fx_clamp_coord(k, w);
            uint64_t i = ((uint64_t)y * (uint64_t)w + (uint64_t)sx) * 4u;
            for (c = 0u; c < 4u; ++c) sum[c] += src[i + c];
        }
        for (x = 0; x < w; ++x) {
            uint64_t di = ((uint64_t)y * (uint64_t)w + (uint64_t)x) * 4u;
            int32_t out_x = moo_fx_clamp_coord(
                (int64_t)x - (int64_t)radius, w);
            int32_t in_x = moo_fx_clamp_coord(
                (int64_t)x + (int64_t)radius + 1, w);
            uint64_t oi = ((uint64_t)y * (uint64_t)w +
                           (uint64_t)out_x) * 4u;
            uint64_t ii = ((uint64_t)y * (uint64_t)w +
                           (uint64_t)in_x) * 4u;
            for (c = 0u; c < 4u; ++c) {
                dst[di + c] = (uint32_t)moo_fx_round_u64(sum[c], d);
                sum[c] -= src[oi + c];
                sum[c] += src[ii + c];
            }
        }
    }
}

static void moo_fx_box_rgba_v(const uint32_t *src, uint32_t *dst,
                              int32_t w, int32_t h, uint32_t radius) {
    int32_t x;
    uint64_t d = (uint64_t)radius * UINT64_C(2) + UINT64_C(1);
    for (x = 0; x < w; ++x) {
        uint64_t sum[4] = {0u, 0u, 0u, 0u};
        int64_t k;
        int32_t y;
        uint32_t c;
        for (k = -(int64_t)radius; k <= (int64_t)radius; ++k) {
            int32_t sy = moo_fx_clamp_coord(k, h);
            uint64_t i = ((uint64_t)sy * (uint64_t)w + (uint64_t)x) * 4u;
            for (c = 0u; c < 4u; ++c) sum[c] += src[i + c];
        }
        for (y = 0; y < h; ++y) {
            uint64_t di = ((uint64_t)y * (uint64_t)w + (uint64_t)x) * 4u;
            int32_t out_y = moo_fx_clamp_coord(
                (int64_t)y - (int64_t)radius, h);
            int32_t in_y = moo_fx_clamp_coord(
                (int64_t)y + (int64_t)radius + 1, h);
            uint64_t oi = ((uint64_t)out_y * (uint64_t)w +
                           (uint64_t)x) * 4u;
            uint64_t ii = ((uint64_t)in_y * (uint64_t)w +
                           (uint64_t)x) * 4u;
            for (c = 0u; c < 4u; ++c) {
                dst[di + c] = (uint32_t)moo_fx_round_u64(sum[c], d);
                sum[c] -= src[oi + c];
                sum[c] += src[ii + c];
            }
        }
    }
}

static void moo_fx_capture(const MooCompEffectCpuSource *source,
                           uint32_t *rgba) {
    int32_t y;
    for (y = 0; y < source->height; ++y) {
        int32_t x;
        for (x = 0; x < source->width; ++x) {
            const uint8_t *p = moo_fx_source_pixel(source, x, y);
            uint64_t i = ((uint64_t)y * (uint64_t)source->width +
                          (uint64_t)x) * 4u;
            rgba[i] = (uint32_t)p[0] * p[3];
            rgba[i + 1u] = (uint32_t)p[1] * p[3];
            rgba[i + 2u] = (uint32_t)p[2] * p[3];
            rgba[i + 3u] = p[3];
        }
    }
}

static uint8_t moo_fx_coverage(const MooCompEffectCpuJob *job,
                               const MooCompAffine2D *inverse,
                               const MooCompCorners *corners,
                               int32_t x, int32_t y) {
    int32_t sx;
    int32_t sy;
    uint8_t coverage = 0u;
    if (!moo_fx_local_sample(inverse, job->content_rect, x, y, 0, 0,
                             &sx, &sy) ||
        sx < 0 || sy < 0 ||
        sx >= job->content.width || sy >= job->content.height)
        return 0u;
    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_CORNER_CLIP) == 0u)
        return 255u;
    if (moo_comp_effect_rounded_coverage_a8(
            job->content.width, job->content.height, corners,
            sx, sy, &coverage) != MOO_COMP_OK)
        return 0u;
    return coverage;
}

static void moo_fx_render_shadow(const MooCompEffectCpuJob *job,
                                 const MooCompAffine2D *inverse,
                                 uint32_t *ping, uint32_t *pong) {
    MooCompRect spread_rect = job->content_rect;
    MooCompCorners spread_corners = job->effect.corners;
    MooCompCorners normalized;
    int32_t w = job->target.width;
    int32_t h = job->target.height;
    uint32_t spread = job->effect.shadow.spread_radius;
    int32_t y;
    if (spread != 0u) {
        if (moo_comp_effect_rect_expand(job->content_rect, (int32_t)spread,
                                        &spread_rect) != MOO_COMP_OK)
            return;
        spread_corners.top_left = (uint16_t)(spread_corners.top_left + spread);
        spread_corners.top_right = (uint16_t)(spread_corners.top_right + spread);
        spread_corners.bottom_right =
            (uint16_t)(spread_corners.bottom_right + spread);
        spread_corners.bottom_left =
            (uint16_t)(spread_corners.bottom_left + spread);
    }
    if (moo_comp_effect_corners_normalize(
            spread_rect.width, spread_rect.height,
            &spread_corners, &normalized) != MOO_COMP_OK)
        return;
    if (job->effect.shadow.blur_radius == 0u) {
        for (y = 0; y < h; ++y) {
            int32_t x;
            for (x = 0; x < w; ++x) {
                int32_t sx;
                int32_t sy;
                uint8_t a = 0u;
                uint8_t src[4];
                if (moo_fx_local_sample(
                        inverse, spread_rect, x, y,
                        job->effect.shadow.offset_x,
                        job->effect.shadow.offset_y, &sx, &sy) &&
                    sx >= 0 && sy >= 0 &&
                    sx < spread_rect.width && sy < spread_rect.height) {
                    if ((job->effect.enabled_mask &
                         MOO_COMP_EFFECT_CORNER_CLIP) == 0u)
                        a = 255u;
                    else
                        (void)moo_comp_effect_rounded_coverage_a8(
                            spread_rect.width, spread_rect.height, &normalized,
                            sx, sy, &a);
                }
                if (a == 0u) continue;
                src[0] = job->effect.shadow.color.r;
                src[1] = job->effect.shadow.color.g;
                src[2] = job->effect.shadow.color.b;
                src[3] = (uint8_t)moo_fx_round_u64(
                    (uint64_t)job->effect.shadow.color.a * a,
                    UINT64_C(255));
                moo_fx_over(moo_fx_target_pixel(&job->target, x, y), src);
            }
        }
        return;
    }


    for (y = 0; y < h; ++y) {
        int32_t x;
        for (x = 0; x < w; ++x) {
            int32_t sx;
            int32_t sy;
            uint8_t a = 0u;
            if (moo_fx_local_sample(
                    inverse, spread_rect, x, y,
                    job->effect.shadow.offset_x,
                    job->effect.shadow.offset_y, &sx, &sy) &&
                sx >= 0 && sy >= 0 &&
                sx < spread_rect.width && sy < spread_rect.height) {
                if ((job->effect.enabled_mask &
                     MOO_COMP_EFFECT_CORNER_CLIP) == 0u)
                    a = 255u;
                else
                    (void)moo_comp_effect_rounded_coverage_a8(
                        spread_rect.width, spread_rect.height, &normalized,
                        sx, sy, &a);
            }
            ping[(uint64_t)y * (uint64_t)w + (uint64_t)x] = a;
        }
    }
    moo_fx_box_mask_h(ping, pong, w, h, job->effect.shadow.blur_radius);
    moo_fx_box_mask_v(pong, ping, w, h, job->effect.shadow.blur_radius);
    for (y = 0; y < h; ++y) {
        int32_t x;
        for (x = 0; x < w; ++x) {
            uint64_t i = (uint64_t)y * (uint64_t)w + (uint64_t)x;
            uint8_t src[4];
            uint8_t *dst;
            src[0] = job->effect.shadow.color.r;
            src[1] = job->effect.shadow.color.g;
            src[2] = job->effect.shadow.color.b;
            src[3] = (uint8_t)moo_fx_round_u64(
                (uint64_t)job->effect.shadow.color.a * ping[i],
                UINT64_C(255));
            if (src[3] == 0u) continue;
            dst = moo_fx_target_pixel(&job->target, x, y);
            moo_fx_over(dst, src);
        }
    }
}

static void moo_fx_colorize(const MooCompEffectCpuJob *job,
                            int32_t x, int32_t y, uint8_t p[4]) {
    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_SATURATION) != 0u) {
        int64_t l = (int64_t)moo_fx_round_u64(
            UINT64_C(54) * p[0] + UINT64_C(183) * p[1] +
            UINT64_C(19) * p[2], UINT64_C(256));
        uint32_t c;
        for (c = 0u; c < 3u; ++c) {
            int64_t v = l + moo_fx_round_signed(
                ((int64_t)p[c] - l) *
                (int64_t)job->effect.backdrop.saturation_q8_8,
                INT64_C(256));
            p[c] = moo_fx_clamp_u8(v);
        }
    }
    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_TINT) != 0u) {
        uint32_t mix = job->effect.backdrop.tint_mix;
        const MooCompRgba8 *t = &job->effect.backdrop.tint;
        uint8_t tc[3] = {t->r, t->g, t->b};
        uint32_t c;
        for (c = 0u; c < 3u; ++c)
            p[c] = (uint8_t)moo_fx_round_u64(
                (uint64_t)p[c] * (UINT64_C(255) - mix) +
                (uint64_t)tc[c] * mix, UINT64_C(255));
    }
    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_NOISE) != 0u) {
        uint32_t h = moo_comp_effect_cpu_noise_hash(
            (uint32_t)x, (uint32_t)y, job->effect.backdrop.noise_seed);
        int64_t n = (int64_t)(h >> 24) - INT64_C(128);
        int64_t delta = moo_fx_round_signed(
            n * (int64_t)job->effect.backdrop.noise, INT64_C(255));
        p[0] = moo_fx_clamp_u8((int64_t)p[0] + delta);
        p[1] = moo_fx_clamp_u8((int64_t)p[1] + delta);
        p[2] = moo_fx_clamp_u8((int64_t)p[2] + delta);
    }
}

static void moo_fx_render_backdrop(const MooCompEffectCpuJob *job,
                                   const MooCompEffectCpuSource *lower_z,
                                   const MooCompAffine2D *inverse,
                                   const MooCompCorners *corners,
                                   uint32_t *ping, uint32_t *pong) {
    int32_t w = job->target.width;
    int32_t h = job->target.height;
    int32_t y;
    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u &&
        job->effect.backdrop.blur_radius != 0u) {
        moo_fx_box_rgba_h(ping, pong, w, h,
                          job->effect.backdrop.blur_radius);
        moo_fx_box_rgba_v(pong, ping, w, h,
                          job->effect.backdrop.blur_radius);
    }
    for (y = 0; y < h; ++y) {
        int32_t x;
        for (x = 0; x < w; ++x) {
            uint64_t i;
            uint32_t a;
            uint8_t p[4];
            uint8_t *dst;
            if (moo_fx_coverage(job, inverse, corners, x, y) == 0u)
                continue;
            i = ((uint64_t)y * (uint64_t)w + (uint64_t)x) * 4u;
            if ((job->effect.enabled_mask &
                 MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u) {
                a = ping[i + 3u];
                p[3] = (uint8_t)a;
                p[0] = a == 0u ? 0u : moo_fx_clamp_u8(
                    (int64_t)moo_fx_round_u64(ping[i], a));
                p[1] = a == 0u ? 0u : moo_fx_clamp_u8(
                    (int64_t)moo_fx_round_u64(ping[i + 1u], a));
                p[2] = a == 0u ? 0u : moo_fx_clamp_u8(
                    (int64_t)moo_fx_round_u64(ping[i + 2u], a));
            } else {
                const uint8_t *base = moo_fx_source_pixel(lower_z, x, y);
                p[0] = base[0]; p[1] = base[1];
                p[2] = base[2]; p[3] = base[3];
            }
            moo_fx_colorize(job, x, y, p);
            dst = moo_fx_target_pixel(&job->target, x, y);
            dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2]; dst[3] = p[3];
        }
    }
}

static void moo_fx_render_content(const MooCompEffectCpuJob *job,
                                  const MooCompAffine2D *inverse,
                                  const MooCompCorners *corners) {
    int32_t y;
    for (y = 0; y < job->target.height; ++y) {
        int32_t x;
        for (x = 0; x < job->target.width; ++x) {
            int32_t sx;
            int32_t sy;
            const uint8_t *source;
            uint8_t src[4];
            uint8_t coverage;
            if (!moo_fx_local_sample(inverse, job->content_rect, x, y, 0, 0,
                                     &sx, &sy) ||
                sx < 0 || sy < 0 ||
                sx >= job->content_rect.width ||
                sy >= job->content_rect.height)
                continue;
            coverage = 255u;
            if ((job->effect.enabled_mask & MOO_COMP_EFFECT_CORNER_CLIP) != 0u &&
                moo_comp_effect_rounded_coverage_a8(
                    job->content_rect.width, job->content_rect.height, corners,
                    sx, sy, &coverage) != MOO_COMP_OK)
                continue;
            if (coverage == 0u) continue;
            source = moo_fx_source_pixel(
                &job->content,
                sx * (int32_t)job->content_scale,
                sy * (int32_t)job->content_scale);
            src[0] = source[0]; src[1] = source[1]; src[2] = source[2];
            src[3] = (uint8_t)moo_fx_round_u64(
                (uint64_t)source[3] * job->content_opacity,
                UINT64_C(255));
            moo_fx_over(moo_fx_target_pixel(&job->target, x, y), src);
        }
    }
}

static MooCompResult moo_fx_validate_scratch(
    const MooCompEffectCpuScratch *scratch,
    const MooCompEffectCpuStats *stats,
    const MooCompEffectCpuTarget *target) {
    uint64_t rgba_bytes;
    uint64_t target_bytes = (uint64_t)target->stride *
                            (uint64_t)(uint32_t)target->height;
    if (stats->rgba_words_per_buffer != 0u && !scratch)
        return MOO_COMP_LIMIT;
    rgba_bytes = stats->rgba_words_per_buffer * UINT64_C(4);
    if (stats->rgba_words_per_buffer != 0u) {
        if (!scratch->rgba_ping || !scratch->rgba_pong ||
            scratch->rgba_words_per_buffer < stats->rgba_words_per_buffer)
            return MOO_COMP_LIMIT;
        if (moo_fx_ranges_overlap(scratch->rgba_ping, rgba_bytes,
                                  scratch->rgba_pong, rgba_bytes) ||
            moo_fx_ranges_overlap(scratch->rgba_ping, rgba_bytes,
                                  target->pixels, target_bytes) ||
            moo_fx_ranges_overlap(scratch->rgba_pong, rgba_bytes,
                                  target->pixels, target_bytes))
            return MOO_COMP_INVALID;
    }
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_cpu_render(
    const MooCompEffectCpuJob *job, const MooCompEffectCpuScratch *scratch,
    MooCompEffectCpuStats *out_stats) {
    MooCompEffectCpuStats stats;
    MooCompAffine2D affine;
    MooCompAffine2D inverse;
    MooCompCorners corners;
    MooCompResult result;
    uint64_t limit;
    MooCompEffectCpuSource lower_z;
    uint64_t content_bytes;
    uint64_t target_bytes;
    uint64_t lower_z_bytes;
    uint64_t scratch_bytes;
    if (!job) return MOO_COMP_INVALID;
    result = moo_comp_effect_cpu_requirements(job, &stats);
    if (result != MOO_COMP_OK) return result;
    limit = job->max_work_units == 0u ? UINT64_MAX : job->max_work_units;
    if (stats.work_units > limit) return MOO_COMP_LIMIT;
    result = moo_fx_validate_scratch(scratch, &stats, &job->target);
    if (result != MOO_COMP_OK) return result;
    content_bytes = (uint64_t)job->content.stride *
                    (uint64_t)(uint32_t)job->content.height;
    target_bytes = (uint64_t)job->target.stride *
                   (uint64_t)(uint32_t)job->target.height;
    lower_z = job->lower_z;
    if (lower_z.pixels == NULL) {
        lower_z.pixels = job->target.pixels;
        lower_z.buffer_bytes = job->target.buffer_bytes;
        lower_z.stride = job->target.stride;
        lower_z.width = job->target.width;
        lower_z.height = job->target.height;
    }
    lower_z_bytes = (uint64_t)lower_z.stride *
                    (uint64_t)(uint32_t)lower_z.height;
    scratch_bytes = stats.rgba_words_per_buffer * UINT64_C(4);
    if (moo_fx_ranges_overlap(job->content.pixels, content_bytes,
                              job->target.pixels, target_bytes) ||
        (((job->effect.enabled_mask & MOO_FX_BACKDROP_MASK) != 0u) &&
         (job->effect.enabled_mask & MOO_COMP_EFFECT_SHADOW) != 0u &&
         moo_fx_ranges_overlap(lower_z.pixels, lower_z_bytes,
                               job->target.pixels, target_bytes)) ||
        (scratch_bytes != 0u &&
         (moo_fx_ranges_overlap(scratch->rgba_ping, scratch_bytes,
                                lower_z.pixels, lower_z_bytes) ||
          moo_fx_ranges_overlap(scratch->rgba_pong, scratch_bytes,
                                lower_z.pixels, lower_z_bytes))))
        return MOO_COMP_INVALID;
    affine = (job->effect.enabled_mask & MOO_COMP_EFFECT_AFFINE_2D) != 0u ?
             job->effect.affine : moo_fx_identity();
    result = moo_comp_effect_affine_inverse(&affine, &inverse);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_effect_corners_normalize(
        job->content_rect.width, job->content_rect.height,
        &job->effect.corners, &corners);
    if (result != MOO_COMP_OK) return result;

    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_SHADOW) != 0u)
        moo_fx_render_shadow(job, &inverse,
                             scratch ? scratch->rgba_ping : NULL,
                             scratch ? scratch->rgba_pong : NULL);
    if ((job->effect.enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u)
        moo_fx_capture(&lower_z, scratch->rgba_ping);
    if ((job->effect.enabled_mask & MOO_FX_BACKDROP_MASK) != 0u)
        moo_fx_render_backdrop(job, &lower_z, &inverse, &corners,
                               scratch ? scratch->rgba_ping : NULL,
                               scratch ? scratch->rgba_pong : NULL);
    moo_fx_render_content(job, &inverse, &corners);
    if (out_stats) *out_stats = stats;
    return MOO_COMP_OK;
}
