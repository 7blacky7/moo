#ifndef MOO_COMPOSITOR_CORE_H
#define MOO_COMPOSITOR_CORE_H

#include "moo_compositor_protocol.h"
#include "moo_compositor_animation.h"
#include "moo_compositor_effects_cpu.h"
#include "moo_compositor_effects_damage.h"
#include "moo_compositor_effects_gpu.h"
#include "moo_compositor_effects_state.h"

#include <stddef.h>
#include <stdint.h>

#define MOO_COMP_SURFACE_DAMAGE_CAPACITY 16u
#define MOO_COMP_OUTPUT_DAMAGE_CAPACITY 32u
#define MOO_COMP_EFFECT_INTEGRATION_VERSION UINT32_C(1)

typedef enum {
    MOO_COMP_SLOT_FREE = 0,
    MOO_COMP_SLOT_RESERVED = 1,
    MOO_COMP_SLOT_LIVE = 2
} MooCompSlotState;

typedef struct {
    const uint8_t *pixels;
    size_t buffer_bytes;
    size_t stride;
    int32_t width;
    int32_t height;
    uint32_t format;
} MooCompBufferView;

typedef struct {
    uint32_t live;
    uint32_t generation;
} MooCompClientSlot;

typedef struct {
    uint32_t live;
    uint32_t generation;
    MooCompHandle owner;
    MooCompBufferView view;
    uint32_t ref_count;
    uint32_t release_armed;
} MooCompBufferSlot;

typedef struct {
    MooCompHandle buffer;
    uint32_t scale;
    uint32_t opacity;
    uint32_t effective_opaque;
    uint64_t commit_sequence;
} MooCompSurfaceState;

typedef struct {
    uint32_t dirty_mask;
    MooCompHandle buffer;
    uint32_t scale;
    uint32_t opacity;
    uint64_t frame_token;
    MooCompRect damage[MOO_COMP_SURFACE_DAMAGE_CAPACITY];
    uint32_t damage_count;
} MooCompSurfacePending;

typedef struct {
    uint32_t live;
    uint32_t generation;
    MooCompHandle owner;
    int32_t x;
    int32_t y;
    uint64_t z_sequence;
    MooCompSurfaceState committed;
    MooCompSurfacePending pending;
    uint32_t effect_bound;
    uint32_t reserved_effect;
} MooCompSurfaceSlot;

typedef struct {
    uint32_t slot_state;
    uint32_t live;
    uint32_t state;
    uint32_t reserved_slot;
    MooCompHandle owner;
    MooCompHandle surface;
    uint64_t token;
    uint64_t commit_sequence;
    uint64_t frame_id;
    uint32_t completion_status;
    uint32_t reserved;
} MooCompFrameSlot;

typedef struct {
    uint32_t slot_state;
    uint32_t live;
    uint32_t reserved;
    uint32_t reserved_slot;
    MooCompHandle owner;
    uint64_t sequence;
    MooCompEvent event;
} MooCompEventSlot;

typedef struct {
    MooCompHandle focus_owner;
    MooCompHandle buffer;
    int32_t x;
    int32_t y;
    int32_t hotspot_x;
    int32_t hotspot_y;
    uint32_t scale;
    uint32_t visible;
} MooCompCursorState;

typedef struct {
    int32_t output_width;
    int32_t output_height;
    uint8_t background_r;
    uint8_t background_g;
    uint8_t background_b;
    uint8_t background_a;
} MooCompConfig;

typedef struct {
    uint8_t *pixels;
    size_t buffer_bytes;
    size_t stride;
    int32_t width;
    int32_t height;
} MooCompOutput;

struct MooCompEffectIntegration;

typedef void (*MooCompPresentDoneObserverFn)(
    void *user, uint64_t generation, uint64_t frame_id,
    uint64_t present_sequence, uint64_t timestamp_ns);

typedef struct {
    MooCompClientSlot *clients;
    uint32_t client_capacity;
    MooCompSurfaceSlot *surfaces;
    uint32_t surface_capacity;
    MooCompBufferSlot *buffers;
    uint32_t buffer_capacity;
    MooCompFrameSlot *frames;
    uint32_t frame_capacity;
    MooCompEventSlot *events;
    uint32_t event_capacity;

    MooCompConfig config;
    MooCompCursorState cursor;
    MooCompRect output_damage[MOO_COMP_OUTPUT_DAMAGE_CAPACITY];
    uint32_t output_damage_count;

    uint64_t z_sequence;
    uint64_t commit_sequence;
    uint64_t frame_sequence;
    uint64_t event_sequence;
    uint64_t in_flight_frame;
    struct MooCompEffectIntegration *effect_integration;
    MooCompPresentDoneObserverFn present_done_observer;
    void *present_done_observer_user;
    uint64_t present_done_generation;
} MooCompositor;

