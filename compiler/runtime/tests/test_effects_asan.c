/*
 * P016-O5 T0: effect state, capability, alpha and reduced-motion RED gate.
 * No UI, wall clock, allocator or OS backend is used.
 */
#include "../moo_compositor_effects_state.h"
#include "../moo_compositor_effects_cpu.h"
#include "../moo_compositor_animation.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(c, m) do {                                                     \
    checks++;                                                                \
    if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n",             \
                                     __FILE__, __LINE__, (m)); }              \
} while (0)

static uint8_t alpha_over_channel(uint8_t sc, uint8_t sa,
                                  uint8_t dc, uint8_t da) {
    uint64_t a = (uint64_t)sa * UINT64_C(255) +
                 (uint64_t)da * (uint64_t)(UINT32_C(255) - sa);
    uint64_t u = (uint64_t)sc * (uint64_t)sa * UINT64_C(255) +
                 (uint64_t)dc * (uint64_t)da *
                 (uint64_t)(UINT32_C(255) - sa);
    return a == 0u ? 0u : (uint8_t)((u + a / 2u) / a);
}

static uint8_t mutant_alpha_truncation(uint8_t sc, uint8_t sa,
                                       uint8_t dc, uint8_t da) {
    uint64_t a = (uint64_t)sa * UINT64_C(255) +
                 (uint64_t)da * (uint64_t)(UINT32_C(255) - sa);
    uint64_t u = (uint64_t)sc * (uint64_t)sa * UINT64_C(255) +
                 (uint64_t)dc * (uint64_t)da *
                 (uint64_t)(UINT32_C(255) - sa);
    return a == 0u ? 0u : (uint8_t)(u / a);
}

static void test_alpha_rounding_mutant(void) {
    uint32_t sc;
    int detected = 0;
    for (sc = 0u; sc < 256u; sc += 17u) {
        uint8_t good = alpha_over_channel((uint8_t)sc, 113u, 197u, 149u);
        uint8_t bad = mutant_alpha_truncation((uint8_t)sc, 113u, 197u, 149u);
        if (good != bad) detected = 1;
    }
    CHECK(detected, "alpha truncation mutant escaped the oracle");
    CHECK(alpha_over_channel(255u, 255u, 7u, 255u) == 255u,
          "opaque source-over identity changed");
    CHECK(alpha_over_channel(99u, 0u, 37u, 255u) == 37u,
          "transparent source must preserve destination");
}

static MooCompResult mutant_capability_fail_open(void) {
    return MOO_COMP_OK;
}

static uint32_t mutant_reduced_motion_keeps_start(
    const MooCompAnimationDesc *desc) {
    return desc->from.word[0];
}

