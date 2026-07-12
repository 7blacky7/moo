#include "moo_compositor_animation.h"
#include "moo_compositor_effects_state.h"

#include <limits.h>

#define MOO_COMP_ANIMATION_U32_MODULUS INT64_C(4294967296)
#define MOO_COMP_ANIMATION_FNV_OFFSET UINT64_C(1469598103934665603)
#define MOO_COMP_ANIMATION_FNV_PRIME UINT64_C(1099511628211)

static int moo_comp_animation_timeline_valid(
    const MooCompAnimationTimeline *timeline) {
    return timeline && timeline->slots && timeline->capacity > 0u &&
           timeline->active_count <= timeline->capacity;
}

static int moo_comp_animation_limits_valid(
    const MooCompEffectLimits *limits) {
    if (!limits || limits->reserved != 0u ||
        limits->reserved_alignment != 0u) return 0;
    if (limits->max_corner_radius >
            MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS ||
        limits->max_shadow_blur_radius >
            MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_BLUR_RADIUS ||
        limits->max_shadow_spread_radius >
            MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_SPREAD_RADIUS ||
        limits->max_backdrop_blur_radius >
            MOO_COMP_EFFECT_DEFAULT_MAX_BACKDROP_BLUR_RADIUS ||
        limits->max_saturation_q8_8 < MOO_COMP_SATURATION_ONE ||
        limits->max_saturation_q8_8 >
            MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8)
        return 0;
    if (limits->max_abs_shadow_offset < 0 ||
        limits->max_abs_shadow_offset >
            MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET ||
        limits->max_abs_affine_coefficient < MOO_COMP_Q16_ONE ||
        limits->max_abs_affine_coefficient >
            MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT ||
        limits->max_abs_affine_translation < 0 ||
        limits->max_abs_affine_translation >
            MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION ||
        limits->max_abs_affine_origin < 0 ||
        limits->max_abs_affine_origin >
            MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN)
        return 0;
    if (limits->max_animations_per_surface == 0u ||
        limits->max_animations_per_surface >
            MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_SURFACE ||
        limits->max_animations_per_client == 0u ||
        limits->max_animations_per_client >
            MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_CLIENT ||
        limits->max_animations_per_surface >
            limits->max_animations_per_client)
        return 0;
    if (limits->min_animation_duration_ns <
            MOO_COMP_EFFECT_DEFAULT_MIN_ANIMATION_DURATION_NS ||
        limits->min_animation_duration_ns >
            limits->max_animation_duration_ns ||
        limits->max_animation_duration_ns >
            MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS ||
        limits->max_animation_duration_ns >
            limits->max_animation_timeline_ns ||
        limits->max_animation_timeline_ns >
            MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_TIMELINE_NS)
        return 0;
    if (limits->max_effect_work_units_per_frame == 0u ||
        limits->max_effect_work_units_per_frame >
            MOO_COMP_EFFECT_DEFAULT_MAX_WORK_UNITS ||
        limits->max_effect_scratch_bytes == 0u ||
        limits->max_effect_scratch_bytes >
            MOO_COMP_EFFECT_DEFAULT_MAX_SCRATCH_BYTES)
        return 0;
    return 1;
}

static void moo_comp_animation_value_zero(MooCompAnimationValue *value) {
    uint32_t i;
    for (i = 0u; i < MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS; ++i)
        value->word[i] = 0u;
}

static int moo_comp_animation_value_equal(
    const MooCompAnimationValue *left,
    const MooCompAnimationValue *right) {
    uint32_t i;
    if (!left || !right) return 0;
    for (i = 0u; i < MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS; ++i) {
        if (left->word[i] != right->word[i]) return 0;
    }
    return 1;
}

static int64_t moo_comp_animation_decode_signed(uint32_t raw) {
    if (raw <= (uint32_t)INT32_MAX) return (int64_t)raw;
    return (int64_t)raw - MOO_COMP_ANIMATION_U32_MODULUS;
}

static uint32_t moo_comp_animation_encode_signed(int64_t value) {
    if (value >= 0) return (uint32_t)value;
    return (uint32_t)(value + MOO_COMP_ANIMATION_U32_MODULUS);
}

