/*
 * P016-O5 G0: compositor effects protocol V2 contract and ABI gates.
 *
 * This test checks the in-process C value ABI only. These structs are not a
 * wire format: codecs serialize fields individually and never copy padding,
 * enum storage, alignment, unions, or host byte order.
 *
 * Strict link:
 *   cc -std=c11 -Wall -Wextra -Werror -pedantic -Icompiler/runtime \
 *      compiler/runtime/tests/test_compositor_effects_protocol.c \
 *      compiler/runtime/moo_compositor_core.c \
 *      compiler/runtime/moo_compositor_raster.c \
 *      -o /tmp/test_compositor_effects_protocol
 */
#include "../moo_compositor_core.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_checks;
static int g_failures;

#define CHECK(condition, message) do {                                      \
    g_checks++;                                                             \
    if (!(condition)) {                                                     \
        g_failures++;                                                       \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message)); \
    }                                                                       \
} while (0)

#define FIELD_IS_TYPE(expression, expected_type) \
    _Generic((expression), expected_type: 1, default: 0)

/* Literal V1 stability and explicit V2 negotiation constants. */
_Static_assert(MOO_COMP_PROTOCOL_VERSION_V1 == UINT32_C(1),
               "V1 version changed");
_Static_assert(MOO_COMP_PROTOCOL_VERSION == UINT32_C(1),
               "legacy V1 dispatcher alias changed");
_Static_assert(MOO_COMP_PROTOCOL_VERSION_V2 == UINT32_C(2),
               "V2 version changed");
_Static_assert(MOO_COMP_PROTOCOL_MIN_VERSION == UINT32_C(1),
               "minimum must remain V1");
_Static_assert(MOO_COMP_PROTOCOL_MAX_VERSION == UINT32_C(2),
               "maximum must be V2");
_Static_assert(MOO_COMP_PROTOCOL_CURRENT_VERSION == UINT32_C(2),
               "current negotiation version must be V2");
_Static_assert(MOO_COMP_EFFECTS_PROTOCOL_VERSION == UINT32_C(2),
               "effects protocol must identify V2");

_Static_assert(MOO_COMP_OK == 0, "V1 result changed");
_Static_assert(MOO_COMP_INVALID == 1, "V1 result changed");
_Static_assert(MOO_COMP_LIMIT == 2, "V1 result changed");
_Static_assert(MOO_COMP_STALE_HANDLE == 3, "V1 result changed");
_Static_assert(MOO_COMP_ACCESS == 4, "V1 result changed");
_Static_assert(MOO_COMP_WRONG_KIND == 5, "V1 result changed");
_Static_assert(MOO_COMP_BAD_STATE == 6, "V1 result changed");
_Static_assert(MOO_COMP_BAD_BUFFER == 7, "V1 result changed");
_Static_assert(MOO_COMP_WOULD_BLOCK == 8, "V1 result changed");
_Static_assert(MOO_COMP_UNSUPPORTED == 9, "V1 result changed");
_Static_assert(MOO_COMP_VERSION == 10, "V1 result changed");
_Static_assert(MOO_COMP_FORMAT_INVALID == 0, "V1 format changed");
_Static_assert(MOO_COMP_FORMAT_RGBA8888 == 1, "V1 format changed");

_Static_assert(MOO_COMP_FEATURE_SURFACES == (UINT64_C(1) << 0),
               "V1 feature changed");
_Static_assert(MOO_COMP_FEATURE_DAMAGE == (UINT64_C(1) << 1),
               "V1 feature changed");
_Static_assert(MOO_COMP_FEATURE_Z_ORDER == (UINT64_C(1) << 2),
               "V1 feature changed");
_Static_assert(MOO_COMP_FEATURE_INTEGER_SCALE == (UINT64_C(1) << 3),
               "V1 feature changed");
_Static_assert(MOO_COMP_FEATURE_OPACITY == (UINT64_C(1) << 4),
               "V1 feature changed");
_Static_assert(MOO_COMP_FEATURE_FRAME_CALLBACK == (UINT64_C(1) << 5),
               "V1 feature changed");
_Static_assert(MOO_COMP_FEATURE_CURSOR == (UINT64_C(1) << 6),
               "V1 feature changed");
