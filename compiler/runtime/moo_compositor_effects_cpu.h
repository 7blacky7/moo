#ifndef MOO_COMPOSITOR_EFFECTS_CPU_H
#define MOO_COMPOSITOR_EFFECTS_CPU_H

#include <stddef.h>
#include <stdint.h>

#include "moo_compositor_effects_math.h"

typedef struct {
    const uint8_t *pixels;
    size_t buffer_bytes;
    size_t stride;
    int32_t width;
    int32_t height;
} MooCompEffectCpuSource;

typedef struct {
    uint8_t *pixels;
    size_t buffer_bytes;
    size_t stride;
    int32_t width;
    int32_t height;
} MooCompEffectCpuTarget;

/*
 * Scratch is caller-owned and never retained. rgba_ping/rgba_pong are generic
 * uint32_t ping/pong buffers: four words per pixel for backdrop blur, one word
 * per pixel for shadow-only blur. Passes reuse them sequentially, so the exact
 * requirement is the maximum, never the sum. mask_* are reserved compatibility
 * fields and are not required by V2.
 */
typedef struct {
    uint32_t *rgba_ping;
    uint32_t *rgba_pong;
    uint64_t rgba_words_per_buffer;
    uint32_t *mask_ping;
    uint32_t *mask_pong;
    uint64_t mask_words_per_buffer;
} MooCompEffectCpuScratch;

typedef struct {
    MooCompEffectCpuSource content;
    /* Immutable freshly composed lower-Z prefix; required with shadow+backdrop. */
    MooCompEffectCpuSource lower_z;
    MooCompEffectCpuTarget target;
    MooCompRect content_rect;
    MooCompEffectState effect;
    uint8_t content_opacity;
    uint8_t reserved[7];
    /* Zero means no additional caller limit. */
    uint64_t max_work_units;
} MooCompEffectCpuJob;

typedef struct {
    uint64_t affected_pixels;
    uint64_t work_units;
    uint64_t scratch_bytes;
    uint64_t rgba_words_per_buffer;
    uint64_t mask_words_per_buffer;
} MooCompEffectCpuStats;

/*
 * Computes exact fixed-capacity requirements without reading or mutating
 * pixels. render repeats this preflight and performs no mutation on failure.
 */
MooCompResult moo_comp_effect_cpu_requirements(
    const MooCompEffectCpuJob *job, MooCompEffectCpuStats *out_stats);

MooCompResult moo_comp_effect_cpu_render(
    const MooCompEffectCpuJob *job, const MooCompEffectCpuScratch *scratch,
    MooCompEffectCpuStats *out_stats);

/* Semantic FNV-1a over dimensions and active RGBA bytes, never padding. */
MooCompResult moo_comp_effect_cpu_hash_rgba(
    const MooCompEffectCpuSource *image, uint64_t *out_hash);

/* G0 coordinate-noise hash, exposed for CPU/GPU differential tests. */
uint32_t moo_comp_effect_cpu_noise_hash(
    uint32_t x, uint32_t y, uint32_t seed);

#endif