static uint64_t moo_comp_animation_magnitude_i64(int64_t value) {
    return value < 0 ? (uint64_t)(-value) : (uint64_t)value;
}

static int64_t moo_comp_animation_round_signed_div(
    int64_t numerator,
    uint64_t denominator) {
    uint64_t magnitude = moo_comp_animation_magnitude_i64(numerator);
    uint64_t rounded = (magnitude + denominator / 2u) / denominator;
    return numerator < 0 ? -(int64_t)rounded : (int64_t)rounded;
}

static uint64_t moo_comp_animation_round_unsigned_div(
    uint64_t numerator,
    uint64_t denominator) {
    return (numerator + denominator / 2u) / denominator;
}

static int moo_comp_animation_unused_zero(
    const MooCompAnimationValue *value,
    uint32_t first_unused) {
    uint32_t i;
    for (i = first_unused; i < MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS; ++i) {
        if (value->word[i] != 0u) return 0;
    }
    return 1;
}

static int moo_comp_animation_magnitude_within(
    uint32_t raw,
    int32_t limit) {
    int64_t value = moo_comp_animation_decode_signed(raw);
    return value >= -(int64_t)limit && value <= (int64_t)limit;
}

static MooCompResult moo_comp_animation_validate_value(
    uint32_t property,
    const MooCompAnimationValue *value,
    const MooCompEffectLimits *limits,
    uint64_t enabled_effect_mask) {
    uint32_t i;
    if (!value || !limits) return MOO_COMP_INVALID;
    switch (property) {
        case MOO_COMP_ANIMATION_PROPERTY_OPACITY:
            if (!moo_comp_animation_unused_zero(value, 1u) ||
                value->word[0] > UINT32_C(65536))
                return MOO_COMP_INVALID;
            return MOO_COMP_OK;

        case MOO_COMP_ANIMATION_PROPERTY_CORNERS:
            if ((enabled_effect_mask & MOO_COMP_EFFECT_CORNER_CLIP) == 0u)
                return MOO_COMP_INVALID;
            if (!moo_comp_animation_unused_zero(value, 4u))
                return MOO_COMP_INVALID;
            for (i = 0u; i < 4u; ++i) {
                if (value->word[i] > UINT16_MAX) return MOO_COMP_INVALID;
                if (value->word[i] > limits->max_corner_radius)
                    return MOO_COMP_LIMIT;
            }
            return MOO_COMP_OK;

        case MOO_COMP_ANIMATION_PROPERTY_SHADOW:
            if ((enabled_effect_mask & MOO_COMP_EFFECT_SHADOW) == 0u)
                return MOO_COMP_INVALID;
            if (!moo_comp_animation_unused_zero(value, 5u) ||
                value->word[2] > UINT16_MAX ||
                value->word[3] > UINT16_MAX)
                return MOO_COMP_INVALID;
            if (!moo_comp_animation_magnitude_within(
                    value->word[0], limits->max_abs_shadow_offset) ||
                !moo_comp_animation_magnitude_within(
                    value->word[1], limits->max_abs_shadow_offset) ||
                value->word[2] > limits->max_shadow_blur_radius ||
                value->word[3] > limits->max_shadow_spread_radius)
                return MOO_COMP_LIMIT;
            return MOO_COMP_OK;

        case MOO_COMP_ANIMATION_PROPERTY_BACKDROP:
            if ((enabled_effect_mask &
                 (MOO_COMP_EFFECT_BACKDROP_BLUR |
                  MOO_COMP_EFFECT_SATURATION |
                  MOO_COMP_EFFECT_TINT |
                  MOO_COMP_EFFECT_NOISE)) == 0u)
                return MOO_COMP_INVALID;
            if (!moo_comp_animation_unused_zero(value, 6u) ||
                value->word[0] > UINT16_MAX ||
                value->word[1] > UINT16_MAX ||
                value->word[3] > UINT8_MAX ||
                value->word[4] > UINT8_MAX)
                return MOO_COMP_INVALID;
            if ((enabled_effect_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) == 0u &&
                value->word[0] != 0u) return MOO_COMP_INVALID;
            if ((enabled_effect_mask & MOO_COMP_EFFECT_SATURATION) == 0u &&
                value->word[1] != MOO_COMP_SATURATION_ONE)
                return MOO_COMP_INVALID;
            if ((enabled_effect_mask & MOO_COMP_EFFECT_TINT) == 0u &&
                (value->word[2] != 0u || value->word[3] != 0u))
                return MOO_COMP_INVALID;
            if ((enabled_effect_mask & MOO_COMP_EFFECT_NOISE) == 0u &&
                (value->word[4] != 0u || value->word[5] != 0u))
                return MOO_COMP_INVALID;
            if (value->word[0] > limits->max_backdrop_blur_radius ||
                value->word[1] > limits->max_saturation_q8_8)
                return MOO_COMP_LIMIT;
            return MOO_COMP_OK;

        case MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D:
            if ((enabled_effect_mask & MOO_COMP_EFFECT_AFFINE_2D) == 0u)
                return MOO_COMP_INVALID;
            for (i = 0u; i < 4u; ++i) {
                if (!moo_comp_animation_magnitude_within(
                        value->word[i],
                        limits->max_abs_affine_coefficient))
                    return MOO_COMP_LIMIT;
            }
            for (i = 4u; i < 6u; ++i) {
                if (!moo_comp_animation_magnitude_within(
                        value->word[i],
                        limits->max_abs_affine_translation))
                    return MOO_COMP_LIMIT;
            }
            for (i = 6u; i < 8u; ++i) {
                if (!moo_comp_animation_magnitude_within(
                        value->word[i],
                        limits->max_abs_affine_origin))
                    return MOO_COMP_LIMIT;
            }
            return MOO_COMP_OK;

        default:
            return MOO_COMP_INVALID;
    }
}

