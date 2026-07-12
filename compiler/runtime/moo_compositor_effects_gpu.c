#include "moo_compositor_effects_gpu.h"

#include <limits.h>
#include <stddef.h>

#define MOO_GPU_HASH_OFFSET UINT64_C(1469598103934665603)
#define MOO_GPU_HASH_PRIME UINT64_C(1099511628211)

static uint64_t
moo_gpu_hash_u64(uint64_t hash, uint64_t value)
{
    uint32_t i;
    for (i = 0u; i < 8u; ++i) {
        hash ^= value & UINT64_C(255);
        hash *= MOO_GPU_HASH_PRIME;
        value >>= 8u;
    }
    return hash;
}

static uint64_t
moo_gpu_hash_rect(uint64_t hash, MooCompRect rect)
{
    hash = moo_gpu_hash_u64(hash, (uint32_t)rect.x);
    hash = moo_gpu_hash_u64(hash, (uint32_t)rect.y);
    hash = moo_gpu_hash_u64(hash, (uint32_t)rect.width);
    return moo_gpu_hash_u64(hash, (uint32_t)rect.height);
}

static int
moo_gpu_rect_valid(MooCompRect rect)
{
    return rect.width >= 0 && rect.height >= 0;
}

static int
moo_gpu_state_valid(const MooCompEffectState *state)
{
    if (state == NULL || (state->enabled_mask & ~(uint64_t)MOO_COMP_EFFECTS_V2) != 0u ||
        (state->required_mask & ~state->enabled_mask) != 0u ||
        state->reserved != 0u || state->backdrop.reserved != 0u) {
        return 0;
    }
    return state->fallback_policy <= MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE;
}

