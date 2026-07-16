/*
 * P016-O5 I1 real-binding integration gate.
 * QA_I1_BIND_REAL stays 1 after Completion, Damage and Alias-Poison GO.
 * Full green prints a measured check count plus a stable PASS marker.
 */
#include "../moo_compositor_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define QA_I1_BIND_REAL 1
#define QA_CLIENT_CAP 3u
#define QA_SURFACE_CAP 4u
#define QA_BUFFER_CAP 4u
#define QA_FRAME_CAP 4u
#define QA_EVENT_CAP 6u
#define QA_PIXEL_BYTES 64u
#define QA_DAMAGE_CAP 32u
#define QA_PASS_CAP 32u

static int checks;
static int failures;

#define CHECK(c, m) do {                                                     \
    checks++;                                                                \
    if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n",             \
                                     __FILE__, __LINE__, (m)); }              \
} while (0)

_Static_assert(MOO_COMP_EFFECT_INTEGRATION_VERSION == UINT32_C(1),
               "I1 contract version changed");
_Static_assert(sizeof(MooCompEffectSurfaceBinding) > 0u,
               "binding type missing");
_Static_assert(sizeof(MooCompEffectTransactionWorkspace) > 0u,
               "transaction workspace missing");
_Static_assert(sizeof(MooCompEffectFrameWorkspace) > 0u,
               "frame workspace missing");
_Static_assert(sizeof(MooCompEffectCommitRequest) > 0u,
               "commit request missing");
_Static_assert(sizeof(MooCompEffectFrameRequest) > 0u,
               "frame request missing");
_Static_assert(sizeof(MooCompEffectFrameResult) > 0u,
               "frame result missing");
_Static_assert(sizeof(&moo_comp_effects_bind) > 0u, "bind symbol missing");
_Static_assert(sizeof(&moo_comp_surface_effect_set) > 0u,
               "effect_set symbol missing");
_Static_assert(sizeof(&moo_comp_surface_commit_ex) > 0u,
               "commit_ex symbol missing");
_Static_assert(sizeof(&moo_comp_surface_animation_start) > 0u,
               "animation_start symbol missing");
_Static_assert(sizeof(&moo_comp_surface_animation_cancel) > 0u,
               "animation_cancel symbol missing");
_Static_assert(sizeof(&moo_comp_surface_destroy_ex) > 0u,
               "destroy_ex symbol missing");
_Static_assert(sizeof(&moo_comp_client_disconnect_ex) > 0u,
               "disconnect_ex symbol missing");
_Static_assert(sizeof(&moo_comp_build_frame_ex) > 0u,
               "build_frame_ex symbol missing");
_Static_assert(sizeof(&moo_comp_raster_copy_rgba) > 0u,
               "raster copy symbol missing");

typedef enum {
    QA_CALL_BIND = 1,
    QA_CALL_SET = 2,
    QA_CALL_COMMIT = 3,
    QA_CALL_START = 4,
    QA_CALL_CANCEL = 5,
    QA_CALL_DESTROY = 6,
    QA_CALL_DISCONNECT = 7,
    QA_CALL_FRAME = 8
} QaCall;

typedef struct {
    MooCompositor core;
    MooCompClientSlot clients[QA_CLIENT_CAP];
    MooCompSurfaceSlot surfaces[QA_SURFACE_CAP];
    MooCompBufferSlot buffers[QA_BUFFER_CAP];
    MooCompFrameSlot frames[QA_FRAME_CAP];
    MooCompEventSlot events[QA_EVENT_CAP];

    uint8_t lower_pixels[QA_PIXEL_BYTES];
    uint8_t upper_pixels[QA_PIXEL_BYTES];
    uint8_t output_pixels[QA_PIXEL_BYTES];
    uint8_t prefix_pixels[QA_PIXEL_BYTES];
    MooCompOutput output;
    MooCompHandle client_a;
    MooCompHandle client_b;
    MooCompHandle lower_surface;
    MooCompHandle upper_surface;
    MooCompHandle lower_buffer;
    MooCompHandle upper_buffer;

    MooCompEffectStateConfig effect_config;
    MooCompEffectSurfaceBinding bindings[QA_SURFACE_CAP];
    MooCompAnimationTimeline timeline;
    MooCompAnimationSlot timeline_slots[8];
    MooCompGpuBackendState gpu;
    MooCompEffectCompletionReservation reservations[QA_EVENT_CAP];
    MooCompEffectIntegration integration;

    MooCompEffectPreparedCore prepared;
    MooCompSurfaceSlot tx_surfaces[QA_SURFACE_CAP];
    MooCompBufferSlot tx_buffers[QA_BUFFER_CAP];
    MooCompFrameSlot tx_frames[QA_FRAME_CAP];
    MooCompEventSlot tx_events[QA_EVENT_CAP];
    MooCompAnimationSlot tx_timeline[8];
    MooCompAnimationCompletion tx_completions[QA_EVENT_CAP];
    MooCompEffectCompletionReservation tx_reservations[QA_EVENT_CAP];
    MooCompEffectTransactionWorkspace tx;

    MooCompAnimationSlot frame_timeline[8];
    MooCompEffectSurfaceBinding frame_bindings[QA_SURFACE_CAP];
    MooCompAnimationSample samples[8];
    MooCompAnimationCompletion completions[QA_EVENT_CAP];
    MooCompEffectCompletionReservation frame_reservations[QA_EVENT_CAP];
    MooCompEffectDamageSurface damage_surfaces[QA_SURFACE_CAP];
    MooCompRect damage_workspace[QA_DAMAGE_CAP];
    MooCompRect damage_output[QA_DAMAGE_CAP];
    uint32_t surface_order[QA_SURFACE_CAP];
    uint32_t rgba_ping[QA_PIXEL_BYTES];
    uint32_t rgba_pong[QA_PIXEL_BYTES];
    MooCompGpuPass gpu_passes[QA_PASS_CAP];
    MooCompEffectFrameWorkspace frame_workspace;

    MooCompEffectCommitRequest commit;
    MooCompEffectFrameRequest frame_request;
    MooCompEffectFrameResult frame_result;
    MooCompAnimationDesc animation;
    MooCompHandle request_owner;
    MooCompHandle request_surface;
    uint32_t completion_count;
} QaFixture;

typedef struct {
    MooCompositor core;
    MooCompClientSlot clients[QA_CLIENT_CAP];
    MooCompSurfaceSlot surfaces[QA_SURFACE_CAP];
    MooCompBufferSlot buffers[QA_BUFFER_CAP];
    MooCompFrameSlot frames[QA_FRAME_CAP];
    MooCompEventSlot events[QA_EVENT_CAP];
    MooCompEffectSurfaceBinding bindings[QA_SURFACE_CAP];
    MooCompAnimationTimeline timeline;
    MooCompAnimationSlot timeline_slots[8];
    MooCompGpuBackendState gpu;
    MooCompEffectCompletionReservation reservations[QA_EVENT_CAP];
    MooCompEffectIntegration integration;
    uint8_t output_pixels[QA_PIXEL_BYTES];
} QaAuthoritativeSnapshot;

static void snapshot_take(
    const QaFixture *f, QaAuthoritativeSnapshot *out) {
    out->core = f->core;
    memcpy(out->clients, f->clients, sizeof(out->clients));
    memcpy(out->surfaces, f->surfaces, sizeof(out->surfaces));
    memcpy(out->buffers, f->buffers, sizeof(out->buffers));
    memcpy(out->frames, f->frames, sizeof(out->frames));
    memcpy(out->events, f->events, sizeof(out->events));
    memcpy(out->bindings, f->bindings, sizeof(out->bindings));
    out->timeline = f->timeline;
    memcpy(out->timeline_slots, f->timeline_slots,
           sizeof(out->timeline_slots));
    out->gpu = f->gpu;
    memcpy(out->reservations, f->reservations,
           sizeof(out->reservations));
    out->integration = f->integration;
    memcpy(out->output_pixels, f->output_pixels,
           sizeof(out->output_pixels));
}

static int snapshot_equal(
    const QaFixture *f, const QaAuthoritativeSnapshot *before) {
    QaAuthoritativeSnapshot after;
    snapshot_take(f, &after);
    return memcmp(&after, before, sizeof(after)) == 0;
}

static uint64_t hash_bytes(const uint8_t *bytes, size_t count) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0u; i < count; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void fill_solid(
    uint8_t *pixels, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    size_t i;
    for (i = 0u; i < QA_PIXEL_BYTES; i += 4u) {
        pixels[i] = r;
        pixels[i + 1u] = g;
        pixels[i + 2u] = b;
        pixels[i + 3u] = a;
    }
}

static int pixel_is(
    const uint8_t *pixels, uint32_t x, uint32_t y,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    size_t i = ((size_t)y * 4u + (size_t)x) * 4u;
    return pixels[i] == r && pixels[i + 1u] == g &&
           pixels[i + 2u] == b && pixels[i + 3u] == a;
}

static MooCompBufferView rgba_view(const uint8_t *pixels) {
    MooCompBufferView view = {
        pixels, QA_PIXEL_BYTES, 16u, 4, 4, MOO_COMP_FORMAT_RGBA8888
    };
    return view;
}

static MooCompResult attach_commit(
    QaFixture *f, MooCompHandle surface, MooCompHandle buffer) {
    MooCompRect full = {0, 0, 4, 4};
    MooCompResult r;
    r = moo_comp_surface_attach(&f->core, f->client_a, surface, buffer);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_surface_damage(&f->core, f->client_a, surface, full);
    if (r != MOO_COMP_OK) return r;
    return moo_comp_surface_commit(&f->core, f->client_a, surface);
}