static MooCompResult moo_comp_animation_validate_time(
    const MooCompAnimationTimeline *timeline,
    uint64_t timestamp_ns) {
    if (!moo_comp_animation_timeline_valid(timeline))
        return MOO_COMP_INVALID;
    if (timeline->has_timestamp &&
        timestamp_ns < timeline->last_timestamp_ns)
        return MOO_COMP_INVALID;
    return MOO_COMP_OK;
}

static int moo_comp_animation_outputs_valid(
    const void *output,
    uint32_t capacity,
    uint32_t required) {
    if (required > capacity) return 0;
    return required == 0u || output != 0;
}

static void moo_comp_animation_write_completion(
    MooCompAnimationCompletion *completion,
    MooCompHandle surface,
    uint64_t token,
    uint32_t status,
    uint64_t timestamp_ns) {
    completion->surface = surface;
    completion->token = token;
    completion->status = status;
    completion->reserved = 0u;
    completion->timestamp_ns = timestamp_ns;
}

static uint32_t moo_comp_animation_direction_forward(
    uint32_t direction,
    uint64_t play) {
    switch (direction) {
        case MOO_COMP_ANIMATION_DIRECTION_NORMAL:
            return 1u;
        case MOO_COMP_ANIMATION_DIRECTION_REVERSE:
            return 0u;
        case MOO_COMP_ANIMATION_DIRECTION_ALTERNATE:
            return (play & UINT64_C(1)) == 0u;
        case MOO_COMP_ANIMATION_DIRECTION_ALTERNATE_REVERSE:
            return (play & UINT64_C(1)) != 0u;
        default:
            return 0u;
    }
}

static MooCompResult moo_comp_animation_eval_slot(
    const MooCompAnimationSlot *slot,
    uint64_t timestamp_ns,
    uint32_t reduced_motion,
    MooCompAnimationValue *out_value,
    uint32_t *out_terminal) {
    uint64_t elapsed;
    uint64_t play;
    uint64_t local;
    uint64_t product;
    uint32_t progress;
    uint32_t eased;
    MooCompResult result;
    if (!slot || !slot->active || !out_value || !out_terminal)
        return MOO_COMP_INVALID;
    if (reduced_motion) {
        *out_value = slot->desc.to;
        *out_terminal = 1u;
        return MOO_COMP_OK;
    }
    if (timestamp_ns < slot->start_ns) {
        *out_value = slot->desc.from;
        *out_terminal = 0u;
        return MOO_COMP_OK;
    }
    if (timestamp_ns >= slot->end_ns) {
        play = (uint64_t)slot->desc.repeat_count - UINT64_C(1);
        local = slot->desc.duration_ns;
        *out_terminal = 1u;
    } else {
        elapsed = timestamp_ns - slot->start_ns;
        play = elapsed / slot->desc.duration_ns;
        local = elapsed - play * slot->desc.duration_ns;
        *out_terminal = 0u;
    }
    product = local * (uint64_t)MOO_COMP_ANIMATION_PROGRESS_ONE;
    progress = (uint32_t)moo_comp_animation_round_unsigned_div(
        product, slot->desc.duration_ns);
    result = moo_comp_animation_ease(slot->desc.easing, progress, &eased);
    if (result != MOO_COMP_OK) return result;
    if (!moo_comp_animation_direction_forward(slot->desc.direction, play))
        eased = MOO_COMP_ANIMATION_PROGRESS_ONE - eased;
    return moo_comp_animation_interpolate(
        slot->desc.property, &slot->desc.from, &slot->desc.to,
        eased, out_value);
}