_Static_assert(MOO_COMP_FEATURES_V1 == UINT64_C(0x7f),
               "V1 feature aggregate changed");

_Static_assert(MOO_COMP_REQUEST_INVALID == 0, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_CREATE == 1, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_DESTROY == 2, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_ATTACH == 3, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_DAMAGE_BUFFER == 4,
               "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_SET_SCALE == 5, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_SET_OPACITY == 6,
               "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_FRAME == 7, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_SURFACE_COMMIT == 8, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_BUFFER_CREATE == 16, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_BUFFER_DESTROY == 17, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_CURSOR_SET_BUFFER == 24, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_CURSOR_HIDE == 25, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_CLIPBOARD_SET == 32, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_CLIPBOARD_OFFER == 33, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_CLIPBOARD_RECEIVE == 34, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_DND_BEGIN == 40, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_DND_ACCEPT == 41, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_DND_DROP == 42, "V1 request changed");
_Static_assert(MOO_COMP_REQUEST_DND_CANCEL == 43, "V1 request changed");

_Static_assert(MOO_COMP_EVENT_INVALID == 0, "V1 event changed");
_Static_assert(MOO_COMP_EVENT_BUFFER_RELEASE == 1, "V1 event changed");
_Static_assert(MOO_COMP_EVENT_FRAME_DONE == 2, "V1 event changed");
_Static_assert(MOO_COMP_FRAME_PRESENTED == 1, "V1 frame status changed");
_Static_assert(MOO_COMP_FRAME_SUPERSEDED == 2, "V1 frame status changed");
_Static_assert(MOO_COMP_FRAME_OCCLUDED == 3, "V1 frame status changed");
_Static_assert(MOO_COMP_FRAME_CANCELLED == 4, "V1 frame status changed");

/* V2 effect bits and capability bits are unique and mapped one-to-one. */
_Static_assert(MOO_COMP_EFFECT_CORNER_CLIP == (UINT64_C(1) << 0),
               "corner mask changed");
_Static_assert(MOO_COMP_EFFECT_SHADOW == (UINT64_C(1) << 1),
               "shadow mask changed");
_Static_assert(MOO_COMP_EFFECT_BACKDROP_BLUR == (UINT64_C(1) << 2),
               "blur mask changed");
_Static_assert(MOO_COMP_EFFECT_SATURATION == (UINT64_C(1) << 3),
               "saturation mask changed");
_Static_assert(MOO_COMP_EFFECT_TINT == (UINT64_C(1) << 4),
               "tint mask changed");
_Static_assert(MOO_COMP_EFFECT_NOISE == (UINT64_C(1) << 5),
               "noise mask changed");
_Static_assert(MOO_COMP_EFFECT_AFFINE_2D == (UINT64_C(1) << 6),
               "affine mask changed");
_Static_assert(MOO_COMP_EFFECT_ANIMATION == (UINT64_C(1) << 7),
               "animation mask changed");
_Static_assert(MOO_COMP_EFFECTS_V2 == UINT64_C(0xff),
               "V2 effect mask must contain exactly eight unique bits");

_Static_assert(MOO_COMP_FEATURE_CORNER_CLIP ==
                   (MOO_COMP_EFFECT_CORNER_CLIP << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURE_SHADOW == (MOO_COMP_EFFECT_SHADOW << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURE_BACKDROP_BLUR ==
                   (MOO_COMP_EFFECT_BACKDROP_BLUR << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURE_SATURATION ==
                   (MOO_COMP_EFFECT_SATURATION << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURE_TINT == (MOO_COMP_EFFECT_TINT << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURE_NOISE == (MOO_COMP_EFFECT_NOISE << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURE_AFFINE_2D ==
                   (MOO_COMP_EFFECT_AFFINE_2D << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURE_ANIMATION ==
                   (MOO_COMP_EFFECT_ANIMATION << 7),
               "feature/effect mapping changed");
_Static_assert(MOO_COMP_FEATURES_EFFECTS_V2 == UINT64_C(0x7f80),
               "V2 feature aggregate changed");
_Static_assert((MOO_COMP_FEATURES_EFFECTS_V2 & MOO_COMP_FEATURES_V1) == 0,
               "V2 features overlap V1");
_Static_assert((MOO_COMP_FEATURES_EFFECTS_V2 &
                (MOO_COMP_FEATURE_CLIPBOARD |
                 MOO_COMP_FEATURE_DRAG_AND_DROP)) == 0,
               "V2 features overlap reserved V1 extensions");
_Static_assert(MOO_COMP_FEATURES_V2 ==
                   (MOO_COMP_FEATURES_V1 | MOO_COMP_FEATURES_EFFECTS_V2),
               "V2 feature aggregate is incomplete");

_Static_assert(MOO_COMP_REQUEST_SURFACE_SET_EFFECTS == 44,
               "V2 request must append after V1");
_Static_assert(MOO_COMP_REQUEST_SURFACE_ANIMATE_EFFECT == 45,
               "V2 request must be unique");
_Static_assert(MOO_COMP_REQUEST_SURFACE_CANCEL_ANIMATION == 46,
               "V2 request must be unique");
_Static_assert(MOO_COMP_EVENT_EFFECT_STATUS == 3,
               "V2 event must append after V1");
_Static_assert(MOO_COMP_EVENT_ANIMATION_DONE == 4,
               "V2 event must be unique");

/* Legacy V1 record ABI is frozen and cannot absorb the larger V2 payloads. */
_Static_assert(sizeof(MooCompMessageHeader) == 24,
               "message header ABI changed");
_Static_assert(offsetof(MooCompMessageHeader, version) == 0,
               "message header offset changed");
_Static_assert(offsetof(MooCompMessageHeader, opcode) == 4,
               "message header offset changed");
_Static_assert(offsetof(MooCompMessageHeader, byte_length) == 8,
               "message header offset changed");
_Static_assert(offsetof(MooCompMessageHeader, reserved) == 12,
               "message header offset changed");
_Static_assert(offsetof(MooCompMessageHeader, serial) == 16,
               "message header offset changed");
_Static_assert(sizeof(MooCompRequest) == 88,
               "legacy V1 request must remain 88 bytes");
_Static_assert(offsetof(MooCompRequest, header) == 0,
               "V1 request header offset changed");
_Static_assert(offsetof(MooCompRequest, payload) == 24,
               "V1 request payload offset changed");
_Static_assert(sizeof(MooCompEvent) == 40,
               "legacy V1 event must remain 40 bytes");
_Static_assert(offsetof(MooCompEvent, type) == 0, "V1 event offset changed");
_Static_assert(offsetof(MooCompEvent, status) == 4,
               "V1 event offset changed");
_Static_assert(offsetof(MooCompEvent, object) == 8,
               "V1 event offset changed");
_Static_assert(offsetof(MooCompEvent, token) == 16,
               "V1 event offset changed");
_Static_assert(offsetof(MooCompEvent, present_sequence) == 24,
               "V1 event offset changed");
_Static_assert(offsetof(MooCompEvent, timestamp_ns) == 32,
               "V1 event offset changed");

/* Fixed-width effect value ABI. This is not permission to raw-serialize it. */
_Static_assert(sizeof(MooCompQ16) == 4, "Q16 width changed");
_Static_assert(FIELD_IS_TYPE(((MooCompAffine2D *)0)->m11, MooCompQ16),
               "affine coefficients must use MooCompQ16");
_Static_assert(sizeof(MooCompRgba8) == 4, "RGBA8 ABI changed");
_Static_assert(offsetof(MooCompRgba8, r) == 0, "RGBA8 offset changed");
_Static_assert(offsetof(MooCompRgba8, a) == 3, "RGBA8 offset changed");
_Static_assert(sizeof(MooCompCorners) == 8, "corners ABI changed");
_Static_assert(offsetof(MooCompCorners, top_left) == 0,
               "corners offset changed");
_Static_assert(offsetof(MooCompCorners, top_right) == 2,
               "corners offset changed");
_Static_assert(offsetof(MooCompCorners, bottom_right) == 4,
               "corners offset changed");
_Static_assert(offsetof(MooCompCorners, bottom_left) == 6,
               "corners offset changed");

_Static_assert(sizeof(MooCompShadow) == 16, "shadow ABI changed");
_Static_assert(offsetof(MooCompShadow, offset_x) == 0,
               "shadow offset changed");
_Static_assert(offsetof(MooCompShadow, offset_y) == 4,
               "shadow offset changed");
_Static_assert(offsetof(MooCompShadow, blur_radius) == 8,
               "shadow offset changed");
_Static_assert(offsetof(MooCompShadow, spread_radius) == 10,
               "shadow offset changed");
_Static_assert(offsetof(MooCompShadow, color) == 12,
               "shadow offset changed");

_Static_assert(sizeof(MooCompBackdrop) == 16, "backdrop ABI changed");
_Static_assert(offsetof(MooCompBackdrop, blur_radius) == 0,
               "backdrop offset changed");
_Static_assert(offsetof(MooCompBackdrop, saturation_q8_8) == 2,
               "backdrop offset changed");
_Static_assert(offsetof(MooCompBackdrop, tint) == 4,
               "backdrop offset changed");
_Static_assert(offsetof(MooCompBackdrop, tint_mix) == 8,
               "backdrop offset changed");
_Static_assert(offsetof(MooCompBackdrop, noise) == 9,
               "backdrop offset changed");
_Static_assert(offsetof(MooCompBackdrop, reserved) == 10,
               "backdrop offset changed");
_Static_assert(offsetof(MooCompBackdrop, noise_seed) == 12,
               "backdrop offset changed");

_Static_assert(sizeof(MooCompAffine2D) == 32, "affine ABI changed");
_Static_assert(offsetof(MooCompAffine2D, m11) == 0, "affine offset changed");
_Static_assert(offsetof(MooCompAffine2D, m12) == 4, "affine offset changed");
_Static_assert(offsetof(MooCompAffine2D, m21) == 8, "affine offset changed");
_Static_assert(offsetof(MooCompAffine2D, m22) == 12, "affine offset changed");
_Static_assert(offsetof(MooCompAffine2D, tx) == 16, "affine offset changed");
_Static_assert(offsetof(MooCompAffine2D, ty) == 20, "affine offset changed");
_Static_assert(offsetof(MooCompAffine2D, origin_x) == 24,
               "affine offset changed");
_Static_assert(offsetof(MooCompAffine2D, origin_y) == 28,
               "affine offset changed");

_Static_assert(sizeof(MooCompEffectState) == 96, "effect state ABI changed");
_Static_assert(offsetof(MooCompEffectState, enabled_mask) == 0,
               "effect state offset changed");
_Static_assert(offsetof(MooCompEffectState, required_mask) == 8,
               "effect state offset changed");
_Static_assert(offsetof(MooCompEffectState, fallback_policy) == 16,
               "effect state offset changed");
_Static_assert(offsetof(MooCompEffectState, reserved) == 20,
               "effect state offset changed");
_Static_assert(offsetof(MooCompEffectState, corners) == 24,
               "effect state offset changed");
_Static_assert(offsetof(MooCompEffectState, shadow) == 32,
               "effect state offset changed");
_Static_assert(offsetof(MooCompEffectState, backdrop) == 48,
               "effect state offset changed");
_Static_assert(offsetof(MooCompEffectState, affine) == 64,
               "effect state offset changed");

_Static_assert(sizeof(MooCompEffectStatus) == 200,
               "effect status ABI changed");
_Static_assert(offsetof(MooCompEffectStatus, requested) == 0,
               "effect status offset changed");
_Static_assert(offsetof(MooCompEffectStatus, effective) == 96,
               "effect status offset changed");
_Static_assert(offsetof(MooCompEffectStatus, degraded_mask) == 192,
               "effect status offset changed");

_Static_assert(sizeof(MooCompAnimationValue) == 32,
               "animation value ABI changed");
_Static_assert(FIELD_IS_TYPE(((MooCompAnimationValue *)0)->word[0], uint32_t),
               "animation words must be raw uint32_t");
_Static_assert(sizeof(MooCompAnimationDesc) == 112,
               "animation descriptor ABI changed");
_Static_assert(offsetof(MooCompAnimationDesc, token) == 0,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, delay_ns) == 8,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, duration_ns) == 16,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, repeat_count) == 24,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, property) == 28,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, easing) == 32,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, direction) == 36,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, flags) == 40,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, reserved) == 44,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, from) == 48,
               "animation descriptor offset changed");
