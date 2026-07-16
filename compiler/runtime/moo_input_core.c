#include "moo_input_core.h"

#include <limits.h>

#define MOO_INPUT_HANDLE_KIND_SHIFT 28u
#define MOO_INPUT_HANDLE_INDEX_MASK UINT32_C(0x0fffffff)
#define MOO_INPUT_KIND_CLIENT 1u
#define MOO_INPUT_KIND_TARGET 2u
#define MOO_INPUT_KIND_NODE 3u

static void copy_record(void *destination, const void *source, size_t count);

static MooInputHandle make_handle(uint32_t kind, uint32_t index, uint32_t generation) {
    uint32_t low = (kind << MOO_INPUT_HANDLE_KIND_SHIFT) | (index + 1u);
    return ((uint64_t)generation << 32u) | (uint64_t)low;
}

static int decode_handle(MooInputHandle handle, uint32_t kind,
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

static uint32_t next_generation(uint32_t generation) {
    return generation == UINT32_MAX ? 0u : generation + 1u;
}

static int core_valid(const MooInputCore *core) {
    return core && core->clients && core->client_capacity > 0u &&
           core->targets && core->target_capacity > 0u &&
           core->events && core->event_capacity > 0u &&
           core->nodes && core->node_capacity > 0u &&
           core->config.max_events_per_client > 0u &&
           core->config.max_events_per_client <= core->event_capacity;
}

static MooInputResult client_slot(const MooInputCore *core, MooInputHandle handle,
                                  uint32_t *out_index) {
    uint32_t index;
    int decoded;
    if (!core_valid(core)) return MOO_INPUT_INVALID;
    decoded = decode_handle(handle, MOO_INPUT_KIND_CLIENT,
                            core->client_capacity, &index);
    if (decoded <= 0 || !core->clients[index].live ||
        core->clients[index].generation != (uint32_t)(handle >> 32u))
        return MOO_INPUT_STALE_HANDLE;
    if (out_index) *out_index = index;
    return MOO_INPUT_OK;
}

static MooInputResult target_slot(const MooInputCore *core, MooInputHandle handle,
                                  uint32_t *out_index) {
    uint32_t index;
    int decoded;
    if (!core_valid(core)) return MOO_INPUT_INVALID;
    decoded = decode_handle(handle, MOO_INPUT_KIND_TARGET,
                            core->target_capacity, &index);
    if (decoded <= 0 || !core->targets[index].live ||
        core->targets[index].generation != (uint32_t)(handle >> 32u))
        return MOO_INPUT_STALE_HANDLE;
    if (out_index) *out_index = index;
    return MOO_INPUT_OK;
}

static MooInputResult owner_for_target(const MooInputCore *core,
                                       MooInputHandle target,
                                       MooInputHandle *out_owner) {
    uint32_t index;
    MooInputResult result = target_slot(core, target, &index);
    if (result != MOO_INPUT_OK) return result;
    if (!out_owner) return MOO_INPUT_INVALID;
    *out_owner = core->targets[index].owner;
    return MOO_INPUT_OK;
}

static uint32_t free_events(const MooInputCore *core) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->event_capacity; ++i)
        if (!core->events[i].live) count++;
    return count;
}

static uint32_t total_cleanup_reservations(const MooInputCore *core) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < core->client_capacity; ++i)
        if (core->clients[i].live) count += core->clients[i].reserved_cleanup;
    return count;
}

static MooInputResult can_queue(const MooInputCore *core, MooInputHandle owner,
                                uint32_t count, uint32_t consume_reserved) {
    uint32_t ci;
    uint32_t free_count;
    uint32_t reserved;
    MooInputResult result = client_slot(core, owner, &ci);
    if (result != MOO_INPUT_OK) return result;
    if (consume_reserved > core->clients[ci].reserved_cleanup)
        return MOO_INPUT_BAD_STATE;
    if (count > UINT32_MAX - core->clients[ci].queued)
        return MOO_INPUT_LIMIT;
    if (core->clients[ci].queued + count +
            core->clients[ci].reserved_cleanup - consume_reserved >
        core->config.max_events_per_client)
        return MOO_INPUT_WOULD_BLOCK;
    free_count = free_events(core);
    reserved = total_cleanup_reservations(core);
    if (free_count < count || reserved < consume_reserved ||
        free_count - count < reserved - consume_reserved)
        return MOO_INPUT_WOULD_BLOCK;
    if (core->event_sequence > UINT64_MAX - count) return MOO_INPUT_LIMIT;
    return MOO_INPUT_OK;
}

static MooInputResult can_pointer_transition(const MooInputCore *core,
    MooInputHandle old_owner, uint32_t has_old,
    MooInputHandle new_owner, uint32_t has_new) {
    uint32_t old_index = UINT32_MAX;
    uint32_t new_index = UINT32_MAX;
    uint32_t old_count = has_old ? 1u : 0u;
    uint32_t new_count = has_new ? 2u : 0u;
    uint32_t total = old_count + new_count;
    uint32_t reserved = total_cleanup_reservations(core);
    uint32_t final_reserved;
    MooInputResult result;
    if (has_old) {
        result = client_slot(core, old_owner, &old_index);
        if (result != MOO_INPUT_OK) return result;
        if (core->clients[old_index].reserved_cleanup == 0u)
            return MOO_INPUT_BAD_STATE;
    }
    if (has_new) {
        result = client_slot(core, new_owner, &new_index);
        if (result != MOO_INPUT_OK) return result;
        if (core->clients[new_index].reserved_cleanup == UINT32_MAX)
            return MOO_INPUT_LIMIT;
    }
    if (reserved < old_count ||
        (has_new && reserved - old_count == UINT32_MAX))
        return MOO_INPUT_LIMIT;
    final_reserved = reserved - old_count + (has_new ? 1u : 0u);
    if (old_index != UINT32_MAX && old_index == new_index) {
        uint32_t ci = old_index;
        uint32_t final_client_reserved =
            core->clients[ci].reserved_cleanup - 1u + (has_new ? 1u : 0u);
        if (total > core->config.max_events_per_client ||
            core->clients[ci].queued >
                core->config.max_events_per_client - total ||
            final_client_reserved >
                core->config.max_events_per_client - total -
                    core->clients[ci].queued)
            return MOO_INPUT_WOULD_BLOCK;
    } else {
        if (old_index != UINT32_MAX &&
            (old_count > core->config.max_events_per_client ||
             core->clients[old_index].queued >
                core->config.max_events_per_client - old_count ||
             core->clients[old_index].reserved_cleanup - 1u >
                core->config.max_events_per_client - old_count -
                    core->clients[old_index].queued))
            return MOO_INPUT_WOULD_BLOCK;
        if (new_index != UINT32_MAX &&
            (new_count > core->config.max_events_per_client ||
             core->clients[new_index].queued >
                core->config.max_events_per_client - new_count ||
             core->clients[new_index].reserved_cleanup + 1u >
                core->config.max_events_per_client - new_count -
                    core->clients[new_index].queued))
            return MOO_INPUT_WOULD_BLOCK;
    }
    if (free_events(core) < total ||
        free_events(core) - total < final_reserved)
        return MOO_INPUT_WOULD_BLOCK;
    if (core->event_sequence > UINT64_MAX - total)
        return MOO_INPUT_LIMIT;
    return MOO_INPUT_OK;
}

