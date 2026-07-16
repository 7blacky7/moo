/*
 * P016-O5 T0: semantic state/animation determinism plus renderer RED seam.
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

static MooCompResult portable_renderer_hash(uint64_t *out_hash) {
    uint8_t a[24] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 0xa5u, 0xa5u, 0xa5u, 0xa5u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 0xa5u, 0xa5u, 0xa5u, 0xa5u
    };
    uint8_t b[24] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 0x5au, 0x5au, 0x5au, 0x5au,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 0x5au, 0x5au, 0x5au, 0x5au
    };
    MooCompEffectCpuSource ia = {a, sizeof(a), 12u, 2, 2};
    MooCompEffectCpuSource ib = {b, sizeof(b), 12u, 2, 2};
    uint64_t padding_hash;

    if (moo_comp_effect_cpu_hash_rgba(&ia, out_hash) != MOO_COMP_OK ||
        moo_comp_effect_cpu_hash_rgba(&ib, &padding_hash) != MOO_COMP_OK)
        return MOO_COMP_INVALID;
    return *out_hash == padding_hash ? MOO_COMP_OK : MOO_COMP_INVALID;
}

int main(void) {
    MooCompEffectLimits limits = moo_comp_effect_limits_default();
    MooCompEffectStateConfig config;
    MooCompEffectSurfaceState a;
    MooCompEffectSurfaceState b;
    MooCompEffectUsage usage = {0u, 0u, UINT64_C(256)};
    MooCompEffectPreflight pa;
    MooCompEffectPreflight pb;
    MooCompEffectState requested = moo_comp_effect_state_neutral();
    MooCompAnimationTimeline ta;
    MooCompAnimationTimeline tb;
    MooCompAnimationSlot slots_a[2];
    MooCompAnimationSlot slots_b[2];
    uint64_t renderer_hash = 0u;

    memset(&a, 0xa5, sizeof(a));
    memset(&b, 0x5a, sizeof(b));
    CHECK(moo_comp_effect_state_config_init(
              &config, MOO_COMP_EFFECT_TINT, &limits) == MOO_COMP_OK,
          "state config init failed");
    CHECK(moo_comp_effect_surface_init(&a, 1u, 2u) == MOO_COMP_OK &&
          moo_comp_effect_surface_init(&b, 1u, 2u) == MOO_COMP_OK,
          "surface init failed");
    requested.enabled_mask = MOO_COMP_EFFECT_TINT;
    requested.backdrop.tint = (MooCompRgba8){10u, 20u, 30u, 255u};
    requested.backdrop.tint_mix = 64u;
    CHECK(moo_comp_effect_state_preflight(
              &config, &a, 1u, 2u, &requested, &usage, &pa) == MOO_COMP_OK &&
          moo_comp_effect_state_preflight(
              &config, &b, 1u, 2u, &requested, &usage, &pb) == MOO_COMP_OK,
          "deterministic preflight setup failed");
    moo_comp_effect_state_apply(&a, &pa);
    moo_comp_effect_state_apply(&b, &pb);
    CHECK(moo_comp_effect_surface_hash(&a) ==
              moo_comp_effect_surface_hash(&b),
          "surface hash depends on prior storage bytes");

    memset(slots_a, 0x11, sizeof(slots_a));
    memset(slots_b, 0xee, sizeof(slots_b));
    CHECK(moo_comp_animation_timeline_init(
              &ta, slots_a, 2u, &limits) == MOO_COMP_OK &&
          moo_comp_animation_timeline_init(
              &tb, slots_b, 2u, &limits) == MOO_COMP_OK,
          "timeline init failed");
    CHECK(moo_comp_animation_state_hash(&ta) ==
              moo_comp_animation_state_hash(&tb),
          "animation hash depends on prior slot bytes");

    CHECK(moo_comp_effect_cpu_noise_hash(17u, 29u, 123u) ==
              moo_comp_effect_cpu_noise_hash(17u, 29u, 123u),
          "same noise coordinates/seed changed");
    CHECK(moo_comp_effect_cpu_noise_hash(17u, 29u, 123u) !=
              moo_comp_effect_cpu_noise_hash(17u, 29u, 124u),
          "different noise seeds collided in the probe");

    CHECK(portable_renderer_hash(&renderer_hash) == MOO_COMP_OK,
          "portable renderer semantic hash failed");

    {
        uint64_t surface_hash = moo_comp_effect_surface_hash(&a);
        uint64_t animation_hash = moo_comp_animation_state_hash(&ta);
        uint32_t i;
        for (i = 0u; i < 1000u; ++i) {
            MooCompEffectSurfaceState probe_surface;
            MooCompEffectPreflight probe_preflight;
            MooCompAnimationSlot probe_slots[2];
            MooCompAnimationTimeline probe_timeline;
            uint64_t probe_renderer_hash = 0u;

            memset(&probe_surface, (int)(i & 0xffu), sizeof(probe_surface));
            CHECK(moo_comp_effect_surface_init(
                      &probe_surface, 1u, 2u) == MOO_COMP_OK &&
                  moo_comp_effect_state_preflight(
                      &config, &probe_surface, 1u, 2u,
                      &requested, &usage, &probe_preflight) == MOO_COMP_OK,
                  "repeated surface setup failed");
            moo_comp_effect_state_apply(&probe_surface, &probe_preflight);
            CHECK(moo_comp_effect_surface_hash(&probe_surface) == surface_hash,
                  "surface hash changed across storage fill/repetition");

            memset(probe_slots, (int)((~i) & 0xffu), sizeof(probe_slots));
            CHECK(moo_comp_animation_timeline_init(
                      &probe_timeline, probe_slots, 2u, &limits) == MOO_COMP_OK,
                  "repeated timeline setup failed");
            CHECK(moo_comp_animation_state_hash(&probe_timeline) ==
                      animation_hash,
                  "animation hash changed across slot fill/repetition");

            CHECK(portable_renderer_hash(&probe_renderer_hash) == MOO_COMP_OK &&
                      probe_renderer_hash == renderer_hash,
                  "renderer hash changed across repetition");
            CHECK(moo_comp_effect_cpu_noise_hash(17u, 29u, 123u) ==
                      moo_comp_effect_cpu_noise_hash(17u, 29u, 123u),
                  "noise hash changed across repetition");
        }
    }

    if (failures != 0) {
        fprintf(stderr, "P016-O5 EFFECTS DETERMINISM RED: %d/%d failed\n",
                failures, checks);
        return 1;
    }
    printf("P016-O5 EFFECTS DETERMINISM GREEN: %d checks hash=%llu\n",
           checks, (unsigned long long)renderer_hash);
    return 0;
}