static void workspace_init(QaFixture *f, uint32_t event_capacity) {
    f->tx.prepared = &f->prepared;
    f->tx.surface_slots = f->tx_surfaces;
    f->tx.surface_capacity = QA_SURFACE_CAP;
    f->tx.buffer_slots = f->tx_buffers;
    f->tx.buffer_capacity = QA_BUFFER_CAP;
    f->tx.frame_slots = f->tx_frames;
    f->tx.frame_capacity = QA_FRAME_CAP;
    f->tx.event_slots = f->tx_events;
    f->tx.event_capacity = event_capacity;
    f->tx.timeline_slots = f->tx_timeline;
    f->tx.timeline_capacity = 8u;
    f->tx.completions = f->tx_completions;
    f->tx.completion_capacity = QA_EVENT_CAP;
    f->tx.reservation_clones = f->tx_reservations;
    f->tx.reservation_capacity = QA_EVENT_CAP;

    f->frame_workspace.timeline_slots = f->frame_timeline;
    f->frame_workspace.timeline_capacity = 8u;
    f->frame_workspace.binding_clones = f->frame_bindings;
    f->frame_workspace.binding_capacity = QA_SURFACE_CAP;
    f->frame_workspace.samples = f->samples;
    f->frame_workspace.sample_capacity = 8u;
    f->frame_workspace.completions = f->completions;
    f->frame_workspace.completion_capacity = QA_EVENT_CAP;
    f->frame_workspace.reservation_clones = f->frame_reservations;
    f->frame_workspace.reservation_capacity = QA_EVENT_CAP;
    f->frame_workspace.damage_surfaces = f->damage_surfaces;
    f->frame_workspace.damage_surface_capacity = QA_SURFACE_CAP;
    f->frame_workspace.damage_workspace_regions = f->damage_workspace;
    f->frame_workspace.damage_workspace_capacity = QA_DAMAGE_CAP;
    f->frame_workspace.damage_output_regions = f->damage_output;
    f->frame_workspace.damage_output_capacity = QA_DAMAGE_CAP;
    f->frame_workspace.surface_order = f->surface_order;
    f->frame_workspace.surface_order_capacity = QA_SURFACE_CAP;
    f->frame_workspace.lower_z_pixels = f->prefix_pixels;
    f->frame_workspace.lower_z_capacity = QA_PIXEL_BYTES;
    f->frame_workspace.rgba_ping = f->rgba_ping;
    f->frame_workspace.rgba_pong = f->rgba_pong;
    f->frame_workspace.rgba_words_per_buffer = QA_PIXEL_BYTES;
    f->frame_workspace.gpu_passes = f->gpu_passes;
    f->frame_workspace.gpu_pass_capacity = QA_PASS_CAP;
}

static MooCompResult fixture_init(QaFixture *f, uint32_t event_capacity) {
    MooCompConfig config = {4, 4, 3u, 5u, 7u, 255u};
    MooCompEffectLimits limits = moo_comp_effect_limits_default();
    MooCompBufferView lower;
    MooCompBufferView upper;
    MooCompResult r;

    memset(f, 0, sizeof(*f));
    fill_solid(f->lower_pixels, 180u, 20u, 10u, 255u);
    fill_solid(f->upper_pixels, 20u, 40u, 220u, 160u);
    memset(f->output_pixels, 0x6cu, sizeof(f->output_pixels));
    memset(f->prefix_pixels, 0xa5, sizeof(f->prefix_pixels));
    f->output = (MooCompOutput){
        f->output_pixels, QA_PIXEL_BYTES, 16u, 4, 4
    };
    workspace_init(f, event_capacity);

    r = moo_comp_init(
        &f->core, &config, f->clients, QA_CLIENT_CAP,
        f->surfaces, QA_SURFACE_CAP, f->buffers, QA_BUFFER_CAP,
        f->frames, QA_FRAME_CAP, f->events, event_capacity);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_client_create(&f->core, &f->client_a);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_client_create(&f->core, &f->client_b);
    if (r != MOO_COMP_OK) return r;
    lower = rgba_view(f->lower_pixels);
    upper = rgba_view(f->upper_pixels);
    r = moo_comp_buffer_register(
        &f->core, f->client_a, &lower, &f->lower_buffer);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_buffer_register(
        &f->core, f->client_a, &upper, &f->upper_buffer);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_surface_create(
        &f->core, f->client_a, &f->lower_surface);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_surface_create(
        &f->core, f->client_a, &f->upper_surface);
    if (r != MOO_COMP_OK) return r;
    r = attach_commit(f, f->lower_surface, f->lower_buffer);
    if (r != MOO_COMP_OK) return r;
    r = attach_commit(f, f->upper_surface, f->upper_buffer);
    if (r != MOO_COMP_OK) return r;

    r = moo_comp_effect_state_config_init(
        &f->effect_config, MOO_COMP_EFFECTS_V2, &limits);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_animation_timeline_init(
        &f->timeline, f->timeline_slots, 8u, &limits);
    if (r != MOO_COMP_OK) return r;
    r = moo_comp_effects_gpu_backend_init(&f->gpu, UINT64_C(1));
    if (r != MOO_COMP_OK) return r;

    f->request_owner = f->client_a;
    f->request_surface = f->upper_surface;
    f->commit.buffer = f->upper_buffer;
    f->commit.scale = 1u;
    f->commit.opacity = 255u;
    f->commit.damage[0] = (MooCompRect){0, 0, 4, 4};
    f->commit.damage_count = 1u;
    f->commit.effect = moo_comp_effect_state_neutral();
    f->commit.effect.enabled_mask = MOO_COMP_EFFECT_TINT;
    f->commit.effect.backdrop.tint =
        (MooCompRgba8){30u, 60u, 90u, 255u};
    f->commit.effect.backdrop.tint_mix = 128u;
    f->commit.target = (MooCompEffectCpuTarget){
        f->output_pixels, QA_PIXEL_BYTES, 16u, 4, 4
    };
    f->commit.lower_z = (MooCompEffectCpuSource){
        f->prefix_pixels, QA_PIXEL_BYTES, 16u, 4, 4
    };
    f->animation.token = UINT64_C(77);
    f->animation.duration_ns = limits.min_animation_duration_ns;
    f->animation.repeat_count = 1u;
    f->animation.property = MOO_COMP_ANIMATION_PROPERTY_OPACITY;
    f->animation.easing = MOO_COMP_ANIMATION_EASING_LINEAR;
    f->animation.direction = MOO_COMP_ANIMATION_DIRECTION_NORMAL;
    f->animation.from.word[0] = (uint32_t)MOO_COMP_Q16_ONE;
    f->animation.to.word[0] = 0u;
    f->frame_request.timestamp_ns = UINT64_C(1000);
    f->frame_request.presenter_fence = UINT64_C(1);
    f->frame_request.allow_gpu = 1u;
    return MOO_COMP_OK;
}

/*
 * The only temporary RED seam. Setting QA_I1_BIND_REAL to 1 binds these exact
 * V2 types and symbols without changing the scenario tests.
 */
static MooCompResult qa_i1_execute(QaFixture *f, QaCall call) {
#if QA_I1_BIND_REAL
    MooCompResult r;
    if (f->integration.version == 0u) {
        r = moo_comp_effects_bind(
            &f->core, &f->integration, &f->effect_config,
            f->bindings, QA_SURFACE_CAP, &f->timeline, &f->gpu,
            f->reservations, QA_EVENT_CAP, &f->tx);
        if (r != MOO_COMP_OK || call == QA_CALL_BIND) return r;
    }
    switch (call) {
        case QA_CALL_BIND:
            return MOO_COMP_OK;
        case QA_CALL_SET:
            return moo_comp_surface_effect_set(
                &f->core, &f->integration, f->request_owner,
                f->request_surface, &f->commit.effect);
        case QA_CALL_COMMIT:
            return moo_comp_surface_commit_ex(
                &f->core, &f->integration, f->request_owner,
                f->request_surface, &f->commit, &f->tx);
        case QA_CALL_START:
            return moo_comp_surface_animation_start(
                &f->core, &f->integration, f->request_owner,
                f->request_surface, &f->animation, UINT64_C(1000), 0u,
                &f->tx, &f->completion_count);
        case QA_CALL_CANCEL:
            return moo_comp_surface_animation_cancel(
                &f->core, &f->integration, f->request_owner,
                f->request_surface, f->animation.token, UINT64_C(2000),
                &f->tx, &f->completion_count);
        case QA_CALL_DESTROY:
            return moo_comp_surface_destroy_ex(
                &f->core, &f->integration, f->request_owner,
                f->request_surface, UINT64_C(2000), &f->tx,
                &f->completion_count);
        case QA_CALL_DISCONNECT:
            return moo_comp_client_disconnect_ex(
                &f->core, &f->integration, f->request_owner,
                UINT64_C(2000), &f->tx, &f->completion_count);
        case QA_CALL_FRAME:
            return moo_comp_build_frame_ex(
                &f->core, &f->integration, &f->output,
                &f->frame_request, &f->frame_workspace, &f->frame_result);
        default:
            return MOO_COMP_INVALID;
    }
#else
    (void)f;
    (void)call;
    return MOO_COMP_UNSUPPORTED;
#endif
}

static MooCompEffectSurfaceBinding *binding_for(
    QaFixture *f, MooCompHandle surface) {
    uint32_t i;
    for (i = 0u; i < QA_SURFACE_CAP; ++i)
        if (f->bindings[i].active && f->bindings[i].surface == surface)
            return &f->bindings[i];
    return NULL;
}

static void test_bind_rejects_nonempty_candidate_graph(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompResult r;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "nonempty bind fixture init");
    f.timeline_slots[0].active = 1u;
    f.timeline_slots[0].surface = f.upper_surface;
    f.timeline_slots[0].desc = f.animation;
    f.timeline.active_count = 1u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "bind published a nonempty timeline without reservation graph");

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "orphan event bind fixture init");
    f.events[0].slot_state = MOO_COMP_SLOT_RESERVED;
    f.events[0].live = 0u;
    f.events[0].reserved = 1u;
    f.events[0].reserved_slot = 1u;
    f.events[0].owner = f.client_a;
    f.events[0].sequence = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "bind published an orphan physical RESERVED EventSlot");
}

