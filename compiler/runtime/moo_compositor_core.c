#include "moo_compositor_core.h"

#include <limits.h>

#define MOO_COMP_HANDLE_KIND_SHIFT 28u
#define MOO_COMP_HANDLE_INDEX_MASK UINT32_C(0x0fffffff)
#define MOO_COMP_KIND_CLIENT 1u
#define MOO_COMP_KIND_SURFACE 2u
#define MOO_COMP_KIND_BUFFER 3u

#define MOO_COMP_PENDING_ATTACH UINT32_C(1)
#define MOO_COMP_PENDING_SCALE UINT32_C(2)
#define MOO_COMP_PENDING_OPACITY UINT32_C(4)

#define MOO_COMP_FRAME_WAITING 1u
#define MOO_COMP_FRAME_ASSIGNED 2u

static MooCompHandle moo_comp_make_handle(uint32_t kind, uint32_t index,
                                          uint32_t generation) {
    uint32_t low = (kind << MOO_COMP_HANDLE_KIND_SHIFT) | (index + 1u);
    return ((uint64_t)generation << 32u) | (uint64_t)low;
}

static uint32_t moo_comp_handle_kind(MooCompHandle handle) {
    return ((uint32_t)handle) >> MOO_COMP_HANDLE_KIND_SHIFT;
}

static int moo_comp_handle_index(MooCompHandle handle, uint32_t kind,
                                 uint32_t capacity, uint32_t *out_index) {
    uint32_t low;
    uint32_t encoded;
    if (!out_index || handle == MOO_COMP_HANDLE_INVALID) return 0;
    low = (uint32_t)handle;
    if (moo_comp_handle_kind(handle) != kind) return -1;
    encoded = low & MOO_COMP_HANDLE_INDEX_MASK;
    if (encoded == 0u || encoded - 1u >= capacity) return 0;
    *out_index = encoded - 1u;
    return 1;
}

static uint32_t moo_comp_next_generation(uint32_t generation) {
    if (generation == UINT32_MAX) return 0u;
    return generation + 1u;
}

static int moo_comp_core_valid(const MooCompositor *core) {
    uint32_t i;
    if (!core || !core->clients || core->client_capacity == 0u ||
        !core->surfaces || core->surface_capacity == 0u ||
        !core->buffers || core->buffer_capacity == 0u ||
        !core->frames || core->frame_capacity == 0u ||
        !core->events || core->event_capacity == 0u ||
        core->config.output_width <= 0 || core->config.output_height <= 0)
        return 0;
    for (i = 0u; i < core->frame_capacity; ++i) {
        uint32_t state = core->frames[i].slot_state;
        if (state > MOO_COMP_SLOT_LIVE ||
            (state == MOO_COMP_SLOT_FREE && core->frames[i].live) ||
            (state == MOO_COMP_SLOT_RESERVED && core->frames[i].live) ||
            (state == MOO_COMP_SLOT_LIVE && !core->frames[i].live))
            return 0;
    }
    for (i = 0u; i < core->event_capacity; ++i) {
        uint32_t state = core->events[i].slot_state;
        if (state > MOO_COMP_SLOT_LIVE ||
            (state == MOO_COMP_SLOT_FREE && core->events[i].live) ||
            (state == MOO_COMP_SLOT_RESERVED && core->events[i].live) ||
            (state == MOO_COMP_SLOT_LIVE && !core->events[i].live))
            return 0;
    }
    if (core->effect_integration) {
        const MooCompEffectIntegration *integration = core->effect_integration;
        if (integration->version != MOO_COMP_EFFECT_INTEGRATION_VERSION ||
            integration->core != core || !integration->reservations)
            return 0;
        for (i = 0u; i < integration->reservation_capacity; ++i) {
            uint32_t j;
            const MooCompEffectCompletionReservation *reservation =
                &integration->reservations[i];
            if (reservation->slot_state > MOO_COMP_SLOT_RESERVED)
                return 0;
            if (reservation->slot_state == MOO_COMP_SLOT_RESERVED) {
                if (reservation->event_index >= core->event_capacity ||
                    reservation->owner == MOO_COMP_HANDLE_INVALID ||
                    reservation->surface == MOO_COMP_HANDLE_INVALID ||
                    reservation->token == 0u ||
                    core->events[reservation->event_index].slot_state !=
                        MOO_COMP_SLOT_RESERVED ||
                    core->events[reservation->event_index].owner !=
                        reservation->owner ||
                    core->events[reservation->event_index].reserved != 1u ||
                    core->events[reservation->event_index].reserved_slot !=
                        i + 1u)
                    return 0;
                for (j = i + 1u; j < integration->reservation_capacity; ++j)
                    if (integration->reservations[j].slot_state ==
                            MOO_COMP_SLOT_RESERVED &&
                        (integration->reservations[j].event_index ==
                             reservation->event_index ||
                         (integration->reservations[j].owner ==
                              reservation->owner &&
                          integration->reservations[j].token ==
                              reservation->token)))
                        return 0;
            }
        }
        for (i = 0u; i < core->event_capacity; ++i) {
            uint32_t j, matches = 0u;
            if (core->events[i].slot_state != MOO_COMP_SLOT_RESERVED)
                continue;
            for (j = 0u; j < integration->reservation_capacity; ++j)
                if (integration->reservations[j].slot_state ==
                        MOO_COMP_SLOT_RESERVED &&
                    integration->reservations[j].event_index == i)
                    matches++;
            if (matches != 1u) return 0;
        }
    }
    return 1;
}

static void moo_comp_frame_slot_free(MooCompFrameSlot *slot) {
    slot->slot_state = MOO_COMP_SLOT_FREE;
    slot->live = 0u; slot->state = 0u; slot->reserved_slot = 0u;
    slot->owner = MOO_COMP_HANDLE_INVALID;
    slot->surface = MOO_COMP_HANDLE_INVALID;
    slot->token = 0u; slot->commit_sequence = 0u; slot->frame_id = 0u;
    slot->completion_status = 0u; slot->reserved = 0u;
}
static void moo_comp_frame_slot_live(MooCompFrameSlot *slot) {
    slot->slot_state = MOO_COMP_SLOT_LIVE;
    slot->live = 1u; slot->reserved_slot = 0u;
}
static void moo_comp_event_slot_free(MooCompEventSlot *slot) {
    slot->slot_state = MOO_COMP_SLOT_FREE;
    slot->live = 0u; slot->reserved = 0u; slot->reserved_slot = 0u;
    slot->owner = MOO_COMP_HANDLE_INVALID; slot->sequence = 0u;
    slot->event = (MooCompEvent){0u, 0u, MOO_COMP_HANDLE_INVALID,
                                 0u, 0u, 0u};
}
static void moo_comp_event_slot_live(MooCompEventSlot *slot) {
    slot->slot_state = MOO_COMP_SLOT_LIVE;
    slot->live = 1u; slot->reserved = 0u; slot->reserved_slot = 0u;
}

static MooCompResult moo_comp_client_slot(const MooCompositor *core,
                                          MooCompHandle handle,
                                          uint32_t *out_index) {
    uint32_t index;
    int decoded;
    if (!moo_comp_core_valid(core)) return MOO_COMP_INVALID;
    decoded = moo_comp_handle_index(handle, MOO_COMP_KIND_CLIENT,
                                    core->client_capacity, &index);
    if (decoded < 0) return MOO_COMP_WRONG_KIND;
    if (decoded == 0 || !core->clients[index].live ||
        core->clients[index].generation != (uint32_t)(handle >> 32u))
        return MOO_COMP_STALE_HANDLE;
    if (out_index) *out_index = index;
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_surface_slot(const MooCompositor *core,
                                           MooCompHandle handle,
                                           uint32_t *out_index) {
    uint32_t index;
    int decoded;
    if (!moo_comp_core_valid(core)) return MOO_COMP_INVALID;
    decoded = moo_comp_handle_index(handle, MOO_COMP_KIND_SURFACE,
                                    core->surface_capacity, &index);
    if (decoded < 0) return MOO_COMP_WRONG_KIND;
    if (decoded == 0 || !core->surfaces[index].live ||
        core->surfaces[index].generation != (uint32_t)(handle >> 32u))
        return MOO_COMP_STALE_HANDLE;
    if (out_index) *out_index = index;
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_buffer_slot(const MooCompositor *core,
                                          MooCompHandle handle,
                                          uint32_t *out_index) {
    uint32_t index;
    int decoded;
    if (!moo_comp_core_valid(core)) return MOO_COMP_INVALID;
    decoded = moo_comp_handle_index(handle, MOO_COMP_KIND_BUFFER,
                                    core->buffer_capacity, &index);
    if (decoded < 0) return MOO_COMP_WRONG_KIND;
    if (decoded == 0 || !core->buffers[index].live ||
        core->buffers[index].generation != (uint32_t)(handle >> 32u))
        return MOO_COMP_STALE_HANDLE;
    if (out_index) *out_index = index;
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_owned_surface(const MooCompositor *core,
                                            MooCompHandle client,
                                            MooCompHandle surface,
                                            uint32_t *out_index) {
    uint32_t index;
    MooCompResult result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_surface_slot(core, surface, &index);
    if (result != MOO_COMP_OK) return result;
    if (core->surfaces[index].owner != client) return MOO_COMP_ACCESS;
    if (out_index) *out_index = index;
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_owned_buffer(const MooCompositor *core,
                                           MooCompHandle client,
                                           MooCompHandle buffer,
                                           uint32_t *out_index) {
    uint32_t index;
    MooCompResult result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_buffer_slot(core, buffer, &index);
    if (result != MOO_COMP_OK) return result;
    if (core->buffers[index].owner != client) return MOO_COMP_ACCESS;
    if (out_index) *out_index = index;
    return MOO_COMP_OK;
}

static int moo_comp_view_valid(const MooCompBufferView *view) {
    size_t row_bytes;
    if (!view || !view->pixels ||
        view->format != MOO_COMP_FORMAT_RGBA8888 ||
        view->width <= 0 || view->height <= 0) return 0;
    if ((size_t)view->width > SIZE_MAX / 4u) return 0;
    row_bytes = (size_t)view->width * 4u;
    if (view->stride < row_bytes || view->stride % 4u != 0u) return 0;
    if ((size_t)view->height > SIZE_MAX / view->stride) return 0;
    return view->stride * (size_t)view->height <= view->buffer_bytes;
}

static int moo_comp_view_effectively_opaque(const MooCompBufferView *view,
                                            uint32_t scale,
                                            uint32_t opacity) {
    int32_t x;
    int32_t y;
    if (!moo_comp_view_valid(view) || opacity != 255u ||
        scale < MOO_COMP_SCALE_MIN || scale > MOO_COMP_SCALE_MAX ||
        view->width % (int32_t)scale != 0 ||
        view->height % (int32_t)scale != 0)
        return 0;
    for (y = 0; y < view->height; y += (int32_t)scale) {
        for (x = 0; x < view->width; x += (int32_t)scale) {
            const uint8_t *pixel =
                view->pixels + (size_t)y * view->stride + (size_t)x * 4u;
            if (pixel[3] != 255u) return 0;
        }
    }
    return 1;
}

static int moo_comp_output_valid(const MooCompositor *core,
                                 const MooCompOutput *output) {
    size_t row_bytes;
    if (!moo_comp_core_valid(core) || !output || !output->pixels ||
        output->width != core->config.output_width ||
        output->height != core->config.output_height) return 0;
    if ((size_t)output->width > SIZE_MAX / 4u) return 0;
    row_bytes = (size_t)output->width * 4u;
    if (output->stride < row_bytes || output->stride % 4u != 0u) return 0;
    if ((size_t)output->height > SIZE_MAX / output->stride) return 0;
    return output->stride * (size_t)output->height <= output->buffer_bytes;
}

static MooCompRect moo_comp_surface_bounds(const MooCompositor *core,
                                           const MooCompSurfaceSlot *surface) {
    MooCompRect rect;
    rect.x = surface->x;
    rect.y = surface->y;
    rect.width = 0;
    rect.height = 0;
    if (surface->committed.buffer != MOO_COMP_HANDLE_INVALID &&
        surface->committed.scale >= MOO_COMP_SCALE_MIN &&
        surface->committed.scale <= MOO_COMP_SCALE_MAX) {
        uint32_t index;
        if (moo_comp_buffer_slot(core, surface->committed.buffer, &index) ==
            MOO_COMP_OK) {
            rect.width = core->buffers[index].view.width /
                         (int32_t)surface->committed.scale;
            rect.height = core->buffers[index].view.height /
                          (int32_t)surface->committed.scale;
        }
    }
    return rect;
}

/*
 * Exact rectangular union coverage for server-validated opaque content.
 * A cover participates only when every source sample used by the v1 scaler has
 * alpha 255 and surface opacity is 255. No client-supplied opaque hint exists.
 */
static int moo_comp_region_fully_covered(
    const MooCompositor *core, const MooCompSurfaceSlot *target,
    int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    int32_t y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > core->config.output_width) x1 = core->config.output_width;
    if (y1 > core->config.output_height) y1 = core->config.output_height;
    if (x1 <= x0 || y1 <= y0) return 0;

    for (y = (int32_t)y0; y < (int32_t)y1; ++y) {
        int64_t covered = x0;
        while (covered < x1) {
            int64_t best = covered;
            uint32_t i;
            for (i = 0u; i < core->surface_capacity; ++i) {
                const MooCompSurfaceSlot *cover = &core->surfaces[i];
                MooCompRect bounds;
                int64_t right;
                int64_t bottom;
                if (!cover->live || cover == target ||
                    cover->z_sequence <= target->z_sequence ||
                    !cover->committed.effective_opaque)
                    continue;
                bounds = moo_comp_surface_bounds(core, cover);
                right = (int64_t)bounds.x + bounds.width;
                bottom = (int64_t)bounds.y + bounds.height;
                if (bounds.y > y || bottom <= y ||
                    bounds.x > covered || right <= covered)
                    continue;
                if (right > best) best = right < x1 ? right : x1;
            }
            if (core->cursor.visible) {
                uint32_t buffer_index;
                if (moo_comp_buffer_slot(core, core->cursor.buffer,
                                         &buffer_index) == MOO_COMP_OK) {
                    const MooCompBufferView *view =
                        &core->buffers[buffer_index].view;
                    if (moo_comp_view_effectively_opaque(
                            view, core->cursor.scale, 255u)) {
                        int64_t left =
                            (int64_t)core->cursor.x - core->cursor.hotspot_x;
                        int64_t top =
                            (int64_t)core->cursor.y - core->cursor.hotspot_y;
                        int64_t right =
                            left + view->width / (int32_t)core->cursor.scale;
                        int64_t bottom =
                            top + view->height / (int32_t)core->cursor.scale;
                        if (top <= y && bottom > y &&
                            left <= covered && right > covered &&
                            right > best)
                            best = right < x1 ? right : x1;
                    }
                }
            }
            if (best == covered) return 0;
            covered = best;
        }
    }
    return 1;
}

static void moo_comp_damage_i64(MooCompositor *core,
                                int64_t x0, int64_t y0,
                                int64_t x1, int64_t y1) {
    MooCompRect rect;
    uint32_t i;
    int64_t ux0;
    int64_t uy0;
    int64_t ux1;
    int64_t uy1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > core->config.output_width) x1 = core->config.output_width;
    if (y1 > core->config.output_height) y1 = core->config.output_height;
    if (x1 <= x0 || y1 <= y0) return;
    rect.x = (int32_t)x0;
    rect.y = (int32_t)y0;
    rect.width = (int32_t)(x1 - x0);
    rect.height = (int32_t)(y1 - y0);
    if (core->output_damage_count < MOO_COMP_OUTPUT_DAMAGE_CAPACITY) {
        core->output_damage[core->output_damage_count++] = rect;
        return;
    }
    ux0 = rect.x;
    uy0 = rect.y;
    ux1 = (int64_t)rect.x + rect.width;
    uy1 = (int64_t)rect.y + rect.height;
    for (i = 0u; i < core->output_damage_count; ++i) {
        int64_t right = (int64_t)core->output_damage[i].x +
                        core->output_damage[i].width;
        int64_t bottom = (int64_t)core->output_damage[i].y +
                         core->output_damage[i].height;
        if (core->output_damage[i].x < ux0) ux0 = core->output_damage[i].x;
        if (core->output_damage[i].y < uy0) uy0 = core->output_damage[i].y;
        if (right > ux1) ux1 = right;
        if (bottom > uy1) uy1 = bottom;
    }
    core->output_damage[0].x = (int32_t)ux0;
    core->output_damage[0].y = (int32_t)uy0;
    core->output_damage[0].width = (int32_t)(ux1 - ux0);
    core->output_damage[0].height = (int32_t)(uy1 - uy0);
    core->output_damage_count = 1u;
}

static void moo_comp_damage_rect(MooCompositor *core, MooCompRect rect) {
    moo_comp_damage_i64(core, rect.x, rect.y,
                        (int64_t)rect.x + rect.width,
                        (int64_t)rect.y + rect.height);
}
static uint32_t moo_comp_free_event_count(const MooCompositor *core) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].slot_state == MOO_COMP_SLOT_FREE) count++;
    }
    return count;
}

static uint32_t moo_comp_event_quota(const MooCompositor *core) {
    return core->event_capacity / core->client_capacity;
}

static uint32_t moo_comp_frame_quota(const MooCompositor *core) {
    return core->frame_capacity / core->client_capacity;
}

static uint32_t moo_comp_events_owned(const MooCompositor *core,
                                      MooCompHandle owner) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].slot_state != MOO_COMP_SLOT_FREE &&
            core->events[i].owner == owner) count++;
    }
    return count;
}

/* Pending tokens already reserve both a future frame slot and event slot. */
static uint32_t moo_comp_frames_owned(const MooCompositor *core,
                                      MooCompHandle owner) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->frame_capacity; ++i) {
        if (core->frames[i].slot_state != MOO_COMP_SLOT_FREE &&
            core->frames[i].owner == owner) count++;
    }
    for (i = 0u; i < core->surface_capacity; ++i) {
        if (core->surfaces[i].live && core->surfaces[i].owner == owner &&
            core->surfaces[i].pending.frame_token != 0u)
            count++;
    }
    return count;
}

static int moo_comp_event_reservation_available(const MooCompositor *core,
                                                MooCompHandle owner) {
    uint32_t events_owned = moo_comp_events_owned(core, owner);
    uint32_t frames_owned = moo_comp_frames_owned(core, owner);
    return events_owned < moo_comp_event_quota(core) &&
           frames_owned <= moo_comp_event_quota(core) - events_owned - 1u;
}

static MooCompResult moo_comp_push_event(MooCompositor *core,
                                         MooCompHandle owner,
                                         const MooCompEvent *event) {
    uint32_t i;
    if (!moo_comp_event_reservation_available(core, owner))
        return MOO_COMP_WOULD_BLOCK;
    if (core->event_sequence == UINT64_MAX) return MOO_COMP_LIMIT;
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].slot_state == MOO_COMP_SLOT_FREE) {
            core->event_sequence++;
            moo_comp_event_slot_live(&core->events[i]);
            core->events[i].owner = owner;
            core->events[i].sequence = core->event_sequence;
            core->events[i].event = *event;
            return MOO_COMP_OK;
        }
    }
    return MOO_COMP_WOULD_BLOCK;
}

static MooCompResult moo_comp_push_release(MooCompositor *core,
                                           MooCompBufferSlot *buffer,
                                           MooCompHandle handle) {
    MooCompEvent event;
    MooCompResult result;
    event.type = MOO_COMP_EVENT_BUFFER_RELEASE;
    event.status = 0u;
    event.object = handle;
    event.token = 0u;
    event.present_sequence = 0u;
    event.timestamp_ns = 0u;
    result = moo_comp_push_event(core, buffer->owner, &event);
    if (result == MOO_COMP_OK) buffer->release_armed = 0u;
    return result;
}