static uint32_t
moo_gpu_required_passes(const MooCompEffectState *state)
{
    uint32_t count = 1u;
    const uint64_t backdrop_mask =
        MOO_COMP_EFFECT_BACKDROP_BLUR | MOO_COMP_EFFECT_SATURATION |
        MOO_COMP_EFFECT_TINT | MOO_COMP_EFFECT_NOISE;

    if ((state->enabled_mask & MOO_COMP_EFFECT_SHADOW) != 0u) {
        count += 2u;
        if (state->shadow.blur_radius != 0u)
            count += 2u;
    }
    if ((state->enabled_mask & backdrop_mask) != 0u) {
        count += 2u;
        if ((state->enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u &&
            state->backdrop.blur_radius != 0u)
            count += 2u;
        if ((state->enabled_mask & MOO_COMP_EFFECT_SATURATION) != 0u)
            ++count;
        if ((state->enabled_mask & MOO_COMP_EFFECT_TINT) != 0u)
            ++count;
        if ((state->enabled_mask & MOO_COMP_EFFECT_NOISE) != 0u)
            ++count;
    }
    return count;
}

static int
moo_gpu_resources_valid(const MooCompGpuSurfaceInput *input)
{
    const MooCompGpuResources *r = &input->resources;
    const uint64_t mask = input->effective.enabled_mask;
    const uint64_t backdrop_mask =
        MOO_COMP_EFFECT_BACKDROP_BLUR | MOO_COMP_EFFECT_SATURATION |
        MOO_COMP_EFFECT_TINT | MOO_COMP_EFFECT_NOISE;

    if (r->content == MOO_COMP_GPU_RESOURCE_INVALID ||
        r->output == MOO_COMP_GPU_RESOURCE_INVALID ||
        r->content == r->output)
        return 0;

    if ((mask & MOO_COMP_EFFECT_SHADOW) != 0u) {
        if (r->mask == MOO_COMP_GPU_RESOURCE_INVALID ||
            r->mask == r->content || r->mask == r->output)
            return 0;
        if (input->effective.shadow.blur_radius != 0u &&
            (r->ping == MOO_COMP_GPU_RESOURCE_INVALID ||
             r->pong == MOO_COMP_GPU_RESOURCE_INVALID ||
             r->ping == r->pong || r->ping == r->content ||
             r->pong == r->content || r->ping == r->output ||
             r->pong == r->output || r->ping == r->mask ||
             r->pong == r->mask))
            return 0;
    }

    if ((mask & backdrop_mask) != 0u) {
        if (r->lower_z_prefix == MOO_COMP_GPU_RESOURCE_INVALID ||
            r->ping == MOO_COMP_GPU_RESOURCE_INVALID ||
            r->pong == MOO_COMP_GPU_RESOURCE_INVALID ||
            r->lower_z_prefix == r->content ||
            r->lower_z_prefix == r->output || r->ping == r->pong ||
            r->ping == r->content || r->pong == r->content ||
            r->ping == r->output || r->pong == r->output ||
            r->ping == r->lower_z_prefix || r->pong == r->lower_z_prefix)
            return 0;
        if ((mask & MOO_COMP_EFFECT_SHADOW) != 0u &&
            (r->lower_z_prefix == r->mask || r->ping == r->mask ||
             r->pong == r->mask))
            return 0;
    }
    return 1;
}

static MooCompGpuPass *
moo_gpu_append(MooCompGpuGraph *graph, const MooCompGpuSurfaceInput *input,
    uint32_t type, MooCompGpuResourceId source,
    MooCompGpuResourceId destination, MooCompRect bounds)
{
    MooCompGpuPass *pass = &graph->passes[graph->count++];
    *pass = (MooCompGpuPass){0};
    pass->type = type;
    pass->surface = input->surface;
    pass->commit_sequence = input->commit_sequence;
    pass->source = source;
    pass->destination = destination;
    pass->cache_generation = input->resources.cache_generation;
    pass->bounds = bounds;
    return pass;
}

static uint64_t
moo_gpu_hash_pass(uint64_t hash, const MooCompGpuPass *pass)
{
    hash = moo_gpu_hash_u64(hash, pass->type);
    hash = moo_gpu_hash_u64(hash, pass->flags);
    hash = moo_gpu_hash_u64(hash, pass->surface);
    hash = moo_gpu_hash_u64(hash, pass->commit_sequence);
    hash = moo_gpu_hash_u64(hash, pass->source);
    hash = moo_gpu_hash_u64(hash, pass->destination);
    hash = moo_gpu_hash_u64(hash, pass->auxiliary);
    hash = moo_gpu_hash_u64(hash, pass->cache_generation);
    hash = moo_gpu_hash_rect(hash, pass->bounds);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->offset_x);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->offset_y);
    hash = moo_gpu_hash_u64(hash, pass->radius);
    hash = moo_gpu_hash_u64(hash, pass->saturation_q8_8);
    hash = moo_gpu_hash_u64(hash, pass->tint_mix);
    hash = moo_gpu_hash_u64(hash, pass->noise);
    hash = moo_gpu_hash_u64(hash, pass->color.r);
    hash = moo_gpu_hash_u64(hash, pass->color.g);
    hash = moo_gpu_hash_u64(hash, pass->color.b);
    hash = moo_gpu_hash_u64(hash, pass->color.a);
    hash = moo_gpu_hash_u64(hash, pass->noise_seed);
    hash = moo_gpu_hash_u64(hash, pass->corners.top_left);
    hash = moo_gpu_hash_u64(hash, pass->corners.top_right);
    hash = moo_gpu_hash_u64(hash, pass->corners.bottom_right);
    hash = moo_gpu_hash_u64(hash, pass->corners.bottom_left);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->affine.m11);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->affine.m12);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->affine.m21);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->affine.m22);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->affine.tx);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->affine.ty);
    hash = moo_gpu_hash_u64(hash, (uint32_t)pass->affine.origin_x);
    return moo_gpu_hash_u64(hash, (uint32_t)pass->affine.origin_y);
}

MooCompResult
moo_comp_effects_gpu_graph_init(
    MooCompGpuGraph *graph, MooCompGpuPass *passes, uint32_t capacity)
{
    if (graph == NULL || passes == NULL || capacity == 0u)
        return MOO_COMP_INVALID;
    if (capacity > MOO_COMP_GPU_MAX_PASSES)
        return MOO_COMP_LIMIT;
    *graph = (MooCompGpuGraph){passes, capacity, 0u, 0u};
    return MOO_COMP_OK;
}