static void test_capability_fallback_and_atomicity(void) {
    MooCompEffectLimits limits = moo_comp_effect_limits_default();
    MooCompEffectStateConfig config;
    MooCompEffectSurfaceState surface;
    MooCompEffectUsage usage = {0u, 0u, UINT64_C(64)};
    MooCompEffectPreflight preflight;
    MooCompEffectState requested = moo_comp_effect_state_neutral();
    MooCompEffectStatus status;
    MooCompResult required_result;
    uint64_t before;

    CHECK(moo_comp_effect_state_config_init(
              &config, MOO_COMP_EFFECT_TINT, &limits) == MOO_COMP_OK,
          "state config init failed");
    CHECK(moo_comp_effect_surface_init(&surface, UINT64_C(11),
                                       UINT64_C(22)) == MOO_COMP_OK,
          "surface state init failed");

    requested.enabled_mask = MOO_COMP_EFFECT_BACKDROP_BLUR |
                             MOO_COMP_EFFECT_TINT;
    requested.required_mask = MOO_COMP_EFFECT_BACKDROP_BLUR;
    requested.fallback_policy = MOO_COMP_EFFECT_FALLBACK_REQUIRE;
    requested.backdrop.blur_radius = 4u;
    requested.backdrop.tint = (MooCompRgba8){20u, 40u, 60u, 255u};
    requested.backdrop.tint_mix = 128u;
    before = moo_comp_effect_surface_hash(&surface);
    required_result = moo_comp_effect_state_preflight(
        &config, &surface, UINT64_C(11), UINT64_C(22),
        &requested, &usage, &preflight);
    CHECK(required_result == MOO_COMP_UNSUPPORTED,
          "missing required blur must fail closed");
    CHECK(mutant_capability_fail_open() != required_result,
          "capability fail-open mutant escaped the oracle");
    CHECK(moo_comp_effect_surface_hash(&surface) == before,
          "failed REQUIRE preflight mutated surface state");

    requested.required_mask = 0u;
    requested.fallback_policy = MOO_COMP_EFFECT_FALLBACK_ALLOW_DISABLE;
    CHECK(moo_comp_effect_state_preflight(
              &config, &surface, UINT64_C(11), UINT64_C(22),
              &requested, &usage, &preflight) == MOO_COMP_OK,
          "optional blur disable fallback failed");
    CHECK(preflight.effective.enabled_mask == MOO_COMP_EFFECT_TINT,
          "disable fallback removed the wrong effects");
    CHECK(preflight.degraded_mask == MOO_COMP_EFFECT_BACKDROP_BLUR,
          "degraded mask must identify disabled blur");
    moo_comp_effect_state_apply(&surface, &preflight);
    CHECK(moo_comp_effect_surface_status(&surface, &status) == MOO_COMP_OK,
          "effect status not observable");
    CHECK(status.requested.enabled_mask == requested.enabled_mask &&
          status.effective.enabled_mask == MOO_COMP_EFFECT_TINT,
          "requested/effective status mismatch");

    requested.enabled_mask |= UINT64_C(1) << 63;
    before = moo_comp_effect_surface_hash(&surface);
    CHECK(moo_comp_effect_state_preflight(
              &config, &surface, UINT64_C(11), UINT64_C(22),
              &requested, &usage, &preflight) == MOO_COMP_INVALID,
          "unknown effect mask bit must be invalid");
    CHECK(moo_comp_effect_surface_hash(&surface) == before,
          "invalid-mask preflight mutated surface state");
}

static void test_reduced_motion(void) {
    MooCompEffectLimits limits = moo_comp_effect_limits_default();
    MooCompAnimationTimeline timeline;
    MooCompAnimationSlot slots[2];
    MooCompAnimationDesc desc = {0};
    MooCompAnimationValue current = {{0}};
    MooCompAnimationValue out = {{0}};
    MooCompAnimationCompletion completions[2];
    uint32_t completion_count = 0u;

    CHECK(moo_comp_animation_timeline_init(
              &timeline, slots, 2u, &limits) == MOO_COMP_OK,
          "animation timeline init failed");
    desc.token = UINT64_C(7);
    desc.duration_ns = limits.min_animation_duration_ns;
    desc.repeat_count = 1u;
    desc.property = MOO_COMP_ANIMATION_PROPERTY_OPACITY;
    desc.easing = MOO_COMP_ANIMATION_EASING_LINEAR;
    desc.direction = MOO_COMP_ANIMATION_DIRECTION_NORMAL;
    desc.from.word[0] = 0u;
    desc.to.word[0] = (uint32_t)MOO_COMP_Q16_ONE;

    CHECK(moo_comp_animation_start(
              &timeline, UINT64_C(33), &desc, &current, 0u,
              UINT64_C(1000), 1u, &out, completions, 2u,
              &completion_count) == MOO_COMP_OK,
          "reduced-motion animation start failed");
    CHECK(out.word[0] == desc.to.word[0],
          "reduced motion did not publish the endpoint");
    CHECK(mutant_reduced_motion_keeps_start(&desc) != out.word[0],
          "reduced-motion start-value mutant escaped the oracle");
    CHECK(completion_count == 1u &&
          completions[0].status == MOO_COMP_ANIMATION_DONE_REDUCED_MOTION,
          "reduced motion must emit one terminal completion");
    CHECK(moo_comp_animation_active_count(&timeline) == 0u,
          "reduced motion left an active animation");
}

