/*
 * P016-O5 D1: deterministic damage closure, capacity and atomicity gate.
 */
#include "../moo_compositor_effects_damage.h"

#include <limits.h>
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

static int rect_eq(MooCompRect a, MooCompRect b) {
    return a.x == b.x && a.y == b.y &&
           a.width == b.width && a.height == b.height;
}

static MooCompEffectDamageSurface backdrop(
    MooCompHandle surface, uint16_t radius) {
    MooCompEffectDamageSurface value = {0};
    value.surface = surface;
    value.enabled_mask = MOO_COMP_EFFECT_BACKDROP_BLUR;
    value.backdrop_coverage_bounds = (MooCompRect){0, 0, 100, 100};
    value.backdrop_blur_radius = radius;
    return value;
}

static int output_unchanged(
    const MooCompEffectDamageOutput *actual,
    const MooCompEffectDamageOutput *before,
    const MooCompRect *storage,
    const MooCompRect *storage_before,
    size_t storage_count) {
    return actual->regions == before->regions &&
           actual->capacity == before->capacity &&
           actual->count == before->count &&
           actual->full_damage == before->full_damage &&
           actual->reserved == before->reserved &&
           memcmp(storage, storage_before,
                  storage_count * sizeof(*storage)) == 0;
}

static void test_primitive(void) {
    MooCompRect propagated = {0, 0, 0, 0};
    CHECK(moo_comp_effect_damage_propagate_backdrop(
              (MooCompRect){10, 20, 30, 40},
              (MooCompRect){-100, -100, 300, 300},
              4u, &propagated) == MOO_COMP_OK,
          "backdrop propagation failed");
    CHECK(rect_eq(propagated, (MooCompRect){6, 16, 38, 48}),
          "backdrop halo changed");
    CHECK(moo_comp_effect_damage_propagate_backdrop(
              (MooCompRect){INT32_MIN, 0, 1, 1},
              (MooCompRect){INT32_MIN, 0, 100, 100},
              1u, &propagated) == MOO_COMP_LIMIT,
          "halo arithmetic overflow must be LIMIT");
    CHECK(moo_comp_effect_damage_propagate_backdrop(
              (MooCompRect){0, 0, -1, 1},
              (MooCompRect){0, 0, 100, 100},
              1u, &propagated) == MOO_COMP_INVALID,
          "negative damage size must be INVALID");
}

static void test_closure_and_hash(void) {
    MooCompRect full = {0, 0, 100, 100};
    MooCompRect seed = {10, 10, 2, 2};
    MooCompEffectDamageSurface surfaces[2] = {
        backdrop(UINT64_C(1), 2u), backdrop(UINT64_C(2), 3u)
    };
    MooCompRect scratch[16];
    MooCompRect regions[16];
    MooCompRect copied_regions[16];
    MooCompEffectDamageWorkspace workspace = {scratch, 16u, 0u};
    MooCompEffectDamageOutput output = {regions, 16u, 0u, 0u, 0u};
    MooCompEffectDamageOutput copied = {
        copied_regions, 16u, 0u, 0u, 0u
    };
    uint64_t first_hash;

    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, surfaces, 2u,
              &workspace, &output) == MOO_COMP_OK,
          "stacked backdrop closure failed");
    CHECK(output.count == 4u && output.full_damage == 0u,
          "stacked backdrop closure count changed");
    CHECK(rect_eq(output.regions[0], seed), "seed order changed");
    CHECK(rect_eq(output.regions[1], (MooCompRect){8, 8, 6, 6}),
          "first backdrop closure changed");
    CHECK(rect_eq(output.regions[2], (MooCompRect){7, 7, 8, 8}),
          "second backdrop lower seed changed");
    CHECK(rect_eq(output.regions[3], (MooCompRect){5, 5, 12, 12}),
          "transitive backdrop closure changed");

    first_hash = moo_comp_effect_damage_hash(&output);
    CHECK(first_hash != 0u, "semantic damage hash is zero");
    memcpy(copied_regions, regions, output.count * sizeof(*regions));
    copied.count = output.count;
    copied.full_damage = output.full_damage;
    CHECK(moo_comp_effect_damage_hash(&copied) == first_hash,
          "damage hash depends on record/storage identity");

    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, surfaces, 2u,
              &workspace, &output) == MOO_COMP_OK &&
          moo_comp_effect_damage_hash(&output) == first_hash,
          "repeated closure/hash is non-deterministic");
}

