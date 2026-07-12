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
    return core && core->clients && core->client_capacity > 0u &&
           core->surfaces && core->surface_capacity > 0u &&
           core->buffers && core->buffer_capacity > 0u &&
           core->frames && core->frame_capacity > 0u &&
           core->events && core->event_capacity > 0u &&
           core->config.output_width > 0 &&
           core->config.output_height > 0;
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
        if (!core->events[i].live) count++;
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
        if (core->events[i].live && core->events[i].owner == owner) count++;
    }
    return count;
}

/* Pending tokens already reserve both a future frame slot and event slot. */
static uint32_t moo_comp_frames_owned(const MooCompositor *core,
                                      MooCompHandle owner) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->frame_capacity; ++i) {
        if (core->frames[i].live && core->frames[i].owner == owner) count++;
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
        if (!core->events[i].live) {
            core->event_sequence++;
            core->events[i].live = 1u;
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
        if (!core->frames[i].live) return i;
    }
    return UINT32_MAX;
}

static int moo_comp_token_available(const MooCompositor *core,
                                    MooCompHandle owner, uint64_t token) {
    uint32_t i;
    for (i = 0u; i < core->surface_capacity; ++i) {
        if (core->surfaces[i].live && core->surfaces[i].owner == owner &&
            core->surfaces[i].pending.frame_token == token)
            return 0;
    }
    for (i = 0u; i < core->frame_capacity; ++i) {
        if (core->frames[i].live && core->frames[i].owner == owner &&
            core->frames[i].token == token) return 0;
    }
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].live && core->events[i].owner == owner &&
            core->events[i].event.type == MOO_COMP_EVENT_FRAME_DONE &&
            core->events[i].event.token == token) return 0;
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
    }
    for (i = 0u; i < buffer_capacity; ++i) {
        buffers[i].live = 0u;
        buffers[i].generation = 1u;
        buffers[i].owner = MOO_COMP_HANDLE_INVALID;
        buffers[i].ref_count = 0u;
        buffers[i].release_armed = 0u;
    }
    for (i = 0u; i < frame_capacity; ++i) frames[i].live = 0u;
    for (i = 0u; i < event_capacity; ++i) events[i].live = 0u;

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
        frame->live = 1u;
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
        if (core->frames[i].live &&
            core->frames[i].surface == surface)
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
        frame->live = 1u;
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
        if (core->frames[i].live && core->frames[i].owner == client)
            core->frames[i].live = 0u;
    }
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].live && core->events[i].owner == client)
            core->events[i].live = 0u;
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
        if (frame->live && frame->state == MOO_COMP_FRAME_WAITING) {
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
        if (core->frames[i].live &&
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
        if (frame->live && frame->state == MOO_COMP_FRAME_ASSIGNED &&
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
            frame->live = 0u;
            (void)moo_comp_push_event(core, frame->owner, &event);
        }
    }
    core->in_flight_frame = 0u;
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
        if (core->events[i].live && core->events[i].owner == client &&
            core->events[i].sequence < sequence) {
            best = i;
            sequence = core->events[i].sequence;
        }
    }
    if (best == UINT32_MAX) return MOO_COMP_WOULD_BLOCK;
    *out_event = core->events[best].event;
    core->events[best].live = 0u;
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
        hash = moo_comp_hash_mix(hash, frame->live);
        if (!frame->live) continue;
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
        hash = moo_comp_hash_mix(hash, slot->live);
        if (!slot->live) continue;
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