_Static_assert(offsetof(MooCompAnimationDesc, to) == 80,
               "animation descriptor offset changed");

_Static_assert(sizeof(MooCompEffectLimits) == 80,
               "limits ABI changed");
_Static_assert(offsetof(MooCompEffectLimits, max_corner_radius) == 0,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_shadow_blur_radius) == 2,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_shadow_spread_radius) == 4,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_backdrop_blur_radius) == 6,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_saturation_q8_8) == 8,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, reserved) == 10,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_abs_shadow_offset) == 12,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_abs_affine_coefficient) == 16,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_abs_affine_translation) == 20,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_abs_affine_origin) == 24,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_animations_per_surface) == 28,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_animations_per_client) == 32,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, reserved_alignment) == 36,
               "limits explicit alignment field changed");
_Static_assert(offsetof(MooCompEffectLimits, min_animation_duration_ns) == 40,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_animation_duration_ns) == 48,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_animation_timeline_ns) == 56,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits,
                        max_effect_work_units_per_frame) == 64,
               "limits offset changed");
_Static_assert(offsetof(MooCompEffectLimits, max_effect_scratch_bytes) == 72,
               "limits offset changed");
_Static_assert(FIELD_IS_TYPE(((MooCompEffectLimits *)0)->max_corner_radius,
                             uint16_t),
               "corner limit width/type changed");
