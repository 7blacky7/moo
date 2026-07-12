#include "moo_input_core.h"

#include <limits.h>

#define MOO_INPUT_HANDLE_KIND_SHIFT 28u
#define MOO_INPUT_HANDLE_INDEX_MASK UINT32_C(0x0fffffff)
#define MOO_INPUT_KIND_CLIENT 1u
#define MOO_INPUT_KIND_TARGET 2u
#define MOO_INPUT_KIND_NODE 3u

static void a11y_copy_record(void *destination, const void *source,
                              size_t count) {
    volatile uint8_t *dst = (volatile uint8_t *)destination;
    const volatile uint8_t *src = (const volatile uint8_t *)source;
    size_t i;
    for (i = 0u; i < count; ++i) dst[i] = src[i];
}

static void a11y_clear_record(void *record, size_t count) {
    volatile uint8_t *bytes = (volatile uint8_t *)record;
    size_t i;
    for (i = 0u; i < count; ++i) bytes[i] = 0u;
}

static void a11y_store_data(MooA11yNodeData *destination,
                             const MooA11yNodeData *source) {
    uint32_t i;
    a11y_clear_record(destination, sizeof(*destination));
    a11y_copy_record(destination, source, sizeof(*destination));
    for (i = destination->name.length; i < MOO_INPUT_TEXT_CAPACITY; ++i)
        destination->name.bytes[i] = 0u;
    for (i = destination->value.length; i < MOO_INPUT_TEXT_CAPACITY; ++i)
        destination->value.bytes[i] = 0u;
    for (i = destination->description.length; i < MOO_INPUT_TEXT_CAPACITY; ++i)
        destination->description.bytes[i] = 0u;
}

static MooInputHandle a11y_make_handle(uint32_t kind, uint32_t index,
                                       uint32_t generation) {
    uint32_t low = (kind << MOO_INPUT_HANDLE_KIND_SHIFT) | (index + 1u);
    return ((uint64_t)generation << 32u) | (uint64_t)low;
}

static int a11y_decode(MooInputHandle handle, uint32_t kind,
                        uint32_t capacity, uint32_t *out_index) {
    uint32_t low;
    uint32_t encoded;
    if (!out_index || handle == MOO_INPUT_HANDLE_INVALID) return 0;
    low = (uint32_t)handle;
    if ((low >> MOO_INPUT_HANDLE_KIND_SHIFT) != kind) return -1;
    encoded = low & MOO_INPUT_HANDLE_INDEX_MASK;
    if (encoded == 0u || encoded - 1u >= capacity) return 0;
    *out_index = encoded - 1u;
    return 1;
}

static uint32_t a11y_next_generation(uint32_t generation) {
    return generation == UINT32_MAX ? 0u : generation + 1u;
}

static int a11y_core_valid(const MooInputCore *core) {
    return core && core->clients && core->client_capacity > 0u &&
           core->targets && core->target_capacity > 0u &&
           core->events && core->event_capacity > 0u &&
           core->nodes && core->node_capacity > 0u;
}

static MooInputResult a11y_client(const MooInputCore *core,
                                  MooInputHandle handle, uint32_t *out) {
    uint32_t index;
    if (!a11y_core_valid(core) ||
        a11y_decode(handle, MOO_INPUT_KIND_CLIENT,
                    core->client_capacity, &index) <= 0 ||
        !core->clients[index].live ||
        core->clients[index].generation != (uint32_t)(handle >> 32u))
        return MOO_INPUT_STALE_HANDLE;
    if (out) *out = index;
    return MOO_INPUT_OK;
}

static MooInputResult a11y_target(const MooInputCore *core,
                                  MooInputHandle handle, uint32_t *out) {
    uint32_t index;
    if (!a11y_core_valid(core) ||
        a11y_decode(handle, MOO_INPUT_KIND_TARGET,
                    core->target_capacity, &index) <= 0 ||
        !core->targets[index].live ||
        core->targets[index].generation != (uint32_t)(handle >> 32u))
        return MOO_INPUT_STALE_HANDLE;
    if (out) *out = index;
    return MOO_INPUT_OK;
}