static void test_changed_and_capacity(void) {
    MooCompRect full = {0, 0, 100, 100};
    MooCompRect seed = {10, 10, 2, 2};
    MooCompEffectDamageSurface stacked[2] = {
        backdrop(UINT64_C(1), 2u), backdrop(UINT64_C(2), 3u)
    };
    MooCompEffectDamageSurface changed[2] = {{0}, {0}};
    MooCompRect scratch[16];
    MooCompRect small_scratch[2];
    MooCompRect regions[16];
    MooCompRect one_region[1];
    MooCompEffectDamageWorkspace workspace = {scratch, 16u, 0u};
    MooCompEffectDamageWorkspace small_workspace = {
        small_scratch, 2u, 0u
    };
    MooCompEffectDamageOutput output = {regions, 16u, 0u, 0u, 0u};
    MooCompEffectDamageOutput small_output = {
        one_region, 1u, 0u, 0u, 0u
    };

    changed[0].surface = UINT64_C(3);
    changed[0].changed = 1u;
    changed[0].old_visual_bounds = (MooCompRect){20, 20, 5, 5};
    changed[0].new_visual_bounds = (MooCompRect){30, 30, 5, 5};
    changed[1] = backdrop(UINT64_C(4), 1u);
    CHECK(moo_comp_effect_damage_build(
              full, NULL, 0u, changed, 2u,
              &workspace, &output) == MOO_COMP_OK,
          "changed old/new closure failed");
    CHECK(output.count == 2u &&
          rect_eq(output.regions[0], (MooCompRect){20, 20, 15, 15}) &&
          rect_eq(output.regions[1], (MooCompRect){19, 19, 17, 17}),
          "old/new union did not propagate to higher backdrop");

    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, stacked, 2u,
              &small_workspace, &output) == MOO_COMP_OK,
          "workspace capacity collapse failed");
    CHECK(output.count == 1u && output.full_damage == 1u &&
          rect_eq(output.regions[0], full),
          "workspace overflow did not collapse exactly to full output");

    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, stacked, 2u,
              &workspace, &small_output) == MOO_COMP_OK,
          "output capacity collapse failed");
    CHECK(small_output.count == 1u && small_output.full_damage == 1u &&
          rect_eq(small_output.regions[0], full),
          "output overflow did not collapse exactly to full output");
}

static void test_fail_closed_atomicity(void) {
    MooCompRect full = {0, 0, 100, 100};
    MooCompRect seed = {10, 10, 2, 2};
    MooCompEffectDamageSurface surface = backdrop(UINT64_C(1), 2u);
    MooCompRect scratch[8];
    MooCompRect regions[8];
    MooCompRect regions_before[8];
    MooCompEffectDamageWorkspace workspace = {scratch, 8u, 0u};
    MooCompEffectDamageOutput output = {regions, 8u, 5u, 1u, 0u};
    MooCompEffectDamageOutput before;

    memset(regions, 0x6c, sizeof(regions));
    memcpy(regions_before, regions, sizeof(regions));
    before = output;
    seed.width = -1;
    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, &surface, 1u,
              &workspace, &output) == MOO_COMP_INVALID,
          "negative input must fail INVALID");
    CHECK(output_unchanged(
              &output, &before, regions, regions_before, 8u),
          "INVALID mutated output header or storage");

    seed = (MooCompRect){INT32_MIN, 0, 1, 1};
    full = (MooCompRect){INT32_MIN, 0, 100, 100};
    surface.backdrop_coverage_bounds = full;
    surface.backdrop_blur_radius = 1u;
    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, &surface, 1u,
              &workspace, &output) == MOO_COMP_LIMIT,
          "late halo overflow must fail LIMIT");
    CHECK(output_unchanged(
              &output, &before, regions, regions_before, 8u),
          "late LIMIT mutated output header or storage");
}

static void test_overlap_rejected(void) {
    MooCompRect full = {0, 0, 100, 100};
    MooCompRect seed = {10, 10, 2, 2};
    MooCompEffectDamageSurface surface = backdrop(UINT64_C(1), 2u);
    MooCompRect shared[9];
    MooCompRect before_shared[9];
    MooCompEffectDamageWorkspace workspace = {shared, 8u, 0u};
    MooCompEffectDamageOutput output = {shared, 8u, 3u, 1u, 0u};
    MooCompEffectDamageOutput before;

    memset(shared, 0x37, sizeof(shared));
    memcpy(before_shared, shared, sizeof(shared));
    before = output;
    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, &surface, 1u,
              &workspace, &output) == MOO_COMP_INVALID,
          "exact workspace/output overlap must be INVALID");
    CHECK(output_unchanged(
              &output, &before, shared, before_shared, 9u),
          "exact overlap mutated shared output storage");

    output.regions = shared + 1;
    output.capacity = 8u;
    output.count = 4u;
    output.full_damage = 1u;
    before = output;
    CHECK(moo_comp_effect_damage_build(
              full, &seed, 1u, &surface, 1u,
              &workspace, &output) == MOO_COMP_INVALID,
          "partial workspace/output overlap must be INVALID");
    CHECK(output_unchanged(
              &output, &before, shared, before_shared, 9u),
          "partial overlap mutated shared output storage");
}

int main(void) {
    test_primitive();
    test_closure_and_hash();
    test_changed_and_capacity();
    test_fail_closed_atomicity();
    test_overlap_rejected();

    if (failures != 0) {
        fprintf(stderr, "P016-O5 EFFECTS DAMAGE RED: %d/%d failed\n",
                failures, checks);
        return 1;
    }
    printf("P016-O5 EFFECTS DAMAGE GREEN: %d checks\n", checks);
    return 0;
}