static uint32_t moo_comp_animation_find_free(
    const MooCompAnimationTimeline *timeline) {
    uint32_t i;
    for (i = 0u; i < timeline->capacity; ++i) {
        if (!timeline->slots[i].active) return i;
    }
    return timeline->capacity;
}

MooCompResult moo_comp_animation_timeline_init(
    MooCompAnimationTimeline *timeline,
    MooCompAnimationSlot *slots,
    uint32_t capacity,
    const MooCompEffectLimits *limits) {
    uint32_t i;
    if (!timeline || !slots || capacity == 0u ||
        !moo_comp_animation_limits_valid(limits) ||
        capacity > limits->max_animations_per_client)
        return MOO_COMP_INVALID;
    timeline->slots = slots;
    timeline->capacity = capacity;
    timeline->active_count = 0u;
    timeline->limits = *limits;
    timeline->last_timestamp_ns = 0u;
    timeline->has_timestamp = 0u;
    timeline->reserved = 0u;
    for (i = 0u; i < capacity; ++i) {
        slots[i].surface = MOO_COMP_HANDLE_INVALID;
        moo_comp_animation_value_zero(&slots[i].current);
        slots[i].start_ns = 0u;
        slots[i].end_ns = 0u;
        slots[i].active = 0u;
        slots[i].reserved = 0u;
    }
    return MOO_COMP_OK;
}

MooCompResult moo_comp_animation_validate_desc(
    const MooCompAnimationDesc *desc,
    const MooCompEffectLimits *limits,
    uint64_t enabled_effect_mask) {
    uint64_t duration_total;
    MooCompResult result;
    if (!desc || !moo_comp_animation_limits_valid(limits))
        return MOO_COMP_INVALID;
    if (desc->token == 0u || desc->repeat_count == 0u ||
        desc->property == MOO_COMP_ANIMATION_PROPERTY_INVALID ||
        desc->property > MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D ||
        desc->easing > MOO_COMP_ANIMATION_EASING_IN_OUT_QUAD ||
        desc->direction > MOO_COMP_ANIMATION_DIRECTION_ALTERNATE_REVERSE ||
        desc->flags != 0u || desc->reserved != 0u)
        return MOO_COMP_INVALID;
    if (desc->duration_ns < limits->min_animation_duration_ns ||
        desc->duration_ns > limits->max_animation_duration_ns)
        return MOO_COMP_LIMIT;
    if ((uint64_t)desc->repeat_count >
        UINT64_MAX / desc->duration_ns)
        return MOO_COMP_LIMIT;
    duration_total = desc->duration_ns * (uint64_t)desc->repeat_count;
    if (desc->delay_ns > UINT64_MAX - duration_total ||
        desc->delay_ns + duration_total >
            limits->max_animation_timeline_ns)
        return MOO_COMP_LIMIT;
    result = moo_comp_animation_validate_value(
        desc->property, &desc->from, limits, enabled_effect_mask);
    if (result != MOO_COMP_OK) return result;
    return moo_comp_animation_validate_value(
        desc->property, &desc->to, limits, enabled_effect_mask);
}

