#ifndef MOO_COMPOSITOR_EFFECTS_MATH_H
#define MOO_COMPOSITOR_EFFECTS_MATH_H
#include <stdint.h>
#include "moo_compositor_protocol.h"

typedef struct {
    MooCompQ16 x;
    MooCompQ16 y;
} MooCompEffectPointQ16;

typedef struct {
    MooCompRect content_bounds;
    MooCompRect visual_bounds;
    MooCompRect backdrop_sample_bounds;
} MooCompEffectBounds;

MooCompResult moo_comp_effect_q16_mul(
    MooCompQ16 a, MooCompQ16 b, MooCompQ16 *out);
MooCompResult moo_comp_effect_q16_div(
    MooCompQ16 numerator, MooCompQ16 denominator, MooCompQ16 *out);

MooCompResult moo_comp_effect_affine_transform_point(
    const MooCompAffine2D *affine,
    MooCompEffectPointQ16 point,
    MooCompEffectPointQ16 *out);
MooCompResult moo_comp_effect_affine_inverse(
    const MooCompAffine2D *affine, MooCompAffine2D *out_inverse);
MooCompResult moo_comp_effect_transform_rect_aabb(
    const MooCompAffine2D *affine,
    MooCompRect local_rect,
    MooCompRect *out_bounds);

MooCompResult moo_comp_effect_rect_union(
    MooCompRect a, MooCompRect b, MooCompRect *out);
MooCompResult moo_comp_effect_rect_intersect(
    MooCompRect a, MooCompRect b, MooCompRect *out);
MooCompResult moo_comp_effect_rect_expand(
    MooCompRect rect, int32_t radius, MooCompRect *out);

MooCompResult moo_comp_effect_corners_normalize(
    int32_t width, int32_t height,
    const MooCompCorners *requested, MooCompCorners *out_normalized);

/*
 * Binary A8 coverage required by G0 pixel-center semantics: 255 inside,
 * including an exact ellipse boundary, and 0 outside.
 */
MooCompResult moo_comp_effect_rounded_coverage_a8(
    int32_t width, int32_t height, const MooCompCorners *normalized,
    int32_t pixel_x, int32_t pixel_y, uint8_t *out_coverage);

MooCompResult moo_comp_effect_compute_bounds(
    MooCompRect local_rect, const MooCompAffine2D *affine,
    uint64_t enabled_mask, const MooCompShadow *shadow,
    const MooCompBackdrop *backdrop, MooCompEffectBounds *out_bounds);

#endif