static MooCompResult moo_comp_buffer_ref(MooCompBufferSlot *buffer) {
    if (buffer->ref_count == UINT32_MAX) return MOO_COMP_LIMIT;
    if (buffer->ref_count == 0u) buffer->release_armed = 1u;
    buffer->ref_count++;
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_buffer_unref(MooCompositor *core,
                                           uint32_t index,
                                           MooCompHandle handle,
                                           int emit_event) {
    MooCompBufferSlot *buffer = &core->buffers[index];
    if (buffer->ref_count == 0u) return MOO_COMP_BAD_STATE;
    buffer->ref_count--;
    if (buffer->ref_count == 0u && buffer->release_armed) {
        if (emit_event) return moo_comp_push_release(core, buffer, handle);
        buffer->release_armed = 0u;
    }
    return MOO_COMP_OK;
}

static uint32_t moo_comp_find_free_frame(const MooCompositor *core) {
    uint32_t i;
    for (i = 0u; i < core->frame_capacity; ++i) {
        if (core->frames[i].slot_state == MOO_COMP_SLOT_FREE) return i;
    }
    return UINT32_MAX;
}

static int moo_comp_token_available(const MooCompositor *core,
                                    MooCompHandle owner, uint64_t token) {
    uint32_t i;
    for (i = 0u; i < core->surface_capacity; ++i)
        if (core->surfaces[i].live && core->surfaces[i].owner == owner &&
            core->surfaces[i].pending.frame_token == token) return 0;
    for (i = 0u; i < core->frame_capacity; ++i)
        if (core->frames[i].slot_state == MOO_COMP_SLOT_LIVE &&
            core->frames[i].owner == owner &&
            core->frames[i].token == token) return 0;
    for (i = 0u; i < core->event_capacity; ++i)
        if (core->events[i].slot_state == MOO_COMP_SLOT_LIVE &&
            core->events[i].owner == owner &&
            core->events[i].event.type == MOO_COMP_EVENT_FRAME_DONE &&
            core->events[i].event.token == token) return 0;
    if (core->effect_integration)
        for (i = 0u; i < core->effect_integration->reservation_capacity; ++i) {
            const MooCompEffectCompletionReservation *reservation =
                &core->effect_integration->reservations[i];
            if (reservation->slot_state == MOO_COMP_SLOT_RESERVED &&
                reservation->owner == owner && reservation->token == token)
                return 0;
        }
    return 1;
}

MooCompResult moo_comp_init(MooCompositor *core, const MooCompConfig *config,
                            MooCompClientSlot *clients, uint32_t client_capacity,
                            MooCompSurfaceSlot *surfaces, uint32_t surface_capacity,
                            MooCompBufferSlot *buffers, uint32_t buffer_capacity,
                            MooCompFrameSlot *frames, uint32_t frame_capacity,
                            MooCompEventSlot *events, uint32_t event_capacity) {
    uint32_t i;
    if (!core || !config || !clients || !surfaces || !buffers ||
        !frames || !events || client_capacity == 0u ||
        surface_capacity == 0u || buffer_capacity == 0u ||
        frame_capacity < client_capacity ||
        event_capacity < client_capacity ||
        client_capacity >= MOO_COMP_HANDLE_INDEX_MASK ||
        surface_capacity >= MOO_COMP_HANDLE_INDEX_MASK ||
        buffer_capacity >= MOO_COMP_HANDLE_INDEX_MASK ||
        config->output_width <= 0 || config->output_height <= 0)
        return MOO_COMP_INVALID;

    core->clients = clients;
    core->client_capacity = client_capacity;
    core->surfaces = surfaces;
    core->surface_capacity = surface_capacity;
    core->buffers = buffers;
    core->buffer_capacity = buffer_capacity;
    core->frames = frames;
    core->frame_capacity = frame_capacity;
    core->events = events;
    core->event_capacity = event_capacity;
    core->config = *config;
    core->output_damage_count = 0u;
    core->z_sequence = 0u;
    core->commit_sequence = 0u;
    core->frame_sequence = 0u;
    core->event_sequence = 0u;
    core->in_flight_frame = 0u;
    core->effect_integration = NULL;
    core->present_done_observer = NULL;
    core->present_done_observer_user = NULL;
    core->present_done_generation = 0u;
    core->cursor.focus_owner = MOO_COMP_HANDLE_INVALID;
    core->cursor.buffer = MOO_COMP_HANDLE_INVALID;
    core->cursor.x = 0;
    core->cursor.y = 0;
    core->cursor.hotspot_x = 0;
    core->cursor.hotspot_y = 0;
    core->cursor.scale = 1u;
    core->cursor.visible = 0u;

    for (i = 0u; i < client_capacity; ++i) {
        clients[i].live = 0u;
        clients[i].generation = 1u;
    }
    for (i = 0u; i < surface_capacity; ++i) {
        surfaces[i].live = 0u;
        surfaces[i].generation = 1u;
        surfaces[i].owner = MOO_COMP_HANDLE_INVALID;
        surfaces[i].committed.buffer = MOO_COMP_HANDLE_INVALID;
        surfaces[i].pending.damage_count = 0u;
        surfaces[i].pending.frame_token = 0u;
        surfaces[i].effect_bound = 0u;
        surfaces[i].reserved_effect = 0u;
    }
    for (i = 0u; i < buffer_capacity; ++i) {
        buffers[i].live = 0u;
        buffers[i].generation = 1u;
        buffers[i].owner = MOO_COMP_HANDLE_INVALID;
        buffers[i].ref_count = 0u;
        buffers[i].release_armed = 0u;
    }
    for (i = 0u; i < frame_capacity; ++i)
        moo_comp_frame_slot_free(&frames[i]);
    for (i = 0u; i < event_capacity; ++i)
        moo_comp_event_slot_free(&events[i]);

    moo_comp_damage_i64(core, 0, 0, config->output_width,
                        config->output_height);
    return MOO_COMP_OK;
}

MooCompResult moo_comp_client_create(MooCompositor *core,
                                     MooCompHandle *out_client) {
    uint32_t i;
    if (!moo_comp_core_valid(core) || !out_client) return MOO_COMP_INVALID;
    for (i = 0u; i < core->client_capacity; ++i) {
        if (!core->clients[i].live && core->clients[i].generation != 0u) {
            core->clients[i].live = 1u;
            *out_client = moo_comp_make_handle(MOO_COMP_KIND_CLIENT, i,
                                               core->clients[i].generation);
            return MOO_COMP_OK;
        }
    }
    return MOO_COMP_LIMIT;
}

MooCompResult moo_comp_buffer_register(MooCompositor *core,
                                       MooCompHandle client,
                                       const MooCompBufferView *view,
                                       MooCompHandle *out_buffer) {
    uint32_t i;
    MooCompResult result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    if (!out_buffer || !moo_comp_view_valid(view)) return MOO_COMP_BAD_BUFFER;
    for (i = 0u; i < core->buffer_capacity; ++i) {
        if (!core->buffers[i].live && core->buffers[i].generation != 0u) {
            core->buffers[i].owner = client;
            core->buffers[i].view = *view;
            core->buffers[i].ref_count = 0u;
            core->buffers[i].release_armed = 0u;
            core->buffers[i].live = 1u;
            *out_buffer = moo_comp_make_handle(MOO_COMP_KIND_BUFFER, i,
                                               core->buffers[i].generation);
            return MOO_COMP_OK;
        }
    }
    return MOO_COMP_LIMIT;
}

MooCompResult moo_comp_buffer_unregister(MooCompositor *core,
                                         MooCompHandle client,
                                         MooCompHandle buffer) {
    uint32_t i;
    uint32_t index;
    MooCompResult result = moo_comp_owned_buffer(core, client, buffer, &index);
    if (result != MOO_COMP_OK) return result;
    if (core->buffers[index].ref_count != 0u) return MOO_COMP_BAD_STATE;
    for (i = 0u; i < core->surface_capacity; ++i) {
        if (core->surfaces[i].live &&
            (core->surfaces[i].pending.dirty_mask & MOO_COMP_PENDING_ATTACH) &&
            core->surfaces[i].pending.buffer == buffer)
            return MOO_COMP_BAD_STATE;
    }
    core->buffers[index].live = 0u;
    core->buffers[index].owner = MOO_COMP_HANDLE_INVALID;
    core->buffers[index].view.pixels = 0;
    core->buffers[index].view.buffer_bytes = 0u;
    core->buffers[index].view.stride = 0u;
    core->buffers[index].view.width = 0;
    core->buffers[index].view.height = 0;
    core->buffers[index].view.format = MOO_COMP_FORMAT_INVALID;
    core->buffers[index].release_armed = 0u;
    core->buffers[index].generation =
        moo_comp_next_generation(core->buffers[index].generation);
    return MOO_COMP_OK;
}

MooCompResult moo_comp_surface_create(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle *out_surface) {
    uint32_t i;
    MooCompSurfaceSlot *surface;
    MooCompResult result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    if (!out_surface) return MOO_COMP_INVALID;
    if (core->z_sequence == UINT64_MAX) return MOO_COMP_LIMIT;
    for (i = 0u; i < core->surface_capacity; ++i) {
        if (!core->surfaces[i].live && core->surfaces[i].generation != 0u) {
            surface = &core->surfaces[i];
            core->z_sequence++;
            surface->owner = client;
            surface->x = 0;
            surface->y = 0;
            surface->z_sequence = core->z_sequence;
            surface->committed.buffer = MOO_COMP_HANDLE_INVALID;
            surface->committed.scale = 1u;
            surface->committed.opacity = 255u;
            surface->committed.effective_opaque = 0u;
            surface->committed.commit_sequence = 0u;
            surface->pending.dirty_mask = 0u;
            surface->pending.buffer = MOO_COMP_HANDLE_INVALID;
            surface->pending.scale = 1u;
            surface->pending.opacity = 255u;
            surface->pending.frame_token = 0u;
            surface->pending.damage_count = 0u;
            surface->effect_bound = 0u;
            surface->reserved_effect = 0u;
            surface->live = 1u;
            *out_surface = moo_comp_make_handle(MOO_COMP_KIND_SURFACE, i,
                                                surface->generation);
            return MOO_COMP_OK;
        }
    }
    return MOO_COMP_LIMIT;
}

MooCompResult moo_comp_surface_attach(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle surface_handle,
                                      MooCompHandle buffer) {
    uint32_t surface_index;
    MooCompResult result = moo_comp_owned_surface(core, client, surface_handle,
                                                 &surface_index);
    if (result != MOO_COMP_OK) return result;
    if (buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_owned_buffer(core, client, buffer, 0);
        if (result != MOO_COMP_OK) return result;
    }
    core->surfaces[surface_index].pending.buffer = buffer;
    core->surfaces[surface_index].pending.dirty_mask |=
        MOO_COMP_PENDING_ATTACH;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_surface_damage(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle surface_handle,
                                      MooCompRect buffer_rect) {
    uint32_t index;
    MooCompSurfacePending *pending;
    MooCompResult result = moo_comp_owned_surface(core, client, surface_handle,
                                                 &index);
    if (result != MOO_COMP_OK) return result;
    if (buffer_rect.width <= 0 || buffer_rect.height <= 0)
        return MOO_COMP_INVALID;
    pending = &core->surfaces[index].pending;
    if (pending->damage_count < MOO_COMP_SURFACE_DAMAGE_CAPACITY) {
        pending->damage[pending->damage_count++] = buffer_rect;
    } else {
        pending->damage[0].x = 0;
        pending->damage[0].y = 0;
        pending->damage[0].width = INT32_MAX;
        pending->damage[0].height = INT32_MAX;
        pending->damage_count = 1u;
    }
    return MOO_COMP_OK;
}

MooCompResult moo_comp_surface_set_scale(MooCompositor *core,
                                         MooCompHandle client,
                                         MooCompHandle surface_handle,
                                         uint32_t scale) {
    uint32_t index;
    MooCompResult result = moo_comp_owned_surface(core, client, surface_handle,
                                                 &index);
    if (result != MOO_COMP_OK) return result;
    if (scale < MOO_COMP_SCALE_MIN || scale > MOO_COMP_SCALE_MAX)
        return MOO_COMP_INVALID;
    core->surfaces[index].pending.scale = scale;
    core->surfaces[index].pending.dirty_mask |= MOO_COMP_PENDING_SCALE;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_surface_set_opacity(MooCompositor *core,
                                           MooCompHandle client,
                                           MooCompHandle surface_handle,
                                           uint32_t opacity) {
    uint32_t index;
    MooCompResult result = moo_comp_owned_surface(core, client, surface_handle,
                                                 &index);
    if (result != MOO_COMP_OK) return result;
    if (opacity > 255u) return MOO_COMP_INVALID;
    core->surfaces[index].pending.opacity = opacity;
    core->surfaces[index].pending.dirty_mask |= MOO_COMP_PENDING_OPACITY;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_surface_frame(MooCompositor *core,
                                     MooCompHandle client,
                                     MooCompHandle surface_handle,
                                     uint64_t token) {
    uint32_t index;
    MooCompResult result = moo_comp_owned_surface(core, client, surface_handle,
                                                 &index);
    if (result != MOO_COMP_OK) return result;
    if (token == 0u || core->surfaces[index].pending.frame_token != 0u)
        return MOO_COMP_BAD_STATE;
    if (!moo_comp_token_available(core, client, token))
        return MOO_COMP_BAD_STATE;
    if (moo_comp_frames_owned(core, client) >= moo_comp_frame_quota(core))
        return MOO_COMP_LIMIT;
    if (!moo_comp_event_reservation_available(core, client))
        return MOO_COMP_WOULD_BLOCK;
    core->surfaces[index].pending.frame_token = token;
    return MOO_COMP_OK;
}

static void moo_comp_damage_buffer_rect(MooCompositor *core,
                                        const MooCompSurfaceSlot *surface,
                                        const MooCompBufferView *view,
                                        uint32_t scale,
                                        MooCompRect damage) {
    int64_t x0 = damage.x;
    int64_t y0 = damage.y;
    int64_t x1 = (int64_t)damage.x + damage.width;
    int64_t y1 = (int64_t)damage.y + damage.height;
    int64_t lx0;
    int64_t ly0;
    int64_t lx1;
    int64_t ly1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > view->width) x1 = view->width;
    if (y1 > view->height) y1 = view->height;
    if (x1 <= x0 || y1 <= y0) return;
    lx0 = x0 / scale;
    ly0 = y0 / scale;
    lx1 = (x1 + scale - 1u) / scale;
    ly1 = (y1 + scale - 1u) / scale;
    if (moo_comp_region_fully_covered(
            core, surface, (int64_t)surface->x + lx0,
            (int64_t)surface->y + ly0,
            (int64_t)surface->x + lx1,
            (int64_t)surface->y + ly1))
        return;
    moo_comp_damage_i64(core, (int64_t)surface->x + lx0,
                        (int64_t)surface->y + ly0,
                        (int64_t)surface->x + lx1,
                        (int64_t)surface->y + ly1);
}

MooCompResult moo_comp_surface_commit(MooCompositor *core,
                                      MooCompHandle client,
                                      MooCompHandle surface_handle) {
    uint32_t surface_index;
    uint32_t new_buffer_index = 0u;
    uint32_t old_buffer_index = 0u;
    uint32_t frame_index = UINT32_MAX;
    MooCompSurfaceSlot *surface;
    MooCompHandle old_buffer;
    MooCompHandle new_buffer;
    uint32_t new_scale;
    uint32_t new_opacity;
    MooCompRect old_bounds;
    MooCompRect new_bounds;
    int geometry_changed;
    int buffer_changed;
    int release_event = 0;
    uint32_t i;
    MooCompResult result = moo_comp_owned_surface(core, client, surface_handle,
                                                 &surface_index);
    if (result != MOO_COMP_OK) return result;
    surface = &core->surfaces[surface_index];
    if (surface->effect_bound) return MOO_COMP_BAD_STATE;
    old_buffer = surface->committed.buffer;
    new_buffer = (surface->pending.dirty_mask & MOO_COMP_PENDING_ATTACH) ?
                 surface->pending.buffer : old_buffer;
    new_scale = (surface->pending.dirty_mask & MOO_COMP_PENDING_SCALE) ?
                surface->pending.scale : surface->committed.scale;
    new_opacity = (surface->pending.dirty_mask & MOO_COMP_PENDING_OPACITY) ?
                  surface->pending.opacity : surface->committed.opacity;
    if (new_scale < MOO_COMP_SCALE_MIN || new_scale > MOO_COMP_SCALE_MAX ||
        new_opacity > 255u) return MOO_COMP_INVALID;
    if (new_buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_owned_buffer(core, client, new_buffer,
                                       &new_buffer_index);
        if (result != MOO_COMP_OK) return result;
        if (core->buffers[new_buffer_index].view.width %
                (int32_t)new_scale != 0 ||
            core->buffers[new_buffer_index].view.height %
                (int32_t)new_scale != 0)
            return MOO_COMP_BAD_BUFFER;
    }
    if (old_buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_buffer_slot(core, old_buffer, &old_buffer_index);
        if (result != MOO_COMP_OK) return result;
    }
    buffer_changed = new_buffer != old_buffer;
    if (buffer_changed && new_buffer != MOO_COMP_HANDLE_INVALID &&
        core->buffers[new_buffer_index].ref_count == UINT32_MAX)
        return MOO_COMP_LIMIT;
    if (buffer_changed && old_buffer != MOO_COMP_HANDLE_INVALID &&
        core->buffers[old_buffer_index].ref_count == 1u &&
        core->buffers[old_buffer_index].release_armed)
        release_event = 1;
    if (release_event &&
        (!moo_comp_event_reservation_available(core, client) ||
         moo_comp_free_event_count(core) == 0u ||
         core->event_sequence == UINT64_MAX))
        return MOO_COMP_WOULD_BLOCK;
    if (surface->pending.frame_token != 0u) {
        frame_index = moo_comp_find_free_frame(core);
        if (frame_index == UINT32_MAX) return MOO_COMP_LIMIT;
    }
    if (core->commit_sequence == UINT64_MAX) return MOO_COMP_LIMIT;

    old_bounds = moo_comp_surface_bounds(core, surface);
    geometry_changed = (old_buffer == MOO_COMP_HANDLE_INVALID) !=
                       (new_buffer == MOO_COMP_HANDLE_INVALID);
    if (old_buffer != MOO_COMP_HANDLE_INVALID &&
        new_buffer != MOO_COMP_HANDLE_INVALID) {
        const MooCompBufferView *old_view = &core->buffers[old_buffer_index].view;
        const MooCompBufferView *new_view = &core->buffers[new_buffer_index].view;
        if (old_view->width / (int32_t)surface->committed.scale !=
                new_view->width / (int32_t)new_scale ||
            old_view->height / (int32_t)surface->committed.scale !=
                new_view->height / (int32_t)new_scale)
            geometry_changed = 1;
    }
    if (new_scale != surface->committed.scale ||
        new_opacity != surface->committed.opacity)
        geometry_changed = 1;

    if (buffer_changed && new_buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_buffer_ref(&core->buffers[new_buffer_index]);
        if (result != MOO_COMP_OK) return result;
    }
    core->commit_sequence++;
    surface->committed.buffer = new_buffer;
    surface->committed.scale = new_scale;
    surface->committed.opacity = new_opacity;
    surface->committed.effective_opaque =
        new_buffer != MOO_COMP_HANDLE_INVALID &&
        moo_comp_view_effectively_opaque(
            &core->buffers[new_buffer_index].view, new_scale, new_opacity);
    surface->committed.commit_sequence = core->commit_sequence;
    new_bounds = moo_comp_surface_bounds(core, surface);

    if (geometry_changed) {
        moo_comp_damage_rect(core, old_bounds);
        moo_comp_damage_rect(core, new_bounds);
    } else if (surface->pending.damage_count != 0u &&
               new_buffer != MOO_COMP_HANDLE_INVALID) {
        for (i = 0u; i < surface->pending.damage_count; ++i) {
            moo_comp_damage_buffer_rect(core, surface,
                &core->buffers[new_buffer_index].view, new_scale,
                surface->pending.damage[i]);
        }
    } else if (buffer_changed) {
        moo_comp_damage_rect(core, new_bounds);
    }

    if (frame_index != UINT32_MAX) {
        MooCompFrameSlot *frame = &core->frames[frame_index];
        moo_comp_frame_slot_live(frame);
        frame->state = MOO_COMP_FRAME_WAITING;
        frame->owner = client;
        frame->surface = surface_handle;
        frame->token = surface->pending.frame_token;
        frame->commit_sequence = surface->committed.commit_sequence;
        frame->frame_id = 0u;
        frame->completion_status = 0u;
    }
    surface->pending.dirty_mask = 0u;
    surface->pending.damage_count = 0u;
    surface->pending.frame_token = 0u;

    if (buffer_changed && old_buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_buffer_unref(core, old_buffer_index, old_buffer, 1);
        if (result != MOO_COMP_OK) return result;
    }
    return MOO_COMP_OK;
}

MooCompResult moo_comp_surface_set_position(MooCompositor *core,
                                            MooCompHandle surface_handle,
                                            int32_t x, int32_t y) {
    uint32_t index;
    MooCompRect old_bounds;
    MooCompRect new_bounds;
    MooCompResult result = moo_comp_surface_slot(core, surface_handle, &index);
    if (result != MOO_COMP_OK) return result;
    old_bounds = moo_comp_surface_bounds(core, &core->surfaces[index]);
    core->surfaces[index].x = x;
    core->surfaces[index].y = y;
    new_bounds = moo_comp_surface_bounds(core, &core->surfaces[index]);
    moo_comp_damage_rect(core, old_bounds);
    moo_comp_damage_rect(core, new_bounds);
    return MOO_COMP_OK;
}

MooCompResult moo_comp_surface_raise(MooCompositor *core,
                                     MooCompHandle surface_handle) {
    uint32_t index;
    MooCompRect bounds;
    MooCompResult result = moo_comp_surface_slot(core, surface_handle, &index);
    if (result != MOO_COMP_OK) return result;
    if (core->z_sequence == UINT64_MAX) return MOO_COMP_LIMIT;
    bounds = moo_comp_surface_bounds(core, &core->surfaces[index]);
    core->z_sequence++;
    core->surfaces[index].z_sequence = core->z_sequence;
    moo_comp_damage_rect(core, bounds);
    return MOO_COMP_OK;
}

static void moo_comp_cancel_surface_frames(MooCompositor *core,
                                           MooCompHandle surface) {
    uint32_t i;
    for (i = 0u; i < core->frame_capacity; ++i) {
        if (core->frames[i].slot_state == MOO_COMP_SLOT_LIVE &&
            core->frames[i].live && core->frames[i].surface == surface)
            core->frames[i].completion_status = MOO_COMP_FRAME_CANCELLED;
    }
}

MooCompResult moo_comp_surface_destroy(MooCompositor *core,
                                       MooCompHandle client,
                                       MooCompHandle surface_handle) {
    uint32_t index;
    uint32_t buffer_index = 0u;
    uint32_t cancel_frame_index = UINT32_MAX;
    MooCompSurfaceSlot *surface;
    MooCompHandle buffer;
    MooCompRect bounds;
    int release_event = 0;
    MooCompResult result = moo_comp_owned_surface(core, client, surface_handle,
                                                 &index);
    if (result != MOO_COMP_OK) return result;
    surface = &core->surfaces[index];
    if (surface->effect_bound) return MOO_COMP_BAD_STATE;
    if (surface->pending.frame_token != 0u) {
        cancel_frame_index = moo_comp_find_free_frame(core);
        if (cancel_frame_index == UINT32_MAX) return MOO_COMP_LIMIT;
    }
    buffer = surface->committed.buffer;
    if (buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_buffer_slot(core, buffer, &buffer_index);
        if (result != MOO_COMP_OK) return result;
        release_event = core->buffers[buffer_index].ref_count == 1u &&
                        core->buffers[buffer_index].release_armed;
    }
    if (release_event &&
        (!moo_comp_event_reservation_available(core, client) ||
         moo_comp_free_event_count(core) == 0u ||
         core->event_sequence == UINT64_MAX))
        return MOO_COMP_WOULD_BLOCK;
    bounds = moo_comp_surface_bounds(core, surface);
    moo_comp_damage_rect(core, bounds);
    if (cancel_frame_index != UINT32_MAX) {
        MooCompFrameSlot *frame = &core->frames[cancel_frame_index];
        moo_comp_frame_slot_live(frame);
        frame->state = MOO_COMP_FRAME_WAITING;
        frame->owner = client;
        frame->surface = surface_handle;
        frame->token = surface->pending.frame_token;
        frame->commit_sequence = surface->committed.commit_sequence;
        frame->frame_id = 0u;
        frame->completion_status = MOO_COMP_FRAME_CANCELLED;
    }
    moo_comp_cancel_surface_frames(core, surface_handle);
    surface->live = 0u;
    surface->owner = MOO_COMP_HANDLE_INVALID;
    surface->committed.buffer = MOO_COMP_HANDLE_INVALID;
    surface->pending.dirty_mask = 0u;
    surface->pending.damage_count = 0u;
    surface->pending.frame_token = 0u;
    surface->generation = moo_comp_next_generation(surface->generation);
    if (buffer != MOO_COMP_HANDLE_INVALID)
        return moo_comp_buffer_unref(core, buffer_index, buffer, 1);
    return MOO_COMP_OK;
}

static int moo_comp_cursor_bounds(const MooCompositor *core,
                                  int64_t *x0, int64_t *y0,
                                  int64_t *x1, int64_t *y1) {
    uint32_t index;
    const MooCompBufferView *view;
    if (!core->cursor.visible ||
        moo_comp_buffer_slot(core, core->cursor.buffer, &index) != MOO_COMP_OK)
        return 0;
    view = &core->buffers[index].view;
    *x0 = (int64_t)core->cursor.x - core->cursor.hotspot_x;
    *y0 = (int64_t)core->cursor.y - core->cursor.hotspot_y;
    *x1 = *x0 + view->width / (int32_t)core->cursor.scale;
    *y1 = *y0 + view->height / (int32_t)core->cursor.scale;
    return 1;
}

static MooCompResult moo_comp_cursor_release(MooCompositor *core,
                                             int emit_event) {
    uint32_t index;
    MooCompHandle buffer = core->cursor.buffer;
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;
    if (!core->cursor.visible) return MOO_COMP_OK;
    if (moo_comp_cursor_bounds(core, &x0, &y0, &x1, &y1))
        moo_comp_damage_i64(core, x0, y0, x1, y1);
    if (moo_comp_buffer_slot(core, buffer, &index) != MOO_COMP_OK)
        return MOO_COMP_BAD_STATE;
    core->cursor.visible = 0u;
    core->cursor.buffer = MOO_COMP_HANDLE_INVALID;
    return moo_comp_buffer_unref(core, index, buffer, emit_event);
}

MooCompResult moo_comp_pointer_focus(MooCompositor *core,
                                     MooCompHandle client) {
    uint32_t buffer_index;
    MooCompResult result;
    if (!moo_comp_core_valid(core)) return MOO_COMP_INVALID;
    if (client != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_client_slot(core, client, 0);
        if (result != MOO_COMP_OK) return result;
    }
    if (client == core->cursor.focus_owner) return MOO_COMP_OK;
    if (core->cursor.visible &&
        moo_comp_buffer_slot(core, core->cursor.buffer, &buffer_index) ==
            MOO_COMP_OK &&
        core->buffers[buffer_index].ref_count == 1u &&
        core->buffers[buffer_index].release_armed &&
        (!moo_comp_event_reservation_available(
             core, core->buffers[buffer_index].owner) ||
         moo_comp_free_event_count(core) == 0u ||
         core->event_sequence == UINT64_MAX))
        return MOO_COMP_WOULD_BLOCK;
    result = moo_comp_cursor_release(core, 1);
    if (result != MOO_COMP_OK) return result;
    core->cursor.focus_owner = client;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_pointer_position(MooCompositor *core,
                                        int32_t x, int32_t y) {
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;
    if (!moo_comp_core_valid(core)) return MOO_COMP_INVALID;
    if (moo_comp_cursor_bounds(core, &x0, &y0, &x1, &y1))
        moo_comp_damage_i64(core, x0, y0, x1, y1);
    core->cursor.x = x;
    core->cursor.y = y;
    if (moo_comp_cursor_bounds(core, &x0, &y0, &x1, &y1))
        moo_comp_damage_i64(core, x0, y0, x1, y1);
    return MOO_COMP_OK;
}

MooCompResult moo_comp_cursor_set_buffer(MooCompositor *core,
                                         MooCompHandle client,
                                         MooCompHandle buffer,
                                         int32_t hotspot_x,
                                         int32_t hotspot_y,
                                         uint32_t scale) {
    uint32_t new_index;
    uint32_t old_index = 0u;
    const MooCompBufferView *view;
    MooCompHandle old_buffer;
    int release_event = 0;
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;
    MooCompResult result = moo_comp_owned_buffer(core, client, buffer,
                                                &new_index);
    if (result != MOO_COMP_OK) return result;
    if (core->cursor.focus_owner != client) return MOO_COMP_ACCESS;
    view = &core->buffers[new_index].view;
    if (scale < MOO_COMP_SCALE_MIN || scale > MOO_COMP_SCALE_MAX ||
        view->width % (int32_t)scale != 0 ||
        view->height % (int32_t)scale != 0 ||
        hotspot_x < 0 || hotspot_y < 0 ||
        hotspot_x >= view->width / (int32_t)scale ||
        hotspot_y >= view->height / (int32_t)scale)
        return MOO_COMP_INVALID;
    old_buffer = core->cursor.visible ? core->cursor.buffer :
                 MOO_COMP_HANDLE_INVALID;
    if (old_buffer != buffer &&
        core->buffers[new_index].ref_count == UINT32_MAX)
        return MOO_COMP_LIMIT;
    if (old_buffer != MOO_COMP_HANDLE_INVALID && old_buffer != buffer) {
        result = moo_comp_buffer_slot(core, old_buffer, &old_index);
        if (result != MOO_COMP_OK) return result;
        release_event = core->buffers[old_index].ref_count == 1u &&
                        core->buffers[old_index].release_armed;
    }
    if (release_event &&
        (!moo_comp_event_reservation_available(core, client) ||
         moo_comp_free_event_count(core) == 0u ||
         core->event_sequence == UINT64_MAX))
        return MOO_COMP_WOULD_BLOCK;
    if (moo_comp_cursor_bounds(core, &x0, &y0, &x1, &y1))
        moo_comp_damage_i64(core, x0, y0, x1, y1);
    if (old_buffer != buffer) {
        result = moo_comp_buffer_ref(&core->buffers[new_index]);
        if (result != MOO_COMP_OK) return result;
    }
    core->cursor.buffer = buffer;
    core->cursor.hotspot_x = hotspot_x;
    core->cursor.hotspot_y = hotspot_y;
    core->cursor.scale = scale;
    core->cursor.visible = 1u;
    if (moo_comp_cursor_bounds(core, &x0, &y0, &x1, &y1))
        moo_comp_damage_i64(core, x0, y0, x1, y1);
    if (old_buffer != MOO_COMP_HANDLE_INVALID && old_buffer != buffer)
        return moo_comp_buffer_unref(core, old_index, old_buffer, 1);
    return MOO_COMP_OK;
}

MooCompResult moo_comp_cursor_hide(MooCompositor *core,
                                   MooCompHandle client) {
    uint32_t index;
    MooCompResult result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    if (core->cursor.focus_owner != client) return MOO_COMP_ACCESS;
    if (!core->cursor.visible) return MOO_COMP_OK;
    result = moo_comp_buffer_slot(core, core->cursor.buffer, &index);
    if (result != MOO_COMP_OK) return result;
    if (core->buffers[index].ref_count == 1u &&
        core->buffers[index].release_armed &&
        (!moo_comp_event_reservation_available(core, client) ||
         moo_comp_free_event_count(core) == 0u ||
         core->event_sequence == UINT64_MAX))
        return MOO_COMP_WOULD_BLOCK;
    return moo_comp_cursor_release(core, 1);
}

MooCompResult moo_comp_client_disconnect(MooCompositor *core,
                                         MooCompHandle client) {
    uint32_t client_index;
    uint32_t i;
    if (core && core->effect_integration) {
        uint32_t completion_count = 0u;
        MooCompEffectIntegration *integration = core->effect_integration;
        if (!integration->timeline || !integration->disconnect_workspace)
            return MOO_COMP_BAD_STATE;
        return moo_comp_client_disconnect_ex(
            core, integration, client,
            integration->timeline->last_timestamp_ns,
            integration->disconnect_workspace, &completion_count);
    }
    MooCompResult result = moo_comp_client_slot(core, client, &client_index);
    if (result != MOO_COMP_OK) return result;

    if (core->cursor.focus_owner == client) {
        (void)moo_comp_cursor_release(core, 0);
        core->cursor.focus_owner = MOO_COMP_HANDLE_INVALID;
    }
    for (i = 0u; i < core->surface_capacity; ++i) {
        MooCompSurfaceSlot *surface = &core->surfaces[i];
        if (surface->live && surface->owner == client) {
            MooCompHandle handle = moo_comp_make_handle(MOO_COMP_KIND_SURFACE,
                                                        i, surface->generation);
            MooCompRect bounds = moo_comp_surface_bounds(core, surface);
            MooCompHandle buffer = surface->committed.buffer;
            uint32_t buffer_index;
            moo_comp_damage_rect(core, bounds);
            moo_comp_cancel_surface_frames(core, handle);
            if (buffer != MOO_COMP_HANDLE_INVALID &&
                moo_comp_buffer_slot(core, buffer, &buffer_index) ==
                    MOO_COMP_OK)
                (void)moo_comp_buffer_unref(core, buffer_index, buffer, 0);
            surface->live = 0u;
            surface->owner = MOO_COMP_HANDLE_INVALID;
            surface->committed.buffer = MOO_COMP_HANDLE_INVALID;
            surface->pending.dirty_mask = 0u;
            surface->pending.damage_count = 0u;
            surface->pending.frame_token = 0u;
            surface->generation = moo_comp_next_generation(surface->generation);
        }
    }
    for (i = 0u; i < core->frame_capacity; ++i) {
        if (core->frames[i].slot_state != MOO_COMP_SLOT_FREE &&
            core->frames[i].owner == client)
            moo_comp_frame_slot_free(&core->frames[i]);
    }
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].slot_state != MOO_COMP_SLOT_FREE &&
            core->events[i].owner == client)
            moo_comp_event_slot_free(&core->events[i]);
    }
    for (i = 0u; i < core->buffer_capacity; ++i) {
        MooCompBufferSlot *buffer = &core->buffers[i];
        if (buffer->live && buffer->owner == client) {
            buffer->live = 0u;
            buffer->owner = MOO_COMP_HANDLE_INVALID;
            buffer->view.pixels = 0;
            buffer->view.buffer_bytes = 0u;
            buffer->view.stride = 0u;
            buffer->view.width = 0;
            buffer->view.height = 0;
            buffer->view.format = MOO_COMP_FORMAT_INVALID;
            buffer->ref_count = 0u;
            buffer->release_armed = 0u;
            buffer->generation = moo_comp_next_generation(buffer->generation);
        }
    }
    core->clients[client_index].live = 0u;
    core->clients[client_index].generation =
        moo_comp_next_generation(core->clients[client_index].generation);
    return MOO_COMP_OK;
}

static int moo_comp_surface_visible(const MooCompositor *core,
                                    const MooCompSurfaceSlot *surface) {
    MooCompRect bounds = moo_comp_surface_bounds(core, surface);
    return surface->committed.buffer != MOO_COMP_HANDLE_INVALID &&
           surface->committed.opacity != 0u &&
           bounds.width > 0 && bounds.height > 0 &&
           (int64_t)bounds.x + bounds.width > 0 &&
           (int64_t)bounds.y + bounds.height > 0 &&
           bounds.x < core->config.output_width &&
           bounds.y < core->config.output_height;
}

static MooCompResult moo_comp_draw_surfaces(MooCompositor *core,
                                            const MooCompOutput *output,
                                            MooCompRect damage) {
    uint32_t drawn = 0u;
    uint64_t after = 0u;
    while (drawn < core->surface_capacity) {
        uint32_t i;
        uint32_t best = UINT32_MAX;
        uint64_t best_z = UINT64_MAX;
        for (i = 0u; i < core->surface_capacity; ++i) {
            if (core->surfaces[i].live &&
                core->surfaces[i].committed.buffer !=
                    MOO_COMP_HANDLE_INVALID &&
                core->surfaces[i].z_sequence > after &&
                core->surfaces[i].z_sequence < best_z) {
                best = i;
                best_z = core->surfaces[i].z_sequence;
            }
        }
        if (best == UINT32_MAX) break;
        {
            MooCompSurfaceSlot *surface = &core->surfaces[best];
            uint32_t buffer_index;
            MooCompResult result = moo_comp_buffer_slot(
                core, surface->committed.buffer, &buffer_index);
            if (result != MOO_COMP_OK) return result;
            result = moo_comp_raster_blit(
                output, &core->buffers[buffer_index].view,
                surface->x, surface->y, surface->committed.scale,
                surface->committed.opacity, damage);
            if (result != MOO_COMP_OK) return result;
        }
        after = best_z;
        drawn++;
    }
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_draw_cursor(MooCompositor *core,
                                          const MooCompOutput *output,
                                          MooCompRect damage) {
    uint32_t index;
    int64_t dst_x;
    int64_t dst_y;
    if (!core->cursor.visible) return MOO_COMP_OK;
    if (moo_comp_buffer_slot(core, core->cursor.buffer, &index) != MOO_COMP_OK)
        return MOO_COMP_BAD_STATE;
    dst_x = (int64_t)core->cursor.x - core->cursor.hotspot_x;
    dst_y = (int64_t)core->cursor.y - core->cursor.hotspot_y;
    if (dst_x < INT32_MIN || dst_y < INT32_MIN)
        return MOO_COMP_OK;
    return moo_comp_raster_blit(output, &core->buffers[index].view,
                                (int32_t)dst_x, (int32_t)dst_y,
                                core->cursor.scale, 255u, damage);
}

MooCompResult moo_comp_build_frame(MooCompositor *core,
                                   const MooCompOutput *output,
                                   uint64_t *out_frame_id) {
    uint32_t i;
    uint64_t frame_id;
    MooCompResult result;
    if (!moo_comp_output_valid(core, output) || !out_frame_id)
        return MOO_COMP_INVALID;
    if (core->in_flight_frame != 0u) return MOO_COMP_WOULD_BLOCK;
    if (core->frame_sequence == UINT64_MAX) return MOO_COMP_LIMIT;
    frame_id = core->frame_sequence + 1u;
    for (i = 0u; i < core->output_damage_count; ++i) {
        result = moo_comp_raster_clear(output, core->output_damage[i],
                                       core->config.background_r,
                                       core->config.background_g,
                                       core->config.background_b,
                                       core->config.background_a);
        if (result != MOO_COMP_OK) return result;
        result = moo_comp_draw_surfaces(core, output, core->output_damage[i]);
        if (result != MOO_COMP_OK) return result;
        result = moo_comp_draw_cursor(core, output, core->output_damage[i]);
        if (result != MOO_COMP_OK) return result;
    }
    for (i = 0u; i < core->frame_capacity; ++i) {
        MooCompFrameSlot *frame = &core->frames[i];
        if (frame->slot_state == MOO_COMP_SLOT_LIVE &&
            frame->live && frame->state == MOO_COMP_FRAME_WAITING) {
            uint32_t surface_index;
            if (frame->completion_status == MOO_COMP_FRAME_CANCELLED) {
                frame->completion_status = MOO_COMP_FRAME_CANCELLED;
            } else if (moo_comp_surface_slot(core, frame->surface,
                                             &surface_index) != MOO_COMP_OK) {
                frame->completion_status = MOO_COMP_FRAME_CANCELLED;
            } else if (core->surfaces[surface_index].committed.commit_sequence !=
                       frame->commit_sequence) {
                frame->completion_status = MOO_COMP_FRAME_SUPERSEDED;
            } else if (!moo_comp_surface_visible(
                           core, &core->surfaces[surface_index])) {
                frame->completion_status = MOO_COMP_FRAME_OCCLUDED;
            } else {
                MooCompRect bounds = moo_comp_surface_bounds(
                    core, &core->surfaces[surface_index]);
                if (moo_comp_region_fully_covered(
                        core, &core->surfaces[surface_index],
                        bounds.x, bounds.y,
                        (int64_t)bounds.x + bounds.width,
                        (int64_t)bounds.y + bounds.height))
                    frame->completion_status = MOO_COMP_FRAME_OCCLUDED;
                else
                    frame->completion_status = MOO_COMP_FRAME_PRESENTED;
            }
            frame->frame_id = frame_id;
            frame->state = MOO_COMP_FRAME_ASSIGNED;
        }
    }
    core->frame_sequence = frame_id;
    core->in_flight_frame = frame_id;
    core->output_damage_count = 0u;
    *out_frame_id = frame_id;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_present_done_observer_bind(
    MooCompositor *core, uint64_t generation,
    MooCompPresentDoneObserverFn observer, void *user) {
    if (!moo_comp_core_valid(core) || generation == 0u || observer == NULL)
        return MOO_COMP_INVALID;
    if (core->in_flight_frame != 0u ||
        core->present_done_observer != NULL)
        return MOO_COMP_BAD_STATE;
    core->present_done_generation = generation;
    core->present_done_observer = observer;
    core->present_done_observer_user = user;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_present_done(MooCompositor *core,
                                    uint64_t frame_id,
                                    uint64_t present_sequence,
                                    uint64_t timestamp_ns) {
    uint32_t i;
    uint32_t needed = 0u;
    if (!moo_comp_core_valid(core) || frame_id == 0u)
        return MOO_COMP_INVALID;
    if (core->in_flight_frame != frame_id) return MOO_COMP_BAD_STATE;
    for (i = 0u; i < core->frame_capacity; ++i) {
        if (core->frames[i].slot_state == MOO_COMP_SLOT_LIVE &&
            core->frames[i].live &&
            core->frames[i].state == MOO_COMP_FRAME_ASSIGNED &&
            core->frames[i].frame_id == frame_id)
            needed++;
    }
    if (moo_comp_free_event_count(core) < needed)
        return MOO_COMP_WOULD_BLOCK;
    if (needed > 0u && core->event_sequence > UINT64_MAX - needed)
        return MOO_COMP_LIMIT;
    for (i = 0u; i < core->frame_capacity; ++i) {
        MooCompFrameSlot *frame = &core->frames[i];
        if (frame->slot_state == MOO_COMP_SLOT_LIVE &&
            frame->live && frame->state == MOO_COMP_FRAME_ASSIGNED &&
            frame->frame_id == frame_id) {
            MooCompEvent event;
            event.type = MOO_COMP_EVENT_FRAME_DONE;
            event.status = frame->completion_status;
            event.object = frame->surface;
            event.token = frame->token;
            event.present_sequence = present_sequence;
            event.timestamp_ns = timestamp_ns;
            /*
             * Convert the reserved frame slot into its reserved event slot:
             * dropping live first keeps events_owned+frames_owned constant.
             */
            {
                MooCompHandle owner = frame->owner;
                moo_comp_frame_slot_free(frame);
                (void)moo_comp_push_event(core, owner, &event);
            }
        }
    }
    core->in_flight_frame = 0u;
    if (core->present_done_observer != NULL)
        core->present_done_observer(core->present_done_observer_user,
            core->present_done_generation, frame_id,
            present_sequence, timestamp_ns);
    return MOO_COMP_OK;
}

MooCompResult moo_comp_next_event(MooCompositor *core,
                                  MooCompHandle client,
                                  MooCompEvent *out_event) {
    uint32_t i;
    uint32_t best = UINT32_MAX;
    uint64_t sequence = UINT64_MAX;
    MooCompResult result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    if (!out_event) return MOO_COMP_INVALID;
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].slot_state == MOO_COMP_SLOT_LIVE &&
            core->events[i].live && core->events[i].owner == client &&
            core->events[i].sequence < sequence) {
            best = i;
            sequence = core->events[i].sequence;
        }
    }
    if (best == UINT32_MAX) return MOO_COMP_WOULD_BLOCK;
    *out_event = core->events[best].event;
    moo_comp_event_slot_free(&core->events[best]);
    return MOO_COMP_OK;
}

MooCompResult moo_comp_dispatch_stub(MooCompositor *core,
                                     MooCompHandle client,
                                     const MooCompRequest *request) {
    MooCompResult result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    if (!request) return MOO_COMP_INVALID;
    if (request->header.version != MOO_COMP_PROTOCOL_VERSION)
        return MOO_COMP_VERSION;
    switch (request->header.opcode) {
        case MOO_COMP_REQUEST_CLIPBOARD_SET:
        case MOO_COMP_REQUEST_CLIPBOARD_OFFER:
        case MOO_COMP_REQUEST_CLIPBOARD_RECEIVE:
        case MOO_COMP_REQUEST_DND_BEGIN:
        case MOO_COMP_REQUEST_DND_ACCEPT:
        case MOO_COMP_REQUEST_DND_DROP:
        case MOO_COMP_REQUEST_DND_CANCEL:
            return MOO_COMP_UNSUPPORTED;
        default:
            return MOO_COMP_UNSUPPORTED;
    }
}

MooCompResult moo_comp_surface_info(const MooCompositor *core,
                                    MooCompHandle surface_handle,
                                    MooCompSurfaceInfo *out_info) {
    uint32_t index;
    MooCompRect bounds;
    MooCompResult result;
    if (!out_info) return MOO_COMP_INVALID;
    result = moo_comp_surface_slot(core, surface_handle, &index);
    if (result != MOO_COMP_OK) return result;
    bounds = moo_comp_surface_bounds(core, &core->surfaces[index]);
    out_info->owner = core->surfaces[index].owner;
    out_info->buffer = core->surfaces[index].committed.buffer;
    out_info->x = core->surfaces[index].x;
    out_info->y = core->surfaces[index].y;
    out_info->logical_width = bounds.width;
    out_info->logical_height = bounds.height;
    out_info->scale = core->surfaces[index].committed.scale;
    out_info->opacity = core->surfaces[index].committed.opacity;
    out_info->z_sequence = core->surfaces[index].z_sequence;
    out_info->commit_sequence =
        core->surfaces[index].committed.commit_sequence;
    out_info->mapped = core->surfaces[index].committed.buffer !=
                       MOO_COMP_HANDLE_INVALID;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_buffer_info(const MooCompositor *core,
                                   MooCompHandle buffer_handle,
                                   MooCompBufferInfo *out_info) {
    uint32_t index;
    MooCompResult result;
    if (!out_info) return MOO_COMP_INVALID;
    result = moo_comp_buffer_slot(core, buffer_handle, &index);
    if (result != MOO_COMP_OK) return result;
    out_info->owner = core->buffers[index].owner;
    out_info->view = core->buffers[index].view;
    out_info->ref_count = core->buffers[index].ref_count;
    return MOO_COMP_OK;
}

uint32_t moo_comp_damage_count(const MooCompositor *core) {
    return moo_comp_core_valid(core) ? core->output_damage_count : 0u;
}

MooCompResult moo_comp_damage_get(const MooCompositor *core,
                                  uint32_t index,
                                  MooCompRect *out_rect) {
    if (!moo_comp_core_valid(core) || !out_rect ||
        index >= core->output_damage_count)
        return MOO_COMP_INVALID;
    *out_rect = core->output_damage[index];
    return MOO_COMP_OK;
}

static uint64_t moo_comp_hash_mix(uint64_t hash, uint64_t value) {
    uint32_t i;
    for (i = 0u; i < 8u; ++i) {
        hash ^= (value >> (i * 8u)) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t moo_comp_hash_bytes(uint64_t hash,
                                    const uint8_t *bytes, size_t count) {
    size_t i;
    for (i = 0u; i < count; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t moo_comp_hash_rect(uint64_t hash, MooCompRect rect) {
    hash = moo_comp_hash_mix(hash, (uint32_t)rect.x);
    hash = moo_comp_hash_mix(hash, (uint32_t)rect.y);
    hash = moo_comp_hash_mix(hash, (uint32_t)rect.width);
    return moo_comp_hash_mix(hash, (uint32_t)rect.height);
}

uint64_t moo_comp_state_hash(const MooCompositor *core) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t i;
    if (!moo_comp_core_valid(core)) return 0u;

    hash = moo_comp_hash_mix(hash, core->client_capacity);
    hash = moo_comp_hash_mix(hash, core->surface_capacity);
    hash = moo_comp_hash_mix(hash, core->buffer_capacity);
    hash = moo_comp_hash_mix(hash, core->frame_capacity);
    hash = moo_comp_hash_mix(hash, core->event_capacity);
    hash = moo_comp_hash_mix(hash, (uint32_t)core->config.output_width);
    hash = moo_comp_hash_mix(hash, (uint32_t)core->config.output_height);
    hash = moo_comp_hash_mix(hash, core->config.background_r);
    hash = moo_comp_hash_mix(hash, core->config.background_g);
    hash = moo_comp_hash_mix(hash, core->config.background_b);
    hash = moo_comp_hash_mix(hash, core->config.background_a);
    hash = moo_comp_hash_mix(hash, core->output_damage_count);
    for (i = 0u; i < core->output_damage_count; ++i)
        hash = moo_comp_hash_rect(hash, core->output_damage[i]);
    hash = moo_comp_hash_mix(hash, core->z_sequence);
    hash = moo_comp_hash_mix(hash, core->commit_sequence);
    hash = moo_comp_hash_mix(hash, core->frame_sequence);
    hash = moo_comp_hash_mix(hash, core->event_sequence);
    hash = moo_comp_hash_mix(hash, core->in_flight_frame);

    for (i = 0u; i < core->client_capacity; ++i) {
        hash = moo_comp_hash_mix(hash, core->clients[i].live);
        hash = moo_comp_hash_mix(hash, core->clients[i].generation);
    }
    for (i = 0u; i < core->surface_capacity; ++i) {
        const MooCompSurfaceSlot *surface = &core->surfaces[i];
        uint32_t d;
        hash = moo_comp_hash_mix(hash, surface->live);
        hash = moo_comp_hash_mix(hash, surface->generation);
        hash = moo_comp_hash_mix(hash, surface->effect_bound);
        hash = moo_comp_hash_mix(hash, surface->reserved_effect);
        if (!surface->live) continue;
        hash = moo_comp_hash_mix(hash, surface->owner);
        hash = moo_comp_hash_mix(hash, (uint32_t)surface->x);
        hash = moo_comp_hash_mix(hash, (uint32_t)surface->y);
        hash = moo_comp_hash_mix(hash, surface->z_sequence);
        hash = moo_comp_hash_mix(hash, surface->committed.buffer);
        hash = moo_comp_hash_mix(hash, surface->committed.scale);
        hash = moo_comp_hash_mix(hash, surface->committed.opacity);
        hash = moo_comp_hash_mix(hash,
                                 surface->committed.effective_opaque);
        hash = moo_comp_hash_mix(hash,
                                 surface->committed.commit_sequence);
        hash = moo_comp_hash_mix(hash, surface->pending.dirty_mask);
        hash = moo_comp_hash_mix(hash, surface->pending.buffer);
        hash = moo_comp_hash_mix(hash, surface->pending.scale);
        hash = moo_comp_hash_mix(hash, surface->pending.opacity);
        hash = moo_comp_hash_mix(hash, surface->pending.frame_token);
        hash = moo_comp_hash_mix(hash, surface->pending.damage_count);
        for (d = 0u; d < surface->pending.damage_count; ++d)
            hash = moo_comp_hash_rect(hash, surface->pending.damage[d]);
    }
    for (i = 0u; i < core->buffer_capacity; ++i) {
        const MooCompBufferSlot *buffer = &core->buffers[i];
        size_t y;
        size_t active_row_bytes;
        hash = moo_comp_hash_mix(hash, buffer->live);
        hash = moo_comp_hash_mix(hash, buffer->generation);
        if (!buffer->live) continue;
        hash = moo_comp_hash_mix(hash, buffer->owner);
        hash = moo_comp_hash_mix(hash, buffer->ref_count);
        hash = moo_comp_hash_mix(hash, buffer->release_armed);
        hash = moo_comp_hash_mix(hash, buffer->view.buffer_bytes);
        hash = moo_comp_hash_mix(hash, buffer->view.stride);
        hash = moo_comp_hash_mix(hash, (uint32_t)buffer->view.width);
        hash = moo_comp_hash_mix(hash, (uint32_t)buffer->view.height);
        hash = moo_comp_hash_mix(hash, buffer->view.format);
        active_row_bytes = (size_t)buffer->view.width * 4u;
        for (y = 0u; y < (size_t)buffer->view.height; ++y) {
            hash = moo_comp_hash_bytes(
                hash, buffer->view.pixels + y * buffer->view.stride,
                active_row_bytes);
        }
    }
    for (i = 0u; i < core->frame_capacity; ++i) {
        const MooCompFrameSlot *frame = &core->frames[i];
        hash = moo_comp_hash_mix(hash, frame->slot_state);
        hash = moo_comp_hash_mix(hash, frame->live);
        hash = moo_comp_hash_mix(hash, frame->reserved_slot);
        if (frame->slot_state == MOO_COMP_SLOT_FREE) continue;
        hash = moo_comp_hash_mix(hash, frame->state);
        hash = moo_comp_hash_mix(hash, frame->owner);
        hash = moo_comp_hash_mix(hash, frame->surface);
        hash = moo_comp_hash_mix(hash, frame->token);
        hash = moo_comp_hash_mix(hash, frame->commit_sequence);
        hash = moo_comp_hash_mix(hash, frame->frame_id);
        hash = moo_comp_hash_mix(hash, frame->completion_status);
    }
    for (i = 0u; i < core->event_capacity; ++i) {
        const MooCompEventSlot *slot = &core->events[i];
        hash = moo_comp_hash_mix(hash, slot->slot_state);
        hash = moo_comp_hash_mix(hash, slot->live);
        hash = moo_comp_hash_mix(hash, slot->reserved);
        hash = moo_comp_hash_mix(hash, slot->reserved_slot);
        if (slot->slot_state == MOO_COMP_SLOT_FREE) continue;
        hash = moo_comp_hash_mix(hash, slot->owner);
        hash = moo_comp_hash_mix(hash, slot->sequence);
        hash = moo_comp_hash_mix(hash, slot->event.type);
        hash = moo_comp_hash_mix(hash, slot->event.status);
        hash = moo_comp_hash_mix(hash, slot->event.object);
        hash = moo_comp_hash_mix(hash, slot->event.token);
        hash = moo_comp_hash_mix(hash, slot->event.present_sequence);
        hash = moo_comp_hash_mix(hash, slot->event.timestamp_ns);
    }

    hash = moo_comp_hash_mix(hash, core->cursor.focus_owner);
    hash = moo_comp_hash_mix(hash, core->cursor.buffer);
    hash = moo_comp_hash_mix(hash, (uint32_t)core->cursor.x);
    hash = moo_comp_hash_mix(hash, (uint32_t)core->cursor.y);
    hash = moo_comp_hash_mix(hash, (uint32_t)core->cursor.hotspot_x);
    hash = moo_comp_hash_mix(hash, (uint32_t)core->cursor.hotspot_y);
    hash = moo_comp_hash_mix(hash, core->cursor.scale);
    hash = moo_comp_hash_mix(hash, core->cursor.visible);
    return hash;
}

static void moo_i1_disconnect_apply_unbound(MooCompositor *core,
                                             MooCompHandle client) {
    uint32_t client_index;
    uint32_t i;
    if (moo_comp_client_slot(core, client, &client_index) != MOO_COMP_OK)
        return;
    if (core->cursor.focus_owner == client) {
        int64_t x0, y0, x1, y1;
        if (core->cursor.visible &&
            moo_comp_cursor_bounds(core, &x0, &y0, &x1, &y1))
            moo_comp_damage_i64(core, x0, y0, x1, y1);
        core->cursor.focus_owner = MOO_COMP_HANDLE_INVALID;
        core->cursor.visible = 0u;
        core->cursor.buffer = MOO_COMP_HANDLE_INVALID;
    }
    for (i = 0u; i < core->surface_capacity; ++i) {
        MooCompSurfaceSlot *surface = &core->surfaces[i];
        if (surface->live && surface->owner == client) {
            MooCompRect bounds = moo_comp_surface_bounds(core, surface);
            moo_comp_damage_rect(core, bounds);
            surface->live = 0u;
            surface->owner = MOO_COMP_HANDLE_INVALID;
            surface->committed.buffer = MOO_COMP_HANDLE_INVALID;
            surface->pending.dirty_mask = 0u;
            surface->pending.damage_count = 0u;
            surface->pending.frame_token = 0u;
            surface->effect_bound = 0u;
            surface->reserved_effect = 0u;
            surface->generation =
                moo_comp_next_generation(surface->generation);
        }
    }
    for (i = 0u; i < core->frame_capacity; ++i)
        if (core->frames[i].owner == client)
            moo_comp_frame_slot_free(&core->frames[i]);
    for (i = 0u; i < core->event_capacity; ++i)
        if (core->events[i].owner == client)
            moo_comp_event_slot_free(&core->events[i]);
    for (i = 0u; i < core->buffer_capacity; ++i) {
        MooCompBufferSlot *buffer = &core->buffers[i];
        if (buffer->live && buffer->owner == client) {
            buffer->live = 0u;
            buffer->owner = MOO_COMP_HANDLE_INVALID;
            buffer->view.pixels = NULL;
            buffer->view.buffer_bytes = 0u;
            buffer->view.stride = 0u;
            buffer->view.width = 0;
            buffer->view.height = 0;
            buffer->view.format = MOO_COMP_FORMAT_INVALID;
            buffer->ref_count = 0u;
            buffer->release_armed = 0u;
            buffer->generation =
                moo_comp_next_generation(buffer->generation);
        }
    }
    core->clients[client_index].live = 0u;
    core->clients[client_index].generation =
        moo_comp_next_generation(core->clients[client_index].generation);
}

MooCompResult moo_i1_client_disconnect_ex_obsolete(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, uint64_t timestamp_ns,
    MooCompEffectTransactionWorkspace *workspace,
    uint32_t *out_completion_count) {
    uint32_t i;
    uint32_t needed = 0u;
    uint32_t written = 0u;
    MooCompResult result;
    if (!core || !integration || !workspace || !out_completion_count ||
        integration->core != core ||
        integration->version != MOO_COMP_EFFECT_INTEGRATION_VERSION)
        return MOO_COMP_INVALID;
    result = moo_comp_client_slot(core, client, 0);
    if (result != MOO_COMP_OK) return result;
    if (!workspace->completions || !workspace->timeline_slots ||
        workspace->timeline_capacity != integration->timeline->capacity ||
        !workspace->reservation_clones ||
        workspace->reservation_capacity !=
            integration->reservation_capacity)
        return MOO_COMP_LIMIT;
    for (i = 0u; i < integration->timeline->capacity; ++i) {
        const MooCompAnimationSlot *slot =
            &integration->timeline->slots[i];
        uint32_t surface_index;
        if (slot->active &&
            moo_comp_surface_slot(core, slot->surface,
                                  &surface_index) == MOO_COMP_OK &&
            core->surfaces[surface_index].owner == client)
            needed++;
    }
    if (needed > workspace->completion_capacity)
        return MOO_COMP_LIMIT;
    if ((uint64_t)needed > UINT64_MAX - core->event_sequence)
        return MOO_COMP_LIMIT;
    for (i = 0u; i < needed; ++i) {
        if (integration->reservations[i].slot_state !=
                MOO_COMP_SLOT_RESERVED)
            return MOO_COMP_BAD_STATE;
    }
    for (i = 0u; i < integration->timeline->capacity; ++i) {
        MooCompAnimationSlot *slot = &integration->timeline->slots[i];
        uint32_t surface_index;
        if (slot->active &&
            moo_comp_surface_slot(core, slot->surface,
                                  &surface_index) == MOO_COMP_OK &&
            core->surfaces[surface_index].owner == client) {
            uint32_t j;
            workspace->completions[written].surface = slot->surface;
            workspace->completions[written].token = slot->desc.token;
            workspace->completions[written].status =
                MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED;
            workspace->completions[written].reserved = 0u;
            workspace->completions[written].timestamp_ns = timestamp_ns;
            for (j = 0u; j < integration->reservation_capacity; ++j) {
                MooCompEffectCompletionReservation *reservation =
                    &integration->reservations[j];
                if (reservation->slot_state ==
                        MOO_COMP_SLOT_RESERVED &&
                    reservation->surface == slot->surface &&
                    reservation->token == slot->desc.token) {
                    MooCompEventSlot *event =
                        &core->events[reservation->event_index];
                    core->event_sequence++;
                    event->slot_state = MOO_COMP_SLOT_LIVE;
                    event->live = 1u;
                    event->reserved = 0u;
                    event->reserved_slot = 0u;
                    event->sequence = core->event_sequence;
                    event->event.type =
                        MOO_COMP_EVENT_ANIMATION_DONE;
                    event->event.status =
                        MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED;
                    event->event.object = slot->surface;
                    event->event.token = slot->desc.token;
                    event->event.present_sequence =
                        core->commit_sequence;
                    event->event.timestamp_ns = timestamp_ns;
                    *reservation =
                        (MooCompEffectCompletionReservation){0};
                    reservation->event_index = UINT32_MAX;
                    reservation->owner = MOO_COMP_HANDLE_INVALID;
                    reservation->surface = MOO_COMP_HANDLE_INVALID;
                    break;
                }
            }
            slot->active = 0u;
            integration->timeline->active_count--;
            written++;
        }
    }
    for (i = 0u; i < integration->binding_capacity; ++i) {
        if (integration->bindings[i].active &&
            integration->bindings[i].owner == client) {
            integration->bindings[i].active = 0u;
            integration->bindings[i].effect_dirty = 0u;
            integration->bindings[i].surface =
                MOO_COMP_HANDLE_INVALID;
            integration->bindings[i].owner =
                MOO_COMP_HANDLE_INVALID;
            integration->bindings[i].reserved_completion_events = 0u;
        }
    }
    moo_i1_disconnect_apply_unbound(core, client);
    *out_completion_count = written;
    return MOO_COMP_OK;
}

static int moo_i1_reservation_graph_valid(
 const MooCompositor *core,
 const MooCompEffectIntegration *integration) {
    uint32_t i, active_count = 0u;
    for (i = 0u; i < integration->timeline->capacity; ++i) {
        const MooCompAnimationSlot *slot = &integration->timeline->slots[i];
        const MooCompEffectSurfaceBinding *binding;
        uint32_t surface_index, j, matches = 0u;
        if (!slot->active) continue;
        active_count++;
        if (slot->reserved != 0u || slot->desc.token == 0u ||
            moo_comp_surface_slot(core, slot->surface, &surface_index) !=
                MOO_COMP_OK)
            return 0;
        binding = &integration->bindings[surface_index];
        if (!binding->active || binding->surface != slot->surface ||
            binding->owner != core->surfaces[surface_index].owner)
            return 0;
        for (j = 0u; j < integration->reservation_capacity; ++j) {
            const MooCompEffectCompletionReservation *reservation =
                &integration->reservations[j];
            if (reservation->slot_state == MOO_COMP_SLOT_RESERVED &&
                reservation->owner == binding->owner &&
                reservation->surface == slot->surface &&
                reservation->token == slot->desc.token)
                matches++;
        }
        if (matches != 1u) return 0;
    }
    if (active_count != integration->timeline->active_count) return 0;

    for (i = 0u; i < integration->reservation_capacity; ++i) {
        const MooCompEffectCompletionReservation *reservation =
            &integration->reservations[i];
        uint32_t surface_index, j, matches = 0u;
        if (reservation->slot_state == MOO_COMP_SLOT_FREE) {
            if (reservation->event_index != UINT32_MAX ||
                reservation->owner != MOO_COMP_HANDLE_INVALID ||
                reservation->surface != MOO_COMP_HANDLE_INVALID ||
                reservation->token != 0u)
                return 0;
            continue;
        }
        if (reservation->slot_state != MOO_COMP_SLOT_RESERVED ||
            moo_comp_surface_slot(core, reservation->surface,
                                  &surface_index) != MOO_COMP_OK ||
            core->surfaces[surface_index].owner != reservation->owner ||
            !integration->bindings[surface_index].active ||
            integration->bindings[surface_index].surface !=
                reservation->surface ||
            integration->bindings[surface_index].owner != reservation->owner)
            return 0;
        for (j = 0u; j < integration->timeline->capacity; ++j) {
            const MooCompAnimationSlot *slot =
                &integration->timeline->slots[j];
            if (slot->active && slot->surface == reservation->surface &&
                slot->desc.token == reservation->token)
                matches++;
        }
        if (matches != 1u || reservation->event_index >= core->event_capacity ||
            core->events[reservation->event_index].slot_state !=
                MOO_COMP_SLOT_RESERVED ||
            core->events[reservation->event_index].live != 0u ||
            core->events[reservation->event_index].reserved != 1u ||
            core->events[reservation->event_index].reserved_slot != i + 1u ||
            core->events[reservation->event_index].owner != reservation->owner ||
            core->events[reservation->event_index].sequence != 0u)
            return 0;
    }

    for (i = 0u; i < integration->binding_capacity; ++i) {
        const MooCompEffectSurfaceBinding *binding =
            &integration->bindings[i];
        uint32_t j, reserved_count = 0u;
        if (binding->active) {
            MooCompHandle surface;
            if (!core->surfaces[i].live || !core->surfaces[i].effect_bound)
                return 0;
            surface = moo_comp_make_handle(MOO_COMP_KIND_SURFACE, i,
                                            core->surfaces[i].generation);
            if (binding->surface != surface ||
                binding->owner != core->surfaces[i].owner)
                return 0;
        }
        for (j = 0u; j < integration->reservation_capacity; ++j)
            if (integration->reservations[j].slot_state ==
                    MOO_COMP_SLOT_RESERVED &&
                integration->reservations[j].surface == binding->surface &&
                integration->reservations[j].owner == binding->owner)
                reserved_count++;
        if (reserved_count != binding->reserved_completion_events)
            return 0;
    }
    return 1;
}

static int moo_i1_integration_valid(
 const MooCompositor *core,
 const MooCompEffectIntegration *integration) {
    if (!core || !integration || core->effect_integration != integration ||
        integration->version != MOO_COMP_EFFECT_INTEGRATION_VERSION ||
        integration->reserved != 0u || integration->reserved2 != 0u ||
        integration->reserved3 != 0u || integration->core != core ||
        !integration->bindings ||
        integration->binding_capacity != core->surface_capacity ||
        !integration->timeline || !integration->timeline->slots ||
        integration->timeline->capacity == 0u ||
        integration->timeline->active_count > integration->timeline->capacity ||
        !integration->reservations ||
        integration->reservation_capacity < core->event_capacity ||
        !integration->disconnect_workspace)
        return 0;
    return moo_comp_core_valid(core) &&
        moo_i1_reservation_graph_valid(core, integration);
}

static int moo_i1_workspace_exact(const MooCompositor *core,
 const MooCompEffectIntegration *integration,
 const MooCompEffectTransactionWorkspace *workspace) {
    return moo_i1_integration_valid(core, integration) && workspace &&
      workspace->prepared && workspace->surface_slots &&
      workspace->surface_capacity==core->surface_capacity &&
      workspace->buffer_slots && workspace->buffer_capacity==core->buffer_capacity &&
      workspace->frame_slots && workspace->frame_capacity==core->frame_capacity &&
      workspace->event_slots && workspace->event_capacity==core->event_capacity &&
      workspace->timeline_slots &&
      workspace->timeline_capacity==integration->timeline->capacity &&
      workspace->completions && workspace->reservation_clones &&
      workspace->reservation_capacity==integration->reservation_capacity;
}
static void moo_i1_clone_core2(const MooCompositor *core,
 MooCompEffectTransactionWorkspace *workspace,MooCompositor *clone) {
    uint32_t i;*clone=*core;
    for(i=0;i<core->surface_capacity;i++)workspace->surface_slots[i]=core->surfaces[i];
    for(i=0;i<core->buffer_capacity;i++)workspace->buffer_slots[i]=core->buffers[i];
    for(i=0;i<core->frame_capacity;i++)workspace->frame_slots[i]=core->frames[i];
    for(i=0;i<core->event_capacity;i++)workspace->event_slots[i]=core->events[i];
    clone->surfaces=workspace->surface_slots;clone->buffers=workspace->buffer_slots;
    clone->frames=workspace->frame_slots;clone->events=workspace->event_slots;
}
static void moo_i1_apply_core2(MooCompositor *core,const MooCompositor *clone) {
    MooCompClientSlot *clients=core->clients;MooCompSurfaceSlot *surfaces=core->surfaces;
    MooCompBufferSlot *buffers=core->buffers;MooCompFrameSlot *frames=core->frames;
    MooCompEventSlot *events=core->events;MooCompEffectIntegration *integration=core->effect_integration;
    uint32_t i;
    for(i=0;i<core->surface_capacity;i++)surfaces[i]=clone->surfaces[i];
    for(i=0;i<core->buffer_capacity;i++)buffers[i]=clone->buffers[i];
    for(i=0;i<core->frame_capacity;i++)frames[i]=clone->frames[i];
    for(i=0;i<core->event_capacity;i++)events[i]=clone->events[i];
    *core=*clone;core->clients=clients;core->surfaces=surfaces;core->buffers=buffers;
    core->frames=frames;core->events=events;core->effect_integration=integration;
}
static void moo_i1_clone_timeline2(const MooCompAnimationTimeline *timeline,
 MooCompAnimationSlot *slots,MooCompAnimationTimeline *clone) {
    uint32_t i;*clone=*timeline;for(i=0;i<timeline->capacity;i++)slots[i]=timeline->slots[i];
    clone->slots=slots;
}
static void moo_i1_apply_timeline2(MooCompAnimationTimeline *timeline,
 const MooCompAnimationTimeline *clone) {
    MooCompAnimationSlot *slots=timeline->slots;uint32_t i;
    for (i = 0u; i < timeline->capacity; ++i)
        slots[i] = clone->slots[i];
    *timeline = *clone;
    timeline->slots = slots;
}
static MooCompResult moo_i1_owned2(MooCompositor *core,MooCompEffectIntegration *integration,
 MooCompHandle owner,MooCompHandle surface,uint32_t *out_index) {
    uint32_t index;MooCompResult result;
    if (surface == MOO_COMP_HANDLE_INVALID) return MOO_COMP_INVALID;
    result=moo_comp_owned_surface(core,owner,surface,&index);
    if(result!=MOO_COMP_OK)return result;
    if (!moo_i1_integration_valid(core, integration))
        return MOO_COMP_BAD_STATE;
    *out_index=index;return MOO_COMP_OK;
}
static MooCompResult moo_i1_reserve2(MooCompositor *core,
 MooCompEffectIntegration *integration,MooCompEffectCompletionReservation *reservations,
 MooCompHandle owner,MooCompHandle surface,uint64_t token) {
    uint32_t event_index = UINT32_MAX, reservation_index = UINT32_MAX;
    uint32_t i;
    if (token == 0u) return MOO_COMP_INVALID;
    if (!moo_comp_event_reservation_available(core, owner))
        return MOO_COMP_LIMIT;
    for (i = 0u; i < core->event_capacity; ++i)
        if (core->events[i].slot_state == MOO_COMP_SLOT_FREE) {
            event_index = i;
            break;
        }
    for (i = 0u; i < integration->reservation_capacity; ++i) {
        if (reservations[i].slot_state == MOO_COMP_SLOT_RESERVED &&
            reservations[i].owner == owner &&
            reservations[i].token == token)
            return MOO_COMP_INVALID;
        if (reservation_index == UINT32_MAX &&
            reservations[i].slot_state == MOO_COMP_SLOT_FREE)
            reservation_index = i;
    }
    if (event_index == UINT32_MAX || reservation_index == UINT32_MAX)
        return MOO_COMP_LIMIT;
    core->events[event_index].slot_state = MOO_COMP_SLOT_RESERVED;
    core->events[event_index].live = 0u;
    core->events[event_index].reserved = 1u;
    core->events[event_index].reserved_slot = reservation_index + 1u;
    core->events[event_index].owner = owner;
    core->events[event_index].sequence = 0u;
    core->events[event_index].event = (MooCompEvent){0};
    reservations[reservation_index] = (MooCompEffectCompletionReservation){
        MOO_COMP_SLOT_RESERVED, event_index, owner, surface, token};
    return MOO_COMP_OK;
}
static MooCompResult moo_i1_complete2(MooCompositor *core,
 MooCompEffectCompletionReservation *reservations,uint32_t capacity,
 const MooCompAnimationCompletion *completion) {
    MooCompEventSlot *event;
    uint32_t surface_index, reservation_index = UINT32_MAX;
    uint32_t matches = 0u, i;
    int decoded;
    if (!core || !reservations || !completion ||
        completion->surface == MOO_COMP_HANDLE_INVALID ||
        completion->token == 0u)
        return MOO_COMP_INVALID;
    decoded = moo_comp_handle_index(
        completion->surface, MOO_COMP_KIND_SURFACE,
        core->surface_capacity, &surface_index);
    if (decoded != 1 || !core->surfaces[surface_index].live ||
        core->surfaces[surface_index].generation !=
            (uint32_t)(completion->surface >> 32u))
        return MOO_COMP_BAD_STATE;
    for (i = 0u; i < capacity; ++i)
        if (reservations[i].slot_state == MOO_COMP_SLOT_RESERVED &&
            reservations[i].surface == completion->surface &&
            reservations[i].token == completion->token) {
            reservation_index = i;
            matches++;
        }
    if (matches != 1u) return MOO_COMP_BAD_STATE;
    if (reservations[reservation_index].owner !=
            core->surfaces[surface_index].owner ||
        reservations[reservation_index].event_index >= core->event_capacity)
        return MOO_COMP_BAD_STATE;
    event = &core->events[reservations[reservation_index].event_index];
    if (event->slot_state != MOO_COMP_SLOT_RESERVED || event->live != 0u ||
        event->reserved != 1u ||
        event->reserved_slot != reservation_index + 1u ||
        event->owner != reservations[reservation_index].owner ||
        event->sequence != 0u)
        return MOO_COMP_BAD_STATE;
    if (core->event_sequence == UINT64_MAX) return MOO_COMP_LIMIT;
    core->event_sequence++;
    event->sequence = core->event_sequence;
    event->event = (MooCompEvent){
        MOO_COMP_EVENT_ANIMATION_DONE, completion->status,
        completion->surface, completion->token,
        core->surfaces[surface_index].committed.commit_sequence,
        completion->timestamp_ns};
    moo_comp_event_slot_live(event);
    reservations[reservation_index] =
        (MooCompEffectCompletionReservation){0};
    reservations[reservation_index].event_index = UINT32_MAX;
    reservations[reservation_index].owner = MOO_COMP_HANDLE_INVALID;
    reservations[reservation_index].surface = MOO_COMP_HANDLE_INVALID;
    return MOO_COMP_OK;
}
MooCompResult moo_comp_effects_bind(MooCompositor *core,MooCompEffectIntegration *integration,
 const MooCompEffectStateConfig *config,MooCompEffectSurfaceBinding *bindings,
 uint32_t binding_capacity,MooCompAnimationTimeline *timeline,
 MooCompGpuBackendState *gpu_backend,MooCompEffectCompletionReservation *reservations,
 uint32_t reservation_capacity,MooCompEffectTransactionWorkspace *disconnect_workspace) {
    MooCompEffectStateConfig validated_config;
    MooCompEffectIntegration candidate;
    MooCompEffectSurfaceState probe;
    MooCompEffectState neutral;
    uint32_t i;
    MooCompResult result;
    if (!moo_comp_core_valid(core) || !integration || !config || !bindings ||
        binding_capacity != core->surface_capacity || !timeline ||
        !timeline->slots || timeline->capacity == 0u || !reservations ||
        reservation_capacity < core->event_capacity ||
        !disconnect_workspace || core->effect_integration)
        return MOO_COMP_INVALID;
    result = moo_comp_effect_state_config_init(
        &validated_config, config->capabilities, &config->limits);
    if (result != MOO_COMP_OK) return result;
    if (timeline->active_count != 0u || timeline->reserved != 0u)
        return MOO_COMP_BAD_STATE;
    for (i = 0u; i < timeline->capacity; ++i)
        if (timeline->slots[i].active || timeline->slots[i].reserved)
            return MOO_COMP_BAD_STATE;
    for (i = 0u; i < core->event_capacity; ++i)
        if (core->events[i].slot_state == MOO_COMP_SLOT_RESERVED)
            return MOO_COMP_BAD_STATE;
    if (!disconnect_workspace->prepared ||
        !disconnect_workspace->surface_slots ||
        disconnect_workspace->surface_capacity != core->surface_capacity ||
        !disconnect_workspace->buffer_slots ||
        disconnect_workspace->buffer_capacity != core->buffer_capacity ||
        !disconnect_workspace->frame_slots ||
        disconnect_workspace->frame_capacity != core->frame_capacity ||
        !disconnect_workspace->event_slots ||
        disconnect_workspace->event_capacity != core->event_capacity ||
        !disconnect_workspace->timeline_slots ||
        disconnect_workspace->timeline_capacity != timeline->capacity ||
        !disconnect_workspace->completions ||
        !disconnect_workspace->reservation_clones ||
        disconnect_workspace->reservation_capacity != reservation_capacity)
        return MOO_COMP_INVALID;
    for (i = 0u; i < binding_capacity; ++i) {
        if (!core->surfaces[i].live) continue;
        result = moo_comp_effect_surface_init(
            &probe, core->surfaces[i].owner,
            moo_comp_make_handle(MOO_COMP_KIND_SURFACE, i,
                                 core->surfaces[i].generation));
        if (result != MOO_COMP_OK) return MOO_COMP_BAD_STATE;
    }

    candidate = (MooCompEffectIntegration){0};
    candidate.version = MOO_COMP_EFFECT_INTEGRATION_VERSION;
    candidate.core = core;
    candidate.config = validated_config;
    candidate.bindings = bindings;
    candidate.binding_capacity = binding_capacity;
    candidate.timeline = timeline;
    candidate.gpu_backend = gpu_backend;
    candidate.reservations = reservations;
    candidate.reservation_capacity = reservation_capacity;
    candidate.disconnect_workspace = disconnect_workspace;
    neutral = moo_comp_effect_state_neutral();
    for (i = 0u; i < binding_capacity; ++i) {
        bindings[i] = (MooCompEffectSurfaceBinding){0};
        bindings[i].surface = MOO_COMP_HANDLE_INVALID;
        bindings[i].owner = MOO_COMP_HANDLE_INVALID;
        bindings[i].evaluated_opacity = 255u;
        bindings[i].pending_effect = neutral;
        bindings[i].evaluated = neutral;
        if (core->surfaces[i].live) {
            MooCompHandle surface = moo_comp_make_handle(
                MOO_COMP_KIND_SURFACE, i, core->surfaces[i].generation);
            (void)moo_comp_effect_surface_init(
                &bindings[i].state, core->surfaces[i].owner, surface);
            bindings[i].active = 1u;
            bindings[i].surface = surface;
            bindings[i].owner = core->surfaces[i].owner;
            bindings[i].evaluated_opacity =
                core->surfaces[i].committed.opacity;
            bindings[i].state.commit_sequence =
                core->surfaces[i].committed.commit_sequence;
        }
    }
    for (i = 0u; i < reservation_capacity; ++i) {
        reservations[i] = (MooCompEffectCompletionReservation){0};
        reservations[i].event_index = UINT32_MAX;
        reservations[i].owner = MOO_COMP_HANDLE_INVALID;
        reservations[i].surface = MOO_COMP_HANDLE_INVALID;
    }
    for (i = 0u; i < binding_capacity; ++i)
        if (bindings[i].active) core->surfaces[i].effect_bound = 1u;
    *integration = candidate;
    core->effect_integration = integration;
    return MOO_COMP_OK;
}
MooCompResult moo_comp_surface_effect_set(MooCompositor *core,
 MooCompEffectIntegration *integration,MooCompHandle owner,MooCompHandle surface,
 const MooCompEffectState *requested) {
    MooCompEffectSurfaceBinding candidate;
    MooCompEffectPreflight preflight;
    MooCompEffectUsage usage;
    uint32_t index, i;
    MooCompResult result;
    if (!requested) return MOO_COMP_INVALID;
    result = moo_i1_owned2(core, integration, owner, surface, &index);
    if (result != MOO_COMP_OK) return result;
    if (!integration->bindings[index].active ||
        integration->bindings[index].surface != surface ||
        integration->bindings[index].owner != owner ||
        integration->bindings[index].state.active != 1u ||
        integration->bindings[index].state.surface != surface ||
        integration->bindings[index].state.owner != owner)
        return MOO_COMP_BAD_STATE;
    usage = (MooCompEffectUsage){0u, 0u,
        (uint64_t)(uint32_t)core->config.output_width *
        (uint64_t)(uint32_t)core->config.output_height};
    for (i = 0u; i < integration->timeline->capacity; ++i) {
        const MooCompAnimationSlot *slot = &integration->timeline->slots[i];
        uint32_t animated_index;
        if (!slot->active) continue;
        if (slot->surface == surface) usage.animations_on_surface++;
        result = moo_comp_surface_slot(core, slot->surface, &animated_index);
        if (result != MOO_COMP_OK) return MOO_COMP_BAD_STATE;
        if (core->surfaces[animated_index].owner == owner)
            usage.animations_for_client++;
    }
    result = moo_comp_effect_state_preflight(
        &integration->config, &integration->bindings[index].state,
        owner, surface, requested, &usage, &preflight);
    if (result != MOO_COMP_OK) return result;
    candidate = integration->bindings[index];
    candidate.pending_effect = *requested;
    candidate.effect_dirty = 1u;
    integration->bindings[index] = candidate;
    return MOO_COMP_OK;
}
static MooCompResult moo_i1_prepared_buffer_index(
 const MooCompositor *core, MooCompHandle handle, uint32_t *out_index) {
    uint32_t index;
    int decoded;
    if (!core || !out_index || handle == MOO_COMP_HANDLE_INVALID)
        return MOO_COMP_INVALID;
    decoded = moo_comp_handle_index(
        handle, MOO_COMP_KIND_BUFFER, core->buffer_capacity, &index);
    if (decoded != 1 || !core->buffers[index].live ||
        core->buffers[index].generation != (uint32_t)(handle >> 32u))
        return MOO_COMP_BAD_STATE;
    *out_index = index;
    return MOO_COMP_OK;
}

static MooCompResult moo_i1_prepare_surface_buffers(
 const MooCompositor *before, const MooCompositor *after,
 uint32_t surface_index, MooCompEffectPreparedCore *prepared) {
    MooCompHandle old_buffer, new_buffer;
    uint32_t i;
    MooCompResult result;
    if (!before || !after || !prepared ||
        surface_index >= before->surface_capacity ||
        surface_index >= after->surface_capacity ||
        before->output_damage_count > MOO_COMP_OUTPUT_DAMAGE_CAPACITY ||
        after->output_damage_count > MOO_COMP_OUTPUT_DAMAGE_CAPACITY)
        return MOO_COMP_BAD_STATE;
    *prepared = (MooCompEffectPreparedCore){0};
    prepared->surface_index = surface_index;
    prepared->old_buffer_index = UINT32_MAX;
    prepared->new_buffer_index = UINT32_MAX;
    prepared->frame_index = UINT32_MAX;
    prepared->event_index = UINT32_MAX;
    prepared->surface_before = before->surfaces[surface_index];
    prepared->surface_after = after->surfaces[surface_index];
    old_buffer = prepared->surface_before.committed.buffer;
    new_buffer = prepared->surface_after.committed.buffer;
    if (old_buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_i1_prepared_buffer_index(
            before, old_buffer, &prepared->old_buffer_index);
        if (result != MOO_COMP_OK) return result;
        prepared->old_buffer_before =
            before->buffers[prepared->old_buffer_index];
        prepared->old_buffer_after =
            after->buffers[prepared->old_buffer_index];
    }
    if (new_buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_i1_prepared_buffer_index(
            after, new_buffer, &prepared->new_buffer_index);
        if (result != MOO_COMP_OK) return result;
        prepared->new_buffer_before =
            before->buffers[prepared->new_buffer_index];
        prepared->new_buffer_after =
            after->buffers[prepared->new_buffer_index];
    }
    prepared->output_damage_count_before = before->output_damage_count;
    prepared->output_damage_count_after = after->output_damage_count;
    for (i = 0u; i < MOO_COMP_OUTPUT_DAMAGE_CAPACITY; ++i) {
        prepared->output_damage_before[i] = before->output_damage[i];
        prepared->output_damage_after[i] = after->output_damage[i];
    }
    prepared->commit_sequence_before = before->commit_sequence;
    prepared->commit_sequence_after = after->commit_sequence;
    prepared->z_sequence_before = before->z_sequence;
    prepared->z_sequence_after = after->z_sequence;
    prepared->event_sequence_before = before->event_sequence;
    prepared->event_sequence_after = after->event_sequence;
    prepared->valid = 1u;
    return MOO_COMP_OK;
}

static int moo_i1_frame_equal(
 const MooCompFrameSlot *a, const MooCompFrameSlot *b) {
    return a->slot_state == b->slot_state && a->live == b->live &&
        a->state == b->state && a->reserved_slot == b->reserved_slot &&
        a->owner == b->owner && a->surface == b->surface &&
        a->token == b->token && a->commit_sequence == b->commit_sequence &&
        a->frame_id == b->frame_id &&
        a->completion_status == b->completion_status &&
        a->reserved == b->reserved;
}

static int moo_i1_event_equal(
 const MooCompEventSlot *a, const MooCompEventSlot *b) {
    return a->slot_state == b->slot_state && a->live == b->live &&
        a->reserved == b->reserved &&
        a->reserved_slot == b->reserved_slot && a->owner == b->owner &&
        a->sequence == b->sequence && a->event.type == b->event.type &&
        a->event.status == b->event.status &&
        a->event.object == b->event.object &&
        a->event.token == b->event.token &&
        a->event.present_sequence == b->event.present_sequence &&
        a->event.timestamp_ns == b->event.timestamp_ns;
}

static MooCompResult moo_i1_prepare_frame_event(
 const MooCompositor *before, const MooCompositor *after,
 MooCompEffectPreparedCore *prepared) {
    uint32_t i;
    if (!before || !after || !prepared || !prepared->valid ||
        before->frame_capacity != after->frame_capacity ||
        before->event_capacity != after->event_capacity)
        return MOO_COMP_BAD_STATE;
    for (i = 0u; i < before->frame_capacity; ++i) {
        if (moo_i1_frame_equal(&before->frames[i], &after->frames[i]))
            continue;
        if (prepared->frame_index != UINT32_MAX)
            return MOO_COMP_BAD_STATE;
        prepared->frame_index = i;
        prepared->frame_before = before->frames[i];
        prepared->frame_after = after->frames[i];
    }
    for (i = 0u; i < before->event_capacity; ++i) {
        if (moo_i1_event_equal(&before->events[i], &after->events[i]))
            continue;
        if (prepared->event_index != UINT32_MAX)
            return MOO_COMP_BAD_STATE;
        prepared->event_index = i;
        prepared->event_before = before->events[i];
        prepared->event_after = after->events[i];
    }
    return MOO_COMP_OK;
}

static void moo_i1_apply_prepared_core(
 MooCompositor *core, const MooCompEffectPreparedCore *prepared) {
    uint32_t i;
    core->surfaces[prepared->surface_index] = prepared->surface_after;
    if (prepared->old_buffer_index != UINT32_MAX)
        core->buffers[prepared->old_buffer_index] =
            prepared->old_buffer_after;
    if (prepared->new_buffer_index != UINT32_MAX)
        core->buffers[prepared->new_buffer_index] =
            prepared->new_buffer_after;
    if (prepared->frame_index != UINT32_MAX)
        core->frames[prepared->frame_index] = prepared->frame_after;
    if (prepared->event_index != UINT32_MAX)
        core->events[prepared->event_index] = prepared->event_after;
    for (i = 0u; i < MOO_COMP_OUTPUT_DAMAGE_CAPACITY; ++i)
        core->output_damage[i] = prepared->output_damage_after[i];
    core->output_damage_count = prepared->output_damage_count_after;
    core->commit_sequence = prepared->commit_sequence_after;
    core->z_sequence = prepared->z_sequence_after;
    core->event_sequence = prepared->event_sequence_after;
}

static int moo_i1_effect_state_equal(
 const MooCompEffectState *a, const MooCompEffectState *b) {
    return a->enabled_mask == b->enabled_mask &&
        a->required_mask == b->required_mask &&
        a->fallback_policy == b->fallback_policy &&
        a->reserved == b->reserved &&
        a->corners.top_left == b->corners.top_left &&
        a->corners.top_right == b->corners.top_right &&
        a->corners.bottom_right == b->corners.bottom_right &&
        a->corners.bottom_left == b->corners.bottom_left &&
        a->shadow.offset_x == b->shadow.offset_x &&
        a->shadow.offset_y == b->shadow.offset_y &&
        a->shadow.blur_radius == b->shadow.blur_radius &&
        a->shadow.spread_radius == b->shadow.spread_radius &&
        a->shadow.color.r == b->shadow.color.r &&
        a->shadow.color.g == b->shadow.color.g &&
        a->shadow.color.b == b->shadow.color.b &&
        a->shadow.color.a == b->shadow.color.a &&
        a->backdrop.blur_radius == b->backdrop.blur_radius &&
        a->backdrop.saturation_q8_8 == b->backdrop.saturation_q8_8 &&
        a->backdrop.tint.r == b->backdrop.tint.r &&
        a->backdrop.tint.g == b->backdrop.tint.g &&
        a->backdrop.tint.b == b->backdrop.tint.b &&
        a->backdrop.tint.a == b->backdrop.tint.a &&
        a->backdrop.tint_mix == b->backdrop.tint_mix &&
        a->backdrop.noise == b->backdrop.noise &&
        a->backdrop.reserved == b->backdrop.reserved &&
        a->backdrop.noise_seed == b->backdrop.noise_seed &&
        a->affine.m11 == b->affine.m11 &&
        a->affine.m12 == b->affine.m12 &&
        a->affine.m21 == b->affine.m21 &&
        a->affine.m22 == b->affine.m22 &&
        a->affine.tx == b->affine.tx &&
        a->affine.ty == b->affine.ty &&
        a->affine.origin_x == b->affine.origin_x &&
        a->affine.origin_y == b->affine.origin_y;
}

MooCompResult moo_comp_surface_commit_ex(MooCompositor *core,
 MooCompEffectIntegration *integration,MooCompHandle owner,MooCompHandle surface,
 const MooCompEffectCommitRequest *request,MooCompEffectTransactionWorkspace *workspace) {
    MooCompositor clone;
    MooCompEffectSurfaceBinding binding;
    MooCompEffectPreparedCore prepared;
    MooCompEffectPreflight preflight;
    MooCompEffectUsage usage;
    MooCompEffectCpuJob job;
    MooCompEffectCpuStats stats;
    uint32_t index, buffer_index = UINT32_MAX, i;
    uint32_t animations_on_surface = 0u, animations_for_client = 0u;
    MooCompResult result;
    if (!request || !workspace) return MOO_COMP_INVALID;
    if (!moo_i1_integration_valid(core, integration))
        return MOO_COMP_BAD_STATE;
    if (!moo_i1_workspace_exact(core, integration, workspace))
        return MOO_COMP_LIMIT;
    result = moo_i1_owned2(core, integration, owner, surface, &index);
    if (result != MOO_COMP_OK) return result;
    binding = integration->bindings[index];
    if (binding.effect_dirty != 1u ||
        !moo_i1_effect_state_equal(
            &binding.pending_effect, &request->effect))
        return MOO_COMP_BAD_STATE;
    if (request->damage_count > MOO_COMP_SURFACE_DAMAGE_CAPACITY ||
        request->scale < MOO_COMP_SCALE_MIN ||
        request->scale > MOO_COMP_SCALE_MAX ||
        request->opacity > 255u || request->reserved ||
        request->target.width != core->config.output_width ||
        request->target.height != core->config.output_height)
        return MOO_COMP_INVALID;
    for (i = 0u; i < integration->timeline->capacity; ++i) {
        const MooCompAnimationSlot *slot = &integration->timeline->slots[i];
        uint32_t animated_index;
        if (!slot->active) continue;
        result = moo_comp_surface_slot(
            core, slot->surface, &animated_index);
        if (result != MOO_COMP_OK) return MOO_COMP_BAD_STATE;
        if (slot->surface == surface) animations_on_surface++;
        if (core->surfaces[animated_index].owner == owner)
            animations_for_client++;
    }
    if (request->animations_on_surface != animations_on_surface ||
        request->animations_for_client != animations_for_client)
        return MOO_COMP_BAD_STATE;
    if (request->buffer != MOO_COMP_HANDLE_INVALID) {
        result = moo_comp_owned_buffer(
            core, owner, request->buffer, &buffer_index);
        if (result != MOO_COMP_OK) return result;
    }
    usage = (MooCompEffectUsage){
        animations_on_surface, animations_for_client,
        request->buffer == MOO_COMP_HANDLE_INVALID ? UINT64_C(0) :
        (uint64_t)(uint32_t)(core->buffers[buffer_index].view.width /
            (int32_t)request->scale) *
        (uint64_t)(uint32_t)(core->buffers[buffer_index].view.height /
            (int32_t)request->scale)};
    result = moo_comp_effect_state_preflight(
        &integration->config, &binding.state, owner, surface,
        &binding.pending_effect, &usage, &preflight);
    if (result != MOO_COMP_OK) return result;
    if (preflight.expected_commit_sequence !=
            binding.state.commit_sequence ||
        preflight.next_commit_sequence !=
            binding.state.commit_sequence + UINT64_C(1))
        return MOO_COMP_BAD_STATE;
    if (buffer_index != UINT32_MAX) {
        const MooCompBufferView *view = &core->buffers[buffer_index].view;
        int32_t logical_width = view->width / (int32_t)request->scale;
        int32_t logical_height = view->height / (int32_t)request->scale;
        job.content = (MooCompEffectCpuSource){
            view->pixels, view->buffer_bytes, view->stride,
            view->width, view->height};
        job.lower_z = request->lower_z;
        job.target = request->target;
        job.content_rect = (MooCompRect){
            core->surfaces[index].x, core->surfaces[index].y,
            logical_width, logical_height};
        job.effect = preflight.effective;
        job.content_opacity = (uint8_t)request->opacity;
        job.content_scale = request->scale;
        job.reserved_scale = 0u;
        for (i = 0u; i < 7u; ++i) job.reserved[i] = 0u;
        job.max_work_units =
            integration->config.limits.max_effect_work_units_per_frame;
        result = moo_comp_effect_cpu_requirements(&job, &stats);
        if (result != MOO_COMP_OK) return result;
        if (stats.work_units != preflight.work_units ||
            stats.scratch_bytes != preflight.scratch_bytes)
            return MOO_COMP_BAD_STATE;
    } else if (preflight.work_units != 0u ||
               preflight.scratch_bytes != 0u) {
        return MOO_COMP_BAD_STATE;
    }

    moo_i1_clone_core2(core, workspace, &clone);
    clone.effect_integration = NULL;
    clone.surfaces[index].effect_bound = 0u;
    clone.surfaces[index].pending.dirty_mask =
        MOO_COMP_PENDING_ATTACH | MOO_COMP_PENDING_SCALE |
        MOO_COMP_PENDING_OPACITY;
    clone.surfaces[index].pending.buffer = request->buffer;
    clone.surfaces[index].pending.scale = request->scale;
    clone.surfaces[index].pending.opacity = request->opacity;
    clone.surfaces[index].pending.frame_token = request->frame_token;
    clone.surfaces[index].pending.damage_count = request->damage_count;
    for (i = 0u; i < request->damage_count; ++i)
        clone.surfaces[index].pending.damage[i] = request->damage[i];
    result = moo_comp_surface_commit(&clone, owner, surface);
    if (result != MOO_COMP_OK) return result;
    clone.effect_integration = integration;
    clone.surfaces[index].effect_bound = 1u;
    clone.surfaces[index].committed.commit_sequence =
        preflight.next_commit_sequence;
    if (request->frame_token != 0u) {
        uint32_t frame_matches = 0u;
        for (i = 0u; i < clone.frame_capacity; ++i)
            if (clone.frames[i].slot_state == MOO_COMP_SLOT_LIVE &&
                clone.frames[i].owner == owner &&
                clone.frames[i].surface == surface &&
                clone.frames[i].token == request->frame_token) {
                clone.frames[i].commit_sequence =
                    preflight.next_commit_sequence;
                frame_matches++;
            }
        if (frame_matches != 1u) return MOO_COMP_BAD_STATE;
    }
    if (core->commit_sequence == UINT64_MAX ||
        clone.commit_sequence != core->commit_sequence + UINT64_C(1) ||
        clone.surfaces[index].committed.commit_sequence !=
            preflight.next_commit_sequence)
        return MOO_COMP_BAD_STATE;
    result = moo_comp_effect_state_apply(&binding.state, &preflight);
    if (result != MOO_COMP_OK) return result;
    binding.pending_effect = moo_comp_effect_state_neutral();
    binding.effect_dirty = 0u;
    binding.evaluated = binding.state.effective;
    binding.evaluated_opacity = request->opacity;
    result = moo_i1_prepare_surface_buffers(
        core, &clone, index, &prepared);
    if (result != MOO_COMP_OK) return result;
    result = moo_i1_prepare_frame_event(core, &clone, &prepared);
    if (result != MOO_COMP_OK) return result;

    *workspace->prepared = prepared;
    integration->bindings[index] = binding;
    moo_i1_apply_prepared_core(core, &prepared);
    return MOO_COMP_OK;
}
static int moo_i1_surface_animation_candidate_valid(
 const MooCompositor *core, const MooCompEffectIntegration *integration,
 const MooCompAnimationTimeline *timeline,
 const MooCompEffectCompletionReservation *reservations,
 uint32_t surface_index, const MooCompEffectSurfaceBinding *binding) {
    uint32_t i, j, active_count = 0u, reservation_count = 0u;
    MooCompHandle surface;
    if (!core || !integration || !timeline || !reservations || !binding ||
        surface_index >= core->surface_capacity ||
        !core->surfaces[surface_index].live)
        return 0;
    surface = moo_comp_make_handle(
        MOO_COMP_KIND_SURFACE, surface_index,
        core->surfaces[surface_index].generation);
    if (!binding->active || binding->surface != surface ||
        binding->owner != core->surfaces[surface_index].owner)
        return 0;
    for (i = 0u; i < timeline->capacity; ++i) {
        uint32_t matches = 0u;
        if (!timeline->slots[i].active ||
            timeline->slots[i].surface != surface)
            continue;
        active_count++;
        for (j = 0u; j < integration->reservation_capacity; ++j)
            if (reservations[j].slot_state == MOO_COMP_SLOT_RESERVED &&
                reservations[j].owner == binding->owner &&
                reservations[j].surface == surface &&
                reservations[j].token == timeline->slots[i].desc.token)
                matches++;
        if (matches != 1u) return 0;
    }
    for (i = 0u; i < integration->reservation_capacity; ++i) {
        uint32_t matches = 0u, event_matches = 0u;
        const MooCompEffectCompletionReservation *reservation =
            &reservations[i];
        if (reservation->slot_state != MOO_COMP_SLOT_RESERVED ||
            reservation->surface != surface)
            continue;
        reservation_count++;
        if (reservation->owner != binding->owner ||
            reservation->event_index >= core->event_capacity)
            return 0;
        for (j = 0u; j < timeline->capacity; ++j)
            if (timeline->slots[j].active &&
                timeline->slots[j].surface == surface &&
                timeline->slots[j].desc.token == reservation->token)
                matches++;
        for (j = 0u; j < integration->reservation_capacity; ++j)
            if (reservations[j].slot_state == MOO_COMP_SLOT_RESERVED &&
                reservations[j].event_index == reservation->event_index)
                event_matches++;
        if (matches != 1u || event_matches != 1u ||
            core->events[reservation->event_index].slot_state !=
                MOO_COMP_SLOT_RESERVED ||
            core->events[reservation->event_index].live != 0u ||
            core->events[reservation->event_index].reserved != 1u ||
            core->events[reservation->event_index].owner != binding->owner ||
            core->events[reservation->event_index].reserved_slot != i + 1u ||
            core->events[reservation->event_index].sequence != 0u)
            return 0;
    }
    return active_count == reservation_count &&
        reservation_count == binding->reserved_completion_events;
}

static MooCompResult moo_i1_animation_current_value(
 const MooCompEffectSurfaceBinding *binding,uint32_t property,
 MooCompAnimationValue *out) {
    uint64_t q16;
    if(!binding||!out||!binding->active)return MOO_COMP_BAD_STATE;
    *out=(MooCompAnimationValue){{0u}};
    switch(property){
      case MOO_COMP_ANIMATION_PROPERTY_OPACITY:
        q16=((uint64_t)binding->evaluated_opacity*UINT64_C(65536)+
          UINT64_C(127))/UINT64_C(255);
        out->word[0]=(uint32_t)q16;
        return MOO_COMP_OK;
      case MOO_COMP_ANIMATION_PROPERTY_CORNERS:
        out->word[0]=binding->evaluated.corners.top_left;
        out->word[1]=binding->evaluated.corners.top_right;
        out->word[2]=binding->evaluated.corners.bottom_right;
        out->word[3]=binding->evaluated.corners.bottom_left;
        return MOO_COMP_OK;
      case MOO_COMP_ANIMATION_PROPERTY_SHADOW:
        out->word[0]=(uint32_t)binding->evaluated.shadow.offset_x;
        out->word[1]=(uint32_t)binding->evaluated.shadow.offset_y;
        out->word[2]=binding->evaluated.shadow.blur_radius;
        out->word[3]=binding->evaluated.shadow.spread_radius;
        out->word[4]=((uint32_t)binding->evaluated.shadow.color.r<<24u)|
          ((uint32_t)binding->evaluated.shadow.color.g<<16u)|
          ((uint32_t)binding->evaluated.shadow.color.b<<8u)|
          binding->evaluated.shadow.color.a;
        return MOO_COMP_OK;
      case MOO_COMP_ANIMATION_PROPERTY_BACKDROP:
        out->word[0]=binding->evaluated.backdrop.blur_radius;
        out->word[1]=binding->evaluated.backdrop.saturation_q8_8;
        out->word[2]=((uint32_t)binding->evaluated.backdrop.tint.r<<24u)|
          ((uint32_t)binding->evaluated.backdrop.tint.g<<16u)|
          ((uint32_t)binding->evaluated.backdrop.tint.b<<8u)|
          binding->evaluated.backdrop.tint.a;
        out->word[3]=binding->evaluated.backdrop.tint_mix;
        out->word[4]=binding->evaluated.backdrop.noise;
        out->word[5]=binding->evaluated.backdrop.noise_seed;
        return MOO_COMP_OK;
      case MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D:
        out->word[0]=(uint32_t)binding->evaluated.affine.m11;
        out->word[1]=(uint32_t)binding->evaluated.affine.m12;
        out->word[2]=(uint32_t)binding->evaluated.affine.m21;
        out->word[3]=(uint32_t)binding->evaluated.affine.m22;
        out->word[4]=(uint32_t)binding->evaluated.affine.tx;
        out->word[5]=(uint32_t)binding->evaluated.affine.ty;
        out->word[6]=(uint32_t)binding->evaluated.affine.origin_x;
        out->word[7]=(uint32_t)binding->evaluated.affine.origin_y;
        return MOO_COMP_OK;
      default:return MOO_COMP_INVALID;
    }
}

MooCompResult moo_comp_surface_animation_start(MooCompositor *core,
 MooCompEffectIntegration *integration,MooCompHandle owner,MooCompHandle surface,
 const MooCompAnimationDesc *desc,uint64_t timestamp_ns,uint32_t reduced_motion,
 MooCompEffectTransactionWorkspace *workspace,uint32_t *out_count) {
    MooCompositor clone;
    MooCompAnimationTimeline timeline;
    MooCompAnimationValue value,current;
    MooCompEffectSurfaceBinding binding;
    uint32_t index, count = 0u, i;
    MooCompResult result;
    if (!desc || !workspace || !out_count) return MOO_COMP_INVALID;
    if (!moo_i1_integration_valid(core, integration))
        return MOO_COMP_BAD_STATE;
    if (!moo_i1_workspace_exact(core, integration, workspace))
        return MOO_COMP_LIMIT;
    result = moo_i1_owned2(core, integration, owner, surface, &index);
    if (result != MOO_COMP_OK) return result;
    binding = integration->bindings[index];
    result=moo_i1_animation_current_value(&binding,desc->property,&current);
    if(result!=MOO_COMP_OK)return result;
    if (binding.reserved_completion_events == UINT32_MAX)
        return MOO_COMP_LIMIT;
    moo_i1_clone_core2(core, workspace, &clone);
    moo_i1_clone_timeline2(
        integration->timeline, workspace->timeline_slots, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        workspace->reservation_clones[i] = integration->reservations[i];
    result = moo_i1_reserve2(
        &clone, integration, workspace->reservation_clones,
        owner, surface, desc->token);
    if (result != MOO_COMP_OK) return result;
    binding.reserved_completion_events++;
    result = moo_comp_animation_start(
        &timeline, surface, desc, &current,
        binding.evaluated.enabled_mask, timestamp_ns, reduced_motion,
        &value, workspace->completions, workspace->completion_capacity,
        &count);
    if (result != MOO_COMP_OK) return result;
    for (i = 0u; i < count; ++i) {
        result = moo_i1_complete2(
            &clone, workspace->reservation_clones,
            integration->reservation_capacity, &workspace->completions[i]);
        if (result != MOO_COMP_OK) return result;
        if (binding.reserved_completion_events == 0u)
            return MOO_COMP_BAD_STATE;
        binding.reserved_completion_events--;
    }
    if (!moo_i1_surface_animation_candidate_valid(
            &clone, integration, &timeline,
            workspace->reservation_clones, index, &binding))
        return MOO_COMP_BAD_STATE;

    integration->bindings[index] = binding;
    moo_i1_apply_timeline2(integration->timeline, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        integration->reservations[i] = workspace->reservation_clones[i];
    moo_i1_apply_core2(core, &clone);
    *out_count = count;
    return MOO_COMP_OK;
}
MooCompResult moo_comp_surface_animation_cancel(MooCompositor *core,
 MooCompEffectIntegration *integration,MooCompHandle owner,MooCompHandle surface,
 uint64_t token,uint64_t timestamp_ns,MooCompEffectTransactionWorkspace *workspace,
 uint32_t *out_count) {
    MooCompositor clone;
    MooCompAnimationTimeline timeline;
    MooCompEffectSurfaceBinding binding;
    uint32_t index, count = 0u, i;
    MooCompResult result;
    if (!workspace || !out_count || token == 0u) return MOO_COMP_INVALID;
    if (!moo_i1_integration_valid(core, integration))
        return MOO_COMP_BAD_STATE;
    if (!moo_i1_workspace_exact(core, integration, workspace))
        return MOO_COMP_LIMIT;
    result = moo_i1_owned2(core, integration, owner, surface, &index);
    if (result != MOO_COMP_OK) return result;
    binding = integration->bindings[index];
    moo_i1_clone_core2(core, workspace, &clone);
    moo_i1_clone_timeline2(
        integration->timeline, workspace->timeline_slots, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        workspace->reservation_clones[i] = integration->reservations[i];
    result = moo_comp_animation_cancel(
        &timeline, surface, token, timestamp_ns,
        workspace->completions, workspace->completion_capacity, &count);
    if (result != MOO_COMP_OK) return result;
    if (count != 1u ||
        binding.reserved_completion_events < count)
        return MOO_COMP_BAD_STATE;
    result = moo_i1_complete2(
        &clone, workspace->reservation_clones,
        integration->reservation_capacity, &workspace->completions[0]);
    if (result != MOO_COMP_OK) return result;
    binding.reserved_completion_events--;
    if (!moo_i1_surface_animation_candidate_valid(
            &clone, integration, &timeline,
            workspace->reservation_clones, index, &binding))
        return MOO_COMP_BAD_STATE;

    integration->bindings[index] = binding;
    moo_i1_apply_timeline2(integration->timeline, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        integration->reservations[i] = workspace->reservation_clones[i];
    moo_i1_apply_core2(core, &clone);
    *out_count = 1u;
    return MOO_COMP_OK;
}
MooCompResult moo_comp_surface_destroy_ex(MooCompositor *core,
 MooCompEffectIntegration *integration,MooCompHandle owner,MooCompHandle surface,
 uint64_t timestamp_ns,MooCompEffectTransactionWorkspace *workspace,
 uint32_t *out_count) {
    MooCompositor clone;
    MooCompAnimationTimeline timeline;
    MooCompEffectSurfaceBinding binding;
    MooCompEffectState neutral;
    uint32_t index, count = 0u, active = 0u, i;
    MooCompResult result;
    if (!workspace || !out_count) return MOO_COMP_INVALID;
    if (!moo_i1_integration_valid(core, integration))
        return MOO_COMP_BAD_STATE;
    if (!moo_i1_workspace_exact(core, integration, workspace))
        return MOO_COMP_LIMIT;
    result = moo_i1_owned2(core, integration, owner, surface, &index);
    if (result != MOO_COMP_OK) return result;
    binding = integration->bindings[index];
    for (i = 0u; i < integration->timeline->capacity; ++i)
        if (integration->timeline->slots[i].active &&
            integration->timeline->slots[i].surface == surface)
            active++;
    if (active > workspace->completion_capacity)
        return MOO_COMP_LIMIT;
    if (binding.reserved_completion_events != active)
        return MOO_COMP_BAD_STATE;
    moo_i1_clone_core2(core, workspace, &clone);
    moo_i1_clone_timeline2(
        integration->timeline, workspace->timeline_slots, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        workspace->reservation_clones[i] = integration->reservations[i];
    result = moo_comp_animation_destroy_surface(
        &timeline, surface, timestamp_ns, workspace->completions,
        workspace->completion_capacity, &count);
    if (result != MOO_COMP_OK) return result;
    if (count != active) return MOO_COMP_BAD_STATE;
    for (i = 0u; i < count; ++i) {
        result = moo_i1_complete2(
            &clone, workspace->reservation_clones,
            integration->reservation_capacity, &workspace->completions[i]);
        if (result != MOO_COMP_OK) return result;
        if (binding.reserved_completion_events == 0u)
            return MOO_COMP_BAD_STATE;
        binding.reserved_completion_events--;
    }
    if (!moo_i1_surface_animation_candidate_valid(
            &clone, integration, &timeline,
            workspace->reservation_clones, index, &binding))
        return MOO_COMP_BAD_STATE;
    result = moo_comp_effect_surface_destroy(
        &binding.state, owner, surface);
    if (result != MOO_COMP_OK) return result;
    neutral = moo_comp_effect_state_neutral();
    binding.active = 0u;
    binding.effect_dirty = 0u;
    binding.surface = MOO_COMP_HANDLE_INVALID;
    binding.owner = MOO_COMP_HANDLE_INVALID;
    binding.reserved_completion_events = 0u;
    binding.evaluated_opacity = 255u;
    binding.pending_effect = neutral;
    binding.evaluated = neutral;
    binding.previous_bounds = (MooCompEffectBounds){0};
    binding.gpu_resources = (MooCompGpuResources){0};

    clone.effect_integration = NULL;
    clone.surfaces[index].effect_bound = 0u;
    {
        uint32_t buffer_index;
        MooCompHandle committed_buffer = clone.surfaces[index].committed.buffer;
        if (committed_buffer != MOO_COMP_HANDLE_INVALID &&
            moo_comp_buffer_slot(&clone, committed_buffer, &buffer_index) ==
                MOO_COMP_OK)
            clone.buffers[buffer_index].release_armed = 0u;
    }
    result = moo_comp_surface_destroy(&clone, owner, surface);
    if (result != MOO_COMP_OK) return result;
    clone.effect_integration = integration;
    if (clone.surfaces[index].live)
        return MOO_COMP_BAD_STATE;

    integration->bindings[index] = binding;
    moo_i1_apply_timeline2(integration->timeline, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        integration->reservations[i] = workspace->reservation_clones[i];
    moo_i1_apply_core2(core, &clone);
    *out_count = count;
    return MOO_COMP_OK;
}
static int moo_i1_ranges_overlap(
 const void *a, size_t a_bytes, const void *b, size_t b_bytes) {
    uintptr_t a_begin, b_begin, a_end, b_end;
    if (!a || !b || a_bytes == 0u || b_bytes == 0u) return 0;
    a_begin = (uintptr_t)a;
    b_begin = (uintptr_t)b;
    if (a_bytes > (size_t)(UINTPTR_MAX - a_begin) ||
        b_bytes > (size_t)(UINTPTR_MAX - b_begin))
        return 1;
    a_end = a_begin + (uintptr_t)a_bytes;
    b_end = b_begin + (uintptr_t)b_bytes;
    return a_begin < b_end && b_begin < a_end;
}

static int moo_i1_frame_ranges_valid(
 const MooCompositor *core, const MooCompOutput *output,
 const MooCompEffectFrameWorkspace *workspace) {
    size_t frame_bytes, rgba_bytes;
    uint32_t i;
    if (output->stride > SIZE_MAX / (size_t)output->height ||
        workspace->rgba_words_per_buffer > SIZE_MAX / sizeof(uint32_t))
        return 0;
    frame_bytes = output->stride * (size_t)output->height;
    rgba_bytes =
        (size_t)workspace->rgba_words_per_buffer * sizeof(uint32_t);
    if (moo_i1_ranges_overlap(
            output->pixels, frame_bytes,
            workspace->lower_z_pixels, frame_bytes) ||
        moo_i1_ranges_overlap(
            output->pixels, frame_bytes,
            workspace->rgba_ping, rgba_bytes) ||
        moo_i1_ranges_overlap(
            output->pixels, frame_bytes,
            workspace->rgba_pong, rgba_bytes) ||
        moo_i1_ranges_overlap(
            workspace->lower_z_pixels, frame_bytes,
            workspace->rgba_ping, rgba_bytes) ||
        moo_i1_ranges_overlap(
            workspace->lower_z_pixels, frame_bytes,
            workspace->rgba_pong, rgba_bytes) ||
        moo_i1_ranges_overlap(
            workspace->rgba_ping, rgba_bytes,
            workspace->rgba_pong, rgba_bytes))
        return 0;
    for (i = 0u; i < core->surface_capacity; ++i) {
        MooCompHandle handle;
        const MooCompBufferSlot *buffer;
        uint32_t buffer_index;
        size_t content_bytes;
        int decoded;
        if (!core->surfaces[i].live ||
            core->surfaces[i].committed.buffer == MOO_COMP_HANDLE_INVALID)
            continue;
        handle = core->surfaces[i].committed.buffer;
        decoded = moo_comp_handle_index(
            handle, MOO_COMP_KIND_BUFFER, core->buffer_capacity,
            &buffer_index);
        if (decoded != 1 || !core->buffers[buffer_index].live ||
            core->buffers[buffer_index].generation !=
                (uint32_t)(handle >> 32u))
            return 0;
        buffer = &core->buffers[buffer_index];
        if (!buffer->view.pixels || buffer->view.height <= 0 ||
            buffer->view.stride >
                SIZE_MAX / (size_t)buffer->view.height)
            return 0;
        content_bytes =
            buffer->view.stride * (size_t)buffer->view.height;
        if (content_bytes > buffer->view.buffer_bytes ||
            moo_i1_ranges_overlap(
                buffer->view.pixels, content_bytes,
                output->pixels, frame_bytes) ||
            moo_i1_ranges_overlap(
                buffer->view.pixels, content_bytes,
                workspace->lower_z_pixels, frame_bytes) ||
            moo_i1_ranges_overlap(
                buffer->view.pixels, content_bytes,
                workspace->rgba_ping, rgba_bytes) ||
            moo_i1_ranges_overlap(
                buffer->view.pixels, content_bytes,
                workspace->rgba_pong, rgba_bytes))
            return 0;
    }
    return 1;
}

static int moo_i1_bounds_equal(const MooCompEffectBounds *a,
 const MooCompEffectBounds *b) {
    return a&&b&&
      a->content_bounds.x==b->content_bounds.x&&
      a->content_bounds.y==b->content_bounds.y&&
      a->content_bounds.width==b->content_bounds.width&&
      a->content_bounds.height==b->content_bounds.height&&
      a->visual_bounds.x==b->visual_bounds.x&&
      a->visual_bounds.y==b->visual_bounds.y&&
      a->visual_bounds.width==b->visual_bounds.width&&
      a->visual_bounds.height==b->visual_bounds.height&&
      a->backdrop_sample_bounds.x==b->backdrop_sample_bounds.x&&
      a->backdrop_sample_bounds.y==b->backdrop_sample_bounds.y&&
      a->backdrop_sample_bounds.width==b->backdrop_sample_bounds.width&&
      a->backdrop_sample_bounds.height==b->backdrop_sample_bounds.height;
}

static int32_t moo_i1_animation_i32(uint32_t raw) {
    int64_t value=raw<=INT32_MAX?(int64_t)raw:
      (int64_t)raw-INT64_C(4294967296);
    return (int32_t)value;
}

static MooCompRgba8 moo_i1_animation_rgba(uint32_t packed) {
    return (MooCompRgba8){(uint8_t)(packed>>24u),
      (uint8_t)(packed>>16u),(uint8_t)(packed>>8u),(uint8_t)packed};
}

MooCompResult moo_comp_build_frame_ex(MooCompositor *core,
 MooCompEffectIntegration *integration,const MooCompOutput *output,
 const MooCompEffectFrameRequest *request,MooCompEffectFrameWorkspace *workspace,
 MooCompEffectFrameResult *out_result) {
    MooCompAnimationTimeline timeline;MooCompRect full;
    MooCompEffectDamageWorkspace damage_workspace;
    MooCompEffectDamageOutput damage_output;
    uint32_t i,order_count=0u,samples=0u,completions=0u;
    uint32_t cursor_buffer_index=UINT32_MAX,cursor_draw=0u;
    int32_t cursor_dst_x=0,cursor_dst_y=0;
    MooCompEffectSurfaceBinding *bindings = workspace ? workspace->binding_clones : NULL;
    uint64_t after=0u,work=0u,scratch_bytes=0u;MooCompResult result;
    if(!moo_comp_output_valid(core,output)||!request||!workspace||!out_result)
      return MOO_COMP_INVALID;
    if (!moo_i1_integration_valid(core, integration)) return MOO_COMP_BAD_STATE;
    if (request->reduced_motion > 1u || request->allow_gpu > 1u)
      return MOO_COMP_INVALID;
    if (!workspace->timeline_slots ||
      workspace->timeline_capacity!=integration->timeline->capacity||
      !workspace->samples||
      workspace->sample_capacity<integration->timeline->capacity||
      !workspace->completions||
      workspace->completion_capacity<integration->reservation_capacity||
      !workspace->binding_clones||
      workspace->binding_capacity!=integration->binding_capacity||
      !workspace->reservation_clones||
      workspace->reservation_capacity!=integration->reservation_capacity||
      !workspace->damage_surfaces||
      workspace->damage_surface_capacity<core->surface_capacity||
      !workspace->damage_workspace_regions||
      workspace->damage_workspace_capacity==0u||
      !workspace->damage_output_regions||
      workspace->damage_output_capacity==0u||
      !workspace->surface_order||
      workspace->surface_order_capacity<core->surface_capacity||
      !workspace->lower_z_pixels||
      output->stride>(size_t)-1/(size_t)output->height||
      workspace->lower_z_capacity<output->stride*(size_t)output->height||
      !workspace->rgba_ping||!workspace->rgba_pong||
      workspace->rgba_words_per_buffer==0u||
      !workspace->gpu_passes||workspace->gpu_pass_capacity==0u)
      return MOO_COMP_LIMIT;
    if (!moo_i1_frame_ranges_valid(core, output, workspace))
      return MOO_COMP_INVALID;
    if(core->in_flight_frame)return MOO_COMP_WOULD_BLOCK;
    if(core->frame_sequence==UINT64_MAX)return MOO_COMP_LIMIT;
    moo_i1_clone_timeline2(integration->timeline,workspace->timeline_slots,&timeline);
    for (i = 0u; i < integration->binding_capacity; ++i)
        workspace->binding_clones[i] = integration->bindings[i];
    for (i = 0u; i < integration->reservation_capacity; ++i)
        workspace->reservation_clones[i] = integration->reservations[i];
    for(i=0u;i<integration->binding_capacity;++i)
      if(bindings[i].active&&!moo_i1_surface_animation_candidate_valid(core,
        integration,&timeline,workspace->reservation_clones,i,&bindings[i]))
        return MOO_COMP_BAD_STATE;
    result=moo_comp_animation_evaluate(&timeline,
      request->timestamp_ns,request->reduced_motion,workspace->samples,
      workspace->sample_capacity,&samples,workspace->completions,
      workspace->completion_capacity,&completions);
    if (result != MOO_COMP_OK) return result;
    for (i = 0u; i < samples; ++i) {
      uint32_t surface_index;
      if (moo_comp_surface_slot(core, workspace->samples[i].surface, &surface_index) != MOO_COMP_OK)
          return MOO_COMP_BAD_STATE;
      if(!bindings[surface_index].active||
        bindings[surface_index].surface!=workspace->samples[i].surface||
        bindings[surface_index].owner!=core->surfaces[surface_index].owner)
          return MOO_COMP_BAD_STATE;
      if (workspace->samples[i].property == MOO_COMP_ANIMATION_PROPERTY_OPACITY) {
          uint64_t scaled = (uint64_t)workspace->samples[i].value.word[0] * UINT64_C(255) + UINT64_C(32768);
          bindings[surface_index].evaluated_opacity = (uint32_t)(scaled / UINT64_C(65536));
      } else if (workspace->samples[i].property ==
          MOO_COMP_ANIMATION_PROPERTY_CORNERS) {
          if ((bindings[surface_index].evaluated.enabled_mask &
              MOO_COMP_EFFECT_CORNER_CLIP) == 0u)
              return MOO_COMP_BAD_STATE;
          bindings[surface_index].evaluated.corners=(MooCompCorners){
            (uint16_t)workspace->samples[i].value.word[0],
            (uint16_t)workspace->samples[i].value.word[1],
            (uint16_t)workspace->samples[i].value.word[2],
            (uint16_t)workspace->samples[i].value.word[3]};
      } else if (workspace->samples[i].property ==
          MOO_COMP_ANIMATION_PROPERTY_SHADOW) {
          if ((bindings[surface_index].evaluated.enabled_mask &
              MOO_COMP_EFFECT_SHADOW) == 0u)
              return MOO_COMP_BAD_STATE;
          bindings[surface_index].evaluated.shadow=(MooCompShadow){
            moo_i1_animation_i32(workspace->samples[i].value.word[0]),
            moo_i1_animation_i32(workspace->samples[i].value.word[1]),
            (uint16_t)workspace->samples[i].value.word[2],
            (uint16_t)workspace->samples[i].value.word[3],
            moo_i1_animation_rgba(workspace->samples[i].value.word[4])};
      } else if (workspace->samples[i].property ==
          MOO_COMP_ANIMATION_PROPERTY_BACKDROP) {
          uint64_t backdrop_mask=MOO_COMP_EFFECT_BACKDROP_BLUR|
            MOO_COMP_EFFECT_SATURATION|MOO_COMP_EFFECT_TINT|MOO_COMP_EFFECT_NOISE;
          if ((bindings[surface_index].evaluated.enabled_mask & backdrop_mask) == 0u)
              return MOO_COMP_BAD_STATE;
          bindings[surface_index].evaluated.backdrop=(MooCompBackdrop){
            (uint16_t)workspace->samples[i].value.word[0],
            (uint16_t)workspace->samples[i].value.word[1],
            moo_i1_animation_rgba(workspace->samples[i].value.word[2]),
            (uint8_t)workspace->samples[i].value.word[3],
            (uint8_t)workspace->samples[i].value.word[4],0u,
            workspace->samples[i].value.word[5]};
      } else if (workspace->samples[i].property ==
          MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D) {
          if ((bindings[surface_index].evaluated.enabled_mask &
              MOO_COMP_EFFECT_AFFINE_2D) == 0u)
              return MOO_COMP_BAD_STATE;
          bindings[surface_index].evaluated.affine=(MooCompAffine2D){
            moo_i1_animation_i32(workspace->samples[i].value.word[0]),
            moo_i1_animation_i32(workspace->samples[i].value.word[1]),
            moo_i1_animation_i32(workspace->samples[i].value.word[2]),
            moo_i1_animation_i32(workspace->samples[i].value.word[3]),
            moo_i1_animation_i32(workspace->samples[i].value.word[4]),
            moo_i1_animation_i32(workspace->samples[i].value.word[5]),
            moo_i1_animation_i32(workspace->samples[i].value.word[6]),
            moo_i1_animation_i32(workspace->samples[i].value.word[7])};
      } else {
          return MOO_COMP_BAD_STATE;
      }
    }
    if ((uint64_t)completions > UINT64_MAX - core->event_sequence)
      return MOO_COMP_LIMIT;
    for (i = 0u; i < completions; ++i) {
      MooCompAnimationCompletion *completion = &workspace->completions[i];
      MooCompEffectCompletionReservation *reservation;
      const MooCompEventSlot *event;
      uint32_t surface_index,selected=UINT32_MAX,matches=0u,j;
      int decoded=moo_comp_handle_index(completion->surface,
        MOO_COMP_KIND_SURFACE,core->surface_capacity,&surface_index);
      if(decoded!=1||!core->surfaces[surface_index].live||
        core->surfaces[surface_index].generation!=
          (uint32_t)(completion->surface>>32u))return MOO_COMP_BAD_STATE;
      for(j=0u;j<integration->reservation_capacity;++j)
        if(workspace->reservation_clones[j].slot_state==MOO_COMP_SLOT_RESERVED&&
          workspace->reservation_clones[j].surface==completion->surface&&
          workspace->reservation_clones[j].token==completion->token)
          {selected=j;matches++;}
      if(matches!=1u)return MOO_COMP_BAD_STATE;
      reservation=&workspace->reservation_clones[selected];
      if(reservation->owner!=core->surfaces[surface_index].owner||
        reservation->event_index>=core->event_capacity||
        bindings[surface_index].reserved_completion_events==0u)
        return MOO_COMP_BAD_STATE;
      event=&core->events[reservation->event_index];
      if(event->slot_state!=MOO_COMP_SLOT_RESERVED||event->live!=0u||
        event->reserved!=1u||event->reserved_slot!=selected+1u||
        event->owner!=reservation->owner||event->sequence!=0u)
        return MOO_COMP_BAD_STATE;
      {uint32_t completed_on_surface=1u;
        for(j=0u;j<i;++j){
          if(workspace->completions[j].reserved==selected+1u)
            return MOO_COMP_BAD_STATE;
          if(workspace->completions[j].surface==completion->surface)
            completed_on_surface++;
        }
        if(bindings[surface_index].reserved_completion_events<completed_on_surface)
          return MOO_COMP_BAD_STATE;
      }
      completion->reserved=selected+1u;
    }
    while(order_count<core->surface_capacity){uint32_t best=UINT32_MAX;uint64_t best_z=UINT64_MAX;
      for(i=0;i<core->surface_capacity;i++)if(core->surfaces[i].live&&
        core->surfaces[i].committed.buffer!=MOO_COMP_HANDLE_INVALID&&
        core->surfaces[i].z_sequence>after&&core->surfaces[i].z_sequence<best_z)
        {best=i;best_z=core->surfaces[i].z_sequence;}
      if (best == UINT32_MAX) break;
      workspace->surface_order[order_count++] = best;
      after = best_z;
    }
    for(i=0u;i<order_count;++i){
      uint32_t si=workspace->surface_order[i],j;
      MooCompEffectDamageSurface damage=(MooCompEffectDamageSurface){0};
      damage.surface=moo_comp_make_handle(MOO_COMP_KIND_SURFACE,si,
        core->surfaces[si].generation);
      if(bindings[si].active){
        uint32_t bi;
        MooCompRect local;
        MooCompEffectBounds next_bounds;
        result=moo_comp_buffer_slot(core,core->surfaces[si].committed.buffer,&bi);
        if(result!=MOO_COMP_OK)return result;
        local=(MooCompRect){core->surfaces[si].x,core->surfaces[si].y,
          core->buffers[bi].view.width/(int32_t)core->surfaces[si].committed.scale,
          core->buffers[bi].view.height/(int32_t)core->surfaces[si].committed.scale};
        result=moo_comp_effect_compute_bounds(local,&bindings[si].evaluated.affine,
          bindings[si].evaluated.enabled_mask,&bindings[si].evaluated.shadow,
          &bindings[si].evaluated.backdrop,&next_bounds);
        if(result!=MOO_COMP_OK)return result;
        damage.enabled_mask=bindings[si].evaluated.enabled_mask;
        damage.old_visual_bounds=bindings[si].previous_bounds.visual_bounds;
        damage.new_visual_bounds=next_bounds.visual_bounds;
        damage.backdrop_coverage_bounds=next_bounds.content_bounds;
        damage.backdrop_blur_radius=bindings[si].evaluated.backdrop.blur_radius;
        damage.changed=moo_i1_bounds_equal(&bindings[si].previous_bounds,
          &next_bounds)?0u:1u;
        for(j=0u;j<samples;++j)
          if(workspace->samples[j].surface==damage.surface)damage.changed=1u;
        bindings[si].previous_bounds=next_bounds;
      }
      workspace->damage_surfaces[i]=damage;
    }
    for(i=0;i<order_count;i++)if(bindings[workspace->surface_order[i]].active){
      uint32_t si=workspace->surface_order[i],bi,k;MooCompEffectCpuJob job;
      MooCompEffectCpuStats stats;result=moo_comp_buffer_slot(core,
        core->surfaces[si].committed.buffer,&bi);if(result!=MOO_COMP_OK)return result;
      job.content=(MooCompEffectCpuSource){core->buffers[bi].view.pixels,
        core->buffers[bi].view.buffer_bytes,core->buffers[bi].view.stride,
        core->buffers[bi].view.width,core->buffers[bi].view.height};
      job.lower_z=(MooCompEffectCpuSource){workspace->lower_z_pixels,
        workspace->lower_z_capacity,output->stride,output->width,output->height};
      job.target=(MooCompEffectCpuTarget){output->pixels,output->buffer_bytes,
        output->stride,output->width,output->height};
      job.content_rect=(MooCompRect){core->surfaces[si].x,core->surfaces[si].y,
        job.content.width/(int32_t)core->surfaces[si].committed.scale,
        job.content.height/(int32_t)core->surfaces[si].committed.scale};
      job.effect=bindings[si].evaluated;
      job.content_opacity=(uint8_t)bindings[si].evaluated_opacity;
      job.content_scale=core->surfaces[si].committed.scale;job.reserved_scale=0u;
      for(k=0;k<7;k++)job.reserved[k]=0u;
      job.max_work_units=integration->config.limits.max_effect_work_units_per_frame;
      result=moo_comp_effect_cpu_requirements(&job,&stats);if(result!=MOO_COMP_OK)return result;
      if(stats.rgba_words_per_buffer>workspace->rgba_words_per_buffer)return MOO_COMP_LIMIT;
      if(stats.work_units>UINT64_MAX-work)return MOO_COMP_LIMIT;
      work+=stats.work_units;
      if(request->max_frame_work_units!=0u&&
        work>request->max_frame_work_units)return MOO_COMP_LIMIT;
      if(stats.scratch_bytes>scratch_bytes)scratch_bytes=stats.scratch_bytes;}
    full=(MooCompRect){0,0,output->width,output->height};
    damage_workspace=(MooCompEffectDamageWorkspace){
      workspace->damage_workspace_regions,workspace->damage_workspace_capacity,0u};
    damage_output=(MooCompEffectDamageOutput){workspace->damage_output_regions,
      workspace->damage_output_capacity<MOO_COMP_OUTPUT_DAMAGE_CAPACITY?
        workspace->damage_output_capacity:MOO_COMP_OUTPUT_DAMAGE_CAPACITY,
      0u,0u,0u};
    result=moo_comp_effect_damage_build(full,core->output_damage,
      core->output_damage_count,workspace->damage_surfaces,order_count,
      &damage_workspace,&damage_output);
    if(result!=MOO_COMP_OK)return result;
    for(i=0u;i<order_count;++i){
      uint32_t si=workspace->surface_order[i],bi;
      const MooCompSurfaceSlot *surface=&core->surfaces[si];
      const MooCompBufferView *view;
      result=moo_comp_buffer_slot(core,surface->committed.buffer,&bi);
      if(result!=MOO_COMP_OK)return result;
      view=&core->buffers[bi].view;
      if(!moo_comp_view_valid(view)||
        surface->committed.scale<MOO_COMP_SCALE_MIN||
        surface->committed.scale>MOO_COMP_SCALE_MAX||
        surface->committed.opacity>255u||
        view->width%(int32_t)surface->committed.scale!=0||
        view->height%(int32_t)surface->committed.scale!=0)
        return MOO_COMP_BAD_STATE;
      workspace->damage_surfaces[i].reserved=bi+1u;
    }
    if(core->cursor.visible){
      const MooCompBufferView *cursor_view;
      int64_t dst_x,dst_y;
      result=moo_comp_buffer_slot(core,core->cursor.buffer,&cursor_buffer_index);
      if(result!=MOO_COMP_OK)return MOO_COMP_BAD_STATE;
      cursor_view=&core->buffers[cursor_buffer_index].view;
      if(!moo_comp_view_valid(cursor_view)||
        core->cursor.scale<MOO_COMP_SCALE_MIN||
        core->cursor.scale>MOO_COMP_SCALE_MAX||
        cursor_view->width%(int32_t)core->cursor.scale!=0||
        cursor_view->height%(int32_t)core->cursor.scale!=0)
        return MOO_COMP_BAD_STATE;
      dst_x=(int64_t)core->cursor.x-core->cursor.hotspot_x;
      dst_y=(int64_t)core->cursor.y-core->cursor.hotspot_y;
      if(dst_x>=INT32_MIN&&dst_x<=INT32_MAX&&
        dst_y>=INT32_MIN&&dst_y<=INT32_MAX){
        cursor_dst_x=(int32_t)dst_x;
        cursor_dst_y=(int32_t)dst_y;
        cursor_draw=1u;
      }
    }
    (void)cursor_buffer_index;
    (void)cursor_draw;
    (void)cursor_dst_x;
    (void)cursor_dst_y;
    result=moo_comp_raster_clear(output,full,core->config.background_r,
      core->config.background_g,core->config.background_b,core->config.background_a);
    if(result!=MOO_COMP_OK)return result;
    for(i=0;i<order_count;i++){uint32_t si=workspace->surface_order[i],k;
      uint32_t bi=workspace->damage_surfaces[i].reserved-1u;
      if(!bindings[si].active)result=moo_comp_raster_blit(output,
        &core->buffers[bi].view,core->surfaces[si].x,core->surfaces[si].y,
        core->surfaces[si].committed.scale,core->surfaces[si].committed.opacity,full);
      else{MooCompEffectCpuJob job;MooCompEffectCpuScratch scratch;MooCompEffectCpuStats stats;
        result=moo_comp_raster_copy_rgba(output,workspace->lower_z_pixels,
          workspace->lower_z_capacity,output->stride);if(result!=MOO_COMP_OK)return result;
        job.content=(MooCompEffectCpuSource){core->buffers[bi].view.pixels,
          core->buffers[bi].view.buffer_bytes,core->buffers[bi].view.stride,
          core->buffers[bi].view.width,core->buffers[bi].view.height};
        job.lower_z=(MooCompEffectCpuSource){workspace->lower_z_pixels,
          workspace->lower_z_capacity,output->stride,output->width,output->height};
        job.target=(MooCompEffectCpuTarget){output->pixels,output->buffer_bytes,
          output->stride,output->width,output->height};
      job.content_rect=(MooCompRect){core->surfaces[si].x,core->surfaces[si].y,
        job.content.width/(int32_t)core->surfaces[si].committed.scale,
        job.content.height/(int32_t)core->surfaces[si].committed.scale};
      job.effect=bindings[si].evaluated;
      job.content_opacity=(uint8_t)bindings[si].evaluated_opacity;
      job.content_scale=core->surfaces[si].committed.scale;job.reserved_scale=0u;
      for(k=0;k<7;k++)job.reserved[k]=0u;
        job.max_work_units=integration->config.limits.max_effect_work_units_per_frame;
        scratch=(MooCompEffectCpuScratch){workspace->rgba_ping,workspace->rgba_pong,
          workspace->rgba_words_per_buffer,NULL,NULL,0u};
        result=moo_comp_effect_cpu_render(&job,&scratch,&stats);}
      if(result!=MOO_COMP_OK)return result;}
    if(cursor_draw)result=moo_comp_raster_blit(output,
      &core->buffers[cursor_buffer_index].view,cursor_dst_x,cursor_dst_y,
      core->cursor.scale,255u,full);
    else result=MOO_COMP_OK;
    if(result!=MOO_COMP_OK)return result;
    for(i=0u;i<completions;++i){
      MooCompAnimationCompletion *completion=&workspace->completions[i];
      uint32_t selected=completion->reserved-1u;
      uint32_t surface_index=
        (((uint32_t)completion->surface)&MOO_COMP_HANDLE_INDEX_MASK)-1u;
      MooCompEffectCompletionReservation *reservation=
        &integration->reservations[selected];
      MooCompEventSlot *event=&core->events[reservation->event_index];
      core->event_sequence++;
      event->sequence=core->event_sequence;
      event->event=(MooCompEvent){MOO_COMP_EVENT_ANIMATION_DONE,
        completion->status,completion->surface,completion->token,
        core->surfaces[surface_index].committed.commit_sequence,
        completion->timestamp_ns};
      bindings[surface_index].reserved_completion_events--;
      moo_comp_event_slot_live(event);
      integration->reservations[selected]=
        (MooCompEffectCompletionReservation){0};
      integration->reservations[selected].event_index=UINT32_MAX;
      integration->reservations[selected].owner=MOO_COMP_HANDLE_INVALID;
      integration->reservations[selected].surface=MOO_COMP_HANDLE_INVALID;
      completion->reserved=0u;
    }
    for (i = 0u; i < integration->binding_capacity; ++i)
        integration->bindings[i] = bindings[i];
    moo_i1_apply_timeline2(integration->timeline,&timeline);
    core->output_damage_count=0u;
    core->frame_sequence++;
    core->in_flight_frame=core->frame_sequence;
    *out_result=(MooCompEffectFrameResult){core->frame_sequence,work,scratch_bytes,
      damage_output.count,damage_output.full_damage,0u,completions,
      {0u,0u,0u,0u}};return MOO_COMP_OK;
}

MooCompResult moo_comp_client_disconnect_ex(
    MooCompositor *core, MooCompEffectIntegration *integration,
    MooCompHandle client, uint64_t timestamp_ns,
    MooCompEffectTransactionWorkspace *workspace,
    uint32_t *out_completion_count) {
    MooCompositor clone;
    MooCompAnimationTimeline timeline;
    uint32_t needed = 0u, written = 0u, client_index, i;
    MooCompResult result;
    if (!core || !integration || !workspace || !out_completion_count)
        return MOO_COMP_INVALID;
    if (!moo_i1_integration_valid(core, integration))
        return MOO_COMP_BAD_STATE;
    if (!moo_i1_workspace_exact(core, integration, workspace))
        return MOO_COMP_LIMIT;
    result = moo_comp_client_slot(core, client, &client_index);
    if (result != MOO_COMP_OK) return result;
    for (i = 0u; i < integration->timeline->capacity; ++i) {
        const MooCompAnimationSlot *slot = &integration->timeline->slots[i];
        uint32_t surface_index;
        if (slot->active &&
            moo_comp_surface_slot(core, slot->surface, &surface_index) == MOO_COMP_OK &&
            core->surfaces[surface_index].owner == client) {
            uint32_t j, matches = 0u;
            for (j = 0u; j < integration->reservation_capacity; ++j) {
                const MooCompEffectCompletionReservation *reservation =
                    &integration->reservations[j];
                if (reservation->slot_state == MOO_COMP_SLOT_RESERVED &&
                    reservation->owner == client &&
                    reservation->surface == slot->surface &&
                    reservation->token == slot->desc.token) {
                    const MooCompEventSlot *event;
                    if (reservation->event_index >= core->event_capacity)
                        return MOO_COMP_BAD_STATE;
                    event = &core->events[reservation->event_index];
                    if (event->slot_state != MOO_COMP_SLOT_RESERVED ||
                        event->live || event->owner != client ||
                        event->reserved != 1u ||
                        event->reserved_slot != j + 1u)
                        return MOO_COMP_BAD_STATE;
                    matches++;
                }
            }
            if (matches != 1u) return MOO_COMP_BAD_STATE;
            needed++;
        }
    }
    if (needed > integration->timeline->active_count)
        return MOO_COMP_BAD_STATE;
    if (needed > workspace->completion_capacity)
        return MOO_COMP_LIMIT;
    moo_i1_clone_core2(core, workspace, &clone);
    moo_i1_clone_timeline2(integration->timeline, workspace->timeline_slots, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        workspace->reservation_clones[i] = integration->reservations[i];
    for (i = 0u; i < core->surface_capacity; ++i) {
        const MooCompSurfaceSlot *surface = &core->surfaces[i];
        if (surface->live && surface->owner == client &&
            integration->bindings[i].active) {
            MooCompHandle handle = moo_comp_make_handle(
                MOO_COMP_KIND_SURFACE, i, surface->generation);
            MooCompEffectSurfaceState state = integration->bindings[i].state;
            uint32_t count = 0u;
            if (integration->bindings[i].owner != client ||
                integration->bindings[i].surface != handle)
                return MOO_COMP_BAD_STATE;
            result = moo_comp_effect_surface_destroy(&state, client, handle);
            if (result != MOO_COMP_OK) return result;
            result = moo_comp_animation_destroy_surface(
                &timeline, handle, timestamp_ns,
                workspace->completions + written,
                workspace->completion_capacity - written, &count);
            if (result != MOO_COMP_OK) return result;
            if (count !=
                integration->bindings[i].reserved_completion_events)
                return MOO_COMP_BAD_STATE;
            written += count;
        }
    }
    if (written != needed) return MOO_COMP_BAD_STATE;
    for (i = 0u; i < written; ++i) {
        const MooCompAnimationCompletion *completion = &workspace->completions[i];
        uint32_t j, selected = UINT32_MAX;
        for (j = 0u; j < integration->reservation_capacity; ++j) {
            MooCompEffectCompletionReservation *reservation =
                &workspace->reservation_clones[j];
            if (reservation->slot_state == MOO_COMP_SLOT_RESERVED &&
                reservation->owner == client &&
                reservation->surface == completion->surface &&
                reservation->token == completion->token) {
                if (selected != UINT32_MAX) return MOO_COMP_BAD_STATE;
                selected = j;
            }
        }
        if (selected == UINT32_MAX) return MOO_COMP_BAD_STATE;
        {
            MooCompEffectCompletionReservation *reservation =
                &workspace->reservation_clones[selected];
            MooCompEventSlot *event;
            if (reservation->event_index >= clone.event_capacity)
                return MOO_COMP_BAD_STATE;
            event = &clone.events[reservation->event_index];
            if (event->slot_state != MOO_COMP_SLOT_RESERVED || event->live ||
                event->reserved != 1u || event->owner != client ||
                event->reserved_slot != selected + 1u || event->sequence != 0u)
                return MOO_COMP_BAD_STATE;
            moo_comp_event_slot_free(event);
            *reservation = (MooCompEffectCompletionReservation){0};
            reservation->event_index = UINT32_MAX;
            reservation->owner = MOO_COMP_HANDLE_INVALID;
            reservation->surface = MOO_COMP_HANDLE_INVALID;
        }
    }
    for (i = 0u; i < integration->reservation_capacity; ++i)
        if (workspace->reservation_clones[i].slot_state ==
                MOO_COMP_SLOT_RESERVED &&
            workspace->reservation_clones[i].owner == client)
            return MOO_COMP_BAD_STATE;
    for (i = 0u; i < timeline.capacity; ++i) {
        uint32_t surface_index;
        if (timeline.slots[i].active &&
            moo_comp_handle_index(timeline.slots[i].surface,
                MOO_COMP_KIND_SURFACE, core->surface_capacity,
                &surface_index) == 1 &&
            core->surfaces[surface_index].live &&
            core->surfaces[surface_index].owner == client)
            return MOO_COMP_BAD_STATE;
    }
    clone.effect_integration = NULL;
    moo_i1_disconnect_apply_unbound(&clone, client);
    clone.effect_integration = integration;
    if (clone.clients[client_index].live ||
        clone.event_sequence != core->event_sequence)
        return MOO_COMP_BAD_STATE;
    moo_i1_apply_timeline2(integration->timeline, &timeline);
    for (i = 0u; i < integration->reservation_capacity; ++i)
        integration->reservations[i] = workspace->reservation_clones[i];
    for (i = 0u; i < integration->binding_capacity; ++i)
        if (integration->bindings[i].owner == client) {
            MooCompEffectState neutral = moo_comp_effect_state_neutral();
            integration->bindings[i] = (MooCompEffectSurfaceBinding){0};
            integration->bindings[i].surface = MOO_COMP_HANDLE_INVALID;
            integration->bindings[i].owner = MOO_COMP_HANDLE_INVALID;
            integration->bindings[i].evaluated_opacity = 255u;
            integration->bindings[i].pending_effect = neutral;
            integration->bindings[i].evaluated = neutral;
        }
    moo_i1_apply_core2(core, &clone);
    *out_completion_count = written;
    return MOO_COMP_OK;
}