typedef struct {
    MooCompHandle owner;
    MooCompHandle buffer;
    int32_t x;
    int32_t y;
    int32_t logical_width;
    int32_t logical_height;
    uint32_t scale;
    uint32_t opacity;
    uint64_t z_sequence;
    uint64_t commit_sequence;
    uint32_t mapped;
} MooCompSurfaceInfo;

typedef struct {
    MooCompHandle owner;
    MooCompBufferView view;
    uint32_t ref_count;
} MooCompBufferInfo;

typedef struct {
    uint32_t slot_state;
    uint32_t event_index;
    MooCompHandle owner;
    MooCompHandle surface;
    uint64_t token;
} MooCompEffectCompletionReservation;

typedef struct {
    uint32_t active;
    uint32_t effect_dirty;
    MooCompHandle surface;
    MooCompHandle owner;
    uint32_t reserved_completion_events;
    uint32_t evaluated_opacity;
    MooCompEffectSurfaceState state;
    MooCompEffectState pending_effect;
    MooCompEffectState evaluated;
    MooCompEffectBounds previous_bounds;
    MooCompGpuResources gpu_resources;
} MooCompEffectSurfaceBinding;

typedef struct {
    uint32_t valid;
    uint32_t surface_index;
    uint32_t old_buffer_index;
    uint32_t new_buffer_index;
    uint32_t frame_index;
    uint32_t event_index;
    uint32_t output_damage_count_before;
    uint32_t output_damage_count_after;
    MooCompSurfaceSlot surface_before;
    MooCompSurfaceSlot surface_after;
    MooCompBufferSlot old_buffer_before;
    MooCompBufferSlot old_buffer_after;
    MooCompBufferSlot new_buffer_before;
    MooCompBufferSlot new_buffer_after;
    MooCompFrameSlot frame_before;
    MooCompFrameSlot frame_after;
    MooCompEventSlot event_before;
    MooCompEventSlot event_after;
    MooCompRect output_damage_before[MOO_COMP_OUTPUT_DAMAGE_CAPACITY];
    MooCompRect output_damage_after[MOO_COMP_OUTPUT_DAMAGE_CAPACITY];
    uint64_t commit_sequence_before;
    uint64_t commit_sequence_after;
    uint64_t z_sequence_before;
    uint64_t z_sequence_after;
    uint64_t event_sequence_before;
    uint64_t event_sequence_after;
} MooCompEffectPreparedCore;

typedef struct MooCompEffectTransactionWorkspace {
    MooCompEffectPreparedCore *prepared;
    MooCompSurfaceSlot *surface_slots;
    uint32_t surface_capacity;
    MooCompBufferSlot *buffer_slots;
    uint32_t buffer_capacity;
    MooCompFrameSlot *frame_slots;
    uint32_t frame_capacity;
    MooCompEventSlot *event_slots;
    uint32_t event_capacity;
    MooCompAnimationSlot *timeline_slots;
    uint32_t timeline_capacity;
    MooCompAnimationCompletion *completions;
    uint32_t completion_capacity;
    MooCompEffectCompletionReservation *reservation_clones;
    uint32_t reservation_capacity;
} MooCompEffectTransactionWorkspace;

typedef struct MooCompEffectIntegration {
    uint32_t version;
    uint32_t reserved;
    MooCompositor *core;
    MooCompEffectStateConfig config;
    MooCompEffectSurfaceBinding *bindings;
    uint32_t binding_capacity;
    uint32_t reserved2;
    MooCompAnimationTimeline *timeline;
    MooCompGpuBackendState *gpu_backend;
    MooCompEffectCompletionReservation *reservations;
    uint32_t reservation_capacity;
    uint32_t reserved3;
    MooCompEffectTransactionWorkspace *disconnect_workspace;
} MooCompEffectIntegration;

typedef struct {
    MooCompHandle buffer;
    uint32_t scale;
    uint32_t opacity;
    uint64_t frame_token;
    MooCompRect damage[MOO_COMP_SURFACE_DAMAGE_CAPACITY];
    uint32_t damage_count;
    uint32_t reserved;
    MooCompEffectState effect;
    MooCompEffectCpuTarget target;
    MooCompEffectCpuSource lower_z;
    uint32_t animations_on_surface;
    uint32_t animations_for_client;
} MooCompEffectCommitRequest;

