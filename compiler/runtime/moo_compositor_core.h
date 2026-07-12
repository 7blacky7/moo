#ifndef MOO_COMPOSITOR_CORE_H
#define MOO_COMPOSITOR_CORE_H

#include "moo_compositor_protocol.h"

#include <stddef.h>
#include <stdint.h>

#define MOO_COMP_SURFACE_DAMAGE_CAPACITY 16u
#define MOO_COMP_OUTPUT_DAMAGE_CAPACITY 32u

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
} MooCompSurfaceSlot;

typedef struct {
    uint32_t live;
    uint32_t state;
    MooCompHandle owner;
    MooCompHandle surface;
    uint64_t token;
    uint64_t commit_sequence;
    uint64_t frame_id;
    uint32_t completion_status;
    uint32_t reserved;
} MooCompFrameSlot;

typedef struct {
    uint32_t live;
    uint32_t reserved;
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

MooCompResult moo_comp_init(MooCompositor *core, const MooCompConfig *config,
                            MooCompClientSlot *clients, uint32_t client_capacity,
                            MooCompSurfaceSlot *surfaces, uint32_t surface_capacity,
                            MooCompBufferSlot *buffers, uint32_t buffer_capacity,
                            MooCompFrameSlot *frames, uint32_t frame_capacity,
                            MooCompEventSlot *events, uint32_t event_capacity);

MooCompResult moo_comp_client_create(MooCompositor *core,
                                     MooCompHandle *out_client);
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