static MooInputResult push_event(MooInputCore *core, MooInputHandle owner,
                                 const MooInputEvent *event,
                                 uint32_t consume_reserved) {
    uint32_t ci;
    uint32_t i;
    MooInputResult result = can_queue(core, owner, 1u, consume_reserved);
    if (result != MOO_INPUT_OK) return result;
    result = client_slot(core, owner, &ci);
    if (result != MOO_INPUT_OK) return result;
    for (i = 0u; i < core->event_capacity; ++i) {
        if (!core->events[i].live) {
            core->event_sequence++;
            core->events[i].live = 1u;
            core->events[i].owner = owner;
            core->events[i].sequence = core->event_sequence;
            copy_record(&core->events[i].event, event, sizeof(*event));
            core->clients[ci].queued++;
            core->clients[ci].reserved_cleanup -= consume_reserved;
            return MOO_INPUT_OK;
        }
    }
    return MOO_INPUT_WOULD_BLOCK;
}

static MooInputResult serial_ok(const MooInputCore *core, uint64_t serial) {
    if (!core_valid(core) || serial == 0u || serial == UINT64_MAX)
        return MOO_INPUT_INVALID;
    if (core->accepted_serial == UINT64_MAX ||
        serial != core->accepted_serial + 1u)
        return MOO_INPUT_STALE_SERIAL;
    return MOO_INPUT_OK;
}

static void copy_record(void *destination, const void *source, size_t count) {
    volatile uint8_t *dst = (volatile uint8_t *)destination;
    const volatile uint8_t *src = (const volatile uint8_t *)source;
    size_t i;
    for (i = 0u; i < count; ++i) dst[i] = src[i];
}

static void clear_record(void *record, size_t count) {
    volatile uint8_t *bytes = (volatile uint8_t *)record;
    size_t i;
    for (i = 0u; i < count; ++i) bytes[i] = 0u;
}

static void clear_event(MooInputEvent *event) {
    clear_record(event, sizeof(*event));
}

static void clear_event_slot(MooInputEventSlot *slot) {
    clear_record(slot, sizeof(*slot));
}

static void event_base(MooInputEvent *event, uint32_t type,
                       MooInputHandle target, uint64_t serial,
                       uint64_t timestamp_ns, uint64_t focus_epoch) {
    clear_event(event);
    event->type = type;
    event->flags = 0u;
    event->serial = serial;
    event->timestamp_ns = timestamp_ns;
    event->focus_epoch = focus_epoch;
    event->target = target;
}

static int key_is_down(const MooInputCore *core, uint32_t physical) {
    return (core->key_bits[physical / 64u] &
            (UINT64_C(1) << (physical % 64u))) != 0u;
}

static uint32_t modifiers(const MooInputCore *core) {
    uint32_t mods = 0u;
    if (key_is_down(core, 225u) || key_is_down(core, 229u))
        mods |= MOO_INPUT_MOD_SHIFT;
    if (key_is_down(core, 224u) || key_is_down(core, 228u))
        mods |= MOO_INPUT_MOD_CONTROL;
    if (key_is_down(core, 226u) || key_is_down(core, 230u))
        mods |= MOO_INPUT_MOD_ALT;
    if (key_is_down(core, 227u) || key_is_down(core, 231u))
        mods |= MOO_INPUT_MOD_SUPER;
    if (key_is_down(core, 230u)) mods |= MOO_INPUT_MOD_ALT_GR;
    mods |= core->lock_modifiers;
    return mods;
}

MooInputResult moo_input_init(MooInputCore *core, const MooInputConfig *config,
    MooInputClientSlot *clients, uint32_t client_capacity,
    MooInputTargetSlot *targets, uint32_t target_capacity,
    MooInputEventSlot *events, uint32_t event_capacity,
    MooA11yNodeSlot *nodes, uint32_t node_capacity) {
    uint32_t i;
    if (!core || !config ||
        (config->features & ~(MOO_INPUT_FEATURE_POINTER |
          MOO_INPUT_FEATURE_TOUCH | MOO_INPUT_FEATURE_STYLUS |
          MOO_INPUT_FEATURE_KEYBOARD | MOO_INPUT_FEATURE_IME |
          MOO_INPUT_FEATURE_SHORTCUTS | MOO_INPUT_FEATURE_ACCESSIBILITY |
          MOO_INPUT_FEATURE_HIGH_CONTRAST | MOO_INPUT_FEATURE_REDUCED_MOTION |
          MOO_INPUT_FEATURE_AUTOMATION)) != 0u ||
        !clients || client_capacity == 0u ||
        !targets || target_capacity == 0u || !events || event_capacity == 0u ||
        !nodes || node_capacity == 0u || config->max_events_per_client == 0u ||
        config->max_events_per_client > event_capacity ||
        config->max_a11y_depth == 0u)
        return MOO_INPUT_INVALID;
    core->clients = clients; core->client_capacity = client_capacity;
    core->targets = targets; core->target_capacity = target_capacity;
    core->events = events; core->event_capacity = event_capacity;
    core->nodes = nodes; core->node_capacity = node_capacity;
    core->config.max_events_per_client = config->max_events_per_client;
    core->config.max_a11y_depth = config->max_a11y_depth;
    core->config.features = config->features;
    core->focus = MOO_INPUT_HANDLE_INVALID;
    core->pointer_target = MOO_INPUT_HANDLE_INVALID;
    core->pointer_capture = MOO_INPUT_HANDLE_INVALID;
    core->focus_epoch = 1u; core->accepted_serial = 0u;
    core->event_sequence = 0u; core->ime_revision = 0u;
    core->ime_active = 0u;
    core->ime_target = MOO_INPUT_HANDLE_INVALID;
    core->pointer_buttons = 0u; core->lock_modifiers = 0u;
    core->pointer_x = 0; core->pointer_y = 0;
    core->high_contrast = 0u;
    core->reduced_motion = 0u;
    for (i = 0u; i < MOO_INPUT_KEY_WORDS; ++i) core->key_bits[i] = 0u;
    for (i = 0u; i < 256u; ++i) core->key_logical[i] = 0u;
    for (i = 0u; i < MOO_INPUT_MAX_TOUCHES; ++i) {
        core->touches[i].live = 0u;
        core->touches[i].id = 0u;
        core->touches[i].target = MOO_INPUT_HANDLE_INVALID;
    }
    for (i = 0u; i < MOO_INPUT_MAX_SHORTCUTS; ++i) {
        core->shortcuts[i].live = 0u;
        core->shortcuts[i].physical = 0u;
        core->shortcuts[i].modifiers = 0u;
        core->shortcuts[i].reserved = 0u;
        core->shortcuts[i].id = 0u;
        core->shortcuts[i].owner = MOO_INPUT_HANDLE_INVALID;
        core->shortcuts[i].target = MOO_INPUT_HANDLE_INVALID;
    }
    for (i = 0u; i < client_capacity; ++i) {
        clients[i].live = 0u; clients[i].generation = 1u;
        clients[i].queued = 0u; clients[i].reserved_cleanup = 0u;
        clients[i].capabilities = 0u;
    }
    for (i = 0u; i < target_capacity; ++i) {
        targets[i].live = 0u; targets[i].generation = 1u;
        targets[i].owner = MOO_INPUT_HANDLE_INVALID; targets[i].surface = 0u;
        targets[i].text_mode = 0u; targets[i].reserved = 0u;
    }
    for (i = 0u; i < event_capacity; ++i)
        clear_event_slot(&events[i]);
    for (i = 0u; i < node_capacity; ++i) {
        nodes[i].live = 0u; nodes[i].generation = 1u;
        nodes[i].owner = MOO_INPUT_HANDLE_INVALID;
        nodes[i].target = MOO_INPUT_HANDLE_INVALID; nodes[i].revision = 0u;
    }
    return MOO_INPUT_OK;
}