static void test_bind_set_commit_and_v1_guards(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompEffectSurfaceBinding *binding;
    MooCompBufferInfo buffer_before;
    MooCompBufferInfo buffer_after;
    MooCompEffectState matching_effect;
    uint64_t sequence;
    uint64_t event_sequence;
    MooCompResult r;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "commit fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "RED: I1 bind is UNSUPPORTED");
    if (r != MOO_COMP_OK) return;
    r = qa_i1_execute(&f, QA_CALL_SET);
    CHECK(r == MOO_COMP_OK, "effect_set pending failed");
    snapshot_take(&f, &before);
    CHECK(moo_comp_surface_commit(
              &f.core, f.client_a, f.upper_surface) == MOO_COMP_BAD_STATE,
          "bound V1 commit must fail BAD_STATE");
    CHECK(snapshot_equal(&f, &before), "bound V1 commit mutated state");
    CHECK(moo_comp_surface_destroy(
              &f.core, f.client_a, f.upper_surface) == MOO_COMP_BAD_STATE,
          "bound V1 destroy must fail BAD_STATE");
    CHECK(snapshot_equal(&f, &before), "bound V1 destroy mutated state");

    matching_effect = f.commit.effect;
    f.commit.effect.backdrop.tint_mix ^= 1u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_COMMIT);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "commit accepted request.effect different from pending effect");
    f.commit.effect = matching_effect;

    f.commit.frame_token = UINT64_C(91);
    CHECK(moo_comp_buffer_info(&f.core, f.upper_buffer,
                               &buffer_before) == MOO_COMP_OK,
          "same-buffer pre-commit info");
    sequence = f.core.commit_sequence;
    event_sequence = f.core.event_sequence;
    r = qa_i1_execute(&f, QA_CALL_COMMIT);
    CHECK(r == MOO_COMP_OK, "transactional commit failed");
    binding = binding_for(&f, f.upper_surface);
    CHECK(moo_comp_buffer_info(&f.core, f.upper_buffer,
                               &buffer_after) == MOO_COMP_OK &&
          buffer_after.ref_count == buffer_before.ref_count &&
          f.prepared.old_buffer_index == f.prepared.new_buffer_index &&
          f.prepared.old_buffer_before.ref_count ==
              f.prepared.old_buffer_after.ref_count,
          "same-buffer alias changed refcount or used split snapshots");
    CHECK(f.prepared.valid &&
          f.prepared.commit_sequence_before == sequence &&
          f.prepared.commit_sequence_after == sequence + 1u &&
          f.prepared.event_sequence_before == event_sequence &&
          f.prepared.event_sequence_after == event_sequence &&
          f.prepared.frame_index < QA_FRAME_CAP &&
          f.prepared.event_index == UINT32_MAX &&
          f.prepared.frame_before.slot_state == MOO_COMP_SLOT_FREE &&
          f.prepared.frame_after.slot_state == MOO_COMP_SLOT_LIVE,
          "PreparedCore frame/no-event/sequence snapshots are not exact");
    CHECK(binding != NULL &&
          f.core.commit_sequence == sequence + 1u &&
          binding->state.commit_sequence == f.core.commit_sequence &&
          binding->effect_dirty == 0u &&
          f.prepared.surface_after.pending.dirty_mask == 0u &&
          f.prepared.surface_after.pending.damage_count == 0u &&
          f.prepared.surface_after.pending.frame_token == 0u,
          "Core/S1 sequence publish or pending clear is not atomic");
    CHECK(binding != NULL &&
          binding->state.work_units == UINT64_C(16) &&
          binding->state.scratch_bytes == UINT64_C(0),
          "tint commit did not use full 4x4 target budget");

    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_COMMIT);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "commit without dirty pending effect did not fail closed");

    buffer_before = buffer_after;
    sequence = f.core.commit_sequence;
    f.commit.buffer = MOO_COMP_HANDLE_INVALID;
    f.commit.frame_token = 0u;
    f.commit.damage_count = 0u;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK,
          "detach matching pending setup");
    r = qa_i1_execute(&f, QA_CALL_COMMIT);
    CHECK(r == MOO_COMP_OK &&
          moo_comp_buffer_info(&f.core, f.upper_buffer,
                               &buffer_after) == MOO_COMP_OK,
          "detach commit failed");
    CHECK(buffer_before.ref_count > 0u &&
          buffer_after.ref_count + 1u == buffer_before.ref_count &&
          f.prepared.old_buffer_before.ref_count == buffer_before.ref_count &&
          f.prepared.old_buffer_after.ref_count == buffer_after.ref_count &&
          f.prepared.surface_after.committed.buffer ==
              MOO_COMP_HANDLE_INVALID,
          "detach did not release exactly the old buffer");
    CHECK(f.prepared.commit_sequence_before == sequence &&
          f.prepared.commit_sequence_after == sequence + 1u &&
          f.prepared.surface_after.pending.dirty_mask == 0u,
          "detach PreparedCore sequence/pending snapshot mismatch");
}

static void test_alternating_surface_sequences(void) {
    QaFixture f;
    MooCompSurfaceInfo upper_before = {0};
    MooCompSurfaceInfo lower_before = {0};
    MooCompSurfaceInfo upper_after = {0};
    MooCompSurfaceInfo lower_after = {0};
    MooCompEffectSurfaceBinding *upper_binding;
    MooCompEffectSurfaceBinding *lower_binding;
    uint64_t global_sequence;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "alternating sequence fixture init");
    CHECK(qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK,
          "RED: alternating sequence bind unavailable");
    if (f.integration.version == 0u) return;
    upper_binding = binding_for(&f, f.upper_surface);
    lower_binding = binding_for(&f, f.lower_surface);
    CHECK(upper_binding != NULL && lower_binding != NULL &&
          moo_comp_surface_info(&f.core, f.upper_surface,
                                &upper_before) == MOO_COMP_OK &&
          moo_comp_surface_info(&f.core, f.lower_surface,
                                &lower_before) == MOO_COMP_OK,
          "alternating sequence initial state");
    if (upper_binding == NULL || lower_binding == NULL) return;

    global_sequence = f.core.commit_sequence;
    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "alternating A1 set+commit");
    CHECK(moo_comp_surface_info(&f.core, f.upper_surface,
                                &upper_after) == MOO_COMP_OK &&
          moo_comp_surface_info(&f.core, f.lower_surface,
                                &lower_after) == MOO_COMP_OK &&
          f.core.commit_sequence == global_sequence + 1u &&
          upper_after.commit_sequence == upper_before.commit_sequence + 1u &&
          upper_binding->state.commit_sequence ==
              upper_after.commit_sequence &&
          lower_after.commit_sequence == lower_before.commit_sequence &&
          lower_binding->state.commit_sequence ==
              lower_after.commit_sequence &&
          f.prepared.surface_before.committed.commit_sequence ==
              upper_before.commit_sequence &&
          f.prepared.surface_after.committed.commit_sequence ==
              upper_after.commit_sequence,
          "A1 local/global sequence publication diverged");
    upper_before = upper_after;
    lower_before = lower_after;
    global_sequence = f.core.commit_sequence;

    f.request_surface = f.lower_surface;
    f.commit.buffer = f.lower_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "alternating B set+commit");
    CHECK(moo_comp_surface_info(&f.core, f.upper_surface,
                                &upper_after) == MOO_COMP_OK &&
          moo_comp_surface_info(&f.core, f.lower_surface,
                                &lower_after) == MOO_COMP_OK &&
          f.core.commit_sequence == global_sequence + 1u &&
          lower_after.commit_sequence == lower_before.commit_sequence + 1u &&
          lower_binding->state.commit_sequence ==
              lower_after.commit_sequence &&
          upper_after.commit_sequence == upper_before.commit_sequence &&
          upper_binding->state.commit_sequence ==
              upper_after.commit_sequence &&
          f.prepared.surface_before.committed.commit_sequence ==
              lower_before.commit_sequence &&
          f.prepared.surface_after.committed.commit_sequence ==
              lower_after.commit_sequence,
          "B local/global sequence publication diverged");
    upper_before = upper_after;
    lower_before = lower_after;
    global_sequence = f.core.commit_sequence;

    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    f.commit.frame_token = UINT64_C(92);
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "alternating A2 set+commit");
    CHECK(moo_comp_surface_info(&f.core, f.upper_surface,
                                &upper_after) == MOO_COMP_OK &&
          moo_comp_surface_info(&f.core, f.lower_surface,
                                &lower_after) == MOO_COMP_OK &&
          f.core.commit_sequence == global_sequence + 1u &&
          upper_after.commit_sequence == upper_before.commit_sequence + 1u &&
          upper_binding->state.commit_sequence ==
              upper_after.commit_sequence &&
          lower_after.commit_sequence == lower_before.commit_sequence &&
          lower_binding->state.commit_sequence ==
              lower_after.commit_sequence &&
          f.prepared.frame_index < QA_FRAME_CAP &&
          f.prepared.frame_after.slot_state == MOO_COMP_SLOT_LIVE &&
          f.prepared.frame_after.commit_sequence ==
              f.prepared.surface_after.committed.commit_sequence &&
          f.prepared.frame_after.commit_sequence ==
              upper_binding->state.commit_sequence &&
          f.prepared.frame_after.commit_sequence !=
              f.core.commit_sequence,
          "A2 frame did not carry surface-local sequence identity");
}

static void test_scale2_requirements_and_pixels(void) {
    QaFixture f;
    MooCompEffectSurfaceBinding *binding;
    uint32_t x;
    uint32_t y;
    uint8_t *pixel;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "scale2 fixture init");
    for (y = 0u; y < 4u; ++y) {
        for (x = 0u; x < 4u; ++x) {
            pixel = &f.upper_pixels[(y * 4u + x) * 4u];
            pixel[0] = x < 2u ? (y < 2u ? 255u : 0u)
                                : (y < 2u ? 0u : 255u);
            pixel[1] = x < 2u ? 0u : 255u;
            pixel[2] = y < 2u ? 0u : 255u;
            pixel[3] = 255u;
        }
    }
    CHECK(qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK,
          "RED: scale2 bind unavailable");
    if (f.integration.version == 0u) return;
    f.commit.scale = 2u;
    f.commit.effect.backdrop.tint_mix = 0u;
    f.frame_request.allow_gpu = 0u;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "scale2 matching set+commit");
    binding = binding_for(&f, f.upper_surface);
    CHECK(binding != NULL && binding->state.work_units == UINT64_C(4) &&
          binding->state.scratch_bytes == UINT64_C(0),
          "scale2 commit requirements are not logical 2x2");
    CHECK(qa_i1_execute(&f, QA_CALL_FRAME) == MOO_COMP_OK,
          "scale2 CPU frame failed");
    CHECK(binding != NULL &&
          f.frame_result.work_units == binding->state.work_units &&
          f.frame_result.scratch_bytes == binding->state.scratch_bytes,
          "scale2 commit/build requirements diverged");
    CHECK(pixel_is(f.output_pixels, 0u, 0u, 255u, 0u, 0u, 255u) &&
          pixel_is(f.output_pixels, 1u, 0u, 0u, 255u, 0u, 255u) &&
          pixel_is(f.output_pixels, 0u, 1u, 0u, 0u, 255u, 255u) &&
          pixel_is(f.output_pixels, 1u, 1u, 255u, 255u, 255u, 255u),
          "scale2 did not sample physical source at x*2,y*2");
}

