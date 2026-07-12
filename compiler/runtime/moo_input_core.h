#ifndef MOO_INPUT_CORE_H
#define MOO_INPUT_CORE_H

#include "moo_input_protocol.h"
#include <stddef.h>
#include <stdint.h>

#define MOO_INPUT_MAX_TOUCHES 16u
#define MOO_INPUT_KEY_WORDS 4u
#define MOO_INPUT_MAX_SHORTCUTS 32u

typedef struct {
    uint32_t live, generation, queued, reserved_cleanup, capabilities;
} MooInputClientSlot;
typedef struct {
    uint32_t live, generation;
    MooInputHandle owner;
    uint64_t surface;
    uint32_t text_mode, reserved;
} MooInputTargetSlot;
typedef struct {
    uint32_t live, reserved;
    MooInputHandle owner;
    uint64_t sequence;
    MooInputEvent event;
} MooInputEventSlot;
typedef struct {
    uint32_t live, generation;
    MooInputHandle owner, target;
    uint64_t revision;
    MooA11yNodeData data;
} MooA11yNodeSlot;
typedef struct { uint32_t live, id; MooInputHandle target; } MooInputTouchSlot;
typedef struct {
    uint32_t live, physical, modifiers, reserved;
    uint64_t id;
    MooInputHandle owner, target;
} MooInputShortcutSlot;

typedef struct {
    uint32_t max_events_per_client;
    uint32_t max_a11y_depth;
    uint64_t features;
} MooInputConfig;

typedef struct {
    MooInputClientSlot *clients; uint32_t client_capacity;
    MooInputTargetSlot *targets; uint32_t target_capacity;
    MooInputEventSlot *events; uint32_t event_capacity;
    MooA11yNodeSlot *nodes; uint32_t node_capacity;
    MooInputConfig config;
    MooInputHandle focus, pointer_target, pointer_capture;
    uint64_t focus_epoch, accepted_serial, event_sequence, ime_revision;
    uint64_t key_bits[MOO_INPUT_KEY_WORDS];
    uint32_t key_logical[256];
    uint32_t ime_active;
    MooInputHandle ime_target;
    MooInputTouchSlot touches[MOO_INPUT_MAX_TOUCHES];
    MooInputShortcutSlot shortcuts[MOO_INPUT_MAX_SHORTCUTS];
    uint32_t pointer_buttons, lock_modifiers, high_contrast, reduced_motion;
} MooInputCore;

MooInputResult moo_input_init(MooInputCore *core, const MooInputConfig *config,
    MooInputClientSlot *clients, uint32_t client_capacity,
    MooInputTargetSlot *targets, uint32_t target_capacity,
    MooInputEventSlot *events, uint32_t event_capacity,
    MooA11yNodeSlot *nodes, uint32_t node_capacity);
/* Trusted server API: capabilities are policy output, never values copied
 * from an untrusted client request. */
MooInputResult moo_input_client_create(MooInputCore *core, uint32_t capabilities,
    MooInputHandle *out_client);
MooInputResult moo_input_client_disconnect(MooInputCore *core, MooInputHandle client, uint64_t serial);
/* Server-only for non-compositor test transports. Production adapters use
 * moo_input_target_create_for_surface() from the compositor bridge. */
MooInputResult moo_input_target_create_trusted(MooInputCore *core, MooInputHandle client,
    uint64_t surface, uint32_t text_mode, MooInputHandle *out_target);
MooInputResult moo_input_target_destroy(MooInputCore *core, MooInputHandle client,
    MooInputHandle target, uint64_t serial);
MooInputResult moo_input_set_focus(MooInputCore *core, MooInputHandle target,
    uint32_t reason, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_pointer_motion(MooInputCore *core, MooInputHandle target,
    int32_t x, int32_t y, int32_t dx, int32_t dy, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_pointer_button(MooInputCore *core, uint32_t button,
    uint32_t state, int32_t x, int32_t y, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_pointer_axis(MooInputCore *core, int32_t x_120, int32_t y_120,
    uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_stylus(MooInputCore *core, MooInputHandle target,
    int32_t x, int32_t y, uint32_t pressure, int32_t tilt_x, int32_t tilt_y,
    uint32_t buttons, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_touch(MooInputCore *core, MooInputHandle target, uint32_t id,
    uint32_t phase, int32_t x, int32_t y, uint32_t pressure,
    uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_key(MooInputCore *core, uint32_t physical, uint32_t logical,
    uint32_t state, uint32_t repeat, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_text_preedit(MooInputCore *core, uint64_t focus_epoch,
    uint64_t revision, const uint8_t *text, uint32_t length,
    uint32_t selection_start, uint32_t selection_end, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_text_commit(MooInputCore *core, uint64_t focus_epoch,
    uint64_t revision, const uint8_t *text, uint32_t length,
    uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_text_cancel(MooInputCore *core, uint64_t focus_epoch,
    uint64_t revision, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_shortcut_register(MooInputCore *core, MooInputHandle client,
    MooInputHandle target, uint64_t id, uint32_t physical, uint32_t modifiers);
MooInputResult moo_input_shortcut_unregister(MooInputCore *core, MooInputHandle client,
    uint64_t id);
MooInputResult moo_input_set_preferences(MooInputCore *core, uint32_t high_contrast,
    uint32_t reduced_motion, uint64_t serial, uint64_t timestamp_ns);
MooInputResult moo_input_next_event(MooInputCore *core, MooInputHandle client, MooInputEvent *out_event);

MooInputResult moo_a11y_node_create(MooInputCore *core, MooInputHandle client,
    MooInputHandle target, const MooA11yNodeData *data, MooInputHandle *out_node);
MooInputResult moo_a11y_node_update(MooInputCore *core, MooInputHandle client,
    MooInputHandle node, uint64_t expected_revision, const MooA11yNodeData *data);
MooInputResult moo_a11y_node_destroy(MooInputCore *core, MooInputHandle client, MooInputHandle node);
MooInputResult moo_a11y_nodes_update_atomic(MooInputCore *core,
    MooInputHandle client, const MooInputHandle *nodes,
    const uint64_t *expected_revisions, const MooA11yNodeData *data,
    uint32_t count);
MooInputResult moo_a11y_node_read(const MooInputCore *core,
    MooInputHandle requester, MooInputHandle node,
    MooA11yNodeData *out_data, uint64_t *out_revision);
MooInputResult moo_a11y_action(MooInputCore *core, MooInputHandle requester,
    MooInputHandle node,
    uint32_t action, int64_t value, uint64_t serial, uint64_t timestamp_ns);
uint64_t moo_input_state_hash(const MooInputCore *core);

#endif
