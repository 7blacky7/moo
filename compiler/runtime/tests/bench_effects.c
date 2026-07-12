/*
 * P016-O5 T0: deterministic work/scratch budget probe. No wall-clock gate.
 */
#include "../moo_compositor_effects_state.h"
#include "../moo_compositor_effects_cpu.h"

#include <stdint.h>
#include <stdio.h>

static MooCompResult portable_cpu_work_counter(
    const MooCompEffectState *effect,
    uint64_t *out_work,
    uint64_t *out_scratch) {
    static uint8_t target[640u * 360u * 4u];
    uint8_t content[4] = {0u, 0u, 0u, 0u};
    MooCompEffectCpuJob job = {0};
    MooCompEffectCpuStats stats;
    MooCompResult r;

    if (!effect || !out_work || !out_scratch) return MOO_COMP_INVALID;
    job.content = (MooCompEffectCpuSource){
        content, sizeof(content), 4u, 1, 1
    };
    job.target = (MooCompEffectCpuTarget){
        target, sizeof(target), 640u * 4u, 640, 360
    };
    job.content_rect = (MooCompRect){0, 0, 1, 1};
    job.effect = *effect;
    job.content_opacity = 255u;
    r = moo_comp_effect_cpu_requirements(&job, &stats);
    if (r != MOO_COMP_OK) return r;
    *out_work = stats.work_units;
    *out_scratch = stats.scratch_bytes;
    return MOO_COMP_OK;
}

int main(void) {
    MooCompEffectLimits limits = moo_comp_effect_limits_default();
    MooCompEffectStateConfig config;
    MooCompEffectSurfaceState surface;
    MooCompEffectState requested = moo_comp_effect_state_neutral();
    MooCompEffectUsage usage = {0u, 0u, UINT64_C(640) * UINT64_C(360)};
    MooCompEffectPreflight preflight;
    uint64_t cpu_work = 0u;
    uint64_t cpu_scratch = 0u;
    MooCompResult r;

    r = moo_comp_effect_state_config_init(
        &config, MOO_COMP_EFFECT_BACKDROP_BLUR | MOO_COMP_EFFECT_TINT,
        &limits);
    if (r != MOO_COMP_OK) return 2;
    if (moo_comp_effect_surface_init(&surface, 1u, 2u) != MOO_COMP_OK)
        return 2;
    requested.enabled_mask = MOO_COMP_EFFECT_BACKDROP_BLUR |
                             MOO_COMP_EFFECT_TINT;
    requested.backdrop.blur_radius = 24u;
    requested.backdrop.tint = (MooCompRgba8){32u, 64u, 96u, 255u};
    requested.backdrop.tint_mix = 96u;
    r = moo_comp_effect_state_preflight(
        &config, &surface, 1u, 2u, &requested, &usage, &preflight);
    if (r != MOO_COMP_OK) {
        fprintf(stderr, "BENCH SETUP FAIL result=%d\n", (int)r);
        return 2;
    }
    printf("EFFECT_BUDGET pixels=%llu work=%llu scratch=%llu\n",
           (unsigned long long)usage.affected_pixels,
           (unsigned long long)preflight.work_units,
           (unsigned long long)preflight.scratch_bytes);

    r = portable_cpu_work_counter(
        &preflight.effective, &cpu_work, &cpu_scratch);
    if (r != MOO_COMP_OK ||
        cpu_work != preflight.work_units ||
        cpu_scratch != preflight.scratch_bytes) {
        fprintf(stderr,
                "P016-O5 EFFECTS BENCH FAIL: CPU budget mismatch "
                "result=%d work=%llu scratch=%llu\n",
                (int)r, (unsigned long long)cpu_work,
                (unsigned long long)cpu_scratch);
        return 1;
    }
    printf("EFFECT_CPU_WORK %llu scratch=%llu\n",
           (unsigned long long)cpu_work,
           (unsigned long long)cpu_scratch);
    return 0;
}
