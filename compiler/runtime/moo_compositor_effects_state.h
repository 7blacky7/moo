#ifndef MOO_COMPOSITOR_EFFECTS_STATE_H
#define MOO_COMPOSITOR_EFFECTS_STATE_H
#include <stdint.h>
#include "moo_compositor_protocol.h"

#define MOO_COMP_EFFECT_DEFAULT_MAX_WORK_UNITS UINT64_C(268435456)
#define MOO_COMP_EFFECT_DEFAULT_MAX_SCRATCH_BYTES UINT64_C(67108864)
#define MOO_COMP_EFFECT_WORK_CORNER_PER_PIXEL UINT64_C(1)
#define MOO_COMP_EFFECT_WORK_SHADOW_PER_PIXEL UINT64_C(4)
#define MOO_COMP_EFFECT_WORK_BACKDROP_PER_PIXEL UINT64_C(4)
#define MOO_COMP_EFFECT_WORK_COLOR_PER_PIXEL UINT64_C(1)
#define MOO_COMP_EFFECT_WORK_AFFINE_PER_PIXEL UINT64_C(2)
#define MOO_COMP_EFFECT_SCRATCH_SHADOW_PER_PIXEL UINT64_C(8)
#define MOO_COMP_EFFECT_SCRATCH_BACKDROP_PER_PIXEL UINT64_C(32)

/* Pointer-free records; projected counts already include the candidate. */
typedef struct {
    uint64_t capabilities;
    MooCompEffectLimits limits;
} MooCompEffectStateConfig;

typedef struct {
    uint32_t animations_on_surface;
    uint32_t animations_for_client;
    uint64_t affected_pixels;
} MooCompEffectUsage;

typedef struct {
    uint64_t owner;
    uint64_t surface;
    uint64_t expected_commit_sequence;
    uint64_t next_commit_sequence;
    MooCompEffectState base;
    MooCompEffectState requested;
    MooCompEffectState effective;
    uint64_t degraded_mask;
    uint64_t work_units;
    uint64_t scratch_bytes;
} MooCompEffectPreflight;

typedef struct {
    uint32_t active;
    uint32_t reserved;
    uint64_t owner;
    uint64_t surface;
    uint64_t commit_sequence;
    MooCompEffectState base;
    MooCompEffectState requested;
    MooCompEffectState effective;
    uint64_t degraded_mask;
    uint64_t work_units;
    uint64_t scratch_bytes;
} MooCompEffectSurfaceState;

MooCompEffectState moo_comp_effect_state_neutral(void);
MooCompEffectLimits moo_comp_effect_limits_default(void);
MooCompResult moo_comp_effect_state_config_init(
    MooCompEffectStateConfig *out_config, uint64_t capabilities,
    const MooCompEffectLimits *limits);
MooCompResult moo_comp_effect_surface_init(
    MooCompEffectSurfaceState *surface_state, uint64_t owner, uint64_t surface);
MooCompResult moo_comp_effect_surface_destroy(
    MooCompEffectSurfaceState *surface_state, uint64_t owner, uint64_t surface);
MooCompResult moo_comp_effect_state_preflight(
    const MooCompEffectStateConfig *config,
    const MooCompEffectSurfaceState *surface_state,
    uint64_t owner, uint64_t surface,
    const MooCompEffectState *requested, const MooCompEffectUsage *usage,
    MooCompEffectPreflight *out_preflight);
/*
 * Publication succeeds only for the exact active owner/surface/sequence used
 * by preflight. Cross-surface, replay and post-destroy plans do not mutate.
 */
MooCompResult moo_comp_effect_state_apply(
    MooCompEffectSurfaceState *surface_state,
    const MooCompEffectPreflight *preflight);
MooCompResult moo_comp_effect_surface_status(
    const MooCompEffectSurfaceState *surface_state,
    MooCompEffectStatus *out_status);
/* Semantic fields are hashed low byte first; padding is never read. */
uint64_t moo_comp_effect_state_hash(const MooCompEffectState *state);
uint64_t moo_comp_effect_surface_hash(
    const MooCompEffectSurfaceState *surface_state);
#endif