_Static_assert(FIELD_IS_TYPE(
                   ((MooCompEffectLimits *)0)->max_abs_shadow_offset, int32_t),
               "shadow offset limit width/type changed");
_Static_assert(FIELD_IS_TYPE(
                   ((MooCompEffectLimits *)0)->max_animation_duration_ns,
                   uint64_t),
               "duration limit width/type changed");

/* Additive typed V2 records: exact record size, field order and byte_length. */
_Static_assert(MOO_COMP_REQUEST_SET_EFFECTS_V2_BYTE_LENGTH == UINT32_C(128),
               "set-effects byte length changed");
_Static_assert(sizeof(MooCompRequestSetEffectsV2) == 128,
               "set-effects V2 ABI changed");
_Static_assert(sizeof(MooCompRequestSetEffectsV2) ==
                   MOO_COMP_REQUEST_SET_EFFECTS_V2_BYTE_LENGTH,
               "set-effects byte length disagrees with value ABI");
_Static_assert(offsetof(MooCompRequestSetEffectsV2, header) == 0,
               "set-effects header must be prefix");
_Static_assert(offsetof(MooCompRequestSetEffectsV2, surface) == 24,
               "set-effects surface offset changed");
_Static_assert(offsetof(MooCompRequestSetEffectsV2, effects) == 32,
               "set-effects payload offset changed");

