#ifndef MOO_COMPOSITOR_ANIMATION_H
#define MOO_COMPOSITOR_ANIMATION_H

#include <stdint.h>

#include "moo_compositor_protocol.h"

#define MOO_COMP_ANIMATION_PROGRESS_ONE UINT32_C(65535)

/* Timeline init mirrors G0/S1: maxima may only tighten, while the
 * minimum animation duration may only increase. Work/scratch stay nonzero
 * and at or below the public defaults from effects_state.h. */

typedef struct {
    MooCompHandle surface;
    uint64_t token;
    uint32_t status;
    uint32_t reserved;
    uint64_t timestamp_ns;
} MooCompAnimationCompletion;

typedef struct {
    MooCompHandle surface;
    uint64_t token;
    uint32_t property;
    uint32_t terminal;
    MooCompAnimationValue value;
} MooCompAnimationSample;

typedef struct {
    MooCompHandle surface;
    MooCompAnimationDesc desc;
    MooCompAnimationValue current;
    uint64_t start_ns;
    uint64_t end_ns;
    uint32_t active;
    uint32_t reserved;
} MooCompAnimationSlot;

typedef struct {
    MooCompAnimationSlot *slots;
    uint32_t capacity;
    uint32_t active_count;
    MooCompEffectLimits limits;
    uint64_t last_timestamp_ns;
    uint32_t has_timestamp;
    uint32_t reserved;
} MooCompAnimationTimeline;

MooCompResult moo_comp_animation_timeline_init(
    MooCompAnimationTimeline *timeline,
    MooCompAnimationSlot *slots,
    uint32_t capacity,
    const MooCompEffectLimits *limits);

MooCompResult moo_comp_animation_validate_desc(
    const MooCompAnimationDesc *desc,
    const MooCompEffectLimits *limits,
    uint64_t enabled_effect_mask);

MooCompResult moo_comp_animation_ease(
    uint32_t easing,
    uint32_t progress,
    uint32_t *out_eased);

MooCompResult moo_comp_animation_interpolate(
    uint32_t property,
    const MooCompAnimationValue *from,
    const MooCompAnimationValue *to,
    uint32_t eased,
    MooCompAnimationValue *out_value);

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
    uint32_t *out_completion_count);

MooCompResult moo_comp_animation_evaluate(
    MooCompAnimationTimeline *timeline,
    uint64_t timestamp_ns,
    uint32_t reduced_motion,
    MooCompAnimationSample *out_samples,
    uint32_t sample_capacity,
    uint32_t *out_sample_count,
    MooCompAnimationCompletion *out_completions,
    uint32_t completion_capacity,
    uint32_t *out_completion_count);

MooCompResult moo_comp_animation_cancel(
    MooCompAnimationTimeline *timeline,
    MooCompHandle surface,
    uint64_t token,
    uint64_t timestamp_ns,
    MooCompAnimationCompletion *out_completions,
    uint32_t completion_capacity,
    uint32_t *out_completion_count);

MooCompResult moo_comp_animation_destroy_surface(
    MooCompAnimationTimeline *timeline,
    MooCompHandle surface,
    uint64_t timestamp_ns,
    MooCompAnimationCompletion *out_completions,
    uint32_t completion_capacity,
    uint32_t *out_completion_count);

uint32_t moo_comp_animation_active_count(
    const MooCompAnimationTimeline *timeline);

uint64_t moo_comp_animation_state_hash(
    const MooCompAnimationTimeline *timeline);

#endif