MooInputResult moo_input_client_create(MooInputCore *core,
                                       uint32_t capabilities,
                                       MooInputHandle *out_client) {
    uint32_t i;
    if (!core_valid(core) || !out_client ||
        ((capabilities & MOO_INPUT_CLIENT_SCREEN_READER) != 0u &&
         (core->config.features & MOO_INPUT_FEATURE_ACCESSIBILITY) == 0u) ||
        ((capabilities & MOO_INPUT_CLIENT_AUTOMATION) != 0u &&
         (core->config.features & MOO_INPUT_FEATURE_AUTOMATION) == 0u) ||
        (capabilities & ~(MOO_INPUT_CLIENT_SCREEN_READER |
                          MOO_INPUT_CLIENT_AUTOMATION)) != 0u)
        return MOO_INPUT_INVALID;
    for (i = 0u; i < core->client_capacity; ++i) {
        if (!core->clients[i].live && core->clients[i].generation != 0u) {
            core->clients[i].live = 1u;
            core->clients[i].queued = 0u;
            core->clients[i].reserved_cleanup = 0u;
            core->clients[i].capabilities = capabilities;
            *out_client = make_handle(MOO_INPUT_KIND_CLIENT, i,
                                      core->clients[i].generation);
            return MOO_INPUT_OK;
        }
    }
    return MOO_INPUT_LIMIT;
}