_Static_assert(MOO_COMP_REQUEST_ANIMATE_EFFECT_V2_BYTE_LENGTH ==
                   UINT32_C(144),
               "animate byte length changed");
_Static_assert(sizeof(MooCompRequestAnimateEffectV2) == 144,
               "animate V2 ABI changed");
_Static_assert(sizeof(MooCompRequestAnimateEffectV2) ==
                   MOO_COMP_REQUEST_ANIMATE_EFFECT_V2_BYTE_LENGTH,
               "animate byte length disagrees with value ABI");
_Static_assert(offsetof(MooCompRequestAnimateEffectV2, header) == 0,
               "animate header must be prefix");
_Static_assert(offsetof(MooCompRequestAnimateEffectV2, surface) == 24,
               "animate surface offset changed");
_Static_assert(offsetof(MooCompRequestAnimateEffectV2, animation) == 32,
               "animate payload offset changed");

_Static_assert(MOO_COMP_REQUEST_CANCEL_ANIMATION_V2_BYTE_LENGTH ==
                   UINT32_C(40),
               "cancel byte length changed");
_Static_assert(sizeof(MooCompRequestCancelAnimationV2) == 40,
               "cancel V2 ABI changed");
_Static_assert(sizeof(MooCompRequestCancelAnimationV2) ==
                   MOO_COMP_REQUEST_CANCEL_ANIMATION_V2_BYTE_LENGTH,
               "cancel byte length disagrees with value ABI");
_Static_assert(offsetof(MooCompRequestCancelAnimationV2, header) == 0,
               "cancel header must be prefix");
_Static_assert(offsetof(MooCompRequestCancelAnimationV2, surface) == 24,
               "cancel surface offset changed");