static void test_animation_start_retarget_atomicity(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompEffectSurfaceBinding *binding;
    MooCompEvent event;
    MooCompHandle stale;
    MooCompResult r;
    uint32_t old_reservation;
    uint32_t old_event;
    uint32_t active_new;
    uint32_t reserved_new;
    uint32_t i;
    uint64_t event_sequence;
    uint64_t surface_sequence;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "retarget fixture init");
    CHECK(qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK,
          "RED: retarget bind unavailable");
    if (f.integration.version == 0u) return;
    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "retarget upper A commit");
    f.request_surface = f.lower_surface;
    f.commit.buffer = f.lower_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "retarget lower B commit");
    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "retarget upper A second commit");
    binding = binding_for(&f, f.upper_surface);
    surface_sequence = binding != NULL ? binding->state.commit_sequence : 0u;
    CHECK(binding != NULL && surface_sequence != f.core.commit_sequence,
          "retarget setup did not separate surface-local/global sequence");
    CHECK(qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "retarget baseline start");
    old_reservation = UINT32_MAX;
    old_event = UINT32_MAX;
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state == MOO_COMP_SLOT_RESERVED &&
            f.reservations[i].surface == f.upper_surface &&
            f.reservations[i].token == UINT64_C(77)) {
            old_reservation = i;
            old_event = f.reservations[i].event_index;
        }
    }
    CHECK(binding != NULL && f.timeline.active_count == 1u &&
          binding->reserved_completion_events == 1u &&
          old_reservation != UINT32_MAX && old_event < QA_EVENT_CAP,
          "retarget baseline graph/count mismatch");
    if (binding == NULL || old_reservation == UINT32_MAX ||
        old_event >= QA_EVENT_CAP)
        return;

    f.animation.token = UINT64_C(78);
    f.tx.completion_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "retarget completion cap-1 mutated authoritative state");
    f.tx.completion_capacity = QA_EVENT_CAP;

    event_sequence = f.core.event_sequence;
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_OK && f.completion_count == 1u &&
          f.tx_completions[0].surface == f.upper_surface &&
          f.tx_completions[0].token == UINT64_C(77) &&
          f.tx_completions[0].status == MOO_COMP_ANIMATION_DONE_REPLACED &&
          f.tx_completions[0].timestamp_ns == UINT64_C(1000),
          "retarget did not publish exact REPLACED completion");
    active_new = 0u;
    reserved_new = 0u;
    for (i = 0u; i < f.timeline.capacity; ++i) {
        if (f.timeline.slots[i].active &&
            f.timeline.slots[i].surface == f.upper_surface &&
            f.timeline.slots[i].desc.token == UINT64_C(78))
            active_new++;
    }
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state == MOO_COMP_SLOT_RESERVED &&
            f.reservations[i].surface == f.upper_surface &&
            f.reservations[i].token == UINT64_C(78))
            reserved_new++;
    }
    CHECK(f.timeline.active_count == 1u && active_new == 1u &&
          reserved_new == 1u &&
          binding->reserved_completion_events == 1u &&
          f.reservations[old_reservation].slot_state == MOO_COMP_SLOT_FREE &&
          f.events[old_event].slot_state == MOO_COMP_SLOT_LIVE &&
          f.events[old_event].event.type == MOO_COMP_EVENT_ANIMATION_DONE &&
          f.events[old_event].event.token == UINT64_C(77) &&
          f.events[old_event].event.status == MOO_COMP_ANIMATION_DONE_REPLACED &&
          f.events[old_event].event.present_sequence == surface_sequence &&
          f.events[old_event].event.present_sequence ==
              binding->state.commit_sequence &&
          f.events[old_event].event.present_sequence != f.core.commit_sequence &&
          f.core.event_sequence == event_sequence + 1u,
          "retarget reservation/EventSlot/Bindingcount net state diverged");
    CHECK(moo_comp_next_event(&f.core, f.client_a, &event) == MOO_COMP_OK &&
          event.token == UINT64_C(77) &&
          event.status == MOO_COMP_ANIMATION_DONE_REPLACED &&
          moo_comp_next_event(&f.core, f.client_a, &event) ==
              MOO_COMP_WOULD_BLOCK,
          "retarget old terminal/new reservation visibility is wrong");

    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "duplicate active token retarget did not fail closed");
    f.animation.token = UINT64_C(79);
    f.animation.from.word[0] = 1u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "retarget current/from mismatch did not fail closed");

    CHECK(fixture_init(&f, 5u) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "retarget nondivisible quota fixture setup");
    binding = binding_for(&f, f.upper_surface);
    f.animation.token = UINT64_C(78);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "retarget exceeded canonical floor quota non-atomically");
    CHECK(binding != NULL && f.timeline.active_count == 1u &&
          binding->reserved_completion_events == 1u,
          "retarget floor-quota rejection changed graph/count state");

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "retarget invalid-id fixture setup");
    f.request_owner = f.client_b;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_ACCESS && snapshot_equal(&f, &before),
          "cross-owner animation start did not fail closed");
    f.request_owner = f.client_a;
    f.request_surface = MOO_COMP_HANDLE_INVALID;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "invalid surface animation start did not fail closed");
    stale = f.upper_surface;
    f.request_surface = stale;
    CHECK(qa_i1_execute(&f, QA_CALL_DESTROY) == MOO_COMP_OK,
          "retarget stale setup destroy");
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_START);
    CHECK(r == MOO_COMP_STALE_HANDLE && snapshot_equal(&f, &before),
          "stale surface animation start did not fail closed");
}

static void test_animation_reservation_lifecycle(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompEffectSurfaceBinding *binding;
    MooCompEvent event;
    MooCompHandle stale;
    MooCompResult r;
    uint32_t reservation_index;
    uint32_t event_index;
    uint32_t active_matches;
    uint32_t i;
    uint64_t event_sequence;
    uint64_t surface_sequence;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "cancel fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "RED: cancel bind unavailable");
    if (r != MOO_COMP_OK) return;

    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "cancel upper A commit");
    f.request_surface = f.lower_surface;
    f.commit.buffer = f.lower_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "cancel lower B commit");
    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "cancel upper A second commit");
    binding = binding_for(&f, f.upper_surface);
    surface_sequence = binding != NULL ? binding->state.commit_sequence : 0u;
    CHECK(binding != NULL && surface_sequence != f.core.commit_sequence,
          "cancel setup did not separate surface-local/global sequence");
    CHECK(qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "cancel baseline start");

    reservation_index = UINT32_MAX;
    event_index = UINT32_MAX;
    active_matches = 0u;
    for (i = 0u; i < f.timeline.capacity; ++i) {
        if (f.timeline.slots[i].active &&
            f.timeline.slots[i].surface == f.upper_surface &&
            f.timeline.slots[i].desc.token == UINT64_C(77))
            active_matches++;
    }
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state == MOO_COMP_SLOT_RESERVED &&
            f.reservations[i].surface == f.upper_surface &&
            f.reservations[i].token == UINT64_C(77)) {
            reservation_index = i;
            event_index = f.reservations[i].event_index;
        }
    }
    CHECK(binding != NULL && f.timeline.active_count == 1u &&
          active_matches == 1u &&
          binding->reserved_completion_events == 1u &&
          reservation_index != UINT32_MAX && event_index < QA_EVENT_CAP &&
          f.events[event_index].slot_state == MOO_COMP_SLOT_RESERVED &&
          f.events[event_index].live == 0u,
          "cancel baseline graph/count mismatch");
    CHECK(moo_comp_next_event(&f.core, f.client_a, &event) ==
              MOO_COMP_WOULD_BLOCK,
          "cancel RESERVED event leaked through next_event");
    if (binding == NULL || reservation_index == UINT32_MAX ||
        event_index >= QA_EVENT_CAP)
        return;

    f.tx.completion_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "cancel completion-cap failure mutated authoritative state");
    f.tx.completion_capacity = QA_EVENT_CAP;

    f.animation.token = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "cancel token zero did not fail atomically");
    f.animation.token = UINT64_C(78);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "cancel wrong token did not fail atomically");
    f.animation.token = UINT64_C(77);
    f.request_owner = f.client_b;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_ACCESS && snapshot_equal(&f, &before),
          "cancel cross-owner did not fail atomically");
    f.request_owner = f.client_a;
    f.request_surface = MOO_COMP_HANDLE_INVALID;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "cancel invalid surface did not fail atomically");
    f.request_surface = f.upper_surface;

    event_sequence = f.core.event_sequence;
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_OK && f.completion_count == 1u &&
          f.tx_completions[0].surface == f.upper_surface &&
          f.tx_completions[0].token == UINT64_C(77) &&
          f.tx_completions[0].status == MOO_COMP_ANIMATION_DONE_CANCELLED &&
          f.tx_completions[0].timestamp_ns == UINT64_C(2000),
          "cancel did not publish exact CANCELLED completion");
    active_matches = 0u;
    for (i = 0u; i < f.timeline.capacity; ++i) {
        if (f.timeline.slots[i].active &&
            f.timeline.slots[i].surface == f.upper_surface &&
            f.timeline.slots[i].desc.token == UINT64_C(77))
            active_matches++;
    }
    CHECK(f.timeline.active_count == 0u && active_matches == 0u &&
          binding->reserved_completion_events == 0u &&
          f.reservations[reservation_index].slot_state == MOO_COMP_SLOT_FREE &&
          f.events[event_index].slot_state == MOO_COMP_SLOT_LIVE &&
          f.events[event_index].event.type == MOO_COMP_EVENT_ANIMATION_DONE &&
          f.events[event_index].event.token == UINT64_C(77) &&
          f.events[event_index].event.status ==
              MOO_COMP_ANIMATION_DONE_CANCELLED &&
          f.events[event_index].event.timestamp_ns == UINT64_C(2000) &&
          f.events[event_index].event.present_sequence == surface_sequence &&
          f.events[event_index].event.present_sequence ==
              binding->state.commit_sequence &&
          f.events[event_index].event.present_sequence !=
              f.core.commit_sequence &&
          f.core.event_sequence == event_sequence + 1u,
          "cancel terminal graph/count/local-sequence state diverged");
    CHECK(moo_comp_next_event(&f.core, f.client_a, &event) == MOO_COMP_OK &&
          event.object == f.upper_surface &&
          event.token == UINT64_C(77) &&
          event.status == MOO_COMP_ANIMATION_DONE_CANCELLED &&
          event.timestamp_ns == UINT64_C(2000) &&
          event.present_sequence == surface_sequence &&
          moo_comp_next_event(&f.core, f.client_a, &event) ==
              MOO_COMP_WOULD_BLOCK,
          "cancel terminal visibility was not exactly once");
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "duplicate cancel published a second terminal");

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "cancel corrupt-reservation fixture setup");
    reservation_index = UINT32_MAX;
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state == MOO_COMP_SLOT_RESERVED &&
            f.reservations[i].surface == f.upper_surface &&
            f.reservations[i].token == UINT64_C(77))
            reservation_index = i;
    }
    CHECK(reservation_index != UINT32_MAX,
          "cancel corrupt-reservation fixture missing reservation");
    if (reservation_index != UINT32_MAX) {
        f.reservations[reservation_index].event_index =
            f.core.event_capacity;
        snapshot_take(&f, &before);
        r = qa_i1_execute(&f, QA_CALL_CANCEL);
        CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
              "cancel corrupt reverse mapping did not fail atomically");
    }

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "cancel stale fixture setup");
    stale = f.upper_surface;
    CHECK(qa_i1_execute(&f, QA_CALL_DESTROY) == MOO_COMP_OK,
          "cancel stale fixture destroy");
    f.request_surface = stale;
    f.animation.token = UINT64_C(77);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_CANCEL);
    CHECK(r == MOO_COMP_STALE_HANDLE && snapshot_equal(&f, &before),
          "cancel stale surface did not fail atomically");
}