static MooCompResult portable_cpu_renderer_probe(void) {
    uint8_t content[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u
    };
    uint8_t target[16] = {
        12u, 24u, 36u, 255u, 48u, 60u, 72u, 255u,
        84u, 96u, 108u, 255u, 120u, 132u, 144u, 255u
    };
    uint32_t rgba_ping[16];
    uint32_t rgba_pong[16];
    MooCompEffectCpuJob job = {0};
    MooCompEffectCpuScratch scratch = {0};
    MooCompEffectCpuStats stats;

    job.content = (MooCompEffectCpuSource){
        content, sizeof(content), 8u, 2, 2
    };
    job.target = (MooCompEffectCpuTarget){
        target, sizeof(target), 8u, 2, 2
    };
    job.content_rect = (MooCompRect){0, 0, 2, 2};
    job.effect = moo_comp_effect_state_neutral();
    job.effect.enabled_mask = MOO_COMP_EFFECT_TINT;
    job.effect.backdrop.tint = (MooCompRgba8){20u, 40u, 60u, 255u};
    job.effect.backdrop.tint_mix = 128u;
    job.content_opacity = 255u;
    scratch.rgba_ping = rgba_ping;
    scratch.rgba_pong = rgba_pong;
    scratch.rgba_words_per_buffer = 16u;
    return moo_comp_effect_cpu_render(&job, &scratch, &stats);
}

static int cpu_stats_eq(
    const MooCompEffectCpuStats *a,
    const MooCompEffectCpuStats *b) {
    return a->affected_pixels == b->affected_pixels &&
           a->work_units == b->work_units &&
           a->scratch_bytes == b->scratch_bytes &&
           a->rgba_words_per_buffer == b->rgba_words_per_buffer &&
           a->mask_words_per_buffer == b->mask_words_per_buffer;
}

static void test_cpu_blur_unpremul_clamp(void) {
    uint8_t content[36] = {0};
    uint8_t target[36] = {0};
    uint32_t rgba_ping[36];
    uint32_t rgba_pong[36];
    MooCompEffectCpuJob job = {0};
    MooCompEffectCpuScratch scratch = {0};
    MooCompEffectCpuStats stats;
    MooCompResult result;

    target[16] = 255u;
    target[19] = 255u;
    job.content = (MooCompEffectCpuSource){
        content, sizeof(content), 12u, 3, 3
    };
    job.target = (MooCompEffectCpuTarget){
        target, sizeof(target), 12u, 3, 3
    };
    job.content_rect = (MooCompRect){0, 0, 3, 3};
    job.effect = moo_comp_effect_state_neutral();
    job.effect.enabled_mask = MOO_COMP_EFFECT_BACKDROP_BLUR;
    job.effect.backdrop.blur_radius = 1u;
    job.content_opacity = 0u;
    scratch.rgba_ping = rgba_ping;
    scratch.rgba_pong = rgba_pong;
    scratch.rgba_words_per_buffer = 36u;

    result = moo_comp_effect_cpu_render(&job, &scratch, &stats);
    CHECK(result == MOO_COMP_OK, "blur impulse render failed");
    CHECK(target[16] == 255u && target[19] == 28u,
          "premultiplied blur unpremul must clamp red to 255");
}