_Static_assert(offsetof(MooCompRequestCancelAnimationV2, token) == 32,
               "cancel token offset changed");

_Static_assert(MOO_COMP_EVENT_EFFECT_STATUS_V2_BYTE_LENGTH == UINT32_C(232),
               "effect-status byte length changed");
_Static_assert(sizeof(MooCompEventEffectStatusV2) == 232,
               "effect-status V2 ABI changed");
_Static_assert(sizeof(MooCompEventEffectStatusV2) ==
                   MOO_COMP_EVENT_EFFECT_STATUS_V2_BYTE_LENGTH,
               "effect-status byte length disagrees with value ABI");
_Static_assert(offsetof(MooCompEventEffectStatusV2, type) == 0,
               "effect-status type offset changed");
_Static_assert(offsetof(MooCompEventEffectStatusV2, result) == 4,
               "effect-status result offset changed");
_Static_assert(offsetof(MooCompEventEffectStatusV2, surface) == 8,
               "effect-status surface offset changed");
_Static_assert(offsetof(MooCompEventEffectStatusV2, commit_sequence) == 16,
               "effect-status sequence offset changed");
_Static_assert(offsetof(MooCompEventEffectStatusV2, timestamp_ns) == 24,
               "effect-status timestamp offset changed");
_Static_assert(offsetof(MooCompEventEffectStatusV2, effects) == 32,
               "effect-status payload offset changed");

_Static_assert(MOO_COMP_EVENT_ANIMATION_DONE_V2_BYTE_LENGTH == UINT32_C(40),
               "animation-done byte length changed");
_Static_assert(sizeof(MooCompEventAnimationDoneV2) == 40,
               "animation-done V2 ABI changed");
_Static_assert(sizeof(MooCompEventAnimationDoneV2) ==
                   MOO_COMP_EVENT_ANIMATION_DONE_V2_BYTE_LENGTH,
               "animation-done byte length disagrees with value ABI");
_Static_assert(offsetof(MooCompEventAnimationDoneV2, type) == 0,
               "animation-done type offset changed");
_Static_assert(offsetof(MooCompEventAnimationDoneV2, status) == 4,
               "animation-done status offset changed");
_Static_assert(offsetof(MooCompEventAnimationDoneV2, surface) == 8,
               "animation-done surface offset changed");
_Static_assert(offsetof(MooCompEventAnimationDoneV2, token) == 16,
               "animation-done token offset changed");
_Static_assert(offsetof(MooCompEventAnimationDoneV2, commit_sequence) == 24,
               "animation-done sequence offset changed");
_Static_assert(offsetof(MooCompEventAnimationDoneV2, timestamp_ns) == 32,
               "animation-done timestamp offset changed");