static void test_prepared_failures_no_publish(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompEffectSurfaceBinding *binding;
    MooCompResult r;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "prepared failure fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "RED: prepared bind unavailable");
    if (r != MOO_COMP_OK) return;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK,
          "prepared pending effect setup");
    binding = binding_for(&f, f.upper_surface);
    CHECK(binding != NULL && binding->effect_dirty == 1u &&
          binding->pending_effect.enabled_mask ==
              f.commit.effect.enabled_mask,
          "prepared pending state was not established");

    f.tx.buffer_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_COMMIT);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "buffer-preflight failure mutated authoritative state");
    f.tx.buffer_capacity = QA_BUFFER_CAP;

    f.commit.frame_token = UINT64_C(41);
    f.tx.frame_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_COMMIT);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "frame-preflight failure mutated authoritative state");
    f.tx.frame_capacity = QA_FRAME_CAP;

    f.tx.event_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_COMMIT);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "event-preflight failure mutated authoritative state");
    CHECK(binding->effect_dirty == 1u &&
          binding->pending_effect.enabled_mask ==
              f.commit.effect.enabled_mask,
          "commit failure cleared or changed pending effect");
}

static void test_build_frame_alias_poison(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    uint8_t *saved_bytes;
    uint32_t *saved_words;
    MooCompResult r;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "alias-poison fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "alias-poison bind unavailable");
    if (r != MOO_COMP_OK) return;

    saved_bytes = f.frame_workspace.lower_z_pixels;
    f.frame_workspace.lower_z_pixels = f.output_pixels;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "alias A lower-z/output did not fail atomically");
    f.frame_workspace.lower_z_pixels = saved_bytes;

    saved_bytes = f.frame_workspace.lower_z_pixels;
    f.frame_workspace.lower_z_pixels =
        (uint8_t *)(void *)f.frame_workspace.rgba_ping;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "alias B lower-z/ping did not fail atomically");
    f.frame_workspace.lower_z_pixels = saved_bytes;

    saved_bytes = f.frame_workspace.lower_z_pixels;
    f.frame_workspace.lower_z_pixels = f.upper_pixels;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "alias C lower-z/content did not fail atomically");
    f.frame_workspace.lower_z_pixels = saved_bytes;

    saved_words = f.frame_workspace.rgba_pong;
    f.frame_workspace.rgba_pong = f.frame_workspace.rgba_ping;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "alias D ping/pong did not fail atomically");
    f.frame_workspace.rgba_pong = saved_words;

    saved_words = f.frame_workspace.rgba_ping;
    f.frame_workspace.rgba_ping =
        (uint32_t *)(void *)f.output_pixels;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "alias E1 ping/output did not fail atomically");
    f.frame_workspace.rgba_ping = saved_words;

    saved_words = f.frame_workspace.rgba_pong;
    f.frame_workspace.rgba_pong =
        (uint32_t *)(void *)f.output_pixels;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "alias E2 pong/output did not fail atomically");
    f.frame_workspace.rgba_pong = saved_words;

    saved_bytes = f.frame_workspace.lower_z_pixels;
    f.frame_workspace.lower_z_pixels =
        (uint8_t *)(void *)f.frame_workspace.rgba_pong;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "alias F lower-z/pong did not fail atomically");
    f.frame_workspace.lower_z_pixels = saved_bytes;
}

static void test_build_frame_stale_preflight_poison(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompEffectSurfaceBinding *binding;
    MooCompEffectSurfaceBinding binding_before;
    MooCompCursorState cursor_before;
    MooCompHandle buffer_before;
    MooCompResult r;
    uint32_t surface_index;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "stale preflight fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "stale preflight bind unavailable");
    if (r != MOO_COMP_OK) return;

    binding = binding_for(&f, f.lower_surface);
    CHECK(binding != NULL, "inactive-surface poison setup unavailable");
    if (binding == NULL) return;
    surface_index = (uint32_t)(binding - f.bindings);
    CHECK(surface_index < f.core.surface_capacity,
          "inactive-surface binding index out of range");
    if (surface_index >= f.core.surface_capacity) return;

    binding_before = *binding;
    buffer_before = f.core.surfaces[surface_index].committed.buffer;
    binding->active = 0u;
    f.core.surfaces[surface_index].committed.buffer =
        buffer_before ^ (UINT64_C(1) << 32u);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID,
          "stale inactive-surface buffer returned wrong result");
    CHECK(snapshot_equal(&f, &before),
          "stale inactive-surface buffer mutated output/state");
    f.core.surfaces[surface_index].committed.buffer = buffer_before;
    *binding = binding_before;

    cursor_before = f.core.cursor;
    f.core.cursor.focus_owner = f.client_a;
    f.core.cursor.buffer =
        f.upper_buffer ^ (UINT64_C(1) << 32u);
    f.core.cursor.x = 1;
    f.core.cursor.y = 1;
    f.core.cursor.hotspot_x = 0;
    f.core.cursor.hotspot_y = 0;
    f.core.cursor.scale = 1u;
    f.core.cursor.visible = 1u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_BAD_STATE,
          "stale cursor buffer returned wrong result");
    CHECK(snapshot_equal(&f, &before),
          "stale cursor buffer mutated output/state");
    f.core.cursor = cursor_before;
}

static uint32_t qa_pack_rgba(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24u) | ((uint32_t)g << 16u) |
           ((uint32_t)b << 8u) | (uint32_t)a;
}