static MooInputResult a11y_node(const MooInputCore *core,
                                MooInputHandle handle, uint32_t *out) {
    uint32_t index;
    if (!a11y_core_valid(core) ||
        a11y_decode(handle, MOO_INPUT_KIND_NODE,
                    core->node_capacity, &index) <= 0 ||
        !core->nodes[index].live ||
        core->nodes[index].generation != (uint32_t)(handle >> 32u))
        return MOO_INPUT_STALE_HANDLE;
    if (out) *out = index;
    return MOO_INPUT_OK;
}

static int a11y_utf8(const MooInputText *text) {
    uint32_t i = 0u;
    if (!text || text->length > MOO_INPUT_TEXT_CAPACITY) return 0;
    while (i < text->length) {
        uint32_t cp;
        uint32_t need;
        uint8_t b = text->bytes[i++];
        if (b == 0u) return 0;
        if (b < 0x80u) continue;
        if (b >= 0xc2u && b <= 0xdfu) { cp = b & 0x1fu; need = 1u; }
        else if (b >= 0xe0u && b <= 0xefu) { cp = b & 0x0fu; need = 2u; }
        else if (b >= 0xf0u && b <= 0xf4u) { cp = b & 0x07u; need = 3u; }
        else return 0;
        if (need > text->length - i) return 0;
        while (need-- > 0u) {
            uint8_t c = text->bytes[i++];
            if ((c & 0xc0u) != 0x80u) return 0;
            cp = (cp << 6u) | (uint32_t)(c & 0x3fu);
        }
        if (cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu) ||
            cp < 0x80u || (cp < 0x800u && b >= 0xe0u) ||
            (cp < 0x10000u && b >= 0xf0u))
            return 0;
    }
    return 1;
}

static MooInputResult validate_parent(const MooInputCore *core,
                                      MooInputHandle owner,
                                      MooInputHandle target,
                                      MooInputHandle self,
                                      MooInputHandle parent) {
    uint32_t depth = 0u;
    while (parent != MOO_INPUT_HANDLE_INVALID) {
        uint32_t pi;
        MooInputResult result = a11y_node(core, parent, &pi);
        if (result != MOO_INPUT_OK) return result;
        if (core->nodes[pi].owner != owner ||
            core->nodes[pi].target != target) return MOO_INPUT_ACCESS;
        if (parent == self) return MOO_INPUT_BAD_STATE;
        if (++depth > core->config.max_a11y_depth)
            return MOO_INPUT_LIMIT;
        parent = core->nodes[pi].data.parent;
    }
    return MOO_INPUT_OK;
}

static MooInputResult validate_data(const MooInputCore *core,
                                    MooInputHandle owner,
                                    MooInputHandle target,
                                    MooInputHandle self,
                                    const MooA11yNodeData *data) {
    uint32_t known_states = MOO_A11Y_STATE_DISABLED | MOO_A11Y_STATE_FOCUSED |
        MOO_A11Y_STATE_CHECKED | MOO_A11Y_STATE_SELECTED |
        MOO_A11Y_STATE_EXPANDED | MOO_A11Y_STATE_PASSWORD |
        MOO_A11Y_STATE_HIDDEN;
    uint32_t known_actions = MOO_A11Y_ACTION_FOCUS |
        MOO_A11Y_ACTION_ACTIVATE | MOO_A11Y_ACTION_INCREMENT |
        MOO_A11Y_ACTION_DECREMENT | MOO_A11Y_ACTION_SET_VALUE |
        MOO_A11Y_ACTION_SET_SELECTION;
    if (!data || data->role <= MOO_A11Y_ROLE_NONE ||
        data->role > MOO_A11Y_ROLE_IMAGE ||
        (data->states & ~known_states) != 0u ||
        (data->actions & ~known_actions) != 0u ||
        data->bounds.width < 0 || data->bounds.height < 0 ||
        !a11y_utf8(&data->name) || !a11y_utf8(&data->value) ||
        !a11y_utf8(&data->description))
        return MOO_INPUT_INVALID;
    {
        MooInputHandle relations[3];
        uint32_t i;
        MooInputResult result = validate_parent(core, owner, target, self,
                                                 data->parent);
        if (result != MOO_INPUT_OK) return result;
        relations[0] = data->labelled_by;
        relations[1] = data->described_by;
        relations[2] = data->controls;
        for (i = 0u; i < 3u; ++i) {
            uint32_t ri;
            if (relations[i] == MOO_INPUT_HANDLE_INVALID) continue;
            result = a11y_node(core, relations[i], &ri);
            if (result != MOO_INPUT_OK) return result;
            if (relations[i] == self || core->nodes[ri].owner != owner ||
                core->nodes[ri].target != target)
                return MOO_INPUT_ACCESS;
        }
    }
    return MOO_INPUT_OK;
}