static void test_defaults_limits_and_enums(void) {
    CHECK(MOO_COMP_EFFECT_FALLBACK_REQUIRE == 0,
          "zero fallback must fail closed");
    CHECK(MOO_COMP_EFFECT_FALLBACK_ALLOW_DISABLE == 1,
          "disable fallback value changed");
    CHECK(MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE == 2,
          "approximate fallback value changed");
    CHECK(MOO_COMP_ANIMATION_PROPERTY_INVALID == 0,
          "zero property must be invalid");
    CHECK(MOO_COMP_ANIMATION_PROPERTY_OPACITY == 1,
          "opacity property value changed");
    CHECK(MOO_COMP_ANIMATION_PROPERTY_CORNERS == 2,
          "corners property value changed");
    CHECK(MOO_COMP_ANIMATION_PROPERTY_SHADOW == 3,
          "shadow property value changed");
    CHECK(MOO_COMP_ANIMATION_PROPERTY_BACKDROP == 4,
          "backdrop property value changed");
    CHECK(MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D == 5,
          "affine property value changed");
    CHECK(MOO_COMP_ANIMATION_EASING_LINEAR == 0,
          "zero easing must be linear");
    CHECK(MOO_COMP_ANIMATION_EASING_IN_QUAD == 1,
          "easing value changed");
    CHECK(MOO_COMP_ANIMATION_EASING_OUT_QUAD == 2,
          "easing value changed");
    CHECK(MOO_COMP_ANIMATION_EASING_IN_OUT_QUAD == 3,
          "easing value changed");
    CHECK(MOO_COMP_ANIMATION_DIRECTION_NORMAL == 0,
          "zero direction must be normal");
    CHECK(MOO_COMP_ANIMATION_DIRECTION_REVERSE == 1,
          "direction value changed");
    CHECK(MOO_COMP_ANIMATION_DIRECTION_ALTERNATE == 2,
          "direction value changed");
    CHECK(MOO_COMP_ANIMATION_DIRECTION_ALTERNATE_REVERSE == 3,
          "direction value changed");

    CHECK(MOO_COMP_ANIMATION_DONE_COMPLETED == 1,
          "completion status changed");
    CHECK(MOO_COMP_ANIMATION_DONE_CANCELLED == 2,
          "cancel status changed");
    CHECK(MOO_COMP_ANIMATION_DONE_REPLACED == 3,
          "replace status changed");
    CHECK(MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED == 4,
          "destroy status changed");
    CHECK(MOO_COMP_ANIMATION_DONE_REDUCED_MOTION == 5,
          "reduced-motion status changed");

    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS == UINT16_C(4096),
          "corner default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_BLUR_RADIUS == UINT16_C(64),
          "shadow blur default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_SPREAD_RADIUS == UINT16_C(64),
          "shadow spread default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_BACKDROP_BLUR_RADIUS == UINT16_C(32),
          "backdrop blur default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8 == UINT16_C(1024),
          "saturation default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET == INT32_C(4096),
          "shadow offset default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT ==
              INT32_C(16) * MOO_COMP_Q16_ONE,
          "affine coefficient default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION ==
              INT32_C(32767) * MOO_COMP_Q16_ONE,
          "affine translation default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN ==
              INT32_C(32767) * MOO_COMP_Q16_ONE,
          "affine origin default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_SURFACE == UINT32_C(8),
          "surface animation default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_CLIENT == UINT32_C(64),
          "client animation default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MIN_ANIMATION_DURATION_NS ==
              UINT64_C(1000000),
          "minimum duration default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS ==
              UINT64_C(600000000000),
          "maximum duration default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_TIMELINE_NS ==
              UINT64_C(3600000000000),
          "timeline default changed");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8 >=
              MOO_COMP_SATURATION_ONE,
          "default limit must admit saturation identity");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_SURFACE <=
              MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_CLIENT,
          "per-surface animation default exceeds client quota");
    CHECK(MOO_COMP_EFFECT_DEFAULT_MIN_ANIMATION_DURATION_NS <=
              MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS &&
          MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS <=
              MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_TIMELINE_NS,
          "animation duration defaults are inconsistent");
}

static void test_zero_and_reserved_contract(void) {
    MooCompMessageHeader header = {0};
    MooCompEffectState state = {0};
    MooCompEffectStatus status = {0};
    MooCompAnimationDesc animation = {0};
    MooCompEffectLimits limits = {0};
    MooCompRequestSetEffectsV2 set_effects = {0};
    MooCompRequestAnimateEffectV2 animate = {0};

    CHECK(header.reserved == 0, "message reserved input must be zero");
    CHECK(state.enabled_mask == 0, "zero state must enable no effects");
    CHECK(state.required_mask == 0, "zero state must require no effects");
    CHECK(state.fallback_policy == MOO_COMP_EFFECT_FALLBACK_REQUIRE,
          "zero fallback must be REQUIRE");
    CHECK(state.reserved == 0, "effect-state reserved input must be zero");
    CHECK(state.backdrop.reserved == 0,
          "backdrop reserved input must be zero");
    CHECK(status.degraded_mask == 0,
          "zero status must report no degradation");
    CHECK(animation.property == MOO_COMP_ANIMATION_PROPERTY_INVALID,
          "zero animation must not name a property");
    CHECK(animation.flags == 0, "V2 animation flags must be zero");
    CHECK(animation.reserved == 0,
          "animation reserved input must be zero");
    CHECK(animation.from.word[7] == UINT32_C(0) &&
          animation.to.word[7] == UINT32_C(0),
          "unused animation words must default to zero");
    CHECK(limits.reserved == 0, "limits reserved input must be zero");
    CHECK(limits.reserved_alignment == 0,
          "limits alignment-reserved input must be zero");
    CHECK(limits.max_corner_radius == 0,
          "zero limits must permit no corner radius");
    CHECK(limits.max_animations_per_surface == 0,
          "zero limits must permit no animations");
    CHECK(limits.max_effect_work_units_per_frame == 0,
          "zero limits must permit no effect work");
    CHECK(limits.max_effect_scratch_bytes == 0,
          "zero limits must permit no scratch");
    CHECK(set_effects.header.reserved == 0 &&
          set_effects.effects.reserved == 0,
          "set-effects V2 reserved fields must default to zero");
    CHECK(animate.header.reserved == 0 &&
          animate.animation.flags == 0 &&
          animate.animation.reserved == 0,
          "animate V2 reserved fields must default to zero");
}

