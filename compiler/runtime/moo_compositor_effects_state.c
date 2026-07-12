#include "moo_compositor_effects_state.h"

#define MOO_COMP_EFFECT_HASH_OFFSET UINT64_C(1469598103934665603)
#define MOO_COMP_EFFECT_HASH_PRIME UINT64_C(1099511628211)

static void moo_comp_effect_zero_bytes(void *dst, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)dst;
    uint32_t i;
    for (i = 0u; i < size; ++i) bytes[i] = 0u;
}

static void moo_comp_effect_copy_bytes(
    void *dst, const void *src, uint32_t size) {
    volatile uint8_t *to = (volatile uint8_t *)dst;
    const volatile uint8_t *from = (const volatile uint8_t *)src;
    uint32_t i;
    for (i = 0u; i < size; ++i) to[i] = from[i];
}

static MooCompRgba8 moo_comp_effect_zero_rgba(void) {
    MooCompRgba8 value = {0u, 0u, 0u, 0u};
    return value;
}
static int moo_comp_effect_rgba_eq(MooCompRgba8 a, MooCompRgba8 b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

MooCompEffectState moo_comp_effect_state_neutral(void) {
    MooCompEffectState state;
    moo_comp_effect_zero_bytes(&state, (uint32_t)sizeof(state));
    state.backdrop.saturation_q8_8 = MOO_COMP_SATURATION_ONE;
    state.affine.m11 = MOO_COMP_Q16_ONE;
    state.affine.m22 = MOO_COMP_Q16_ONE;
    return state;
}

MooCompEffectLimits moo_comp_effect_limits_default(void) {
    MooCompEffectLimits x;
    moo_comp_effect_zero_bytes(&x, (uint32_t)sizeof(x));
    x.max_corner_radius = MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS;
    x.max_shadow_blur_radius =
        MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_BLUR_RADIUS;
    x.max_shadow_spread_radius =
        MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_SPREAD_RADIUS;
    x.max_backdrop_blur_radius =
        MOO_COMP_EFFECT_DEFAULT_MAX_BACKDROP_BLUR_RADIUS;
    x.max_saturation_q8_8 =
        MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8;
    x.max_abs_shadow_offset =
        MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET;
    x.max_abs_affine_coefficient =
        MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT;
    x.max_abs_affine_translation =
        MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION;
    x.max_abs_affine_origin =
        MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN;
    x.max_animations_per_surface =
        MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_SURFACE;
    x.max_animations_per_client =
        MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_CLIENT;
    x.min_animation_duration_ns =
        MOO_COMP_EFFECT_DEFAULT_MIN_ANIMATION_DURATION_NS;
    x.max_animation_duration_ns =
        MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS;
    x.max_animation_timeline_ns =
        MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_TIMELINE_NS;
    x.max_effect_work_units_per_frame =
        MOO_COMP_EFFECT_DEFAULT_MAX_WORK_UNITS;
    x.max_effect_scratch_bytes =
        MOO_COMP_EFFECT_DEFAULT_MAX_SCRATCH_BYTES;
    return x;
}

static int moo_comp_effect_limits_valid(const MooCompEffectLimits *x) {
    MooCompEffectLimits d = moo_comp_effect_limits_default();
    if (!x || x->reserved != 0u || x->reserved_alignment != 0u) return 0;
    if (x->max_corner_radius > d.max_corner_radius ||
        x->max_shadow_blur_radius > d.max_shadow_blur_radius ||
        x->max_shadow_spread_radius > d.max_shadow_spread_radius ||
        x->max_backdrop_blur_radius > d.max_backdrop_blur_radius ||
        x->max_saturation_q8_8 < MOO_COMP_SATURATION_ONE ||
        x->max_saturation_q8_8 > d.max_saturation_q8_8)
        return 0;
    if (x->max_abs_shadow_offset < 0 ||
        x->max_abs_shadow_offset > d.max_abs_shadow_offset ||
        x->max_abs_affine_coefficient < MOO_COMP_Q16_ONE ||
        x->max_abs_affine_coefficient > d.max_abs_affine_coefficient ||
        x->max_abs_affine_translation < 0 ||
        x->max_abs_affine_translation > d.max_abs_affine_translation ||
        x->max_abs_affine_origin < 0 ||
        x->max_abs_affine_origin > d.max_abs_affine_origin)
        return 0;
    if (x->max_animations_per_surface == 0u ||
        x->max_animations_per_surface > d.max_animations_per_surface ||
        x->max_animations_per_client == 0u ||
        x->max_animations_per_client > d.max_animations_per_client)
        return 0;
    if (x->min_animation_duration_ns < d.min_animation_duration_ns ||
        x->min_animation_duration_ns > x->max_animation_duration_ns ||
        x->max_animation_duration_ns > d.max_animation_duration_ns ||
        x->max_animation_duration_ns > x->max_animation_timeline_ns ||
        x->max_animation_timeline_ns > d.max_animation_timeline_ns)
        return 0;
    if (x->max_effect_work_units_per_frame == 0u ||
        x->max_effect_work_units_per_frame >
            d.max_effect_work_units_per_frame ||
        x->max_effect_scratch_bytes == 0u ||
        x->max_effect_scratch_bytes > d.max_effect_scratch_bytes)
        return 0;
    return 1;
}

MooCompResult moo_comp_effect_state_config_init(
    MooCompEffectStateConfig *out, uint64_t capabilities,
    const MooCompEffectLimits *limits) {
    MooCompEffectStateConfig candidate;
    if (!out || !limits) return MOO_COMP_INVALID;
    if ((capabilities & ~MOO_COMP_EFFECTS_V2) != 0u ||
        !moo_comp_effect_limits_valid(limits))
        return MOO_COMP_INVALID;
    candidate.capabilities = capabilities;
    moo_comp_effect_copy_bytes(
        &candidate.limits, limits, (uint32_t)sizeof(candidate.limits));
    moo_comp_effect_copy_bytes(out, &candidate, (uint32_t)sizeof(*out));
    return MOO_COMP_OK;
}

static int moo_comp_effect_affine_neutral(const MooCompAffine2D *a) {
    return a->m11 == MOO_COMP_Q16_ONE && a->m12 == 0 &&
           a->m21 == 0 && a->m22 == MOO_COMP_Q16_ONE &&
           a->tx == 0 && a->ty == 0 && a->origin_x == 0 &&
           a->origin_y == 0;
}

static int moo_comp_effect_state_valid(const MooCompEffectState *s) {
    MooCompRgba8 z = moo_comp_effect_zero_rgba();
    if (!s || (s->enabled_mask & ~MOO_COMP_EFFECTS_V2) != 0u ||
        (s->required_mask & ~MOO_COMP_EFFECTS_V2) != 0u ||
        (s->required_mask & ~s->enabled_mask) != 0u ||
        s->reserved != 0u || s->backdrop.reserved != 0u ||
        s->fallback_policy >
            (uint32_t)MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE)
        return 0;
    if ((s->enabled_mask & MOO_COMP_EFFECT_CORNER_CLIP) == 0u &&
        (s->corners.top_left != 0u || s->corners.top_right != 0u ||
         s->corners.bottom_right != 0u || s->corners.bottom_left != 0u))
        return 0;
    if ((s->enabled_mask & MOO_COMP_EFFECT_SHADOW) == 0u &&
        (s->shadow.offset_x != 0 || s->shadow.offset_y != 0 ||
         s->shadow.blur_radius != 0u || s->shadow.spread_radius != 0u ||
         !moo_comp_effect_rgba_eq(s->shadow.color, z)))
        return 0;
    if ((s->enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) == 0u &&
        s->backdrop.blur_radius != 0u)
        return 0;
    if ((s->enabled_mask & MOO_COMP_EFFECT_SATURATION) == 0u &&
        s->backdrop.saturation_q8_8 != MOO_COMP_SATURATION_ONE)
        return 0;
    if ((s->enabled_mask & MOO_COMP_EFFECT_TINT) == 0u &&
        (s->backdrop.tint_mix != 0u ||
         !moo_comp_effect_rgba_eq(s->backdrop.tint, z)))
        return 0;
    if ((s->enabled_mask & MOO_COMP_EFFECT_NOISE) == 0u &&
        (s->backdrop.noise != 0u || s->backdrop.noise_seed != 0u))
        return 0;
    if ((s->enabled_mask & MOO_COMP_EFFECT_AFFINE_2D) == 0u &&
        !moo_comp_effect_affine_neutral(&s->affine))
        return 0;
    return 1;
}

static int moo_comp_effect_i32_over(int32_t v, int32_t limit) {
    int64_t w = (int64_t)v;
    int64_t l = (int64_t)limit;
    return w < -l || w > l;
}

static uint64_t moo_comp_effect_over_mask(
    const MooCompEffectState *s, const MooCompEffectLimits *l) {
    uint64_t m = 0u;
    if (s->corners.top_left > l->max_corner_radius ||
        s->corners.top_right > l->max_corner_radius ||
        s->corners.bottom_right > l->max_corner_radius ||
        s->corners.bottom_left > l->max_corner_radius)
        m |= MOO_COMP_EFFECT_CORNER_CLIP;
    if (s->shadow.blur_radius > l->max_shadow_blur_radius ||
        s->shadow.spread_radius > l->max_shadow_spread_radius ||
        moo_comp_effect_i32_over(s->shadow.offset_x,
                                 l->max_abs_shadow_offset) ||
        moo_comp_effect_i32_over(s->shadow.offset_y,
                                 l->max_abs_shadow_offset))
        m |= MOO_COMP_EFFECT_SHADOW;
    if (s->backdrop.blur_radius > l->max_backdrop_blur_radius)
        m |= MOO_COMP_EFFECT_BACKDROP_BLUR;
    if (s->backdrop.saturation_q8_8 > l->max_saturation_q8_8)
        m |= MOO_COMP_EFFECT_SATURATION;
    if (moo_comp_effect_i32_over(s->affine.m11,
                                 l->max_abs_affine_coefficient) ||
        moo_comp_effect_i32_over(s->affine.m12,
                                 l->max_abs_affine_coefficient) ||
        moo_comp_effect_i32_over(s->affine.m21,
                                 l->max_abs_affine_coefficient) ||
        moo_comp_effect_i32_over(s->affine.m22,
                                 l->max_abs_affine_coefficient) ||
        moo_comp_effect_i32_over(s->affine.tx,
                                 l->max_abs_affine_translation) ||
        moo_comp_effect_i32_over(s->affine.ty,
                                 l->max_abs_affine_translation) ||
        moo_comp_effect_i32_over(s->affine.origin_x,
                                 l->max_abs_affine_origin) ||
        moo_comp_effect_i32_over(s->affine.origin_y,
                                 l->max_abs_affine_origin))
        m |= MOO_COMP_EFFECT_AFFINE_2D;
    return m & s->enabled_mask;
}

static void moo_comp_effect_disable(MooCompEffectState *s, uint64_t bit) {
    MooCompRgba8 z = moo_comp_effect_zero_rgba();
    s->enabled_mask &= ~bit;
    s->required_mask &= ~bit;
    if (bit == MOO_COMP_EFFECT_CORNER_CLIP) {
        s->corners.top_left = 0u; s->corners.top_right = 0u;
        s->corners.bottom_right = 0u; s->corners.bottom_left = 0u;
    } else if (bit == MOO_COMP_EFFECT_SHADOW) {
        s->shadow.offset_x = 0; s->shadow.offset_y = 0;
        s->shadow.blur_radius = 0u; s->shadow.spread_radius = 0u;
        s->shadow.color = z;
    } else if (bit == MOO_COMP_EFFECT_BACKDROP_BLUR) {
        s->backdrop.blur_radius = 0u;
    } else if (bit == MOO_COMP_EFFECT_SATURATION) {
        s->backdrop.saturation_q8_8 = MOO_COMP_SATURATION_ONE;
    } else if (bit == MOO_COMP_EFFECT_TINT) {
        s->backdrop.tint = z; s->backdrop.tint_mix = 0u;
    } else if (bit == MOO_COMP_EFFECT_NOISE) {
        s->backdrop.noise = 0u; s->backdrop.noise_seed = 0u;
    } else if (bit == MOO_COMP_EFFECT_AFFINE_2D) {
        s->affine.m11 = MOO_COMP_Q16_ONE; s->affine.m12 = 0;
        s->affine.m21 = 0; s->affine.m22 = MOO_COMP_Q16_ONE;
        s->affine.tx = 0; s->affine.ty = 0;
        s->affine.origin_x = 0; s->affine.origin_y = 0;
    }
}

static MooCompResult moo_comp_effect_resolve_one(
    MooCompEffectState *effective, uint64_t required, uint32_t policy,
    uint64_t bit, int unavailable, int over, uint64_t *degraded) {
    int must = (required & bit) != 0u;
    int approx = bit == MOO_COMP_EFFECT_BACKDROP_BLUR ||
                 bit == MOO_COMP_EFFECT_SATURATION ||
                 bit == MOO_COMP_EFFECT_NOISE;
    if (!unavailable && !over) return MOO_COMP_OK;
    if (must || policy == MOO_COMP_EFFECT_FALLBACK_REQUIRE)
        return unavailable ? MOO_COMP_UNSUPPORTED : MOO_COMP_LIMIT;
    if (policy == MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE && !approx)
        return unavailable ? MOO_COMP_UNSUPPORTED : MOO_COMP_LIMIT;
    moo_comp_effect_disable(effective, bit);
    *degraded |= bit;
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_effect_resolve(
    const MooCompEffectStateConfig *config,
    const MooCompEffectState *requested,
    MooCompEffectState *out, uint64_t *out_degraded) {
    static const uint64_t bits[] = {
        MOO_COMP_EFFECT_CORNER_CLIP, MOO_COMP_EFFECT_SHADOW,
        MOO_COMP_EFFECT_BACKDROP_BLUR, MOO_COMP_EFFECT_SATURATION,
        MOO_COMP_EFFECT_TINT, MOO_COMP_EFFECT_NOISE,
        MOO_COMP_EFFECT_AFFINE_2D, MOO_COMP_EFFECT_ANIMATION
    };
    MooCompEffectState effective;
    uint64_t over = moo_comp_effect_over_mask(requested, &config->limits);
    uint64_t degraded = 0u;
    moo_comp_effect_copy_bytes(
        &effective, requested, (uint32_t)sizeof(effective));
    uint32_t i;
    for (i = 0u; i < (uint32_t)(sizeof(bits) / sizeof(bits[0])); ++i) {
        uint64_t bit = bits[i];
        MooCompResult r;
        if ((requested->enabled_mask & bit) == 0u) continue;
        r = moo_comp_effect_resolve_one(
            &effective, requested->required_mask, requested->fallback_policy,
            bit, (config->capabilities & bit) == 0u, (over & bit) != 0u,
            &degraded);
        if (r != MOO_COMP_OK) return r;
    }
    moo_comp_effect_copy_bytes(
        out, &effective, (uint32_t)sizeof(*out));
    *out_degraded = degraded;
    return MOO_COMP_OK;
}

static int moo_comp_effect_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (UINT64_MAX - a < b) return 0;
    *out = a + b; return 1;
}
static int moo_comp_effect_mul(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0u && b > UINT64_MAX / a) return 0;
    *out = a * b; return 1;
}