MooInputResult moo_input_client_disconnect(MooInputCore *core,
                                           MooInputHandle client,
                                           uint64_t serial) {
    uint32_t ci;
    uint32_t i;
    uint32_t disconnected_capture = 0u;
    uint32_t disconnected_focus = 0u;
    MooInputResult result = client_slot(core, client, &ci);
    if (result != MOO_INPUT_OK) return result;
    result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if (core->focus != MOO_INPUT_HANDLE_INVALID) {
        MooInputHandle focus_owner;
        result = owner_for_target(core, core->focus, &focus_owner);
        if (result != MOO_INPUT_OK) return result;
        if (focus_owner == client && core->focus_epoch == UINT64_MAX)
            return MOO_INPUT_LIMIT;
    }
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].live && core->events[i].owner == client)
            clear_event_slot(&core->events[i]);
    }
    for (i = 0u; i < MOO_INPUT_MAX_SHORTCUTS; ++i)
        if (core->shortcuts[i].live && core->shortcuts[i].owner == client)
            core->shortcuts[i].live = 0u;
    for (i = 0u; i < core->node_capacity; ++i) {
        if (core->nodes[i].live && core->nodes[i].owner == client) {
            clear_record(&core->nodes[i].data, sizeof(core->nodes[i].data));
            core->nodes[i].live = 0u;
            core->nodes[i].generation = next_generation(core->nodes[i].generation);
        }
    }
    for (i = 0u; i < core->target_capacity; ++i) {
        MooInputHandle target = make_handle(MOO_INPUT_KIND_TARGET, i,
                                             core->targets[i].generation);
        if (!core->targets[i].live || core->targets[i].owner != client) continue;
        if (core->focus == target) {
            core->focus = MOO_INPUT_HANDLE_INVALID;
            disconnected_focus = 1u;
        }
        if (core->pointer_target == target)
            core->pointer_target = MOO_INPUT_HANDLE_INVALID;
        if (core->pointer_capture == target) {
            core->pointer_capture = MOO_INPUT_HANDLE_INVALID;
            disconnected_capture = 1u;
        }
        core->targets[i].live = 0u;
        core->targets[i].generation = next_generation(core->targets[i].generation);
    }
    for (i = 0u; i < MOO_INPUT_MAX_TOUCHES; ++i) {
        MooInputHandle owner;
        if (core->touches[i].live &&
            owner_for_target(core, core->touches[i].target, &owner) != MOO_INPUT_OK)
            core->touches[i].live = 0u;
    }
    if (disconnected_capture) core->pointer_buttons = 0u;
    if (disconnected_focus) {
        core->focus_epoch++;
        core->ime_revision = 0u;
        core->ime_active = 0u;
        core->ime_target = MOO_INPUT_HANDLE_INVALID;
        for (i = 0u; i < MOO_INPUT_KEY_WORDS; ++i) core->key_bits[i] = 0u;
        for (i = 0u; i < 256u; ++i) core->key_logical[i] = 0u;
    }
    core->clients[ci].live = 0u;
    core->clients[ci].queued = 0u;
    core->clients[ci].reserved_cleanup = 0u;
    core->clients[ci].capabilities = 0u;
    core->clients[ci].generation = next_generation(core->clients[ci].generation);
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_target_create_trusted(MooInputCore *core,
    MooInputHandle client, uint64_t surface, uint32_t text_mode,
    MooInputHandle *out_target) {
    uint32_t i;
    MooInputResult result = client_slot(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    if (!out_target || surface == 0u || text_mode > MOO_INPUT_TEXT_SENSITIVE)
        return MOO_INPUT_INVALID;
    for (i = 0u; i < core->target_capacity; ++i) {
        if (!core->targets[i].live && core->targets[i].generation != 0u) {
            core->targets[i].live = 1u;
            core->targets[i].owner = client;
            core->targets[i].surface = surface;
            core->targets[i].text_mode = text_mode;
            *out_target = make_handle(MOO_INPUT_KIND_TARGET, i,
                                       core->targets[i].generation);
            return MOO_INPUT_OK;
        }
    }
    return MOO_INPUT_LIMIT;
}

MooInputResult moo_input_target_destroy(MooInputCore *core,
    MooInputHandle client, MooInputHandle target, uint64_t serial) {
    uint32_t ti;
    uint32_t i;
    MooInputResult result = client_slot(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    result = target_slot(core, target, &ti);
    if (result != MOO_INPUT_OK) return result;
    if (core->targets[ti].owner != client) return MOO_INPUT_ACCESS;
    result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if (core->focus == target || core->pointer_capture == target ||
        core->pointer_target == target)
        return MOO_INPUT_BAD_STATE;
    for (i = 0u; i < MOO_INPUT_MAX_TOUCHES; ++i)
        if (core->touches[i].live && core->touches[i].target == target)
            return MOO_INPUT_BAD_STATE;
    for (i = 0u; i < core->node_capacity; ++i)
        if (core->nodes[i].live && core->nodes[i].target == target)
            return MOO_INPUT_BAD_STATE;
    if (core->pointer_target == target)
        core->pointer_target = MOO_INPUT_HANDLE_INVALID;
    core->targets[ti].live = 0u;
    core->targets[ti].generation = next_generation(core->targets[ti].generation);
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

static uint32_t key_down_count(const MooInputCore *core) {
    uint32_t physical;
    uint32_t count = 0u;
    for (physical = 0u; physical < 256u; ++physical)
        if (key_is_down(core, physical)) count++;
    return count;
}

static MooInputResult can_focus_transition(const MooInputCore *core,
    MooInputHandle old_owner, uint32_t old_count, uint32_t old_consume,
    MooInputHandle new_owner, uint32_t new_count) {
    uint32_t oi = UINT32_MAX;
    uint32_t ni = UINT32_MAX;
    uint32_t total = old_count + new_count;
    uint32_t reserved = total_cleanup_reservations(core);
    MooInputResult result;
    if (total < old_count || old_consume > reserved)
        return MOO_INPUT_LIMIT;
    if (old_count != 0u) {
        result = client_slot(core, old_owner, &oi);
        if (result != MOO_INPUT_OK) return result;
        if (old_consume > core->clients[oi].reserved_cleanup)
            return MOO_INPUT_BAD_STATE;
    }
    if (new_count != 0u) {
        result = client_slot(core, new_owner, &ni);
        if (result != MOO_INPUT_OK) return result;
    }
    if (oi != UINT32_MAX && oi == ni) {
        if (core->clients[oi].queued + old_count + new_count +
                core->clients[oi].reserved_cleanup - old_consume >
            core->config.max_events_per_client)
            return MOO_INPUT_WOULD_BLOCK;
    } else {
        if (oi != UINT32_MAX &&
            core->clients[oi].queued + old_count +
                core->clients[oi].reserved_cleanup - old_consume >
            core->config.max_events_per_client)
            return MOO_INPUT_WOULD_BLOCK;
        if (ni != UINT32_MAX &&
            core->clients[ni].queued + new_count +
                core->clients[ni].reserved_cleanup >
            core->config.max_events_per_client)
            return MOO_INPUT_WOULD_BLOCK;
    }
    if (free_events(core) < total ||
        free_events(core) - total < reserved - old_consume)
        return MOO_INPUT_WOULD_BLOCK;
    if (core->event_sequence > UINT64_MAX - total)
        return MOO_INPUT_LIMIT;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_set_focus(MooInputCore *core, MooInputHandle target,
    uint32_t reason, uint64_t serial, uint64_t timestamp_ns) {
    MooInputHandle old_owner = MOO_INPUT_HANDLE_INVALID;
    MooInputHandle new_owner = MOO_INPUT_HANDLE_INVALID;
    MooInputEvent event;
    uint32_t cleanup_count;
    uint32_t physical;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if (core->focus_epoch == UINT64_MAX) return MOO_INPUT_LIMIT;
    if (target != MOO_INPUT_HANDLE_INVALID) {
        result = owner_for_target(core, target, &new_owner);
        if (result != MOO_INPUT_OK) return result;
    }
    if (core->focus == target) {
        core->accepted_serial = serial;
        return MOO_INPUT_OK;
    }
    cleanup_count = key_down_count(core) + (core->ime_active ? 1u : 0u);
    if (core->focus != MOO_INPUT_HANDLE_INVALID) {
        result = owner_for_target(core, core->focus, &old_owner);
        if (result != MOO_INPUT_OK) return result;
    } else if (cleanup_count != 0u) {
        return MOO_INPUT_BAD_STATE;
    }
    result = can_focus_transition(
        core, old_owner,
        core->focus != MOO_INPUT_HANDLE_INVALID ? cleanup_count + 1u : 0u,
        cleanup_count, new_owner,
        target != MOO_INPUT_HANDLE_INVALID ? 1u : 0u);
    if (result != MOO_INPUT_OK) return result;
    core->focus_epoch++;
    for (physical = 0u; physical < 256u; ++physical) {
        uint32_t word;
        uint64_t bit;
        if (!key_is_down(core, physical)) continue;
        word = physical / 64u;
        bit = UINT64_C(1) << (physical % 64u);
        core->key_bits[word] &= ~bit;
        event_base(&event, MOO_INPUT_EVENT_KEY, core->focus, serial,
                   timestamp_ns, core->focus_epoch);
        event.data.key.physical = physical;
        event.data.key.logical = core->key_logical[physical];
        event.data.key.state = MOO_INPUT_RELEASED;
        event.data.key.modifiers = modifiers(core);
        event.data.key.repeat = 0u;
        result = push_event(core, old_owner, &event, 1u);
        if (result != MOO_INPUT_OK) return result;
        core->key_logical[physical] = 0u;
    }
    if (core->ime_active) {
        event_base(&event, MOO_INPUT_EVENT_TEXT_CANCEL, core->focus, serial,
                   timestamp_ns, core->focus_epoch);
        event.data.text.text.length = 0u;
        event.data.text.selection_start = 0u;
        event.data.text.selection_end = 0u;
        event.data.text.revision = core->ime_revision + 1u;
        result = push_event(core, old_owner, &event, 1u);
        if (result != MOO_INPUT_OK) return result;
        core->ime_active = 0u;
        core->ime_target = MOO_INPUT_HANDLE_INVALID;
    }
    core->ime_revision = 0u;
    if (core->focus != MOO_INPUT_HANDLE_INVALID) {
        event_base(&event, MOO_INPUT_EVENT_FOCUS, core->focus, serial,
                   timestamp_ns, core->focus_epoch);
        event.data.focus.focused = 0u; event.data.focus.reason = reason;
        result = push_event(core, old_owner, &event, 0u);
        if (result != MOO_INPUT_OK) return result;
    }
    core->focus = target;
    if (target != MOO_INPUT_HANDLE_INVALID) {
        event_base(&event, MOO_INPUT_EVENT_FOCUS, target, serial,
                   timestamp_ns, core->focus_epoch);
        event.data.focus.focused = 1u; event.data.focus.reason = reason;
        result = push_event(core, new_owner, &event, 0u);
        if (result != MOO_INPUT_OK) return result;
    }
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_pointer_motion(MooInputCore *core,
    MooInputHandle target, int32_t x, int32_t y, int32_t dx, int32_t dy,
    uint64_t serial, uint64_t timestamp_ns) {
    MooInputHandle dispatch = target;
    MooInputHandle owner = MOO_INPUT_HANDLE_INVALID;
    MooInputHandle old_owner = MOO_INPUT_HANDLE_INVALID;
    MooInputEvent event;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_POINTER) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (core->pointer_capture != MOO_INPUT_HANDLE_INVALID)
        dispatch = core->pointer_capture;
    if (dispatch != MOO_INPUT_HANDLE_INVALID) {
        result = owner_for_target(core, dispatch, &owner);
        if (result != MOO_INPUT_OK) return result;
    }
    if (core->pointer_capture == MOO_INPUT_HANDLE_INVALID &&
        core->pointer_target != target) {
        if (core->pointer_target != MOO_INPUT_HANDLE_INVALID) {
            result = owner_for_target(core, core->pointer_target, &old_owner);
            if (result != MOO_INPUT_OK) return result;
        }
        if (target != MOO_INPUT_HANDLE_INVALID) {
            result = owner_for_target(core, target, &owner);
            if (result != MOO_INPUT_OK) return result;
        }
        result = can_pointer_transition(core, old_owner,
                    core->pointer_target != MOO_INPUT_HANDLE_INVALID,
                    owner, target != MOO_INPUT_HANDLE_INVALID);
        if (result != MOO_INPUT_OK) return result;
        if (target != MOO_INPUT_HANDLE_INVALID) {
            uint32_t ci;
            result = client_slot(core, owner, &ci);
            if (result != MOO_INPUT_OK) return result;
            core->clients[ci].reserved_cleanup++;
        }
        if (core->pointer_target != MOO_INPUT_HANDLE_INVALID) {
            event_base(&event, MOO_INPUT_EVENT_POINTER_LEAVE,
                       core->pointer_target, serial, timestamp_ns,
                       core->focus_epoch);
            event.data.pointer.x = x; event.data.pointer.y = y;
            event.data.pointer.dx = dx; event.data.pointer.dy = dy;
            result = push_event(core, old_owner, &event, 1u);
            if (result != MOO_INPUT_OK) return result;
        }
        core->pointer_target = target;
        if (target != MOO_INPUT_HANDLE_INVALID) {
            event_base(&event, MOO_INPUT_EVENT_POINTER_ENTER, target, serial,
                       timestamp_ns, core->focus_epoch);
            event.data.pointer.x = x; event.data.pointer.y = y;
            event.data.pointer.dx = dx; event.data.pointer.dy = dy;
            result = push_event(core, owner, &event, 0u);
            if (result != MOO_INPUT_OK) return result;
        }
    }
    if (dispatch != MOO_INPUT_HANDLE_INVALID) {
        result = can_queue(core, owner, 1u, 0u);
        if (result != MOO_INPUT_OK) return result;
        event_base(&event, MOO_INPUT_EVENT_POINTER_MOTION, dispatch, serial,
                   timestamp_ns, core->focus_epoch);
        event.data.pointer.x = x; event.data.pointer.y = y;
        event.data.pointer.dx = dx; event.data.pointer.dy = dy;
        result = push_event(core, owner, &event, 0u);
        if (result != MOO_INPUT_OK) return result;
    }
    core->pointer_x = x;
    core->pointer_y = y;
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_pointer_button(MooInputCore *core, uint32_t button,
    uint32_t state, int32_t x, int32_t y, uint64_t serial,
    uint64_t timestamp_ns) {
    MooInputHandle target;
    MooInputHandle owner;
    MooInputEvent event;
    uint32_t ci;
    uint32_t bit;
    uint32_t consume;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_POINTER) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (button >= 32u || state > MOO_INPUT_PRESSED)
        return MOO_INPUT_INVALID;
    target = core->pointer_capture != MOO_INPUT_HANDLE_INVALID
           ? core->pointer_capture : core->pointer_target;
    if (target == MOO_INPUT_HANDLE_INVALID) return MOO_INPUT_BAD_STATE;
    result = owner_for_target(core, target, &owner);
    if (result != MOO_INPUT_OK) return result;
    result = client_slot(core, owner, &ci);
    if (result != MOO_INPUT_OK) return result;
    bit = UINT32_C(1) << button;
    if (state == MOO_INPUT_PRESSED) {
        if ((core->pointer_buttons & bit) != 0u) return MOO_INPUT_BAD_STATE;
        if (core->clients[ci].reserved_cleanup == UINT32_MAX)
            return MOO_INPUT_LIMIT;
        core->clients[ci].reserved_cleanup++;
        result = can_queue(core, owner, 1u, 0u);
        if (result != MOO_INPUT_OK) {
            core->clients[ci].reserved_cleanup--;
            return result;
        }
        consume = 0u;
    } else {
        if ((core->pointer_buttons & bit) == 0u) return MOO_INPUT_BAD_STATE;
        consume = 1u;
        result = can_queue(core, owner, 1u, consume);
        if (result != MOO_INPUT_OK) return result;
    }
    event_base(&event, MOO_INPUT_EVENT_POINTER_BUTTON, target, serial,
               timestamp_ns, core->focus_epoch);
    event.data.button.button = button; event.data.button.state = state;
    event.data.button.x = x; event.data.button.y = y;
    result = push_event(core, owner, &event, consume);
    if (result != MOO_INPUT_OK) return result;
    if (state == MOO_INPUT_PRESSED) {
        core->pointer_buttons |= bit;
        core->pointer_capture = target;
    } else {
        core->pointer_buttons &= ~bit;
        if (core->pointer_buttons == 0u)
            core->pointer_capture = MOO_INPUT_HANDLE_INVALID;
    }
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_pointer_axis(MooInputCore *core,
    int32_t x_120, int32_t y_120, uint64_t serial, uint64_t timestamp_ns) {
    MooInputHandle target;
    MooInputHandle owner;
    MooInputEvent event;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_POINTER) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    target = core->pointer_capture != MOO_INPUT_HANDLE_INVALID
           ? core->pointer_capture : core->pointer_target;
    if (target == MOO_INPUT_HANDLE_INVALID) return MOO_INPUT_BAD_STATE;
    result = owner_for_target(core, target, &owner);
    if (result != MOO_INPUT_OK) return result;
    event_base(&event, MOO_INPUT_EVENT_POINTER_AXIS, target, serial,
               timestamp_ns, core->focus_epoch);
    event.data.axis.x_120 = x_120; event.data.axis.y_120 = y_120;
    result = push_event(core, owner, &event, 0u);
    if (result != MOO_INPUT_OK) return result;
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_pointer_cancel(MooInputCore *core,
    uint64_t serial, uint64_t timestamp_ns) {
    MooInputHandle target;
    MooInputHandle owner;
    MooInputEvent event;
    uint32_t button;
    uint32_t cleanup = 0u;
    uint32_t needed;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_POINTER) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    target = core->pointer_capture != MOO_INPUT_HANDLE_INVALID
           ? core->pointer_capture : core->pointer_target;
    if (target == MOO_INPUT_HANDLE_INVALID) {
        core->accepted_serial = serial;
        return MOO_INPUT_OK;
    }
    result = owner_for_target(core, target, &owner);
    if (result != MOO_INPUT_OK) return result;
    for (button = 0u; button < 32u; ++button)
        if ((core->pointer_buttons & (UINT32_C(1) << button)) != 0u)
            cleanup++;
    if (core->pointer_target != MOO_INPUT_HANDLE_INVALID) cleanup++;
    needed = cleanup;
    result = can_queue(core, owner, needed, cleanup);
    if (result != MOO_INPUT_OK) return result;
    for (button = 0u; button < 32u; ++button) {
        uint32_t bit = UINT32_C(1) << button;
        if ((core->pointer_buttons & bit) == 0u) continue;
        event_base(&event, MOO_INPUT_EVENT_POINTER_BUTTON, target, serial,
                   timestamp_ns, core->focus_epoch);
        event.flags = MOO_INPUT_EVENT_SYNTHETIC;
        event.data.button.button = button;
        event.data.button.state = MOO_INPUT_RELEASED;
        event.data.button.x = core->pointer_x;
        event.data.button.y = core->pointer_y;
        result = push_event(core, owner, &event, 1u);
        if (result != MOO_INPUT_OK) return result;
    }
    if (core->pointer_target != MOO_INPUT_HANDLE_INVALID) {
        event_base(&event, MOO_INPUT_EVENT_POINTER_LEAVE,
                   core->pointer_target, serial, timestamp_ns,
                   core->focus_epoch);
        event.flags = MOO_INPUT_EVENT_SYNTHETIC;
        event.data.pointer.x = core->pointer_x;
        event.data.pointer.y = core->pointer_y;
        event.data.pointer.dx = 0;
        event.data.pointer.dy = 0;
        result = push_event(core, owner, &event, 1u);
        if (result != MOO_INPUT_OK) return result;
    }
    core->pointer_buttons = 0u;
    core->pointer_capture = MOO_INPUT_HANDLE_INVALID;
    core->pointer_target = MOO_INPUT_HANDLE_INVALID;
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_stylus(MooInputCore *core, MooInputHandle target,
    int32_t x, int32_t y, uint32_t pressure, int32_t tilt_x, int32_t tilt_y,
    uint32_t buttons, uint64_t serial, uint64_t timestamp_ns) {
    MooInputHandle owner;
    MooInputEvent event;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_STYLUS) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (pressure > 65535u || tilt_x < -9000 || tilt_x > 9000 ||
        tilt_y < -9000 || tilt_y > 9000)
        return MOO_INPUT_INVALID;
    result = owner_for_target(core, target, &owner);
    if (result != MOO_INPUT_OK) return result;
    event_base(&event, MOO_INPUT_EVENT_STYLUS, target, serial,
               timestamp_ns, core->focus_epoch);
    event.data.stylus.x = x; event.data.stylus.y = y;
    event.data.stylus.pressure = pressure;
    event.data.stylus.tilt_x = tilt_x; event.data.stylus.tilt_y = tilt_y;
    event.data.stylus.buttons = buttons;
    result = push_event(core, owner, &event, 0u);
    if (result != MOO_INPUT_OK) return result;
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_touch(MooInputCore *core, MooInputHandle target,
    uint32_t id, uint32_t phase, int32_t x, int32_t y, uint32_t pressure,
    uint64_t serial, uint64_t timestamp_ns) {
    uint32_t i;
    uint32_t free_slot = UINT32_MAX;
    uint32_t found = UINT32_MAX;
    uint32_t ci;
    uint32_t consume = 0u;
    MooInputHandle owner;
    MooInputEvent event;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_TOUCH) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (phase < MOO_INPUT_TOUCH_DOWN || phase > MOO_INPUT_TOUCH_CANCEL)
        return MOO_INPUT_INVALID;
    for (i = 0u; i < MOO_INPUT_MAX_TOUCHES; ++i) {
        if (core->touches[i].live && core->touches[i].id == id) found = i;
        if (!core->touches[i].live && free_slot == UINT32_MAX) free_slot = i;
    }
    if (phase == MOO_INPUT_TOUCH_DOWN) {
        if (found != UINT32_MAX) return MOO_INPUT_BAD_STATE;
        if (free_slot == UINT32_MAX) return MOO_INPUT_LIMIT;
        result = owner_for_target(core, target, &owner);
        if (result != MOO_INPUT_OK) return result;
        result = client_slot(core, owner, &ci);
        if (result != MOO_INPUT_OK) return result;
        if (core->clients[ci].reserved_cleanup == UINT32_MAX)
            return MOO_INPUT_LIMIT;
        core->clients[ci].reserved_cleanup++;
        result = can_queue(core, owner, 1u, 0u);
        if (result != MOO_INPUT_OK) {
            core->clients[ci].reserved_cleanup--;
            return result;
        }
    } else {
        if (found == UINT32_MAX) return MOO_INPUT_BAD_STATE;
        target = core->touches[found].target;
        result = owner_for_target(core, target, &owner);
        if (result != MOO_INPUT_OK) return result;
        if (phase == MOO_INPUT_TOUCH_UP || phase == MOO_INPUT_TOUCH_CANCEL)
            consume = 1u;
        result = can_queue(core, owner, 1u, consume);
        if (result != MOO_INPUT_OK) return result;
    }
    event_base(&event, MOO_INPUT_EVENT_TOUCH, target, serial,
               timestamp_ns, core->focus_epoch);
    event.data.touch.id = id; event.data.touch.phase = phase;
    event.data.touch.x = x; event.data.touch.y = y;
    event.data.touch.pressure = pressure;
    result = push_event(core, owner, &event, consume);
    if (result != MOO_INPUT_OK) return result;
    if (phase == MOO_INPUT_TOUCH_DOWN) {
        core->touches[free_slot].live = 1u;
        core->touches[free_slot].id = id;
        core->touches[free_slot].target = target;
    } else if (consume != 0u) {
        core->touches[found].live = 0u;
    }
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}
MooInputResult moo_input_key(MooInputCore *core, uint32_t physical,
    uint32_t logical, uint32_t state, uint32_t repeat, uint64_t serial,
    uint64_t timestamp_ns) {
    MooInputHandle owner;
    MooInputEvent event;
    uint64_t bit;
    uint32_t word;
    uint32_t ci;
    uint32_t consume = 0u;
    uint32_t shortcut = UINT32_MAX;
    uint32_t current_modifiers;
    int was_down;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_KEYBOARD) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (physical >= 256u || state > MOO_INPUT_PRESSED || repeat > 1u)
        return MOO_INPUT_INVALID;
    if (core->focus == MOO_INPUT_HANDLE_INVALID) return MOO_INPUT_BAD_STATE;
    result = owner_for_target(core, core->focus, &owner);
    if (result != MOO_INPUT_OK) return result;
    result = client_slot(core, owner, &ci);
    if (result != MOO_INPUT_OK) return result;
    word = physical / 64u;
    bit = UINT64_C(1) << (physical % 64u);
    was_down = (core->key_bits[word] & bit) != 0u;
    if (repeat) {
        if (state != MOO_INPUT_PRESSED || !was_down)
            return MOO_INPUT_BAD_STATE;
    } else if ((state == MOO_INPUT_PRESSED) == was_down) {
        return MOO_INPUT_BAD_STATE;
    }
    if (!repeat && state == MOO_INPUT_PRESSED) {
        if (core->clients[ci].reserved_cleanup == UINT32_MAX)
            return MOO_INPUT_LIMIT;
        core->clients[ci].reserved_cleanup++;
    } else if (!repeat) {
        consume = 1u;
    }
    result = can_queue(core, owner, 1u, consume);
    if (result != MOO_INPUT_OK) {
        if (!repeat && state == MOO_INPUT_PRESSED)
            core->clients[ci].reserved_cleanup--;
        return result;
    }
    if (!repeat) {
        if (state == MOO_INPUT_PRESSED) {
            core->key_bits[word] |= bit;
            core->key_logical[physical] = logical;
        } else {
            core->key_bits[word] &= ~bit;
        }
        if (state == MOO_INPUT_PRESSED && physical == 57u)
            core->lock_modifiers ^= MOO_INPUT_MOD_CAPS_LOCK;
        if (state == MOO_INPUT_PRESSED && physical == 83u)
            core->lock_modifiers ^= MOO_INPUT_MOD_NUM_LOCK;
    }
    current_modifiers = modifiers(core);
    if (!repeat && state == MOO_INPUT_PRESSED &&
        (core->config.features & MOO_INPUT_FEATURE_SHORTCUTS) != 0u) {
        uint32_t i;
        for (i = 0u; i < MOO_INPUT_MAX_SHORTCUTS; ++i) {
            if (core->shortcuts[i].live &&
                core->shortcuts[i].target == core->focus &&
                core->shortcuts[i].physical == physical &&
                core->shortcuts[i].modifiers == current_modifiers) {
                shortcut = i;
                break;
            }
        }
    }
    event_base(&event, shortcut == UINT32_MAX ? MOO_INPUT_EVENT_KEY :
               MOO_INPUT_EVENT_SHORTCUT, core->focus, serial,
               timestamp_ns, core->focus_epoch);
    if (shortcut == UINT32_MAX) {
        event.data.key.physical = physical;
        event.data.key.logical = logical;
        event.data.key.state = state;
        event.data.key.modifiers = current_modifiers;
        event.data.key.repeat = repeat;
    } else {
        event.data.shortcut.id = core->shortcuts[shortcut].id;
        event.data.shortcut.physical = physical;
        event.data.shortcut.modifiers = current_modifiers;
    }
    result = push_event(core, owner, &event, consume);
    if (result != MOO_INPUT_OK) return result;
    if (!repeat && state == MOO_INPUT_RELEASED)
        core->key_logical[physical] = 0u;
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

static int valid_utf8(const uint8_t *text, uint32_t length) {
    uint32_t i = 0u;
    if ((!text && length != 0u) || length > MOO_INPUT_TEXT_CAPACITY) return 0;
    while (i < length) {
        uint32_t cp;
        uint32_t need;
        uint8_t b = text[i++];
        if (b == 0u) return 0;
        if (b < 0x80u) continue;
        if (b >= 0xc2u && b <= 0xdfu) { cp = b & 0x1fu; need = 1u; }
        else if (b >= 0xe0u && b <= 0xefu) { cp = b & 0x0fu; need = 2u; }
        else if (b >= 0xf0u && b <= 0xf4u) { cp = b & 0x07u; need = 3u; }
        else return 0;
        if (need > length - i) return 0;
        while (need-- > 0u) {
            uint8_t c = text[i++];
            if ((c & 0xc0u) != 0x80u) return 0;
            cp = (cp << 6u) | (uint32_t)(c & 0x3fu);
        }
        if (cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu) ||
            (cp < 0x80u) || (cp < 0x800u && b >= 0xe0u) ||
            (cp < 0x10000u && b >= 0xf0u))
            return 0;
    }
    return 1;
}

static int utf8_boundary(const uint8_t *text, uint32_t length, uint32_t at) {
    if (at > length) return 0;
    return at == length || (text[at] & 0xc0u) != 0x80u;
}
static MooInputResult text_event(MooInputCore *core, uint32_t type,
    uint64_t focus_epoch, uint64_t revision, const uint8_t *text,
    uint32_t length, uint32_t selection_start, uint32_t selection_end,
    uint64_t serial, uint64_t timestamp_ns) {
    uint32_t ti;
    uint32_t ci;
    uint32_t i;
    uint32_t consume = 0u;
    uint32_t reserve = 0u;
    MooInputHandle owner;
    MooInputEvent event;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_IME) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    if (focus_epoch != core->focus_epoch)
        return MOO_INPUT_STALE_REVISION;
    if (core->ime_revision == UINT64_MAX || revision == UINT64_MAX ||
        revision != core->ime_revision + 1u || revision == 0u)
        return MOO_INPUT_STALE_REVISION;
    if (!valid_utf8(text, length)) return MOO_INPUT_BAD_UTF8;
    if (!utf8_boundary(text, length, selection_start) ||
        !utf8_boundary(text, length, selection_end) ||
        selection_start > selection_end)
        return MOO_INPUT_INVALID;
    if (core->focus == MOO_INPUT_HANDLE_INVALID) return MOO_INPUT_BAD_STATE;
    result = target_slot(core, core->focus, &ti);
    if (result != MOO_INPUT_OK) return result;
    if (core->targets[ti].text_mode == MOO_INPUT_TEXT_NONE)
        return MOO_INPUT_UNSUPPORTED;
    owner = core->targets[ti].owner;
    result = client_slot(core, owner, &ci);
    if (result != MOO_INPUT_OK) return result;
    if (type == MOO_INPUT_EVENT_TEXT_PREEDIT) {
        if (core->ime_active && core->ime_target != core->focus)
            return MOO_INPUT_BAD_STATE;
        if (!core->ime_active) reserve = 1u;
    } else if (type == MOO_INPUT_EVENT_TEXT_CANCEL) {
        if (!core->ime_active || core->ime_target != core->focus)
            return MOO_INPUT_BAD_STATE;
        consume = 1u;
    } else if (type == MOO_INPUT_EVENT_TEXT_COMMIT && core->ime_active) {
        if (core->ime_target != core->focus) return MOO_INPUT_BAD_STATE;
        consume = 1u;
    }
    if (reserve) {
        if (core->clients[ci].reserved_cleanup == UINT32_MAX)
            return MOO_INPUT_LIMIT;
        core->clients[ci].reserved_cleanup++;
    }
    result = can_queue(core, owner, 1u, consume);
    if (result != MOO_INPUT_OK) {
        if (reserve) core->clients[ci].reserved_cleanup--;
        return result;
    }
    event_base(&event, type, core->focus, serial, timestamp_ns,
               core->focus_epoch);
    if (core->targets[ti].text_mode == MOO_INPUT_TEXT_SENSITIVE)
        event.flags |= MOO_INPUT_EVENT_SENSITIVE;
    event.data.text.text.length = length;
    for (i = 0u; i < length; ++i)
        event.data.text.text.bytes[i] = text[i];
    event.data.text.selection_start = selection_start;
    event.data.text.selection_end = selection_end;
    event.data.text.revision = revision;
    result = push_event(core, owner, &event, consume);
    if (result != MOO_INPUT_OK) return result;
    if (type == MOO_INPUT_EVENT_TEXT_PREEDIT) {
        core->ime_active = 1u;
        core->ime_target = core->focus;
    } else if (consume) {
        core->ime_active = 0u;
        core->ime_target = MOO_INPUT_HANDLE_INVALID;
    }
    core->ime_revision = revision;
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_text_preedit(MooInputCore *core,
    uint64_t focus_epoch, uint64_t revision, const uint8_t *text,
    uint32_t length, uint32_t selection_start, uint32_t selection_end,
    uint64_t serial, uint64_t timestamp_ns) {
    return text_event(core, MOO_INPUT_EVENT_TEXT_PREEDIT, focus_epoch,
                      revision, text, length, selection_start, selection_end,
                      serial, timestamp_ns);
}

MooInputResult moo_input_text_commit(MooInputCore *core,
    uint64_t focus_epoch, uint64_t revision, const uint8_t *text,
    uint32_t length, uint64_t serial, uint64_t timestamp_ns) {
    return text_event(core, MOO_INPUT_EVENT_TEXT_COMMIT, focus_epoch,
                      revision, text, length, length, length,
                      serial, timestamp_ns);
}

MooInputResult moo_input_text_cancel(MooInputCore *core,
    uint64_t focus_epoch, uint64_t revision,
    uint64_t serial, uint64_t timestamp_ns) {
    static const uint8_t empty[1] = {0u};
    return text_event(core, MOO_INPUT_EVENT_TEXT_CANCEL, focus_epoch,
                      revision, empty, 0u, 0u, 0u,
                      serial, timestamp_ns);
}

MooInputResult moo_input_shortcut_register(MooInputCore *core,
    MooInputHandle client, MooInputHandle target, uint64_t id,
    uint32_t physical, uint32_t wanted_modifiers) {
    uint32_t ti;
    uint32_t i;
    uint32_t known_modifiers = MOO_INPUT_MOD_SHIFT | MOO_INPUT_MOD_CONTROL |
        MOO_INPUT_MOD_ALT | MOO_INPUT_MOD_SUPER | MOO_INPUT_MOD_CAPS_LOCK |
        MOO_INPUT_MOD_NUM_LOCK | MOO_INPUT_MOD_ALT_GR;
    MooInputResult result = client_slot(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    if ((core->config.features & MOO_INPUT_FEATURE_SHORTCUTS) == 0u)
        return MOO_INPUT_UNSUPPORTED;
    result = target_slot(core, target, &ti);
    if (result != MOO_INPUT_OK) return result;
    if (core->targets[ti].owner != client) return MOO_INPUT_ACCESS;
    if (id == 0u || physical >= 256u ||
        (wanted_modifiers & ~known_modifiers) != 0u)
        return MOO_INPUT_INVALID;
    for (i = 0u; i < MOO_INPUT_MAX_SHORTCUTS; ++i) {
        if (core->shortcuts[i].live &&
            core->shortcuts[i].owner == client &&
            core->shortcuts[i].id == id)
            return MOO_INPUT_BAD_STATE;
    }
    for (i = 0u; i < MOO_INPUT_MAX_SHORTCUTS; ++i) {
        if (!core->shortcuts[i].live) {
            core->shortcuts[i].live = 1u;
            core->shortcuts[i].physical = physical;
            core->shortcuts[i].modifiers = wanted_modifiers;
            core->shortcuts[i].id = id;
            core->shortcuts[i].owner = client;
            core->shortcuts[i].target = target;
            return MOO_INPUT_OK;
        }
    }
    return MOO_INPUT_LIMIT;
}

MooInputResult moo_input_shortcut_unregister(MooInputCore *core,
    MooInputHandle client, uint64_t id) {
    uint32_t i;
    MooInputResult result = client_slot(core, client, 0);
    if (result != MOO_INPUT_OK) return result;
    if (id == 0u) return MOO_INPUT_INVALID;
    for (i = 0u; i < MOO_INPUT_MAX_SHORTCUTS; ++i) {
        if (core->shortcuts[i].live &&
            core->shortcuts[i].owner == client &&
            core->shortcuts[i].id == id) {
            core->shortcuts[i].live = 0u;
            return MOO_INPUT_OK;
        }
    }
    return MOO_INPUT_STALE_HANDLE;
}

MooInputResult moo_input_set_preferences(MooInputCore *core,
    uint32_t high_contrast, uint32_t reduced_motion,
    uint64_t serial, uint64_t timestamp_ns) {
    uint32_t i;
    uint32_t recipient_count = 0u;
    MooInputEvent event;
    MooInputResult result = serial_ok(core, serial);
    if (result != MOO_INPUT_OK) return result;
    if (high_contrast > 1u || reduced_motion > 1u)
        return MOO_INPUT_INVALID;
    if ((high_contrast != 0u &&
         (core->config.features & MOO_INPUT_FEATURE_HIGH_CONTRAST) == 0u) ||
        (reduced_motion != 0u &&
         (core->config.features & MOO_INPUT_FEATURE_REDUCED_MOTION) == 0u))
        return MOO_INPUT_UNSUPPORTED;
    for (i = 0u; i < core->client_capacity; ++i)
        if (core->clients[i].live) recipient_count++;
    if (free_events(core) < recipient_count ||
        free_events(core) - recipient_count < total_cleanup_reservations(core) ||
        core->event_sequence > UINT64_MAX - recipient_count)
        return MOO_INPUT_WOULD_BLOCK;
    for (i = 0u; i < core->client_capacity; ++i) {
        MooInputHandle owner;
        if (!core->clients[i].live) continue;
        owner = make_handle(MOO_INPUT_KIND_CLIENT, i,
                            core->clients[i].generation);
        result = can_queue(core, owner, 1u, 0u);
        if (result != MOO_INPUT_OK) return result;
    }
    core->high_contrast = high_contrast;
    core->reduced_motion = reduced_motion;
    for (i = 0u; i < core->client_capacity; ++i) {
        MooInputHandle owner;
        if (!core->clients[i].live) continue;
        owner = make_handle(MOO_INPUT_KIND_CLIENT, i,
                            core->clients[i].generation);
        event_base(&event, MOO_INPUT_EVENT_PREFERENCES,
                   MOO_INPUT_HANDLE_INVALID, serial, timestamp_ns,
                   core->focus_epoch);
        event.data.preferences.high_contrast = high_contrast;
        event.data.preferences.reduced_motion = reduced_motion;
        result = push_event(core, owner, &event, 0u);
        if (result != MOO_INPUT_OK) return result;
    }
    core->accepted_serial = serial;
    return MOO_INPUT_OK;
}

MooInputResult moo_input_next_event(MooInputCore *core,
    MooInputHandle client, MooInputEvent *out_event) {
    uint32_t ci;
    uint32_t i;
    uint32_t best = UINT32_MAX;
    uint64_t sequence = UINT64_MAX;
    MooInputResult result = client_slot(core, client, &ci);
    if (result != MOO_INPUT_OK) return result;
    if (!out_event) return MOO_INPUT_INVALID;
    for (i = 0u; i < core->event_capacity; ++i) {
        if (core->events[i].live && core->events[i].owner == client &&
            core->events[i].sequence < sequence) {
            best = i; sequence = core->events[i].sequence;
        }
    }
    if (best == UINT32_MAX) return MOO_INPUT_WOULD_BLOCK;
    copy_record(out_event, &core->events[best].event,
                sizeof(*out_event));
    clear_event_slot(&core->events[best]);
    core->clients[ci].queued--;
    return MOO_INPUT_OK;
}