static void test_masks_and_q16_identity(void) {
    MooCompEffectState state = {0};
    MooCompAffine2D identity = {
        MOO_COMP_Q16_ONE, 0,
        0, MOO_COMP_Q16_ONE,
        0, 0,
        0, 0
    };

    state.enabled_mask = MOO_COMP_EFFECTS_V2;
    state.required_mask = MOO_COMP_EFFECT_CORNER_CLIP |
                          MOO_COMP_EFFECT_AFFINE_2D;
    CHECK((state.required_mask & ~state.enabled_mask) == 0,
          "required effects must be a subset of enabled effects");
    CHECK((state.enabled_mask & ~MOO_COMP_EFFECTS_V2) == 0,
          "known enabled mask must not contain unknown bits");
    CHECK(MOO_COMP_Q16_ONE == INT32_C(65536),
          "Q16.16 identity scalar changed");
    CHECK(identity.m11 == INT32_C(65536) &&
          identity.m12 == 0 &&
          identity.m21 == 0 &&
          identity.m22 == INT32_C(65536),
          "Q16.16 identity matrix changed");
    CHECK(identity.tx == 0 && identity.ty == 0 &&
          identity.origin_x == 0 && identity.origin_y == 0,
          "Q16.16 identity translation/origin changed");
    CHECK(MOO_COMP_SATURATION_ONE == UINT16_C(256),
          "Q8.8 saturation identity changed");
    CHECK(MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS == 8u,
          "animation value width changed");
}

static void test_literal_v1_dispatcher_behavior(void) {
    MooCompositor core;
    MooCompConfig config = {1, 1, 0, 0, 0, 255};
    MooCompClientSlot clients[1] = {{0}};
    MooCompSurfaceSlot surfaces[1] = {{0}};
    MooCompBufferSlot buffers[1] = {{0}};
    MooCompFrameSlot frames[1] = {{0}};
    MooCompEventSlot events[1] = {{0}};
    MooCompHandle client = MOO_COMP_HANDLE_INVALID;
    MooCompRequest request = {0};

    CHECK(moo_comp_init(&core, &config,
                        clients, 1u, surfaces, 1u, buffers, 1u,
                        frames, 1u, events, 1u) == MOO_COMP_OK,
          "dispatcher fixture init failed");
    CHECK(moo_comp_client_create(&core, &client) == MOO_COMP_OK,
          "dispatcher fixture client creation failed");

    request.header.version = UINT32_C(1);
    request.header.opcode = MOO_COMP_REQUEST_DND_CANCEL;
    CHECK(moo_comp_dispatch_stub(&core, client, &request) ==
              MOO_COMP_UNSUPPORTED,
          "literal V1 request must reach the legacy dispatcher");

    request.header.version = UINT32_C(2);
    request.header.opcode = MOO_COMP_REQUEST_SURFACE_SET_EFFECTS;
    CHECK(moo_comp_dispatch_stub(&core, client, &request) == MOO_COMP_VERSION,
          "V2 must remain version-gated until the V2 dispatcher lands");
}

int main(void) {
    test_defaults_limits_and_enums();
    test_zero_and_reserved_contract();
    test_masks_and_q16_identity();
    test_literal_v1_dispatcher_behavior();

    if (g_failures != 0) {
        fprintf(stderr,
                "P016-O5 EFFECTS PROTOCOL FAIL: %d/%d checks failed\n",
                g_failures, g_checks);
        return 1;
    }
    printf("P016-O5 EFFECTS PROTOCOL OK: %d checks\n", g_checks);
    return 0;
}