static void test_animation_property_frame_matrix(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompAnimationValue correct_from;
    MooCompEffectSurfaceBinding *binding;
    MooCompResult r;
    uint64_t output_before;
    uint32_t property;
    uint32_t i;

    for (property = MOO_COMP_ANIMATION_PROPERTY_OPACITY;
         property <= MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D;
         ++property) {
        CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
              "property fixture init");
        r = qa_i1_execute(&f, QA_CALL_BIND);
        CHECK(r == MOO_COMP_OK, "property bind unavailable");
        if (r != MOO_COMP_OK) continue;

        f.commit.effect = moo_comp_effect_state_neutral();
        switch (property) {
            case MOO_COMP_ANIMATION_PROPERTY_OPACITY:
                break;
            case MOO_COMP_ANIMATION_PROPERTY_CORNERS:
                f.commit.effect.enabled_mask = MOO_COMP_EFFECT_CORNER_CLIP;
                break;
            case MOO_COMP_ANIMATION_PROPERTY_SHADOW:
                f.commit.effect.enabled_mask = MOO_COMP_EFFECT_SHADOW;
                break;
            case MOO_COMP_ANIMATION_PROPERTY_BACKDROP:
                f.commit.effect.enabled_mask =
                    MOO_COMP_EFFECT_BACKDROP_BLUR |
                    MOO_COMP_EFFECT_SATURATION |
                    MOO_COMP_EFFECT_TINT |
                    MOO_COMP_EFFECT_NOISE;
                break;
            case MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D:
                f.commit.effect.enabled_mask = MOO_COMP_EFFECT_AFFINE_2D;
                break;
            default:
                break;
        }
        CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
              qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
              "property matching set+commit");

        memset(&f.animation.from, 0, sizeof(f.animation.from));
        memset(&f.animation.to, 0, sizeof(f.animation.to));
        f.animation.token = UINT64_C(100) + property;
        f.animation.property = property;
        switch (property) {
            case MOO_COMP_ANIMATION_PROPERTY_OPACITY:
                f.animation.from.word[0] = (uint32_t)MOO_COMP_Q16_ONE;
                f.animation.to.word[0] = UINT32_C(32768);
                break;
            case MOO_COMP_ANIMATION_PROPERTY_CORNERS:
                f.animation.to.word[0] = 4u;
                f.animation.to.word[1] = 6u;
                f.animation.to.word[2] = 8u;
                f.animation.to.word[3] = 10u;
                break;
            case MOO_COMP_ANIMATION_PROPERTY_SHADOW:
                f.animation.to.word[0] = (uint32_t)INT32_C(-4);
                f.animation.to.word[1] = 6u;
                f.animation.to.word[2] = 2u;
                f.animation.to.word[3] = 2u;
                f.animation.to.word[4] =
                    qa_pack_rgba(20u, 40u, 60u, 80u);
                break;
            case MOO_COMP_ANIMATION_PROPERTY_BACKDROP:
                f.animation.from.word[1] = MOO_COMP_SATURATION_ONE;
                f.animation.to.word[0] = 2u;
                f.animation.to.word[1] = 512u;
                f.animation.to.word[2] =
                    qa_pack_rgba(20u, 40u, 60u, 80u);
                f.animation.to.word[3] = 100u;
                f.animation.to.word[4] = 20u;
                f.animation.to.word[5] = UINT32_C(0x12345678);
                break;
            case MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D:
                f.animation.from.word[0] = (uint32_t)MOO_COMP_Q16_ONE;
                f.animation.from.word[3] = (uint32_t)MOO_COMP_Q16_ONE;
                f.animation.to.word[0] = UINT32_C(32768);
                f.animation.to.word[1] = (uint32_t)INT32_C(-32768);
                f.animation.to.word[2] = UINT32_C(32768);
                f.animation.to.word[3] = (uint32_t)MOO_COMP_Q16_ONE;
                f.animation.to.word[4] = (uint32_t)INT32_C(-65536);
                f.animation.to.word[5] = UINT32_C(131072);
                f.animation.to.word[6] = (uint32_t)INT32_C(-65536);
                f.animation.to.word[7] = (uint32_t)MOO_COMP_Q16_ONE;
                break;
            default:
                break;
        }

        correct_from = f.animation.from;
        f.animation.from.word[0] =
            correct_from.word[0] == 0u ? 1u : correct_from.word[0] - 1u;
        snapshot_take(&f, &before);
        r = qa_i1_execute(&f, QA_CALL_START);
        CHECK(r == MOO_COMP_INVALID,
              "property from-mismatch returned wrong result");
        CHECK(snapshot_equal(&f, &before),
              "property from-mismatch mutated authoritative state");
        f.animation.from = correct_from;

        output_before = hash_bytes(f.output_pixels, QA_PIXEL_BYTES);
        r = qa_i1_execute(&f, QA_CALL_START);
        CHECK(r == MOO_COMP_OK, "property exact-from start failed");
        if (r != MOO_COMP_OK) continue;
        f.frame_request.timestamp_ns =
            UINT64_C(1000) + f.animation.duration_ns / UINT64_C(2);
        r = qa_i1_execute(&f, QA_CALL_FRAME);
        binding = binding_for(&f, f.upper_surface);
        CHECK(r == MOO_COMP_OK && binding != NULL &&
              f.frame_result.completion_count == 0u,
              "property midframe failed");
        CHECK(f.frame_result.damage_count != 0u &&
              hash_bytes(f.output_pixels, QA_PIXEL_BYTES) != output_before,
              "property midframe lacked pixel/damage publication");
        if (r != MOO_COMP_OK || binding == NULL) continue;

        switch (property) {
            case MOO_COMP_ANIMATION_PROPERTY_OPACITY:
                CHECK(binding->evaluated_opacity == 191u,
                      "opacity midframe decode mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_CORNERS:
                CHECK(binding->evaluated.corners.top_left == 2u &&
                      binding->evaluated.corners.top_right == 3u &&
                      binding->evaluated.corners.bottom_right == 4u &&
                      binding->evaluated.corners.bottom_left == 5u,
                      "corners midframe decode mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_SHADOW:
                CHECK(binding->evaluated.shadow.offset_x == -2 &&
                      binding->evaluated.shadow.offset_y == 3 &&
                      binding->evaluated.shadow.blur_radius == 1u &&
                      binding->evaluated.shadow.spread_radius == 1u &&
                      binding->evaluated.shadow.color.r == 10u &&
                      binding->evaluated.shadow.color.g == 20u &&
                      binding->evaluated.shadow.color.b == 30u &&
                      binding->evaluated.shadow.color.a == 40u,
                      "shadow midframe signed/RGBA mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_BACKDROP:
                CHECK(binding->evaluated.backdrop.blur_radius == 1u &&
                      binding->evaluated.backdrop.saturation_q8_8 == 384u &&
                      binding->evaluated.backdrop.tint.r == 10u &&
                      binding->evaluated.backdrop.tint.g == 20u &&
                      binding->evaluated.backdrop.tint.b == 30u &&
                      binding->evaluated.backdrop.tint.a == 40u &&
                      binding->evaluated.backdrop.tint_mix == 50u &&
                      binding->evaluated.backdrop.noise == 10u &&
                      binding->evaluated.backdrop.noise_seed == 0u,
                      "backdrop midframe RGBA/noise mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D:
                CHECK(binding->evaluated.affine.m11 == INT32_C(49152) &&
                      binding->evaluated.affine.m12 == INT32_C(-16384) &&
                      binding->evaluated.affine.m21 == INT32_C(16384) &&
                      binding->evaluated.affine.m22 == MOO_COMP_Q16_ONE &&
                      binding->evaluated.affine.tx == INT32_C(-32769) &&
                      binding->evaluated.affine.ty == INT32_C(65537) &&
                      binding->evaluated.affine.origin_x == INT32_C(-32769) &&
                      binding->evaluated.affine.origin_y == INT32_C(32769),
                      "affine midframe signed decode mismatch");
                break;
            default:
                break;
        }

        CHECK(moo_comp_present_done(
                  &f.core, f.frame_result.frame_id,
                  UINT64_C(1), UINT64_C(2000)) == MOO_COMP_OK,
              "property midframe present_done failed");
        f.frame_request.timestamp_ns =
            UINT64_C(1000) + f.animation.duration_ns;
        r = qa_i1_execute(&f, QA_CALL_FRAME);
        binding = binding_for(&f, f.upper_surface);
        CHECK(r == MOO_COMP_OK && binding != NULL &&
              f.frame_result.completion_count == 1u &&
              f.frame_result.damage_count != 0u,
              "property endpoint/completion failed");
        if (r != MOO_COMP_OK || binding == NULL) continue;

        switch (property) {
            case MOO_COMP_ANIMATION_PROPERTY_OPACITY:
                CHECK(binding->evaluated_opacity == 128u,
                      "opacity endpoint mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_CORNERS:
                CHECK(binding->evaluated.corners.top_left == 4u &&
                      binding->evaluated.corners.top_right == 6u &&
                      binding->evaluated.corners.bottom_right == 8u &&
                      binding->evaluated.corners.bottom_left == 10u,
                      "corners endpoint mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_SHADOW:
                CHECK(binding->evaluated.shadow.offset_x == -4 &&
                      binding->evaluated.shadow.offset_y == 6 &&
                      binding->evaluated.shadow.color.r == 20u &&
                      binding->evaluated.shadow.color.g == 40u &&
                      binding->evaluated.shadow.color.b == 60u &&
                      binding->evaluated.shadow.color.a == 80u,
                      "shadow endpoint mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_BACKDROP:
                CHECK(binding->evaluated.backdrop.blur_radius == 2u &&
                      binding->evaluated.backdrop.saturation_q8_8 == 512u &&
                      binding->evaluated.backdrop.tint.r == 20u &&
                      binding->evaluated.backdrop.tint.g == 40u &&
                      binding->evaluated.backdrop.tint.b == 60u &&
                      binding->evaluated.backdrop.tint.a == 80u &&
                      binding->evaluated.backdrop.tint_mix == 100u &&
                      binding->evaluated.backdrop.noise == 20u &&
                      binding->evaluated.backdrop.noise_seed ==
                          UINT32_C(0x12345678),
                      "backdrop endpoint mismatch");
                break;
            case MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D:
                CHECK(binding->evaluated.affine.m11 == INT32_C(32768) &&
                      binding->evaluated.affine.m12 == INT32_C(-32768) &&
                      binding->evaluated.affine.m21 == INT32_C(32768) &&
                      binding->evaluated.affine.m22 == MOO_COMP_Q16_ONE &&
                      binding->evaluated.affine.tx == INT32_C(-65536) &&
                      binding->evaluated.affine.ty == INT32_C(131072) &&
                      binding->evaluated.affine.origin_x == INT32_C(-65536) &&
                      binding->evaluated.affine.origin_y == MOO_COMP_Q16_ONE,
                      "affine endpoint mismatch");
                break;
            default:
                break;
        }
    }

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK,
          "unknown-property fixture init");
    f.animation.property = MOO_COMP_ANIMATION_PROPERTY_OPACITY;
    f.animation.from.word[0] = (uint32_t)MOO_COMP_Q16_ONE;
    f.animation.to.word[0] = 0u;
    for (i = 1u; i < MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS; ++i) {
        f.animation.from.word[i] = 0u;
        f.animation.to.word[i] = 0u;
    }
    CHECK(qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "unknown-property poison start");
    for (i = 0u; i < f.timeline.capacity; ++i)
        if (f.timeline.slots[i].active)
            f.timeline.slots[i].desc.property = UINT32_C(99);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_INVALID,
          "unknown sample property returned wrong result");
    CHECK(snapshot_equal(&f, &before),
          "unknown sample property mutated authoritative state");
}

static void test_clone_damage_gpu_and_full_target(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    uint64_t poison_hash;
    uint64_t output_hash;
    MooCompResult r;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "frame fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "RED: frame bind unavailable");
    if (r != MOO_COMP_OK) return;
    f.commit.effect.enabled_mask =
        MOO_COMP_EFFECT_BACKDROP_BLUR | MOO_COMP_EFFECT_TINT;
    f.commit.effect.backdrop.blur_radius = 1u;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "frame matching effect set+commit");

    f.frame_workspace.sample_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "post-clone sample failure mutated A1/S1/Core/output");
    f.frame_workspace.sample_capacity = 8u;

    poison_hash = hash_bytes(f.output_pixels, QA_PIXEL_BYTES);
    CHECK(moo_comp_effects_gpu_backend_mark_lost(&f.gpu) == MOO_COMP_OK,
          "GPU LOST setup");
    r = qa_i1_execute(&f, QA_CALL_FRAME);
    output_hash = hash_bytes(f.output_pixels, QA_PIXEL_BYTES);
    CHECK(r == MOO_COMP_OK && f.frame_result.used_gpu == 0u,
          "GPU LOST did not choose pre-reserved CPU fallback");
    CHECK(f.frame_result.work_units >= UINT64_C(80) &&
          f.frame_result.damage_count != 0u,
          "frame did not use full-target work and D1 damage");
    CHECK(output_hash != poison_hash &&
          hash_bytes(f.prefix_pixels, QA_PIXEL_BYTES) != output_hash,
          "lower-Z prefix ordering/persistent-output poison escaped");
}

static void test_destroy_ex_animation_lifecycle(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompEffectSurfaceBinding *binding;
    MooCompEvent event;
    MooCompSurfaceInfo surface_info;
    MooCompHandle stale;
    MooCompResult r;
    uint32_t reservation_77;
    uint32_t reservation_78;
    uint32_t event_77;
    uint32_t event_78;
    uint32_t i;
    uint32_t tint;
    uint64_t event_sequence;
    uint64_t surface_sequence;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "destroy multi-animation fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "RED: destroy lifecycle bind unavailable");
    if (r != MOO_COMP_OK) return;

    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "destroy upper A commit");
    f.request_surface = f.lower_surface;
    f.commit.buffer = f.lower_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "destroy lower B commit");
    f.request_surface = f.upper_surface;
    f.commit.buffer = f.upper_buffer;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "destroy upper A second commit");
    binding = binding_for(&f, f.upper_surface);
    surface_sequence = binding != NULL ? binding->state.commit_sequence : 0u;
    CHECK(binding != NULL && surface_sequence != f.core.commit_sequence,
          "destroy setup did not separate surface-local/global sequence");

    CHECK(qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "destroy opacity animation start");
    memset(&f.animation.from, 0, sizeof(f.animation.from));
    memset(&f.animation.to, 0, sizeof(f.animation.to));
    tint = ((uint32_t)binding->state.effective.backdrop.tint.r << 24u) |
           ((uint32_t)binding->state.effective.backdrop.tint.g << 16u) |
           ((uint32_t)binding->state.effective.backdrop.tint.b << 8u) |
           (uint32_t)binding->state.effective.backdrop.tint.a;
    f.animation.token = UINT64_C(78);
    f.animation.property = MOO_COMP_ANIMATION_PROPERTY_BACKDROP;
    f.animation.from.word[0] = binding->state.effective.backdrop.blur_radius;
    f.animation.from.word[1] = binding->state.effective.backdrop.saturation_q8_8;
    f.animation.from.word[2] = tint;
    f.animation.from.word[3] = binding->state.effective.backdrop.tint_mix;
    f.animation.from.word[4] = binding->state.effective.backdrop.noise;
    f.animation.from.word[5] = binding->state.effective.backdrop.noise_seed;
    f.animation.to = f.animation.from;
    f.animation.to.word[3] =
        f.animation.from.word[3] == 255u ? 254u : 255u;
    CHECK(qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "destroy backdrop animation start");

    reservation_77 = UINT32_MAX;
    reservation_78 = UINT32_MAX;
    event_77 = UINT32_MAX;
    event_78 = UINT32_MAX;
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state != MOO_COMP_SLOT_RESERVED ||
            f.reservations[i].surface != f.upper_surface)
            continue;
        if (f.reservations[i].token == UINT64_C(77)) {
            reservation_77 = i;
            event_77 = f.reservations[i].event_index;
        } else if (f.reservations[i].token == UINT64_C(78)) {
            reservation_78 = i;
            event_78 = f.reservations[i].event_index;
        }
    }
    CHECK(binding != NULL && f.timeline.active_count == 2u &&
          binding->reserved_completion_events == 2u &&
          reservation_77 != UINT32_MAX &&
          reservation_78 != UINT32_MAX &&
          event_77 < QA_EVENT_CAP && event_78 < QA_EVENT_CAP,
          "destroy baseline two-animation graph/count mismatch");
    if (binding == NULL || reservation_77 == UINT32_MAX ||
        reservation_78 == UINT32_MAX || event_77 >= QA_EVENT_CAP ||
        event_78 >= QA_EVENT_CAP)
        return;

    f.request_owner = f.client_b;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_ACCESS && snapshot_equal(&f, &before),
          "destroy cross-owner did not fail atomically");
    f.request_owner = f.client_a;
    f.request_surface = MOO_COMP_HANDLE_INVALID;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_INVALID && snapshot_equal(&f, &before),
          "destroy invalid surface did not fail atomically");
    f.request_surface = f.upper_surface;

    f.tx.completion_capacity = 1u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "destroy multi-terminal cap-1 mutated authoritative state");
    f.tx.completion_capacity = QA_EVENT_CAP;

    f.reservations[reservation_78].token ^= UINT64_C(1);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "destroy corrupt reservation mapping did not fail atomically");
    f.reservations[reservation_78].token ^= UINT64_C(1);

    stale = f.upper_surface;
    event_sequence = f.core.event_sequence;
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_OK && f.completion_count == 2u &&
          f.tx_completions[0].surface == stale &&
          f.tx_completions[0].token == UINT64_C(77) &&
          f.tx_completions[0].status ==
              MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          f.tx_completions[0].timestamp_ns == UINT64_C(2000) &&
          f.tx_completions[1].surface == stale &&
          f.tx_completions[1].token == UINT64_C(78) &&
          f.tx_completions[1].status ==
              MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          f.tx_completions[1].timestamp_ns == UINT64_C(2000),
          "destroy workspace completions/order/payload diverged");
    CHECK(f.timeline.active_count == 0u &&
          binding->active == 0u &&
          binding->reserved_completion_events == 0u &&
          f.reservations[reservation_77].slot_state == MOO_COMP_SLOT_FREE &&
          f.reservations[reservation_78].slot_state == MOO_COMP_SLOT_FREE &&
          f.events[event_77].slot_state == MOO_COMP_SLOT_LIVE &&
          f.events[event_78].slot_state == MOO_COMP_SLOT_LIVE &&
          f.events[event_77].event.object == stale &&
          f.events[event_78].event.object == stale &&
          f.events[event_77].event.status ==
              MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          f.events[event_78].event.status ==
              MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          f.events[event_77].event.present_sequence == surface_sequence &&
          f.events[event_78].event.present_sequence == surface_sequence &&
          f.events[event_77].event.present_sequence !=
              f.core.commit_sequence &&
          f.core.event_sequence == event_sequence + 2u,
          "destroy graph/count/queued-local-sequence state diverged");
    CHECK(moo_comp_next_event(&f.core, f.client_a, &event) == MOO_COMP_OK &&
          event.object == stale && event.token == UINT64_C(77) &&
          event.status == MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          event.timestamp_ns == UINT64_C(2000) &&
          event.present_sequence == surface_sequence &&
          moo_comp_next_event(&f.core, f.client_a, &event) == MOO_COMP_OK &&
          event.object == stale && event.token == UINT64_C(78) &&
          event.status == MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          event.timestamp_ns == UINT64_C(2000) &&
          event.present_sequence == surface_sequence &&
          moo_comp_next_event(&f.core, f.client_a, &event) ==
              MOO_COMP_WOULD_BLOCK,
          "destroy queued terminals were not once-visible in slot order");
    CHECK(moo_comp_surface_info(&f.core, stale, &surface_info) ==
              MOO_COMP_STALE_HANDLE &&
          moo_comp_surface_info(&f.core, f.lower_surface, &surface_info) ==
              MOO_COMP_OK,
          "destroy invalidated client or unrelated client-owned surface");

    f.request_surface = stale;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_STALE_HANDLE && snapshot_equal(&f, &before),
          "repeated stale destroy redelivered terminals");
}