typedef struct {
    MooCompAnimationSlot *timeline_slots;
    uint32_t timeline_capacity;
    MooCompEffectSurfaceBinding *binding_clones;
    uint32_t binding_capacity;
    MooCompAnimationSample *samples;
    uint32_t sample_capacity;
    MooCompAnimationCompletion *completions;
    uint32_t completion_capacity;
    MooCompEffectCompletionReservation *reservation_clones;
    uint32_t reservation_capacity;
    MooCompEffectDamageSurface *damage_surfaces;
    uint32_t damage_surface_capacity;
    MooCompRect *damage_workspace_regions;
    uint32_t damage_workspace_capacity;
    MooCompRect *damage_output_regions;
    uint32_t damage_output_capacity;
    uint32_t *surface_order;
    uint32_t surface_order_capacity;
    uint8_t *lower_z_pixels;
    size_t lower_z_capacity;
    uint32_t *rgba_ping;
    uint32_t *rgba_pong;
    uint64_t rgba_words_per_buffer;
    MooCompGpuPass *gpu_passes;
    uint32_t gpu_pass_capacity;
} MooCompEffectFrameWorkspace;

typedef struct {
    uint64_t timestamp_ns;
    uint64_t max_frame_work_units;
    MooCompGpuFenceId presenter_fence;
    uint32_t reduced_motion;
    uint32_t allow_gpu;
} MooCompEffectFrameRequest;

typedef struct {
    uint64_t frame_id;
    uint64_t work_units;
    uint64_t scratch_bytes;
    uint32_t damage_count;
    uint32_t full_damage;
    uint32_t used_gpu;
    uint32_t completion_count;
    MooCompGpuSubmission gpu_submission;
} MooCompEffectFrameResult;

MooCompResult moo_comp_init(MooCompositor *core, const MooCompConfig *config,
                            MooCompClientSlot *clients, uint32_t client_capacity,
                            MooCompSurfaceSlot *surfaces, uint32_t surface_capacity,
                            MooCompBufferSlot *buffers, uint32_t buffer_capacity,
                            MooCompFrameSlot *frames, uint32_t frame_capacity,
                            MooCompEventSlot *events, uint32_t event_capacity);

MooCompResult moo_comp_client_create(MooCompositor *core,
                                     MooCompHandle *out_client);
/* Bound clients delegate nonrecursively through disconnect_workspace. */
MooCompResult moo_comp_client_disconnect(MooCompositor *core,
                                     MooCompHandle client);

MooCompResult moo_comp_buffer_register(MooCompositor *core,
                                       MooCompHandle client,
                                       const MooCompBufferView *view,
                                       MooCompHandle *out_buffer);
MooCompResult moo_comp_buffer_unregister(MooCompositor *core,
                                         MooCompHandle client,
                                         MooCompHandle buffer);

MooCompResult moo_comp_surface_create(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle *out_surface);
/* Effect-bound surfaces return BAD_STATE before mutation; use destroy_ex. */
MooCompResult moo_comp_surface_destroy(MooCompositor *core,
                                       MooCompHandle client,
                                       MooCompHandle surface);
MooCompResult moo_comp_surface_attach(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle surface,
                                      MooCompHandle buffer);
MooCompResult moo_comp_surface_damage(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle surface,
                                      MooCompRect buffer_rect);
MooCompResult moo_comp_surface_set_scale(MooCompositor *core,
                                         MooCompHandle client,
                                         MooCompHandle surface,
                                         uint32_t scale);
MooCompResult moo_comp_surface_set_opacity(MooCompositor *core,
                                           MooCompHandle client,
                                           MooCompHandle surface,
                                           uint32_t opacity);
MooCompResult moo_comp_surface_frame(MooCompositor *core,
                                     MooCompHandle client,
                                     MooCompHandle surface,
                                     uint64_t token);
MooCompResult moo_comp_surface_commit(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle surface);

MooCompResult moo_comp_surface_set_position(MooCompositor *core,
                                            MooCompHandle surface,
                                            int32_t x, int32_t y);
MooCompResult moo_comp_surface_raise(MooCompositor *core,
                                     MooCompHandle surface);

MooCompResult moo_comp_pointer_focus(MooCompositor *core,
                                     MooCompHandle client);
MooCompResult moo_comp_pointer_position(MooCompositor *core,
                                        int32_t x, int32_t y);