uint64_t
moo_comp_effects_gpu_graph_hash(const MooCompGpuGraph *graph)
{
    uint64_t hash = MOO_GPU_HASH_OFFSET;
    uint32_t i;

    if (graph == NULL || graph->passes == NULL || graph->count == 0u ||
        graph->count > graph->capacity ||
        graph->capacity > MOO_COMP_GPU_MAX_PASSES)
        return 0u;
    hash = moo_gpu_hash_u64(hash, MOO_COMP_GPU_GRAPH_VERSION);
    hash = moo_gpu_hash_u64(hash, graph->count);
    for (i = 0u; i < graph->count; ++i)
        hash = moo_gpu_hash_pass(hash, &graph->passes[i]);
    return hash == 0u ? UINT64_C(1) : hash;
}

MooCompResult
moo_comp_effects_gpu_graph_build(
    MooCompGpuGraph *graph, const MooCompGpuSurfaceInput *input)
{
    const uint64_t backdrop_mask =
        MOO_COMP_EFFECT_BACKDROP_BLUR | MOO_COMP_EFFECT_SATURATION |
        MOO_COMP_EFFECT_TINT | MOO_COMP_EFFECT_NOISE;
    uint32_t required;
    uint32_t content_flags = 0u;
    MooCompGpuResourceId current;
    MooCompGpuResourceId next;
    MooCompGpuPass *pass;

    if (graph == NULL || graph->passes == NULL || input == NULL ||
        graph->capacity == 0u || graph->capacity > MOO_COMP_GPU_MAX_PASSES ||
        input->surface == MOO_COMP_HANDLE_INVALID ||
        !moo_gpu_state_valid(&input->effective) ||
        !moo_gpu_rect_valid(input->bounds.content_bounds) ||
        !moo_gpu_rect_valid(input->bounds.visual_bounds) ||
        !moo_gpu_rect_valid(input->bounds.backdrop_sample_bounds) ||
        !moo_gpu_resources_valid(input))
        return MOO_COMP_INVALID;

    required = moo_gpu_required_passes(&input->effective);
    if (required > graph->capacity)
        return MOO_COMP_LIMIT;

    graph->count = 0u;
    graph->semantic_hash = 0u;

    if ((input->effective.enabled_mask & MOO_COMP_EFFECT_SHADOW) != 0u) {
        pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_SHADOW_MASK,
            input->resources.content, input->resources.mask,
            input->bounds.visual_bounds);
        pass->corners = input->effective.corners;
        pass->affine = input->effective.affine;
        pass->offset_x = input->effective.shadow.offset_x;
        pass->offset_y = input->effective.shadow.offset_y;
        pass->radius = input->effective.shadow.spread_radius;
        if ((input->effective.enabled_mask & MOO_COMP_EFFECT_CORNER_CLIP) != 0u)
            pass->flags |= MOO_COMP_GPU_PASS_FLAG_CORNER_CLIP;
        if ((input->effective.enabled_mask & MOO_COMP_EFFECT_AFFINE_2D) != 0u)
            pass->flags |= MOO_COMP_GPU_PASS_FLAG_AFFINE_2D;
        current = input->resources.mask;
        if (input->effective.shadow.blur_radius != 0u) {
            pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_SHADOW_BLUR_H,
                current, input->resources.ping, input->bounds.visual_bounds);
            pass->radius = input->effective.shadow.blur_radius;
            pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_SHADOW_BLUR_V,
                input->resources.ping, input->resources.pong,
                input->bounds.visual_bounds);
            pass->radius = input->effective.shadow.blur_radius;
            current = input->resources.pong;
        }
        pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_SHADOW_COMPOSITE,
            current, input->resources.output, input->bounds.visual_bounds);
        pass->color = input->effective.shadow.color;
    }

    if ((input->effective.enabled_mask & backdrop_mask) != 0u) {
        (void)moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_BACKDROP_CAPTURE,
            input->resources.lower_z_prefix, input->resources.ping,
            input->bounds.backdrop_sample_bounds);
        current = input->resources.ping;
        next = input->resources.pong;

        if ((input->effective.enabled_mask &
             MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u &&
            input->effective.backdrop.blur_radius != 0u) {
            pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_BACKDROP_BLUR_H,
                current, next, input->bounds.backdrop_sample_bounds);
            pass->radius = input->effective.backdrop.blur_radius;
            current = next;
            next = input->resources.ping;
            pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_BACKDROP_BLUR_V,
                current, next, input->bounds.backdrop_sample_bounds);
            pass->radius = input->effective.backdrop.blur_radius;
            current = next;
            next = input->resources.pong;
        }
        if ((input->effective.enabled_mask & MOO_COMP_EFFECT_SATURATION) != 0u) {
            pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_SATURATION,
                current, next, input->bounds.backdrop_sample_bounds);
            pass->saturation_q8_8 = input->effective.backdrop.saturation_q8_8;
            current = next;
            next = current == input->resources.ping
                ? input->resources.pong : input->resources.ping;
        }
        if ((input->effective.enabled_mask & MOO_COMP_EFFECT_TINT) != 0u) {
            pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_TINT,
                current, next, input->bounds.backdrop_sample_bounds);
            pass->color = input->effective.backdrop.tint;
            pass->tint_mix = input->effective.backdrop.tint_mix;
            current = next;
            next = current == input->resources.ping
                ? input->resources.pong : input->resources.ping;
        }
        if ((input->effective.enabled_mask & MOO_COMP_EFFECT_NOISE) != 0u) {
            pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_NOISE,
                current, next, input->bounds.backdrop_sample_bounds);
            pass->noise = input->effective.backdrop.noise;
            pass->noise_seed = input->effective.backdrop.noise_seed;
            current = next;
        }
        (void)moo_gpu_append(graph, input,
            MOO_COMP_GPU_PASS_BACKDROP_COMPOSITE, current,
            input->resources.output, input->bounds.content_bounds);
    }

    if ((input->effective.enabled_mask & MOO_COMP_EFFECT_CORNER_CLIP) != 0u)
        content_flags |= MOO_COMP_GPU_PASS_FLAG_CORNER_CLIP;
    if ((input->effective.enabled_mask & MOO_COMP_EFFECT_AFFINE_2D) != 0u)
        content_flags |= MOO_COMP_GPU_PASS_FLAG_AFFINE_2D;
    pass = moo_gpu_append(graph, input, MOO_COMP_GPU_PASS_CONTENT,
        input->resources.content, input->resources.output,
        input->bounds.content_bounds);
    pass->flags = content_flags;
    pass->corners = input->effective.corners;
    pass->affine = input->effective.affine;

    graph->semantic_hash = moo_comp_effects_gpu_graph_hash(graph);
    return graph->semantic_hash == 0u ? MOO_COMP_LIMIT : MOO_COMP_OK;
}