static void test_disconnect_sparse_foreign_isolation(void) {
    QaFixture f;
    MooCompBufferView view;
    MooCompEffectSurfaceBinding *binding_b;
    MooCompEffectSurfaceBinding binding_b_before;
    MooCompEffectCompletionReservation reservation_b_before;
    MooCompEventSlot event_b_before;
    MooCompAnimationSlot timeline_b_before;
    MooCompEvent event;
    MooCompHandle buffer_b;
    MooCompHandle surface_b;
    MooCompResult r;
    uint32_t reservation_a;
    uint32_t reservation_b;
    uint32_t event_a;
    uint32_t event_b;
    uint32_t target_reservation;
    uint32_t target_event;
    uint32_t timeline_b;
    uint32_t i;
    uint64_t event_sequence;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "disconnect sparse fixture init");
    view = rgba_view(f.upper_pixels);
    CHECK(moo_comp_buffer_register(
              &f.core, f.client_b, &view, &buffer_b) == MOO_COMP_OK &&
          moo_comp_surface_create(
              &f.core, f.client_b, &surface_b) == MOO_COMP_OK &&
          moo_comp_surface_attach(
              &f.core, f.client_b, surface_b, buffer_b) == MOO_COMP_OK &&
          moo_comp_surface_damage(
              &f.core, f.client_b, surface_b,
              (MooCompRect){0, 0, 4, 4}) == MOO_COMP_OK &&
          moo_comp_surface_commit(
              &f.core, f.client_b, surface_b) == MOO_COMP_OK,
          "disconnect sparse client-B Core setup");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "RED: disconnect sparse bind unavailable");
    if (r != MOO_COMP_OK) return;

    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "disconnect sparse client-A animation setup");
    f.request_owner = f.client_b;
    f.request_surface = surface_b;
    f.commit.buffer = buffer_b;
    f.animation.token = UINT64_C(88);
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "disconnect sparse client-B animation setup");

    reservation_a = UINT32_MAX;
    reservation_b = UINT32_MAX;
    event_a = UINT32_MAX;
    event_b = UINT32_MAX;
    target_reservation = UINT32_MAX;
    target_event = UINT32_MAX;
    timeline_b = UINT32_MAX;
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state == MOO_COMP_SLOT_FREE)
            target_reservation = i;
        if (f.events[i].slot_state == MOO_COMP_SLOT_FREE)
            target_event = i;
        if (f.reservations[i].slot_state != MOO_COMP_SLOT_RESERVED)
            continue;
        if (f.reservations[i].owner == f.client_a &&
            f.reservations[i].token == UINT64_C(77)) {
            reservation_a = i;
            event_a = f.reservations[i].event_index;
        } else if (f.reservations[i].owner == f.client_b &&
                   f.reservations[i].surface == surface_b &&
                   f.reservations[i].token == UINT64_C(88)) {
            reservation_b = i;
            event_b = f.reservations[i].event_index;
        }
    }
    for (i = 0u; i < f.timeline.capacity; ++i) {
        if (f.timeline.slots[i].active &&
            f.timeline.slots[i].surface == surface_b &&
            f.timeline.slots[i].desc.token == UINT64_C(88))
            timeline_b = i;
    }
    CHECK(reservation_a != UINT32_MAX && reservation_b != UINT32_MAX &&
          event_a < QA_EVENT_CAP && event_b < QA_EVENT_CAP &&
          target_reservation != UINT32_MAX && target_event != UINT32_MAX &&
          timeline_b < f.timeline.capacity,
          "disconnect sparse fixture graph discovery failed");
    if (reservation_a == UINT32_MAX || reservation_b == UINT32_MAX ||
        event_a >= QA_EVENT_CAP || event_b >= QA_EVENT_CAP ||
        target_reservation == UINT32_MAX || target_event == UINT32_MAX ||
        timeline_b >= f.timeline.capacity)
        return;

    f.reservations[target_reservation] = f.reservations[reservation_b];
    memset(&f.reservations[reservation_b], 0,
           sizeof(f.reservations[reservation_b]));
    f.reservations[reservation_b].event_index = UINT32_MAX;
    f.events[target_event] = f.events[event_b];
    memset(&f.events[event_b], 0, sizeof(f.events[event_b]));
    f.reservations[target_reservation].event_index = target_event;
    f.events[target_event].reserved_slot = target_reservation + 1u;
    reservation_b = target_reservation;
    event_b = target_event;

    binding_b = binding_for(&f, surface_b);
    CHECK(binding_b != NULL &&
          binding_b->reserved_completion_events == 1u &&
          f.timeline.active_count == 2u,
          "disconnect sparse fixture Binding/timeline counts invalid");
    if (binding_b == NULL) return;
    binding_b_before = *binding_b;
    reservation_b_before = f.reservations[reservation_b];
    event_b_before = f.events[event_b];
    timeline_b_before = f.timeline.slots[timeline_b];

    f.request_owner = f.client_a;
    event_sequence = f.core.event_sequence;
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_OK && f.completion_count == 1u &&
          f.tx_completions[0].surface == f.upper_surface &&
          f.tx_completions[0].token == UINT64_C(77) &&
          f.tx_completions[0].status ==
              MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          f.tx_completions[0].timestamp_ns == UINT64_C(2000),
          "disconnect sparse A completion payload diverged");
    CHECK(f.reservations[reservation_a].slot_state == MOO_COMP_SLOT_FREE &&
          f.events[event_a].slot_state == MOO_COMP_SLOT_FREE &&
          f.events[event_a].live == 0u &&
          f.core.event_sequence == event_sequence,
          "disconnect sparse A did not release workspace-only exact slot");
    CHECK(memcmp(&f.reservations[reservation_b], &reservation_b_before,
                 sizeof(reservation_b_before)) == 0 &&
          memcmp(&f.events[event_b], &event_b_before,
                 sizeof(event_b_before)) == 0 &&
          memcmp(&f.timeline.slots[timeline_b], &timeline_b_before,
                 sizeof(timeline_b_before)) == 0 &&
          memcmp(binding_b, &binding_b_before,
                 sizeof(binding_b_before)) == 0 &&
          f.timeline.active_count == 1u,
          "disconnect A mutated sparse interleaved client-B graph");
    CHECK(moo_comp_next_event(&f.core, f.client_a, &event) ==
              MOO_COMP_STALE_HANDLE &&
          moo_comp_next_event(&f.core, f.client_b, &event) ==
              MOO_COMP_WOULD_BLOCK,
          "disconnect sparse client invalidation/isolation diverged");
}

