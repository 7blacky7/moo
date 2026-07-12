#ifndef MOO_COMPOSITOR_EFFECTS_DAMAGE_H
#define MOO_COMPOSITOR_EFFECTS_DAMAGE_H

#include <stdint.h>

#include "moo_compositor_effects_math.h"

#define MOO_COMP_EFFECT_PASS_CAPACITY_MIN 1u

/*
 * Surfaces are ordered low-to-high Z. changed publishes old U new visual
 * bounds after that surface's lower-Z backdrop dependency has been evaluated.
 */
typedef struct {
    MooCompHandle surface;
    uint64_t enabled_mask;
    MooCompRect old_visual_bounds;
    MooCompRect new_visual_bounds;
    MooCompRect backdrop_coverage_bounds;
    uint16_t backdrop_blur_radius;
    uint16_t reserved16;
    uint32_t changed;
    uint32_t reserved;
} MooCompEffectDamageSurface;

typedef struct {
    MooCompRect *regions;
    uint32_t capacity;
    uint32_t reserved;
} MooCompEffectDamageWorkspace;

typedef struct {
    MooCompRect *regions;
    uint32_t capacity;
    uint32_t count;
    uint32_t full_damage;
    uint32_t reserved;
} MooCompEffectDamageOutput;

typedef enum {
    MOO_COMP_EFFECT_PASS_INVALID = 0,
    MOO_COMP_EFFECT_PASS_SHADOW = 1,
    MOO_COMP_EFFECT_PASS_BACKDROP_CAPTURE_LOWER_Z = 2,
    MOO_COMP_EFFECT_PASS_BACKDROP_BLUR_HORIZONTAL = 3,
    MOO_COMP_EFFECT_PASS_BACKDROP_BLUR_VERTICAL = 4,
    MOO_COMP_EFFECT_PASS_COLOR = 5,
    MOO_COMP_EFFECT_PASS_CONTENT = 6
} MooCompEffectPassKind;

/*
 * Pass records are backend-neutral values. BACKDROP_CAPTURE_LOWER_Z always
 * names freshly recomposed lower-Z scratch; persistent final output is never a
 * legal sample source.
 */
typedef struct {
    uint32_t kind;
    uint32_t surface_index;
    MooCompRect damage_bounds;
    MooCompRect sample_bounds;
    uint64_t enabled_mask;
} MooCompEffectPass;

typedef struct {
    MooCompEffectPass *passes;
    uint32_t capacity;
    uint32_t count;
    uint32_t reserved;
} MooCompEffectPassList;

/* Pure primitive used by the closure and by independent QA. */
MooCompResult moo_comp_effect_damage_propagate_backdrop(
    MooCompRect lower_z_dirty,
    MooCompRect backdrop_coverage,
    uint16_t kernel_radius,
    MooCompRect *out_damage);

/*
 * Builds a deterministic conservative closure into caller-owned memory.
 * background_damage is visible below every surface. Each backdrop surface
 * receives coverage intersect expand(lower_dirty,kernel), and that newly dirty
 * coverage propagates to later higher-Z backdrops. A changed surface contributes
 * old_visual U new_visual only after its own backdrop dependency is evaluated.
 *
 * Any region/workspace/output capacity overflow collapses to exactly
 * full_output with full_damage=1 and returns OK. workspace->regions and
 * out_damage->regions must be disjoint across their full declared capacities;
 * any byte-range overlap fails INVALID before mutation. Invalid/LIMIT inputs
 * leave the output record and output region storage unchanged.
 */
MooCompResult moo_comp_effect_damage_build(
    MooCompRect full_output,
    const MooCompRect *background_damage,
    uint32_t background_damage_count,
    const MooCompEffectDamageSurface *surfaces,
    uint32_t surface_count,
    MooCompEffectDamageWorkspace *workspace,
    MooCompEffectDamageOutput *out_damage);

uint64_t moo_comp_effect_damage_hash(
    const MooCompEffectDamageOutput *damage);

#endif