MooCompResult
moo_comp_effects_gpu_backend_init(
    MooCompGpuBackendState *state, uint64_t generation)
{
    if (state == NULL || generation == 0u)
        return MOO_COMP_INVALID;
    *state = (MooCompGpuBackendState){
        MOO_COMP_GPU_BACKEND_READY, 0u, generation, 0u, 0u, 0u,
        0u, MOO_COMP_GPU_FENCE_INVALID, 0u
    };
    return MOO_COMP_OK;
}

MooCompResult
moo_comp_effects_gpu_backend_mark_lost(MooCompGpuBackendState *state)
{
    if (state == NULL)
        return MOO_COMP_INVALID;
    if (state->status != MOO_COMP_GPU_BACKEND_READY)
        return MOO_COMP_BAD_STATE;
    if (state->fallback_count == UINT64_MAX)
        return MOO_COMP_LIMIT;
    state->status = MOO_COMP_GPU_BACKEND_LOST;
    ++state->fallback_count;
    return MOO_COMP_OK;
}

MooCompResult
moo_comp_effects_gpu_backend_recover(
    MooCompGpuBackendState *state, uint64_t new_generation)
{
    if (state == NULL || new_generation == 0u)
        return MOO_COMP_INVALID;
    if (state->status != MOO_COMP_GPU_BACKEND_LOST)
        return MOO_COMP_BAD_STATE;
    if (state->generation == UINT64_MAX)
        return MOO_COMP_LIMIT;
    if (new_generation <= state->generation)
        return MOO_COMP_INVALID;
    state->status = MOO_COMP_GPU_BACKEND_READY;
    state->generation = new_generation;
    state->submitted_serial = 0u;
    state->completed_serial = 0u;
    state->last_graph_hash = 0u;
    state->pending_graph_hash = 0u;
    state->pending_fence = MOO_COMP_GPU_FENCE_INVALID;
    return MOO_COMP_OK;
}