static void test_stale_cross_destroy_disconnect(void) {
    QaFixture f;
    QaAuthoritativeSnapshot before;
    MooCompHandle stale;
    MooCompResult r;
    MooCompEvent event;
    MooCompSurfaceInfo surface_info;
    MooCompBufferInfo buffer_info;
    MooCompEffectSurfaceBinding *binding;
    MooCompEffectCompletionReservation orphan_reservation_before;
    MooCompEventSlot orphan_event_before;
    uint32_t reservation_index;
    uint32_t event_index;
    uint32_t orphan_reservation_index;
    uint32_t orphan_event_index;
    uint32_t live_animation_done;
    uint32_t i;
    uint64_t event_sequence_before;

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "lifetime fixture init");
    r = qa_i1_execute(&f, QA_CALL_BIND);
    CHECK(r == MOO_COMP_OK, "RED: lifetime bind unavailable");
    if (r != MOO_COMP_OK) return;
    CHECK(qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK,
          "lifetime matching set+commit");

    f.request_owner = f.client_b;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_SET);
    CHECK(r == MOO_COMP_ACCESS && snapshot_equal(&f, &before),
          "cross-owner effect mutation did not fail closed");
    f.request_owner = f.client_a;

    CHECK(qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "destroy animation start");
    f.tx.completion_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "destroy completion cap-1 mutated state");
    f.tx.completion_capacity = QA_EVENT_CAP;
    stale = f.upper_surface;
    r = qa_i1_execute(&f, QA_CALL_DESTROY);
    CHECK(r == MOO_COMP_OK && f.completion_count == 1u,
          "destroy_ex did not terminate exactly once");
    f.request_surface = stale;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_SET);
    CHECK(r == MOO_COMP_STALE_HANDLE && snapshot_equal(&f, &before),
          "stale effect handle did not fail closed");

    CHECK(fixture_init(&f, QA_EVENT_CAP) == MOO_COMP_OK,
          "disconnect fixture init");
    CHECK(qa_i1_execute(&f, QA_CALL_BIND) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_SET) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_COMMIT) == MOO_COMP_OK &&
          qa_i1_execute(&f, QA_CALL_START) == MOO_COMP_OK,
          "disconnect active-animation setup");
    reservation_index = UINT32_MAX;
    event_index = UINT32_MAX;
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state == MOO_COMP_SLOT_RESERVED &&
            f.reservations[i].owner == f.client_a &&
            f.reservations[i].surface == f.upper_surface &&
            f.reservations[i].token == f.animation.token) {
            reservation_index = i;
            event_index = f.reservations[i].event_index;
        }
    }
    CHECK(reservation_index != UINT32_MAX && event_index < QA_EVENT_CAP,
          "disconnect setup lacks exact physical reservation mapping");
    if (reservation_index == UINT32_MAX || event_index >= QA_EVENT_CAP)
        return;

    f.tx.completion_capacity = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_LIMIT && snapshot_equal(&f, &before),
          "disconnect completion cap-1 mutated state");
    f.tx.completion_capacity = QA_EVENT_CAP;

    f.reservations[reservation_index].token ^= UINT64_C(1);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "disconnect corrupt mapping did not fail byte-identically");
    f.reservations[reservation_index].token = f.animation.token;

    f.reservations[reservation_index].event_index =
        f.core.event_capacity;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "disconnect OOB event mapping did not fail byte-identically");
    f.reservations[reservation_index].event_index = event_index;

    f.events[event_index].reserved_slot = 0u;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "disconnect reverse mapping did not fail byte-identically");
    f.events[event_index].reserved_slot = reservation_index + 1u;

    orphan_reservation_index = UINT32_MAX;
    orphan_event_index = UINT32_MAX;
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.reservations[i].slot_state == MOO_COMP_SLOT_FREE)
            orphan_reservation_index = i;
        if (f.events[i].slot_state == MOO_COMP_SLOT_FREE)
            orphan_event_index = i;
    }
    binding = binding_for(&f, f.upper_surface);
    CHECK(orphan_reservation_index != UINT32_MAX &&
          orphan_event_index != UINT32_MAX && binding != NULL,
          "disconnect orphan reverse-coverage setup unavailable");
    if (orphan_reservation_index == UINT32_MAX ||
        orphan_event_index == UINT32_MAX || binding == NULL)
        return;
    orphan_reservation_before = f.reservations[orphan_reservation_index];
    orphan_event_before = f.events[orphan_event_index];
    f.reservations[orphan_reservation_index] =
        f.reservations[reservation_index];
    f.reservations[orphan_reservation_index].event_index =
        orphan_event_index;
    f.events[orphan_event_index] = f.events[event_index];
    f.events[orphan_event_index].reserved_slot =
        orphan_reservation_index + 1u;
    binding->reserved_completion_events++;
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "disconnect duplicate mapping did not fail byte-identically");

    f.reservations[orphan_reservation_index].token =
        f.animation.token + UINT64_C(1);
    snapshot_take(&f, &before);
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_BAD_STATE && snapshot_equal(&f, &before),
          "disconnect orphan reservation did not fail byte-identically");
    binding->reserved_completion_events--;
    f.reservations[orphan_reservation_index] = orphan_reservation_before;
    f.events[orphan_event_index] = orphan_event_before;

    event_sequence_before = f.core.event_sequence;
    r = qa_i1_execute(&f, QA_CALL_DISCONNECT);
    CHECK(r == MOO_COMP_OK && f.completion_count == 1u,
          "disconnect_ex did not terminate active animation exactly once");
    CHECK(f.tx_completions[0].surface == f.upper_surface &&
          f.tx_completions[0].token == f.animation.token &&
          f.tx_completions[0].status ==
              MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED &&
          f.tx_completions[0].timestamp_ns == UINT64_C(2000),
          "disconnect workspace completion payload is not exact");
    CHECK(f.reservations[reservation_index].slot_state ==
              MOO_COMP_SLOT_FREE &&
          f.events[event_index].slot_state == MOO_COMP_SLOT_FREE &&
          f.events[event_index].live == 0u,
          "disconnect did not free exact reservation and Core event slot");
    CHECK(f.core.event_sequence == event_sequence_before,
          "workspace-only disconnect incorrectly advanced event sequence");
    live_animation_done = 0u;
    for (i = 0u; i < QA_EVENT_CAP; ++i) {
        if (f.events[i].slot_state == MOO_COMP_SLOT_LIVE &&
            f.events[i].event.type == MOO_COMP_EVENT_ANIMATION_DONE)
            live_animation_done++;
    }
    CHECK(live_animation_done == 0u,
          "disconnect left queued ANIMATION_DONE after client invalidation");
    CHECK(moo_comp_next_event(&f.core, f.client_a, &event) ==
              MOO_COMP_STALE_HANDLE &&
          moo_comp_surface_info(&f.core, f.upper_surface, &surface_info) ==
              MOO_COMP_STALE_HANDLE &&
          moo_comp_buffer_info(&f.core, f.upper_buffer, &buffer_info) ==
              MOO_COMP_STALE_HANDLE,
          "disconnect did not invalidate client-owned resources");
}

int main(void) {
    test_bind_rejects_nonempty_candidate_graph();
    test_bind_set_commit_and_v1_guards();
    test_alternating_surface_sequences();
    test_scale2_requirements_and_pixels();
    test_animation_start_retarget_atomicity();
    test_animation_reservation_lifecycle();
    test_prepared_failures_no_publish();
    test_build_frame_alias_poison();
    test_build_frame_stale_preflight_poison();
    test_animation_property_frame_matrix();
    test_clone_damage_gpu_and_full_target();
    test_destroy_ex_animation_lifecycle();
    test_disconnect_sparse_foreign_isolation();
    test_stale_cross_destroy_disconnect();

    if (failures != 0) {
        fprintf(stderr, "P016-O5 I1 INTEGRATION RED: %d/%d failed\n",
                failures, checks);
        return 1;
    }
    printf("P016-O5 I1 INTEGRATION GREEN: %d checks\n", checks);
    printf("P016-O5 I1 INTEGRATION PASS\n");
    return 0;
}