MooCompResult moo_comp_animation_ease(
    uint32_t easing,
    uint32_t progress,
    uint32_t *out_eased) {
    const uint64_t q = MOO_COMP_ANIMATION_PROGRESS_ONE;
    uint64_t p = progress;
    uint64_t inverse;
    uint64_t value;
    if (!out_eased || p > q) return MOO_COMP_INVALID;
    switch (easing) {
        case MOO_COMP_ANIMATION_EASING_LINEAR:
            value = p;
            break;
        case MOO_COMP_ANIMATION_EASING_IN_QUAD:
            value = moo_comp_animation_round_unsigned_div(p * p, q);
            break;
        case MOO_COMP_ANIMATION_EASING_OUT_QUAD:
            inverse = q - p;
            value = q - moo_comp_animation_round_unsigned_div(
                inverse * inverse, q);
            break;
        case MOO_COMP_ANIMATION_EASING_IN_OUT_QUAD:
            if (p * 2u <= q) {
                value = moo_comp_animation_round_unsigned_div(
                    2u * p * p, q);
            } else {
                inverse = q - p;
                value = q - moo_comp_animation_round_unsigned_div(
                    2u * inverse * inverse, q);
            }
            break;
        default:
            return MOO_COMP_INVALID;
    }
    *out_eased = (uint32_t)value;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_animation_interpolate(
    uint32_t property,
    const MooCompAnimationValue *from,
    const MooCompAnimationValue *to,
    uint32_t eased,
    MooCompAnimationValue *out_value) {
    uint32_t i;
    uint32_t color_word = UINT32_MAX;
    uint32_t discrete_word = UINT32_MAX;
    uint32_t signed_words = 0u;
    if (!from || !to || !out_value ||
        eased > MOO_COMP_ANIMATION_PROGRESS_ONE)
        return MOO_COMP_INVALID;
    switch (property) {
        case MOO_COMP_ANIMATION_PROPERTY_OPACITY:
            break;
        case MOO_COMP_ANIMATION_PROPERTY_CORNERS:
            break;
        case MOO_COMP_ANIMATION_PROPERTY_SHADOW:
            signed_words = UINT32_C(0x3);
            color_word = 4u;
            break;
        case MOO_COMP_ANIMATION_PROPERTY_BACKDROP:
            color_word = 2u;
            discrete_word = 5u;
            break;
        case MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D:
            signed_words = UINT32_C(0xff);
            break;
        default:
            return MOO_COMP_INVALID;
    }
    moo_comp_animation_value_zero(out_value);
    for (i = 0u; i < MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS; ++i) {
        int64_t a;
        int64_t b;
        int64_t value;
        int64_t delta;
        if (i == discrete_word) {
            out_value->word[i] =
                eased < MOO_COMP_ANIMATION_PROGRESS_ONE ?
                from->word[i] : to->word[i];
            continue;
        }
        if (i == color_word) {
            uint32_t shift;
            uint32_t packed = 0u;
            for (shift = 0u; shift < 32u; shift += 8u) {
                uint32_t av = (from->word[i] >> shift) & UINT32_C(0xff);
                uint32_t bv = (to->word[i] >> shift) & UINT32_C(0xff);
                int64_t channel_delta = (int64_t)bv - (int64_t)av;
                int64_t channel = (int64_t)av +
                    moo_comp_animation_round_signed_div(
                        channel_delta * (int64_t)eased,
                        MOO_COMP_ANIMATION_PROGRESS_ONE);
                packed |= (uint32_t)channel << shift;
            }
            out_value->word[i] = packed;
            continue;
        }
        if ((signed_words & (UINT32_C(1) << i)) != 0u) {
            a = moo_comp_animation_decode_signed(from->word[i]);
            b = moo_comp_animation_decode_signed(to->word[i]);
        } else {
            a = (int64_t)from->word[i];
            b = (int64_t)to->word[i];
        }
        delta = b - a;
        value = a + moo_comp_animation_round_signed_div(
            delta * (int64_t)eased,
            MOO_COMP_ANIMATION_PROGRESS_ONE);
        if ((signed_words & (UINT32_C(1) << i)) != 0u)
            out_value->word[i] = moo_comp_animation_encode_signed(value);
        else
            out_value->word[i] = (uint32_t)value;
    }
    return MOO_COMP_OK;
}

MooCompResult moo_comp_animation_start(
    MooCompAnimationTimeline *timeline,
    MooCompHandle surface,
    const MooCompAnimationDesc *desc,
    const MooCompAnimationValue *current,
    uint64_t enabled_effect_mask,
    uint64_t accepted_frame_ns,
    uint32_t reduced_motion,
    MooCompAnimationValue *out_value,
    MooCompAnimationCompletion *out_completions,
    uint32_t completion_capacity,
    uint32_t *out_completion_count) {
    uint32_t i;
    uint32_t replace = UINT32_MAX;
    uint32_t target;
    uint32_t surface_count = 0u;
    uint32_t completion_count;
    uint64_t duration_total;
    uint64_t start_ns;
    MooCompResult result;
    if (!out_completion_count) return MOO_COMP_INVALID;
    *out_completion_count = 0u;
    if (!moo_comp_animation_timeline_valid(timeline) ||
        surface == MOO_COMP_HANDLE_INVALID || !current || !out_value ||
        reduced_motion > 1u)
        return MOO_COMP_INVALID;
    result = moo_comp_animation_validate_time(timeline, accepted_frame_ns);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_animation_validate_desc(
        desc, &timeline->limits, enabled_effect_mask);
    if (result != MOO_COMP_OK) return result;
    for (i = 0u; i < timeline->capacity; ++i) {
        MooCompAnimationSlot *slot = &timeline->slots[i];
        if (!slot->active) continue;
        if (slot->desc.token == desc->token) return MOO_COMP_INVALID;
        if (slot->surface == surface) {
            ++surface_count;
            if (slot->desc.property == desc->property) replace = i;
        }
    }
    if (replace != UINT32_MAX) {
        if (!moo_comp_animation_value_equal(
                &timeline->slots[replace].current, &desc->from))
            return MOO_COMP_INVALID;
    } else {
        if (!moo_comp_animation_value_equal(current, &desc->from))
            return MOO_COMP_INVALID;
        if (surface_count >=
                timeline->limits.max_animations_per_surface ||
            timeline->active_count >=
                timeline->limits.max_animations_per_client ||
            timeline->active_count >= timeline->capacity)
            return MOO_COMP_LIMIT;
    }
    completion_count =
        (replace != UINT32_MAX ? 1u : 0u) + (reduced_motion ? 1u : 0u);
    if (!moo_comp_animation_outputs_valid(
            out_completions, completion_capacity, completion_count))
        return MOO_COMP_LIMIT;
    duration_total =
        desc->duration_ns * (uint64_t)desc->repeat_count;
    if (accepted_frame_ns > UINT64_MAX - desc->delay_ns)
        return MOO_COMP_LIMIT;
    start_ns = accepted_frame_ns + desc->delay_ns;
    if (start_ns > UINT64_MAX - duration_total)
        return MOO_COMP_LIMIT;
    target = UINT32_MAX;
    if (!reduced_motion) {
        target = replace != UINT32_MAX ?
            replace : moo_comp_animation_find_free(timeline);
        if (target >= timeline->capacity) return MOO_COMP_LIMIT;
    }

    completion_count = 0u;
    if (replace != UINT32_MAX) {
        MooCompAnimationSlot *old = &timeline->slots[replace];
        moo_comp_animation_write_completion(
            &out_completions[completion_count++], old->surface,
            old->desc.token, MOO_COMP_ANIMATION_DONE_REPLACED,
            accepted_frame_ns);
        old->active = 0u;
        --timeline->active_count;
    }
    if (reduced_motion) {
        *out_value = desc->to;
        moo_comp_animation_write_completion(
            &out_completions[completion_count++], surface, desc->token,
            MOO_COMP_ANIMATION_DONE_REDUCED_MOTION,
            accepted_frame_ns);
    } else {
        timeline->slots[target].surface = surface;
        timeline->slots[target].desc = *desc;
        timeline->slots[target].current = desc->from;
        timeline->slots[target].start_ns = start_ns;
        timeline->slots[target].end_ns = start_ns + duration_total;
        timeline->slots[target].active = 1u;
        timeline->slots[target].reserved = 0u;
        ++timeline->active_count;
        *out_value = desc->from;
    }
    timeline->last_timestamp_ns = accepted_frame_ns;
    timeline->has_timestamp = 1u;
    *out_completion_count = completion_count;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_animation_evaluate(
    MooCompAnimationTimeline *timeline,
    uint64_t timestamp_ns,
    uint32_t reduced_motion,
    MooCompAnimationSample *out_samples,
    uint32_t sample_capacity,
    uint32_t *out_sample_count,
    MooCompAnimationCompletion *out_completions,
    uint32_t completion_capacity,
    uint32_t *out_completion_count) {
    uint32_t i;
    uint32_t sample_count = 0u;
    uint32_t completion_count = 0u;
    MooCompResult result;
    if (!out_sample_count || !out_completion_count ||
        reduced_motion > 1u)
        return MOO_COMP_INVALID;
    *out_sample_count = 0u;
    *out_completion_count = 0u;
    result = moo_comp_animation_validate_time(timeline, timestamp_ns);
    if (result != MOO_COMP_OK) return result;
    if (!moo_comp_animation_outputs_valid(
            out_samples, sample_capacity, timeline->active_count))
        return MOO_COMP_LIMIT;

    for (i = 0u; i < timeline->capacity; ++i) {
        MooCompAnimationValue value;
        uint32_t terminal;
        if (!timeline->slots[i].active) continue;
        result = moo_comp_animation_eval_slot(
            &timeline->slots[i], timestamp_ns, reduced_motion,
            &value, &terminal);
        if (result != MOO_COMP_OK) return result;
        if (terminal) ++completion_count;
    }
    if (!moo_comp_animation_outputs_valid(
            out_completions, completion_capacity, completion_count))
        return MOO_COMP_LIMIT;

    completion_count = 0u;
    for (i = 0u; i < timeline->capacity; ++i) {
        MooCompAnimationSlot *slot = &timeline->slots[i];
        MooCompAnimationValue value;
        uint32_t terminal;
        if (!slot->active) continue;
        (void)moo_comp_animation_eval_slot(
            slot, timestamp_ns, reduced_motion, &value, &terminal);
        slot->current = value;
        out_samples[sample_count].surface = slot->surface;
        out_samples[sample_count].token = slot->desc.token;
        out_samples[sample_count].property = slot->desc.property;
        out_samples[sample_count].terminal = terminal;
        out_samples[sample_count].value = value;
        ++sample_count;
        if (terminal) {
            moo_comp_animation_write_completion(
                &out_completions[completion_count++],
                slot->surface, slot->desc.token,
                reduced_motion ?
                    MOO_COMP_ANIMATION_DONE_REDUCED_MOTION :
                    MOO_COMP_ANIMATION_DONE_COMPLETED,
                timestamp_ns);
            slot->active = 0u;
            --timeline->active_count;
        }
    }
    timeline->last_timestamp_ns = timestamp_ns;
    timeline->has_timestamp = 1u;
    *out_sample_count = sample_count;
    *out_completion_count = completion_count;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_animation_cancel(
    MooCompAnimationTimeline *timeline,
    MooCompHandle surface,
    uint64_t token,
    uint64_t timestamp_ns,
    MooCompAnimationCompletion *out_completions,
    uint32_t completion_capacity,
    uint32_t *out_completion_count) {
    uint32_t i;
    MooCompResult result;
    if (!out_completion_count) return MOO_COMP_INVALID;
    *out_completion_count = 0u;
    result = moo_comp_animation_validate_time(timeline, timestamp_ns);
    if (result != MOO_COMP_OK) return result;
    if (surface == MOO_COMP_HANDLE_INVALID || token == 0u)
        return MOO_COMP_INVALID;
    for (i = 0u; i < timeline->capacity; ++i) {
        MooCompAnimationSlot *slot = &timeline->slots[i];
        if (slot->active && slot->surface == surface &&
            slot->desc.token == token) {
            if (!moo_comp_animation_outputs_valid(
                    out_completions, completion_capacity, 1u))
                return MOO_COMP_LIMIT;
            moo_comp_animation_write_completion(
                &out_completions[0], surface, token,
                MOO_COMP_ANIMATION_DONE_CANCELLED, timestamp_ns);
            slot->active = 0u;
            --timeline->active_count;
            timeline->last_timestamp_ns = timestamp_ns;
            timeline->has_timestamp = 1u;
            *out_completion_count = 1u;
            return MOO_COMP_OK;
        }
    }
    return MOO_COMP_INVALID;
}

MooCompResult moo_comp_animation_destroy_surface(
    MooCompAnimationTimeline *timeline,
    MooCompHandle surface,
    uint64_t timestamp_ns,
    MooCompAnimationCompletion *out_completions,
    uint32_t completion_capacity,
    uint32_t *out_completion_count) {
    uint32_t i;
    uint32_t count = 0u;
    MooCompResult result;
    if (!out_completion_count) return MOO_COMP_INVALID;
    *out_completion_count = 0u;
    result = moo_comp_animation_validate_time(timeline, timestamp_ns);
    if (result != MOO_COMP_OK) return result;
    if (surface == MOO_COMP_HANDLE_INVALID) return MOO_COMP_INVALID;
    for (i = 0u; i < timeline->capacity; ++i) {
        if (timeline->slots[i].active &&
            timeline->slots[i].surface == surface)
            ++count;
    }
    if (!moo_comp_animation_outputs_valid(
            out_completions, completion_capacity, count))
        return MOO_COMP_LIMIT;
    count = 0u;
    for (i = 0u; i < timeline->capacity; ++i) {
        MooCompAnimationSlot *slot = &timeline->slots[i];
        if (!slot->active || slot->surface != surface) continue;
        moo_comp_animation_write_completion(
            &out_completions[count++], surface, slot->desc.token,
            MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED, timestamp_ns);
        slot->active = 0u;
        --timeline->active_count;
    }
    timeline->last_timestamp_ns = timestamp_ns;
    timeline->has_timestamp = 1u;
    *out_completion_count = count;
    return MOO_COMP_OK;
}

uint32_t moo_comp_animation_active_count(
    const MooCompAnimationTimeline *timeline) {
    if (!moo_comp_animation_timeline_valid(timeline)) return 0u;
    return timeline->active_count;
}

static uint64_t moo_comp_animation_hash_mix(
    uint64_t hash,
    uint64_t value) {
    uint32_t i;
    for (i = 0u; i < 8u; ++i) {
        hash ^= (uint8_t)(value >> (i * 8u));
        hash *= MOO_COMP_ANIMATION_FNV_PRIME;
    }
    return hash;
}

uint64_t moo_comp_animation_state_hash(
    const MooCompAnimationTimeline *timeline) {
    uint64_t hash = MOO_COMP_ANIMATION_FNV_OFFSET;
    uint32_t i;
    uint32_t word;
    if (!moo_comp_animation_timeline_valid(timeline)) return 0u;
    hash = moo_comp_animation_hash_mix(hash, timeline->capacity);
    hash = moo_comp_animation_hash_mix(hash, timeline->active_count);
    hash = moo_comp_animation_hash_mix(hash, timeline->has_timestamp);
    hash = moo_comp_animation_hash_mix(hash, timeline->last_timestamp_ns);
    for (i = 0u; i < timeline->capacity; ++i) {
        const MooCompAnimationSlot *slot = &timeline->slots[i];
        hash = moo_comp_animation_hash_mix(hash, slot->active);
        if (!slot->active) continue;
        hash = moo_comp_animation_hash_mix(hash, i);
        hash = moo_comp_animation_hash_mix(hash, slot->surface);
        hash = moo_comp_animation_hash_mix(hash, slot->desc.token);
        hash = moo_comp_animation_hash_mix(hash, slot->desc.delay_ns);
        hash = moo_comp_animation_hash_mix(hash, slot->desc.duration_ns);
        hash = moo_comp_animation_hash_mix(hash, slot->desc.repeat_count);
        hash = moo_comp_animation_hash_mix(hash, slot->desc.property);
        hash = moo_comp_animation_hash_mix(hash, slot->desc.easing);
        hash = moo_comp_animation_hash_mix(hash, slot->desc.direction);
        hash = moo_comp_animation_hash_mix(hash, slot->start_ns);
        hash = moo_comp_animation_hash_mix(hash, slot->end_ns);
        for (word = 0u;
             word < MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS; ++word) {
            hash = moo_comp_animation_hash_mix(
                hash, slot->desc.from.word[word]);
            hash = moo_comp_animation_hash_mix(
                hash, slot->desc.to.word[word]);
            hash = moo_comp_animation_hash_mix(
                hash, slot->current.word[word]);
        }
    }
    return hash;
}