static void test_cpu_lower_z_content_overlap_atomicity(void) {
    uint8_t content_and_lower[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u
    };
    uint8_t target[16] = {
        1u, 2u, 3u, 255u, 4u, 5u, 6u, 255u,
        7u, 8u, 9u, 255u, 10u, 11u, 12u, 255u
    };
    uint8_t target_before[16];
    uint32_t rgba_ping[16];
    uint32_t rgba_pong[16];
    MooCompEffectCpuJob job = {0};
    MooCompEffectCpuScratch scratch = {0};
    MooCompEffectCpuStats stats = {
        UINT64_C(11), UINT64_C(22), UINT64_C(33),
        UINT64_C(44), UINT64_C(55)
    };
    MooCompEffectCpuStats stats_before = stats;
    MooCompResult result;

    memcpy(target_before, target, sizeof(target));
    job.content = (MooCompEffectCpuSource){
        content_and_lower, sizeof(content_and_lower), 8u, 2, 2
    };
    job.lower_z = job.content;
    job.target = (MooCompEffectCpuTarget){
        target, sizeof(target), 8u, 2, 2
    };
    job.content_rect = (MooCompRect){0, 0, 2, 2};
    job.effect = moo_comp_effect_state_neutral();
    job.effect.enabled_mask =
        MOO_COMP_EFFECT_SHADOW | MOO_COMP_EFFECT_BACKDROP_BLUR;
    job.effect.backdrop.blur_radius = 1u;
    job.content_opacity = 0u;
    scratch.rgba_ping = rgba_ping;
    scratch.rgba_pong = rgba_pong;
    scratch.rgba_words_per_buffer = 16u;

    result = moo_comp_effect_cpu_render(&job, &scratch, &stats);
    CHECK(result == MOO_COMP_INVALID,
          "lower_z/content overlap must fail INVALID");
    CHECK(memcmp(target, target_before, sizeof(target)) == 0,
          "lower_z/content overlap mutated target");
    CHECK(cpu_stats_eq(&stats, &stats_before),
          "lower_z/content overlap mutated stats");
}

static void test_cpu_extreme_shadow_preflight_atomicity(void) {
    uint8_t content[4] = {255u, 255u, 255u, 255u};
    uint8_t target[4] = {12u, 34u, 56u, 255u};
    uint8_t target_before[4];
    MooCompEffectCpuJob job = {0};
    MooCompEffectCpuStats stats = {
        UINT64_C(66), UINT64_C(77), UINT64_C(88),
        UINT64_C(99), UINT64_C(111)
    };
    MooCompEffectCpuStats stats_before = stats;
    MooCompResult result;

    memcpy(target_before, target, sizeof(target));
    job.content = (MooCompEffectCpuSource){
        content, sizeof(content), 4u, 1, 1
    };
    job.target = (MooCompEffectCpuTarget){
        target, sizeof(target), 4u, 1, 1
    };
    job.content_rect = (MooCompRect){INT32_MIN, 0, 1, 1};
    job.effect = moo_comp_effect_state_neutral();
    job.effect.enabled_mask = MOO_COMP_EFFECT_SHADOW;
    job.effect.shadow.spread_radius = 1u;
    job.effect.shadow.color = (MooCompRgba8){1u, 2u, 3u, 255u};
    job.content_opacity = 255u;

    result = moo_comp_effect_cpu_render(&job, NULL, &stats);
    CHECK(result == MOO_COMP_LIMIT,
          "extreme shadow spread must fail preflight LIMIT");
    CHECK(memcmp(target, target_before, sizeof(target)) == 0,
          "extreme shadow failure mutated target");
    CHECK(cpu_stats_eq(&stats, &stats_before),
          "extreme shadow failure mutated stats");
}

int main(void) {
    test_alpha_rounding_mutant();
    test_capability_fallback_and_atomicity();
    test_reduced_motion();

    CHECK(portable_cpu_renderer_probe() == MOO_COMP_OK,
          "portable CPU effect renderer failed");
    test_cpu_blur_unpremul_clamp();
    test_cpu_lower_z_content_overlap_atomicity();
    test_cpu_extreme_shadow_preflight_atomicity();

    if (failures != 0) {
        fprintf(stderr, "P016-O5 EFFECTS ASAN RED: %d/%d failed\n",
                failures, checks);
        return 1;
    }
    printf("P016-O5 EFFECTS ASAN GREEN: %d checks\n", checks);
    return 0;
}