static uint32_t target_root_count(const MooInputCore *core,
                                  MooInputHandle target,
                                  MooInputHandle override_node,
                                  MooInputHandle override_parent,
                                  uint32_t *out_nodes) {
    uint32_t i;
    uint32_t roots = 0u;
    uint32_t nodes = 0u;
    for (i = 0u; i < core->node_capacity; ++i) {
        MooInputHandle handle;
        MooInputHandle parent;
        if (!core->nodes[i].live || core->nodes[i].target != target) continue;
        handle = a11y_make_handle(MOO_INPUT_KIND_NODE, i,
                                   core->nodes[i].generation);
        parent = handle == override_node ? override_parent
                                         : core->nodes[i].data.parent;
        nodes++;
        if (parent == MOO_INPUT_HANDLE_INVALID) roots++;
    }
    if (out_nodes) *out_nodes = nodes;
    return roots;
}

MooInputResult moo_a11y_node_create(MooInputCore *core,
    MooInputHandle client, MooInputHandle target,
    const MooA11yNodeData *data, MooInputHandle *out_node) {
    uint32_t ti;
    uint32_t i;
    MooInputResult result = a11y_client(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    result = a11y_target(core, target, &ti);
    if (result != MOO_INPUT_OK) return result;
    if (core->targets[ti].owner != client) return MOO_INPUT_ACCESS;
    if (!out_node) return MOO_INPUT_INVALID;
    if ((core->config.features & MOO_INPUT_FEATURE_ACCESSIBILITY) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    result = validate_data(core, client, target,
                           MOO_INPUT_HANDLE_INVALID, data);
    if (result != MOO_INPUT_OK) return result;
    {
        uint32_t existing_nodes = 0u;
        uint32_t roots = target_root_count(core, target,
            MOO_INPUT_HANDLE_INVALID, MOO_INPUT_HANDLE_INVALID,
            &existing_nodes);
        if ((existing_nodes == 0u &&
             data->parent != MOO_INPUT_HANDLE_INVALID) ||
            (existing_nodes != 0u &&
             (roots != 1u || data->parent == MOO_INPUT_HANDLE_INVALID)))
            return MOO_INPUT_BAD_STATE;
    }
    for (i = 0u; i < core->node_capacity; ++i) {
        if (!core->nodes[i].live && core->nodes[i].generation != 0u) {
            core->nodes[i].live = 1u;
            core->nodes[i].owner = client;
            core->nodes[i].target = target;
            core->nodes[i].revision = 1u;
            a11y_store_data(&core->nodes[i].data, data);
            *out_node = a11y_make_handle(MOO_INPUT_KIND_NODE, i,
                                          core->nodes[i].generation);
            return MOO_INPUT_OK;
        }
    }
    return MOO_INPUT_LIMIT;
}

MooInputResult moo_a11y_node_update(MooInputCore *core,
    MooInputHandle client, MooInputHandle node,
    uint64_t expected_revision, const MooA11yNodeData *data) {
    uint32_t ni;
    MooInputResult result = a11y_client(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    result = a11y_node(core, node, &ni);
    if (result != MOO_INPUT_OK) return result;
    if (core->nodes[ni].owner != client) return MOO_INPUT_ACCESS;
    if (core->nodes[ni].revision != expected_revision)
        return MOO_INPUT_STALE_REVISION;
    if (expected_revision == UINT64_MAX) return MOO_INPUT_LIMIT;
    if ((core->config.features & MOO_INPUT_FEATURE_ACCESSIBILITY) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    result = validate_data(core, client, core->nodes[ni].target, node, data);
    if (result != MOO_INPUT_OK) return result;
    if (target_root_count(core, core->nodes[ni].target, node,
                          data->parent, 0) != 1u)
        return MOO_INPUT_BAD_STATE;
    a11y_store_data(&core->nodes[ni].data, data);
    core->nodes[ni].revision++;
    return MOO_INPUT_OK;
}

MooInputResult moo_a11y_node_destroy(MooInputCore *core,
    MooInputHandle client, MooInputHandle node) {
    uint32_t ni;
    uint32_t i;
    MooInputResult result = a11y_client(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    result = a11y_node(core, node, &ni);
    if (result != MOO_INPUT_OK) return result;
    if (core->nodes[ni].owner != client) return MOO_INPUT_ACCESS;
    for (i = 0u; i < core->node_capacity; ++i) {
        const MooA11yNodeData *data = &core->nodes[i].data;
        if (core->nodes[i].live &&
            (data->parent == node || data->labelled_by == node ||
             data->described_by == node || data->controls == node))
            return MOO_INPUT_BAD_STATE;
    }
    a11y_clear_record(&core->nodes[ni].data,
                       sizeof(core->nodes[ni].data));
    core->nodes[ni].live = 0u;
    core->nodes[ni].generation =
        a11y_next_generation(core->nodes[ni].generation);
    return MOO_INPUT_OK;
}

static uint32_t proposal_index(const MooInputHandle *nodes,
                               uint32_t count, MooInputHandle handle) {
    uint32_t i;
    for (i = 0u; i < count; ++i)
        if (nodes[i] == handle) return i;
    return UINT32_MAX;
}

MooInputResult moo_a11y_nodes_update_atomic(MooInputCore *core,
    MooInputHandle client, const MooInputHandle *nodes,
    const uint64_t *expected_revisions, const MooA11yNodeData *data,
    uint32_t count) {
    uint32_t i;
    MooInputResult result = a11y_client(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_ACCESSIBILITY) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (!nodes || !expected_revisions || !data ||
        count == 0u || count > core->node_capacity)
        return MOO_INPUT_INVALID;
    for (i = 0u; i < count; ++i) {
        uint32_t ni;
        uint32_t j;
        result = a11y_node(core, nodes[i], &ni);
        if (result != MOO_INPUT_OK) return result;
        if (core->nodes[ni].owner != client) return MOO_INPUT_ACCESS;
        if (core->nodes[ni].revision != expected_revisions[i])
            return MOO_INPUT_STALE_REVISION;
        if (expected_revisions[i] == UINT64_MAX) return MOO_INPUT_LIMIT;
        for (j = 0u; j < i; ++j)
            if (nodes[j] == nodes[i]) return MOO_INPUT_INVALID;
        result = validate_data(core, client, core->nodes[ni].target,
                               nodes[i], &data[i]);
        if (result != MOO_INPUT_OK) return result;
    }
    /* Zweiter Pass benutzt die vorgeschlagenen Parent-Kanten als Overlay.
     * Dadurch werden Zyklen erkannt, die erst durch mehrere zugleich
     * gueltige Einzelupdates entstehen wuerden. */
    for (i = 0u; i < count; ++i) {
        uint32_t ni;
        uint32_t depth = 0u;
        MooInputHandle parent = data[i].parent;
        result = a11y_node(core, nodes[i], &ni);
        if (result != MOO_INPUT_OK) return result;
        while (parent != MOO_INPUT_HANDLE_INVALID) {
            uint32_t pi;
            uint32_t proposed;
            result = a11y_node(core, parent, &pi);
            if (result != MOO_INPUT_OK) return result;
            if (parent == nodes[i]) return MOO_INPUT_BAD_STATE;
            if (core->nodes[pi].owner != client ||
                core->nodes[pi].target != core->nodes[ni].target)
                return MOO_INPUT_ACCESS;
            if (++depth > core->config.max_a11y_depth)
                return MOO_INPUT_LIMIT;
            proposed = proposal_index(nodes, count, parent);
            parent = proposed == UINT32_MAX
                   ? core->nodes[pi].data.parent : data[proposed].parent;
        }
    }
    for (i = 0u; i < count; ++i) {
        uint32_t ni;
        uint32_t earlier;
        uint32_t roots = 0u;
        uint32_t live_nodes = 0u;
        result = a11y_node(core, nodes[i], &ni);
        if (result != MOO_INPUT_OK) return result;
        for (earlier = 0u; earlier < i; ++earlier) {
            uint32_t ei;
            result = a11y_node(core, nodes[earlier], &ei);
            if (result != MOO_INPUT_OK) return result;
            if (core->nodes[ei].target == core->nodes[ni].target) break;
        }
        if (earlier != i) continue;
        for (earlier = 0u; earlier < core->node_capacity; ++earlier) {
            MooInputHandle handle;
            MooInputHandle parent;
            uint32_t proposed;
            if (!core->nodes[earlier].live ||
                core->nodes[earlier].target != core->nodes[ni].target)
                continue;
            handle = a11y_make_handle(MOO_INPUT_KIND_NODE, earlier,
                                       core->nodes[earlier].generation);
            proposed = proposal_index(nodes, count, handle);
            parent = proposed == UINT32_MAX
                   ? core->nodes[earlier].data.parent
                   : data[proposed].parent;
            live_nodes++;
            if (parent == MOO_INPUT_HANDLE_INVALID) roots++;
        }
        if (live_nodes != 0u && roots != 1u)
            return MOO_INPUT_BAD_STATE;
    }
    for (i = 0u; i < count; ++i) {
        uint32_t ni;
        result = a11y_node(core, nodes[i], &ni);
        if (result != MOO_INPUT_OK) return result;
        a11y_store_data(&core->nodes[ni].data, &data[i]);
        core->nodes[ni].revision++;
    }
    return MOO_INPUT_OK;
}

MooInputResult moo_a11y_node_read(const MooInputCore *core,
    MooInputHandle requester, MooInputHandle node,
    MooA11yNodeData *out_data, uint64_t *out_revision) {
    uint32_t ci;
    uint32_t ni;
    uint32_t i;
    MooInputResult result;
    if (!out_data || !out_revision) return MOO_INPUT_INVALID;
    result = a11y_client(core, requester, &ci);
    if (result != MOO_INPUT_OK) return result;
    if ((core->clients[ci].capabilities & MOO_INPUT_CLIENT_SCREEN_READER) == 0u)
        return MOO_INPUT_ACCESS;
    result = a11y_node(core, node, &ni);
    if (result != MOO_INPUT_OK) return result;
    a11y_copy_record(out_data, &core->nodes[ni].data,
                      sizeof(*out_data));
    *out_revision = core->nodes[ni].revision;
    if ((out_data->states & MOO_A11Y_STATE_PASSWORD) != 0u) {
        for (i = 0u; i < MOO_INPUT_TEXT_CAPACITY; ++i)
            out_data->value.bytes[i] = 0u;
        out_data->value.length = 0u;
    }
    return MOO_INPUT_OK;
}

static uint32_t a11y_free_events(const MooInputCore *core) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->event_capacity; ++i)
        if (!core->events[i].live) count++;
    return count;
}

static uint32_t a11y_reserved_events(const MooInputCore *core) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->client_capacity; ++i)
        if (core->clients[i].live)
            count += core->clients[i].reserved_cleanup;
    return count;
}

MooInputResult moo_a11y_action(MooInputCore *core,
    MooInputHandle requester, MooInputHandle node,
    uint32_t action, int64_t value, uint64_t serial,
    uint64_t timestamp_ns) {
    uint32_t ni;
    uint32_t ci;
    uint32_t i;
    MooInputEvent event;
    MooInputResult result;
    if (!a11y_core_valid(core) || serial == 0u || serial == UINT64_MAX)
        return MOO_INPUT_INVALID;
    result = a11y_client(core, requester, &ci);
    if (result != MOO_INPUT_OK) return result;
    if ((core->clients[ci].capabilities & MOO_INPUT_CLIENT_AUTOMATION) == 0u)
        return MOO_INPUT_ACCESS;
    if ((core->config.features & MOO_INPUT_FEATURE_AUTOMATION) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (serial != core->accepted_serial + 1u)
        return MOO_INPUT_STALE_SERIAL;
    if (action == 0u || (action & (action - 1u)) != 0u)
        return MOO_INPUT_INVALID;
    result = a11y_node(core, node, &ni);
    if (result != MOO_INPUT_OK) return result;
    if ((core->nodes[ni].data.actions & action) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    result = a11y_client(core, core->nodes[ni].owner, &ci);
    if (result != MOO_INPUT_OK) return result;
    if (core->clients[ci].queued + core->clients[ci].reserved_cleanup >=
        core->config.max_events_per_client ||
        a11y_free_events(core) <= a11y_reserved_events(core) ||
        core->event_sequence == UINT64_MAX)
        return MOO_INPUT_WOULD_BLOCK;
    for (i = 0u; i < core->event_capacity; ++i) {
        if (!core->events[i].live) {
            {
                uint8_t *bytes = (uint8_t *)&event;
                size_t byte_index;
                for (byte_index = 0u; byte_index < sizeof(event); ++byte_index)
                    bytes[byte_index] = 0u;
            }
            event.type = MOO_INPUT_EVENT_A11Y_ACTION;
            event.flags = 0u;
            event.serial = serial;
            event.timestamp_ns = timestamp_ns;
            event.focus_epoch = core->focus_epoch;
            event.target = core->nodes[ni].target;
            event.data.a11y.node = node;
            event.data.a11y.action = action;
            event.data.a11y.reserved = 0u;
            event.data.a11y.value = value;
            core->event_sequence++;
            core->events[i].live = 1u;
            core->events[i].owner = core->nodes[ni].owner;
            core->events[i].sequence = core->event_sequence;
            a11y_copy_record(&core->events[i].event, &event,
                              sizeof(event));
            core->clients[ci].queued++;
            core->accepted_serial = serial;
            return MOO_INPUT_OK;
        }
    }
    return MOO_INPUT_WOULD_BLOCK;
}

static uint64_t hash_mix(uint64_t hash, uint64_t value) {
    uint32_t i;
    for (i = 0u; i < 8u; ++i) {
        hash ^= (uint8_t)(value >> (i * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_text(uint64_t hash, const MooInputText *text) {
    uint32_t i;
    hash = hash_mix(hash, text->length);
    for (i = 0u; i < text->length && i < MOO_INPUT_TEXT_CAPACITY; ++i) {
        hash ^= text->bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
static uint64_t hash_event(uint64_t hash, const MooInputEvent *event) {
    hash = hash_mix(hash, event->type);
    hash = hash_mix(hash, event->flags);
    hash = hash_mix(hash, event->serial);
    hash = hash_mix(hash, event->timestamp_ns);
    hash = hash_mix(hash, event->focus_epoch);
    hash = hash_mix(hash, event->target);
    switch (event->type) {
    case MOO_INPUT_EVENT_FOCUS:
        hash = hash_mix(hash, event->data.focus.focused);
        return hash_mix(hash, event->data.focus.reason);
    case MOO_INPUT_EVENT_POINTER_ENTER:
    case MOO_INPUT_EVENT_POINTER_LEAVE:
    case MOO_INPUT_EVENT_POINTER_MOTION:
        hash = hash_mix(hash, (uint32_t)event->data.pointer.x);
        hash = hash_mix(hash, (uint32_t)event->data.pointer.y);
        hash = hash_mix(hash, (uint32_t)event->data.pointer.dx);
        return hash_mix(hash, (uint32_t)event->data.pointer.dy);
    case MOO_INPUT_EVENT_POINTER_BUTTON:
        hash = hash_mix(hash, event->data.button.button);
        hash = hash_mix(hash, event->data.button.state);
        hash = hash_mix(hash, (uint32_t)event->data.button.x);
        return hash_mix(hash, (uint32_t)event->data.button.y);
    case MOO_INPUT_EVENT_POINTER_AXIS:
        hash = hash_mix(hash, (uint32_t)event->data.axis.x_120);
        return hash_mix(hash, (uint32_t)event->data.axis.y_120);
    case MOO_INPUT_EVENT_TOUCH:
        hash = hash_mix(hash, event->data.touch.id);
        hash = hash_mix(hash, event->data.touch.phase);
        hash = hash_mix(hash, (uint32_t)event->data.touch.x);
        hash = hash_mix(hash, (uint32_t)event->data.touch.y);
        return hash_mix(hash, event->data.touch.pressure);
    case MOO_INPUT_EVENT_STYLUS:
        hash = hash_mix(hash, (uint32_t)event->data.stylus.x);
        hash = hash_mix(hash, (uint32_t)event->data.stylus.y);
        hash = hash_mix(hash, event->data.stylus.pressure);
        hash = hash_mix(hash, (uint32_t)event->data.stylus.tilt_x);
        hash = hash_mix(hash, (uint32_t)event->data.stylus.tilt_y);
        return hash_mix(hash, event->data.stylus.buttons);
    case MOO_INPUT_EVENT_KEY:
        hash = hash_mix(hash, event->data.key.physical);
        hash = hash_mix(hash, event->data.key.logical);
        hash = hash_mix(hash, event->data.key.state);
        hash = hash_mix(hash, event->data.key.modifiers);
        return hash_mix(hash, event->data.key.repeat);
    case MOO_INPUT_EVENT_TEXT_PREEDIT:
    case MOO_INPUT_EVENT_TEXT_COMMIT:
    case MOO_INPUT_EVENT_TEXT_CANCEL:
        if ((event->flags & MOO_INPUT_EVENT_SENSITIVE) == 0u)
            hash = hash_text(hash, &event->data.text.text);
        else
            hash = hash_mix(hash, event->data.text.text.length);
        hash = hash_mix(hash, event->data.text.selection_start);
        hash = hash_mix(hash, event->data.text.selection_end);
        return hash_mix(hash, event->data.text.revision);
    case MOO_INPUT_EVENT_A11Y_ACTION:
        hash = hash_mix(hash, event->data.a11y.node);
        hash = hash_mix(hash, event->data.a11y.action);
        return hash_mix(hash, (uint64_t)event->data.a11y.value);
    case MOO_INPUT_EVENT_PREFERENCES:
        hash = hash_mix(hash, event->data.preferences.high_contrast);
        return hash_mix(hash, event->data.preferences.reduced_motion);
    case MOO_INPUT_EVENT_SHORTCUT:
        hash = hash_mix(hash, event->data.shortcut.id);
        hash = hash_mix(hash, event->data.shortcut.physical);
        return hash_mix(hash, event->data.shortcut.modifiers);
    default:
        return hash;
    }
}

uint64_t moo_input_state_hash(const MooInputCore *core) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t i;
    if (!a11y_core_valid(core)) return 0u;
    hash = hash_mix(hash, core->client_capacity);
    hash = hash_mix(hash, core->target_capacity);
    hash = hash_mix(hash, core->event_capacity);
    hash = hash_mix(hash, core->node_capacity);
    hash = hash_mix(hash, core->config.max_events_per_client);
    hash = hash_mix(hash, core->config.max_a11y_depth);
    hash = hash_mix(hash, core->config.features);
    hash = hash_mix(hash, core->focus);
    hash = hash_mix(hash, core->pointer_target);
    hash = hash_mix(hash, core->pointer_capture);
    hash = hash_mix(hash, core->focus_epoch);
    hash = hash_mix(hash, core->accepted_serial);
    hash = hash_mix(hash, core->event_sequence);
    hash = hash_mix(hash, core->ime_revision);
    hash = hash_mix(hash, core->ime_active);
    hash = hash_mix(hash, core->ime_target);
    hash = hash_mix(hash, core->pointer_buttons);
    hash = hash_mix(hash, (uint32_t)core->pointer_x);
    hash = hash_mix(hash, (uint32_t)core->pointer_y);
    hash = hash_mix(hash, core->lock_modifiers);
    hash = hash_mix(hash, core->high_contrast);
    hash = hash_mix(hash, core->reduced_motion);
    for (i = 0u; i < MOO_INPUT_KEY_WORDS; ++i)
        hash = hash_mix(hash, core->key_bits[i]);
    for (i = 0u; i < 256u; ++i)
        hash = hash_mix(hash, core->key_logical[i]);
    for (i = 0u; i < core->client_capacity; ++i) {
        hash = hash_mix(hash, core->clients[i].live);
        hash = hash_mix(hash, core->clients[i].generation);
        hash = hash_mix(hash, core->clients[i].queued);
        hash = hash_mix(hash, core->clients[i].reserved_cleanup);
        hash = hash_mix(hash, core->clients[i].capabilities);
    }
    for (i = 0u; i < core->target_capacity; ++i) {
        hash = hash_mix(hash, core->targets[i].live);
        hash = hash_mix(hash, core->targets[i].generation);
        if (!core->targets[i].live) continue;
        hash = hash_mix(hash, core->targets[i].owner);
        hash = hash_mix(hash, core->targets[i].surface);
        hash = hash_mix(hash, core->targets[i].text_mode);
    }
    for (i = 0u; i < MOO_INPUT_MAX_TOUCHES; ++i) {
        hash = hash_mix(hash, core->touches[i].live);
        if (!core->touches[i].live) continue;
        hash = hash_mix(hash, core->touches[i].id);
        hash = hash_mix(hash, core->touches[i].target);
    }
    for (i = 0u; i < MOO_INPUT_MAX_SHORTCUTS; ++i) {
        hash = hash_mix(hash, core->shortcuts[i].live);
        if (!core->shortcuts[i].live) continue;
        hash = hash_mix(hash, core->shortcuts[i].physical);
        hash = hash_mix(hash, core->shortcuts[i].modifiers);
        hash = hash_mix(hash, core->shortcuts[i].id);
        hash = hash_mix(hash, core->shortcuts[i].owner);
        hash = hash_mix(hash, core->shortcuts[i].target);
    }
    for (i = 0u; i < core->node_capacity; ++i) {
        const MooA11yNodeSlot *node_slot = &core->nodes[i];
        hash = hash_mix(hash, node_slot->live);
        hash = hash_mix(hash, node_slot->generation);
        if (!node_slot->live) continue;
        hash = hash_mix(hash, node_slot->owner);
        hash = hash_mix(hash, node_slot->target);
        hash = hash_mix(hash, node_slot->revision);
        hash = hash_mix(hash, node_slot->data.parent);
        hash = hash_mix(hash, node_slot->data.labelled_by);
        hash = hash_mix(hash, node_slot->data.described_by);
        hash = hash_mix(hash, node_slot->data.controls);
        hash = hash_mix(hash, node_slot->data.role);
        hash = hash_mix(hash, node_slot->data.states);
        hash = hash_mix(hash, node_slot->data.actions);
        hash = hash_mix(hash, (uint32_t)node_slot->data.bounds.x);
        hash = hash_mix(hash, (uint32_t)node_slot->data.bounds.y);
        hash = hash_mix(hash, (uint32_t)node_slot->data.bounds.width);
        hash = hash_mix(hash, (uint32_t)node_slot->data.bounds.height);
        hash = hash_text(hash, &node_slot->data.name);
        if ((node_slot->data.states & MOO_A11Y_STATE_PASSWORD) != 0u)
            hash = hash_mix(hash, UINT64_C(0x50415353574f5244));
        else
            hash = hash_text(hash, &node_slot->data.value);
        hash = hash_text(hash, &node_slot->data.description);
    }
    for (i = 0u; i < core->event_capacity; ++i) {
        const MooInputEventSlot *slot = &core->events[i];
        hash = hash_mix(hash, slot->live);
        if (!slot->live) continue;
        hash = hash_mix(hash, slot->owner);
        hash = hash_mix(hash, slot->sequence);
        hash = hash_event(hash, &slot->event);
    }
    return hash;
}
