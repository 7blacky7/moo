#ifndef MOO_COMPOSITOR_EFFECTS_GPU_H
#define MOO_COMPOSITOR_EFFECTS_GPU_H

#include <stdint.h>

#include "moo_compositor_effects_math.h"

/*
 * Portable GPU planning only. Resource and fence values are opaque presenter-
 * owned identifiers; this module never dereferences them or calls a platform
 * API. The CPU renderer remains the semantic oracle.
 */
#define MOO_COMP_GPU_GRAPH_VERSION UINT32_C(1)
#define MOO_COMP_GPU_MAX_PASSES UINT32_C(64)
#define MOO_COMP_GPU_RESOURCE_INVALID UINT64_C(0)
#define MOO_COMP_GPU_FENCE_INVALID UINT64_C(0)

typedef uint64_t MooCompGpuResourceId;
typedef uint64_t MooCompGpuFenceId;

typedef enum {
    MOO_COMP_GPU_PASS_INVALID = 0,
    MOO_COMP_GPU_PASS_SHADOW_MASK = 1,
    MOO_COMP_GPU_PASS_SHADOW_BLUR_H = 2,
    MOO_COMP_GPU_PASS_SHADOW_BLUR_V = 3,
    MOO_COMP_GPU_PASS_SHADOW_COMPOSITE = 4,
    MOO_COMP_GPU_PASS_BACKDROP_CAPTURE = 5,
    MOO_COMP_GPU_PASS_BACKDROP_BLUR_H = 6,
    MOO_COMP_GPU_PASS_BACKDROP_BLUR_V = 7,
    MOO_COMP_GPU_PASS_SATURATION = 8,
    MOO_COMP_GPU_PASS_TINT = 9,
    MOO_COMP_GPU_PASS_NOISE = 10,
    MOO_COMP_GPU_PASS_BACKDROP_COMPOSITE = 11,
    MOO_COMP_GPU_PASS_CONTENT = 12
} MooCompGpuPassType;

enum {
    MOO_COMP_GPU_PASS_FLAG_CORNER_CLIP = UINT32_C(1) << 0,
    MOO_COMP_GPU_PASS_FLAG_AFFINE_2D = UINT32_C(1) << 1
};

typedef struct {
    MooCompGpuResourceId lower_z_prefix;
    MooCompGpuResourceId content;
    MooCompGpuResourceId output;
    MooCompGpuResourceId ping;
    MooCompGpuResourceId pong;
    MooCompGpuResourceId mask;
    uint64_t cache_generation;
} MooCompGpuResources;

typedef struct {
    MooCompHandle surface;
    uint64_t commit_sequence;
    MooCompEffectState effective;
    MooCompEffectBounds bounds;
    MooCompGpuResources resources;
} MooCompGpuSurfaceInput;

typedef struct {
    uint32_t type;
    uint32_t flags;
    MooCompHandle surface;
    uint64_t commit_sequence;
    MooCompGpuResourceId source;
    MooCompGpuResourceId destination;
    MooCompGpuResourceId auxiliary;
    uint64_t cache_generation;
    MooCompRect bounds;
    int32_t offset_x;
    int32_t offset_y;
    uint32_t radius;
    uint16_t saturation_q8_8;
    uint8_t tint_mix;
    uint8_t noise;
    MooCompRgba8 color;
    uint32_t noise_seed;
    MooCompCorners corners;
    MooCompAffine2D affine;
} MooCompGpuPass;

typedef struct {
    MooCompGpuPass *passes;
    uint32_t capacity;
    uint32_t count;
    uint64_t semantic_hash;
} MooCompGpuGraph;

/*
 * Initializes caller-owned graph storage. Building is deterministic and
 * preflights every error before modifying graph metadata or pass storage.
 */
MooCompResult moo_comp_effects_gpu_graph_init(
    MooCompGpuGraph *graph, MooCompGpuPass *passes, uint32_t capacity);
MooCompResult moo_comp_effects_gpu_graph_build(
    MooCompGpuGraph *graph, const MooCompGpuSurfaceInput *input);
uint64_t moo_comp_effects_gpu_graph_hash(const MooCompGpuGraph *graph);

typedef enum {
    MOO_COMP_GPU_BACKEND_DISABLED = 0,
    MOO_COMP_GPU_BACKEND_READY = 1,
    MOO_COMP_GPU_BACKEND_LOST = 2
} MooCompGpuBackendStatus;

typedef struct {
    uint32_t status;
    uint32_t reserved;
    uint64_t generation;
    uint64_t submitted_serial;
    uint64_t completed_serial;
    uint64_t last_graph_hash;
    uint64_t pending_graph_hash;
    MooCompGpuFenceId pending_fence;
    uint64_t fallback_count;
} MooCompGpuBackendState;

typedef struct {
    uint64_t generation;
    uint64_t serial;
    uint64_t graph_hash;
    MooCompGpuFenceId presenter_fence;
} MooCompGpuSubmission;

/*
 * reserve_submission records only an already accepted presenter submission; it
 * does not encode, submit, wait for, or validate GPU work. Exactly one
 * submission may be outstanding; a second reservation returns WOULD_BLOCK.
 * Completion must match its generation, serial, graph hash, and fence exactly.
 * LOST returns MOO_COMP_UNSUPPORTED so the caller must use the CPU path.
 */
MooCompResult moo_comp_effects_gpu_backend_init(
    MooCompGpuBackendState *state, uint64_t generation);
MooCompResult moo_comp_effects_gpu_backend_mark_lost(
    MooCompGpuBackendState *state);
MooCompResult moo_comp_effects_gpu_backend_recover(
    MooCompGpuBackendState *state, uint64_t new_generation);
int moo_comp_effects_gpu_backend_requires_cpu(
    const MooCompGpuBackendState *state);
MooCompResult moo_comp_effects_gpu_reserve_submission(
    MooCompGpuBackendState *state, const MooCompGpuGraph *graph,
    MooCompGpuFenceId presenter_fence, MooCompGpuSubmission *out_submission);
MooCompResult moo_comp_effects_gpu_complete_submission(
    MooCompGpuBackendState *state,
    const MooCompGpuSubmission *submission);
uint64_t moo_comp_effects_gpu_backend_hash(
    const MooCompGpuBackendState *state);

#endif