MooCompResult moo_comp_cursor_set_buffer(MooCompositor *core,
                                         MooCompHandle client,
                                         MooCompHandle buffer,
                                         int32_t hotspot_x,
                                         int32_t hotspot_y,
                                         uint32_t scale);
MooCompResult moo_comp_cursor_hide(MooCompositor *core,
                                   MooCompHandle client);

MooCompResult moo_comp_build_frame(MooCompositor *core,
                                   const MooCompOutput *output,
                                   uint64_t *out_frame_id);
/* Observer is one-time bound, non-reentrant and observation-only. It runs
 * exactly once after each successful present_done state transition. */
MooCompResult moo_comp_present_done_observer_bind(
    MooCompositor *core, uint64_t generation,
    MooCompPresentDoneObserverFn observer, void *user);
MooCompResult moo_comp_present_done(MooCompositor *core,
                                    uint64_t frame_id,
                                    uint64_t present_sequence,
                                    uint64_t timestamp_ns);
MooCompResult moo_comp_next_event(MooCompositor *core,
                                  MooCompHandle client,
                                  MooCompEvent *out_event);

MooCompResult moo_comp_dispatch_stub(MooCompositor *core,
                                     MooCompHandle client,
                                     const MooCompRequest *request);

MooCompResult moo_comp_surface_info(const MooCompositor *core,
                                    MooCompHandle surface,
                                    MooCompSurfaceInfo *out_info);
MooCompResult moo_comp_buffer_info(const MooCompositor *core,
                                   MooCompHandle buffer,
                                   MooCompBufferInfo *out_info);

MooCompResult moo_comp_effects_bind(
    MooCompositor *core, MooCompEffectIntegration *integration,
    const MooCompEffectStateConfig *config,
    MooCompEffectSurfaceBinding *bindings, uint32_t binding_capacity,
    MooCompAnimationTimeline *timeline, MooCompGpuBackendState *gpu_backend,
    MooCompEffectCompletionReservation *reservations,
    uint32_t reservation_capacity,
    MooCompEffectTransactionWorkspace *disconnect_workspace);

MooCompResult moo_comp_surface_effect_set(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, MooCompHandle surface,
    const MooCompEffectState *requested);

MooCompResult moo_comp_surface_commit_ex(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, MooCompHandle surface,
    const MooCompEffectCommitRequest *request,
    MooCompEffectTransactionWorkspace *workspace);

MooCompResult moo_comp_surface_animation_start(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, MooCompHandle surface,
    const MooCompAnimationDesc *desc, uint64_t accepted_frame_ns,
    uint32_t reduced_motion, MooCompEffectTransactionWorkspace *workspace,
    uint32_t *out_completion_count);

MooCompResult moo_comp_surface_animation_cancel(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, MooCompHandle surface, uint64_t token,
    uint64_t timestamp_ns, MooCompEffectTransactionWorkspace *workspace,
    uint32_t *out_completion_count);

MooCompResult moo_comp_surface_destroy_ex(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, MooCompHandle surface, uint64_t timestamp_ns,
    MooCompEffectTransactionWorkspace *workspace,
    uint32_t *out_completion_count);

MooCompResult moo_comp_client_disconnect_ex(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, uint64_t timestamp_ns,
    MooCompEffectTransactionWorkspace *workspace,
    uint32_t *out_completion_count);

MooCompResult moo_comp_build_frame_ex(
    MooCompositor *core, MooCompEffectIntegration *integration,
    const MooCompOutput *output, const MooCompEffectFrameRequest *request,
    MooCompEffectFrameWorkspace *workspace,
    MooCompEffectFrameResult *out_result);

MooCompResult moo_comp_raster_copy_rgba(
    const MooCompOutput *output, uint8_t *destination,
    size_t destination_bytes, size_t destination_stride);

uint32_t moo_comp_damage_count(const MooCompositor *core);
MooCompResult moo_comp_damage_get(const MooCompositor *core,
                                  uint32_t index,
                                  MooCompRect *out_rect);
uint64_t moo_comp_state_hash(const MooCompositor *core);

MooCompResult moo_comp_raster_clear(const MooCompOutput *output,
                                    MooCompRect clip,
                                    uint8_t r, uint8_t g,
                                    uint8_t b, uint8_t a);
MooCompResult moo_comp_raster_blit(const MooCompOutput *output,
                                   const MooCompBufferView *source,
                                   int32_t dst_x, int32_t dst_y,
                                   uint32_t scale, uint32_t opacity,
                                   MooCompRect clip);

#endif