int
moo_comp_effects_gpu_backend_requires_cpu(
    const MooCompGpuBackendState *state)
{
    return state == NULL || state->status != MOO_COMP_GPU_BACKEND_READY;
}

MooCompResult
moo_comp_effects_gpu_reserve_submission(
    MooCompGpuBackendState *state, const MooCompGpuGraph *graph,
    MooCompGpuFenceId presenter_fence, MooCompGpuSubmission *out_submission)
{
    MooCompGpuSubmission submission;
    uint64_t hash;

    if (state == NULL || graph == NULL || out_submission == NULL ||
        presenter_fence == MOO_COMP_GPU_FENCE_INVALID)
        return MOO_COMP_INVALID;
    if (state->status == MOO_COMP_GPU_BACKEND_LOST)
        return MOO_COMP_UNSUPPORTED;
    if (state->status != MOO_COMP_GPU_BACKEND_READY)
        return MOO_COMP_BAD_STATE;
    hash = moo_comp_effects_gpu_graph_hash(graph);
    if (hash == 0u || hash != graph->semantic_hash)
        return MOO_COMP_INVALID;
    if (state->submitted_serial != state->completed_serial)
        return MOO_COMP_WOULD_BLOCK;
    if (state->submitted_serial == UINT64_MAX)
        return MOO_COMP_LIMIT;

    submission.generation = state->generation;
    submission.serial = state->submitted_serial + 1u;
    submission.graph_hash = hash;
    submission.presenter_fence = presenter_fence;
    state->submitted_serial = submission.serial;
    state->last_graph_hash = hash;
    state->pending_graph_hash = hash;
    state->pending_fence = presenter_fence;
    *out_submission = submission;
    return MOO_COMP_OK;
}

MooCompResult
moo_comp_effects_gpu_complete_submission(
    MooCompGpuBackendState *state, const MooCompGpuSubmission *submission)
{
    if (state == NULL || submission == NULL ||
        submission->presenter_fence == MOO_COMP_GPU_FENCE_INVALID ||
        submission->graph_hash == 0u)
        return MOO_COMP_INVALID;
    if (state->status == MOO_COMP_GPU_BACKEND_LOST)
        return MOO_COMP_UNSUPPORTED;
    if (state->status != MOO_COMP_GPU_BACKEND_READY)
        return MOO_COMP_BAD_STATE;
    if (submission->generation != state->generation ||
        state->submitted_serial == state->completed_serial ||
        state->completed_serial == UINT64_MAX ||
        submission->serial != state->submitted_serial ||
        submission->serial != state->completed_serial + 1u ||
        submission->graph_hash != state->pending_graph_hash ||
        submission->presenter_fence != state->pending_fence)
        return MOO_COMP_STALE_HANDLE;
    state->completed_serial = submission->serial;
    state->pending_graph_hash = 0u;
    state->pending_fence = MOO_COMP_GPU_FENCE_INVALID;
    return MOO_COMP_OK;
}

uint64_t
moo_comp_effects_gpu_backend_hash(const MooCompGpuBackendState *state)
{
    uint64_t hash = MOO_GPU_HASH_OFFSET;
    if (state == NULL)
        return 0u;
    hash = moo_gpu_hash_u64(hash, state->status);
    hash = moo_gpu_hash_u64(hash, state->generation);
    hash = moo_gpu_hash_u64(hash, state->submitted_serial);
    hash = moo_gpu_hash_u64(hash, state->completed_serial);
    hash = moo_gpu_hash_u64(hash, state->last_graph_hash);
    hash = moo_gpu_hash_u64(hash, state->pending_graph_hash);
    hash = moo_gpu_hash_u64(hash, state->pending_fence);
    return moo_gpu_hash_u64(hash, state->fallback_count);
}