static MooCompResult moo_comp_effect_estimate(
    const MooCompEffectState *s, uint64_t pixels,
    uint64_t *work, uint64_t *scratch) {
    uint64_t weight = 0u;
    uint64_t sw = 0u;
    if ((s->enabled_mask & MOO_COMP_EFFECT_CORNER_CLIP) != 0u &&
        !moo_comp_effect_add(weight,
            MOO_COMP_EFFECT_WORK_CORNER_PER_PIXEL, &weight))
        return MOO_COMP_LIMIT;
    if ((s->enabled_mask & MOO_COMP_EFFECT_SHADOW) != 0u &&
        !moo_comp_effect_add(weight,
            MOO_COMP_EFFECT_WORK_SHADOW_PER_PIXEL, &weight))
        return MOO_COMP_LIMIT;
    if ((s->enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u &&
        !moo_comp_effect_add(weight,
            MOO_COMP_EFFECT_WORK_BACKDROP_PER_PIXEL, &weight))
        return MOO_COMP_LIMIT;
    if ((s->enabled_mask & MOO_COMP_EFFECT_SATURATION) != 0u &&
        !moo_comp_effect_add(weight,
            MOO_COMP_EFFECT_WORK_COLOR_PER_PIXEL, &weight))
        return MOO_COMP_LIMIT;
    if ((s->enabled_mask & MOO_COMP_EFFECT_TINT) != 0u &&
        !moo_comp_effect_add(weight,
            MOO_COMP_EFFECT_WORK_COLOR_PER_PIXEL, &weight))
        return MOO_COMP_LIMIT;
    if ((s->enabled_mask & MOO_COMP_EFFECT_NOISE) != 0u &&
        !moo_comp_effect_add(weight,
            MOO_COMP_EFFECT_WORK_COLOR_PER_PIXEL, &weight))
        return MOO_COMP_LIMIT;
    if ((s->enabled_mask & MOO_COMP_EFFECT_AFFINE_2D) != 0u &&
        !moo_comp_effect_add(weight,
            MOO_COMP_EFFECT_WORK_AFFINE_PER_PIXEL, &weight))
        return MOO_COMP_LIMIT;
    if ((s->enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u)
        sw = MOO_COMP_EFFECT_SCRATCH_BACKDROP_PER_PIXEL;
    else if ((s->enabled_mask & MOO_COMP_EFFECT_SHADOW) != 0u &&
             s->shadow.blur_radius != 0u)
        sw = MOO_COMP_EFFECT_SCRATCH_SHADOW_PER_PIXEL;
    if (!moo_comp_effect_mul(pixels, weight, work) ||
        !moo_comp_effect_mul(pixels, sw, scratch))
        return MOO_COMP_LIMIT;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_surface_init(
    MooCompEffectSurfaceState *s, uint64_t owner, uint64_t surface) {
    MooCompEffectSurfaceState candidate;
    MooCompEffectState neutral;
    if (!s || owner == 0u || surface == 0u) return MOO_COMP_INVALID;
    moo_comp_effect_zero_bytes(&candidate, (uint32_t)sizeof(candidate));
    neutral = moo_comp_effect_state_neutral();
    candidate.active = 1u; candidate.owner = owner;
    candidate.surface = surface;
    moo_comp_effect_copy_bytes(
        &candidate.base, &neutral, (uint32_t)sizeof(neutral));
    moo_comp_effect_copy_bytes(
        &candidate.requested, &neutral, (uint32_t)sizeof(neutral));
    moo_comp_effect_copy_bytes(
        &candidate.effective, &neutral, (uint32_t)sizeof(neutral));
    moo_comp_effect_copy_bytes(s, &candidate, (uint32_t)sizeof(*s));
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_effect_access(
    const MooCompEffectSurfaceState *s, uint64_t owner, uint64_t surface) {
    if (!s || owner == 0u || surface == 0u) return MOO_COMP_INVALID;
    if (s->active != 1u || s->reserved != 0u || s->surface != surface)
        return MOO_COMP_STALE_HANDLE;
    if (s->owner != owner) return MOO_COMP_ACCESS;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_surface_destroy(
    MooCompEffectSurfaceState *s, uint64_t owner, uint64_t surface) {
    MooCompResult r = moo_comp_effect_access(s, owner, surface);
    if (r != MOO_COMP_OK) return r;
    moo_comp_effect_zero_bytes(s, (uint32_t)sizeof(*s));
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_state_preflight(
    const MooCompEffectStateConfig *config,
    const MooCompEffectSurfaceState *surface_state,
    uint64_t owner, uint64_t surface,
    const MooCompEffectState *requested, const MooCompEffectUsage *usage,
    MooCompEffectPreflight *out) {
    MooCompEffectPreflight candidate;
    MooCompResult r;
    if (!config || !requested || !usage || !out) return MOO_COMP_INVALID;
    if (!moo_comp_effect_limits_valid(&config->limits) ||
        (config->capabilities & ~MOO_COMP_EFFECTS_V2) != 0u)
        return MOO_COMP_INVALID;
    r = moo_comp_effect_access(surface_state, owner, surface);
    if (r != MOO_COMP_OK) return r;
    if (!moo_comp_effect_state_valid(requested)) return MOO_COMP_INVALID;
    if (usage->animations_on_surface >
            config->limits.max_animations_per_surface ||
        usage->animations_for_client >
            config->limits.max_animations_per_client ||
        surface_state->commit_sequence == UINT64_MAX)
        return MOO_COMP_LIMIT;
    candidate.owner = owner;
    candidate.surface = surface;
    candidate.expected_commit_sequence = surface_state->commit_sequence;
    candidate.next_commit_sequence =
        candidate.expected_commit_sequence + UINT64_C(1);
    moo_comp_effect_copy_bytes(
        &candidate.base, requested, (uint32_t)sizeof(candidate.base));
    moo_comp_effect_copy_bytes(
        &candidate.requested, requested,
        (uint32_t)sizeof(candidate.requested));
    r = moo_comp_effect_resolve(
        config, requested, &candidate.effective, &candidate.degraded_mask);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_effect_estimate(
        &candidate.effective, usage->affected_pixels,
        &candidate.work_units, &candidate.scratch_bytes);
    if (r != MOO_COMP_OK) return r;
    if (candidate.work_units >
            config->limits.max_effect_work_units_per_frame ||
        candidate.scratch_bytes > config->limits.max_effect_scratch_bytes)
        return MOO_COMP_LIMIT;
    moo_comp_effect_copy_bytes(
        out, &candidate, (uint32_t)sizeof(*out));
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_state_apply(
    MooCompEffectSurfaceState *s, const MooCompEffectPreflight *p) {
    MooCompResult r;
    if (!s || !p) return MOO_COMP_INVALID;
    r = moo_comp_effect_access(s, p->owner, p->surface);
    if (r != MOO_COMP_OK) return r;
    if (p->expected_commit_sequence == UINT64_MAX ||
        p->next_commit_sequence !=
            p->expected_commit_sequence + UINT64_C(1))
        return MOO_COMP_INVALID;
    if (s->commit_sequence != p->expected_commit_sequence)
        return MOO_COMP_BAD_STATE;
    moo_comp_effect_copy_bytes(
        &s->base, &p->base, (uint32_t)sizeof(s->base));
    moo_comp_effect_copy_bytes(
        &s->requested, &p->requested, (uint32_t)sizeof(s->requested));
    moo_comp_effect_copy_bytes(
        &s->effective, &p->effective, (uint32_t)sizeof(s->effective));
    s->degraded_mask = p->degraded_mask;
    s->work_units = p->work_units; s->scratch_bytes = p->scratch_bytes;
    s->commit_sequence = p->next_commit_sequence;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_surface_status(
    const MooCompEffectSurfaceState *s, MooCompEffectStatus *out) {
    MooCompEffectStatus x;
    if (!s || !out) return MOO_COMP_INVALID;
    if (s->active != 1u || s->reserved != 0u)
        return MOO_COMP_STALE_HANDLE;
    moo_comp_effect_copy_bytes(
        &x.requested, &s->requested, (uint32_t)sizeof(x.requested));
    moo_comp_effect_copy_bytes(
        &x.effective, &s->effective, (uint32_t)sizeof(x.effective));
    x.degraded_mask = s->degraded_mask;
    moo_comp_effect_copy_bytes(out, &x, (uint32_t)sizeof(*out));
    return MOO_COMP_OK;
}

static uint64_t moo_comp_effect_h8(uint64_t h, uint8_t v) {
    return (h ^ (uint64_t)v) * MOO_COMP_EFFECT_HASH_PRIME;
}
static uint64_t moo_comp_effect_h16(uint64_t h, uint16_t v) {
    return moo_comp_effect_h8(moo_comp_effect_h8(h, (uint8_t)v),
                              (uint8_t)(v >> 8));
}
static uint64_t moo_comp_effect_h32(uint64_t h, uint32_t v) {
    return moo_comp_effect_h16(moo_comp_effect_h16(h, (uint16_t)v),
                               (uint16_t)(v >> 16));
}
static uint64_t moo_comp_effect_h64(uint64_t h, uint64_t v) {
    return moo_comp_effect_h32(moo_comp_effect_h32(h, (uint32_t)v),
                               (uint32_t)(v >> 32));
}
static uint64_t moo_comp_effect_hrgba(uint64_t h, MooCompRgba8 v) {
    h = moo_comp_effect_h8(h, v.r); h = moo_comp_effect_h8(h, v.g);
    h = moo_comp_effect_h8(h, v.b); return moo_comp_effect_h8(h, v.a);
}
static uint64_t moo_comp_effect_hstate(
    uint64_t h, const MooCompEffectState *s) {
    h = moo_comp_effect_h64(h, s->enabled_mask);
    h = moo_comp_effect_h64(h, s->required_mask);
    h = moo_comp_effect_h32(h, s->fallback_policy);
    h = moo_comp_effect_h16(h, s->corners.top_left);
    h = moo_comp_effect_h16(h, s->corners.top_right);
    h = moo_comp_effect_h16(h, s->corners.bottom_right);
    h = moo_comp_effect_h16(h, s->corners.bottom_left);
    h = moo_comp_effect_h32(h, (uint32_t)s->shadow.offset_x);
    h = moo_comp_effect_h32(h, (uint32_t)s->shadow.offset_y);
    h = moo_comp_effect_h16(h, s->shadow.blur_radius);
    h = moo_comp_effect_h16(h, s->shadow.spread_radius);
    h = moo_comp_effect_hrgba(h, s->shadow.color);
    h = moo_comp_effect_h16(h, s->backdrop.blur_radius);
    h = moo_comp_effect_h16(h, s->backdrop.saturation_q8_8);
    h = moo_comp_effect_hrgba(h, s->backdrop.tint);
    h = moo_comp_effect_h8(h, s->backdrop.tint_mix);
    h = moo_comp_effect_h8(h, s->backdrop.noise);
    h = moo_comp_effect_h32(h, s->backdrop.noise_seed);
    h = moo_comp_effect_h32(h, (uint32_t)s->affine.m11);
    h = moo_comp_effect_h32(h, (uint32_t)s->affine.m12);
    h = moo_comp_effect_h32(h, (uint32_t)s->affine.m21);
    h = moo_comp_effect_h32(h, (uint32_t)s->affine.m22);
    h = moo_comp_effect_h32(h, (uint32_t)s->affine.tx);
    h = moo_comp_effect_h32(h, (uint32_t)s->affine.ty);
    h = moo_comp_effect_h32(h, (uint32_t)s->affine.origin_x);
    return moo_comp_effect_h32(h, (uint32_t)s->affine.origin_y);
}

uint64_t moo_comp_effect_state_hash(const MooCompEffectState *s) {
    if (!s) return 0u;
    return moo_comp_effect_hstate(MOO_COMP_EFFECT_HASH_OFFSET, s);
}
uint64_t moo_comp_effect_surface_hash(
    const MooCompEffectSurfaceState *s) {
    uint64_t h;
    if (!s) return 0u;
    h = moo_comp_effect_h32(MOO_COMP_EFFECT_HASH_OFFSET, s->active);
    h = moo_comp_effect_h64(h, s->owner);
    h = moo_comp_effect_h64(h, s->surface);
    h = moo_comp_effect_h64(h, s->commit_sequence);
    h = moo_comp_effect_hstate(h, &s->base);
    h = moo_comp_effect_hstate(h, &s->requested);
    h = moo_comp_effect_hstate(h, &s->effective);
    h = moo_comp_effect_h64(h, s->degraded_mask);
    h = moo_comp_effect_h64(h, s->work_units);
    return moo_comp_effect_h64(h, s->scratch_bytes);
}
