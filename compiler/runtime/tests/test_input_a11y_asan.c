#include "../moo_input_core.h"
#include "../moo_ui_host_parity_devtools.h"
#include "../moo_ui_host_parity_instrumentation_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLIENT_CAP 6u
#define TARGET_CAP 10u
#define EVENT_CAP 64u
#define NODE_CAP 32u

static int checks;
static int failures;

#define CHECK(c, m) do { checks++; if (!(c)) { failures++;     fprintf(stderr, "FAIL:%d:%s\n", __LINE__, (m)); } } while (0)

typedef struct {
    MooInputCore core;
    MooInputClientSlot clients[CLIENT_CAP];
    MooInputTargetSlot targets[TARGET_CAP];
    MooInputEventSlot events[EVENT_CAP];
    MooA11yNodeSlot nodes[NODE_CAP];
} Fixture;

static uint64_t all_features(void) {
    return MOO_INPUT_FEATURE_POINTER | MOO_INPUT_FEATURE_TOUCH |
        MOO_INPUT_FEATURE_STYLUS | MOO_INPUT_FEATURE_KEYBOARD |
        MOO_INPUT_FEATURE_IME | MOO_INPUT_FEATURE_SHORTCUTS |
        MOO_INPUT_FEATURE_ACCESSIBILITY | MOO_INPUT_FEATURE_HIGH_CONTRAST |
        MOO_INPUT_FEATURE_REDUCED_MOTION | MOO_INPUT_FEATURE_AUTOMATION;
}

static void init(Fixture *f, uint32_t quota) {
    MooInputConfig config;
    memset(f, 0xa5, sizeof(*f));
    config.max_events_per_client = quota;
    config.max_a11y_depth = 8u;
    config.features = all_features();
    CHECK(moo_input_init(&f->core, &config,
        f->clients, CLIENT_CAP, f->targets, TARGET_CAP,
        f->events, EVENT_CAP, f->nodes, NODE_CAP) == MOO_INPUT_OK, "init");
}

static uint32_t drain(MooInputCore *core, MooInputHandle client,
                      MooInputEvent *last) {
    MooInputEvent event;
    uint32_t count = 0u;
    while (moo_input_next_event(core, client, &event) == MOO_INPUT_OK) {
        if (last) *last = event;
        count++;
    }
    return count;
}

static uint32_t free_event_slots_canonical(const Fixture *f) {
    MooInputEventSlot zero;
    uint32_t i;
    memset(&zero, 0, sizeof(zero));
    for (i = 0u; i < EVENT_CAP; ++i)
        if (!f->events[i].live &&
            memcmp(&f->events[i], &zero, sizeof(zero)) != 0)
            return 0u;
    return 1u;
}

static uint32_t text_tail_zero(const MooInputEvent *event) {
    uint32_t i;
    for (i = event->data.text.text.length; i < MOO_INPUT_TEXT_CAPACITY; ++i)
        if (event->data.text.text.bytes[i] != 0u) return 0u;
    return 1u;
}

static MooA11yNodeData node_data(uint32_t role, const char *name) {
    MooA11yNodeData data;
    size_t n = strlen(name);
    memset(&data, 0, sizeof(data));
    data.role = role;
    data.bounds.width = 100;
    data.bounds.height = 20;
    if (n > MOO_INPUT_TEXT_CAPACITY) n = MOO_INPUT_TEXT_CAPACITY;
    data.name.length = (uint32_t)n;
    memcpy(data.name.bytes, name, n);
    return data;
}

static void test_focus_key_ime(void) {
    Fixture f;
    MooInputHandle app;
    MooInputHandle t1;
    MooInputHandle t2;
    MooInputHandle sensitive;
    MooInputEvent event;
    const uint8_t preedit[] = {0xc3u, 0xa4u, 'b'};
    uint64_t epoch;
    init(&f, 16u);
    CHECK(moo_input_client_create(&f.core, 0u, &app) == MOO_INPUT_OK,
          "client");
    CHECK(moo_input_target_create_trusted(&f.core, app, 11u, 1u, &t1) == MOO_INPUT_OK,
          "target1");
    CHECK(moo_input_target_create_trusted(&f.core, app, 12u, 1u, &t2) == MOO_INPUT_OK,
          "target2");

    CHECK(moo_input_set_focus(&f.core, t1, 1u, 1u, 10u) == MOO_INPUT_OK,
          "focus1");
    CHECK(drain(&f.core, app, &event) == 1u &&
          event.type == MOO_INPUT_EVENT_FOCUS, "focus event");

    CHECK(moo_input_key(&f.core, 225u, 0u, MOO_INPUT_PRESSED, 0u,
                        2u, 20u) == MOO_INPUT_OK, "shift down");
    CHECK(f.clients[0].reserved_cleanup == 1u, "key cleanup reserved");
    CHECK(moo_input_set_focus(&f.core, t2, 2u, 3u, 30u) == MOO_INPUT_OK,
          "focus switch");
    CHECK(f.clients[0].reserved_cleanup == 0u, "key cleanup consumed");
    CHECK(drain(&f.core, app, &event) == 4u, "down release blur focus events");

    epoch = f.core.focus_epoch;
    CHECK(moo_input_text_preedit(&f.core, epoch, 1u, preedit, 3u,
          2u, 3u, 4u, 40u) == MOO_INPUT_OK, "valid preedit");
    CHECK(f.core.ime_active == 1u &&
          f.clients[0].reserved_cleanup == 1u, "ime reserved");
    CHECK(moo_input_text_preedit(&f.core, epoch, 2u, preedit, 3u,
          1u, 3u, 5u, 50u) == MOO_INPUT_INVALID, "utf8 boundary rejected");
    CHECK(f.core.accepted_serial == 4u, "failed event does not consume serial");
    CHECK(moo_input_set_focus(&f.core, t1, 3u, 5u, 50u) == MOO_INPUT_OK,
          "focus cancels ime");
    CHECK(!f.core.ime_active && f.clients[0].reserved_cleanup == 0u,
          "ime cleanup consumed");
    CHECK(drain(&f.core, app, &event) == 4u, "preedit cancel blur focus");
    CHECK(moo_input_target_create_trusted(&f.core, app, 13u,
          MOO_INPUT_TEXT_SENSITIVE, &sensitive) == MOO_INPUT_OK,
          "sensitive text target");
    CHECK(moo_input_set_focus(&f.core, sensitive, 4u, 6u, 60u) ==
          MOO_INPUT_OK, "focus sensitive");
    drain(&f.core, app, 0);
    CHECK(moo_input_text_commit(&f.core, f.core.focus_epoch, 1u,
          (const uint8_t *)"s", 1u, 7u, 70u) == MOO_INPUT_OK,
          "sensitive direct commit");
    CHECK(drain(&f.core, app, &event) == 1u &&
          (event.flags & MOO_INPUT_EVENT_SENSITIVE) != 0u,
          "sensitive text event flagged");
    CHECK(moo_input_text_preedit(&f.core, f.core.focus_epoch, UINT64_MAX,
          (const uint8_t *)"x", 1u, 0u, 1u, 8u, 80u) ==
          MOO_INPUT_STALE_REVISION, "ime max revision rejected");
}

static void test_pointer_touch_stylus_and_quota(void) {
    Fixture f;
    MooInputHandle app;
    MooInputHandle target;
    MooInputEvent event;
    uint64_t serial = 1u;
    uint32_t accepted_axes = 0u;
    init(&f, 8u);
    CHECK(moo_input_client_create(&f.core, 0u, &app) == MOO_INPUT_OK, "client");
    CHECK(moo_input_target_create_trusted(&f.core, app, 21u, 0u, &target) == MOO_INPUT_OK,
          "target");
    CHECK(moo_input_pointer_motion(&f.core, target, 1, 2, 1, 2,
                                   serial++, 1u) == MOO_INPUT_OK, "motion");
    CHECK(drain(&f.core, app, 0) == 2u, "enter motion");
    CHECK(moo_input_pointer_button(&f.core, 1u, MOO_INPUT_PRESSED, 1, 2,
                                   serial++, 2u) == MOO_INPUT_OK, "button down");
    while (moo_input_pointer_axis(&f.core, 0, 120, serial, serial) ==
           MOO_INPUT_OK) {
        accepted_axes++;
        serial++;
    }
    CHECK(accepted_axes == 5u, "quota leaves button and hover cleanup slots");
    CHECK(moo_input_pointer_button(&f.core, 1u, MOO_INPUT_RELEASED, 1, 2,
                                   serial++, 3u) == MOO_INPUT_OK,
          "button release survives saturation");
    CHECK(f.clients[0].reserved_cleanup == 1u, "button reserve consumed, hover reserve remains");
    CHECK(drain(&f.core, app, 0) == 7u, "saturated queue drains");

    CHECK(moo_input_touch(&f.core, target, 77u, MOO_INPUT_TOUCH_DOWN,
                          4, 5, 300u, serial++, 4u) == MOO_INPUT_OK,
          "touch down");
    CHECK(moo_input_touch(&f.core, target, 77u, MOO_INPUT_TOUCH_UP,
                          4, 5, 0u, serial++, 5u) == MOO_INPUT_OK,
          "touch up");
    CHECK(moo_input_stylus(&f.core, target, 8, 9, 512u, -100, 100, 1u,
                           serial++, 6u) == MOO_INPUT_OK, "stylus");
    CHECK(drain(&f.core, app, &event) == 3u &&
          event.type == MOO_INPUT_EVENT_STYLUS, "touch stylus events");
    CHECK(moo_input_pointer_button(&f.core, 0u, MOO_INPUT_PRESSED, 8, 9,
          serial++, 7u) == MOO_INPUT_OK, "cancel seed button");
    CHECK(moo_input_pointer_cancel(&f.core, serial++, 8u) == MOO_INPUT_OK,
          "pointer cancel");
    CHECK(drain(&f.core, app, &event) == 3u &&
          event.type == MOO_INPUT_EVENT_POINTER_LEAVE &&
          (event.flags & MOO_INPUT_EVENT_SYNTHETIC) != 0u,
          "synthetic release and leave");
    CHECK(f.core.pointer_target == MOO_INPUT_HANDLE_INVALID &&
          f.clients[0].reserved_cleanup == 0u, "pointer cancel state");
}

static void test_shortcut_and_modifiers(void) {
    Fixture f;
    MooInputHandle app;
    MooInputHandle target;
    MooInputEvent event;
    init(&f, 16u);
    CHECK(moo_input_client_create(&f.core, 0u, &app) == MOO_INPUT_OK, "client");
    CHECK(moo_input_target_create_trusted(&f.core, app, 31u, 0u, &target) == MOO_INPUT_OK,
          "target");
    CHECK(moo_input_set_focus(&f.core, target, 0u, 1u, 1u) == MOO_INPUT_OK,
          "focus");
    drain(&f.core, app, 0);
    CHECK(moo_input_shortcut_register(&f.core, app, target, 99u, 6u,
          MOO_INPUT_MOD_CONTROL) == MOO_INPUT_OK, "shortcut register");
    CHECK(moo_input_key(&f.core, 224u, 0u, MOO_INPUT_PRESSED, 0u,
                        2u, 2u) == MOO_INPUT_OK, "ctrl down");
    CHECK(moo_input_key(&f.core, 6u, 'c', MOO_INPUT_PRESSED, 0u,
                        3u, 3u) == MOO_INPUT_OK, "shortcut key");
    CHECK(drain(&f.core, app, &event) == 2u &&
          event.type == MOO_INPUT_EVENT_SHORTCUT &&
          event.data.shortcut.id == 99u, "shortcut event");
    CHECK(moo_input_key(&f.core, 6u, 'c', MOO_INPUT_RELEASED, 0u,
                        4u, 4u) == MOO_INPUT_OK, "c up");
    CHECK(moo_input_key(&f.core, 224u, 0u, MOO_INPUT_RELEASED, 0u,
                        5u, 5u) == MOO_INPUT_OK, "ctrl up");
    CHECK(f.clients[0].reserved_cleanup == 0u, "key reserves consumed");
}

static void test_a11y_security_relations(void) {
    Fixture f;
    MooInputHandle app1;
    MooInputHandle app2;
    MooInputHandle reader;
    MooInputHandle automation;
    MooInputHandle target1;
    MooInputHandle target2;
    MooInputHandle root;
    MooInputHandle child;
    MooA11yNodeData root_data;
    MooA11yNodeData child_data;
    MooA11yNodeData out;
    MooInputEvent event;
    uint64_t revision;
    init(&f, 16u);
    CHECK(moo_input_client_create(&f.core, 0u, &app1) == MOO_INPUT_OK, "app1");
    CHECK(moo_input_client_create(&f.core, 0u, &app2) == MOO_INPUT_OK, "app2");
    CHECK(moo_input_client_create(&f.core, MOO_INPUT_CLIENT_SCREEN_READER,
                                  &reader) == MOO_INPUT_OK, "reader");
    CHECK(moo_input_client_create(&f.core, MOO_INPUT_CLIENT_AUTOMATION,
                                  &automation) == MOO_INPUT_OK, "automation");
    CHECK(moo_input_target_create_trusted(&f.core, app1, 41u, 1u, &target1) == MOO_INPUT_OK,
          "target1");
    CHECK(moo_input_target_create_trusted(&f.core, app2, 42u, 1u, &target2) == MOO_INPUT_OK,
          "target2");
    root_data = node_data(MOO_A11Y_ROLE_WINDOW, "root");
    CHECK(moo_a11y_node_create(&f.core, app1, target1, &root_data, &root) ==
          MOO_INPUT_OK, "root node");
    child_data = node_data(MOO_A11Y_ROLE_TEXT_FIELD, "secret");
    child_data.parent = root;
    child_data.states = MOO_A11Y_STATE_PASSWORD;
    child_data.actions = MOO_A11Y_ACTION_FOCUS | MOO_A11Y_ACTION_SET_VALUE;
    child_data.value.length = 4u;
    memcpy(child_data.value.bytes, "1234", 4u);
    child_data.value.bytes[MOO_INPUT_TEXT_CAPACITY - 1u] = 0x7fu;
    CHECK(moo_a11y_node_create(&f.core, app1, target1, &child_data, &child) ==
          MOO_INPUT_OK, "child node");
    {
        MooA11yNodeData second_root = node_data(MOO_A11Y_ROLE_GROUP,
                                                "second-root");
        MooInputHandle rejected;
        CHECK(moo_a11y_node_create(&f.core, app1, target1, &second_root,
              &rejected) == MOO_INPUT_BAD_STATE,
              "second root rejected");
    }
    CHECK(moo_a11y_node_read(&f.core, app1, child, &out, &revision) ==
          MOO_INPUT_ACCESS, "ordinary app cannot screenread");
    CHECK(moo_a11y_node_read(&f.core, reader, child, &out, &revision) ==
          MOO_INPUT_OK && out.value.length == 0u &&
          out.value.bytes[MOO_INPUT_TEXT_CAPACITY - 1u] == 0u,
          "password and tail redacted");
    root_data.parent = child;
    CHECK(moo_a11y_node_update(&f.core, app1, root, 1u, &root_data) ==
          MOO_INPUT_BAD_STATE, "cycle rejected");

    {
        MooA11yNodeData foreign = node_data(MOO_A11Y_ROLE_TEXT, "foreign");
        MooInputHandle foreign_node;
        CHECK(moo_a11y_node_create(&f.core, app2, target2, &foreign,
              &foreign_node) == MOO_INPUT_OK, "foreign node");
        child_data.labelled_by = foreign_node;
        CHECK(moo_a11y_node_update(&f.core, app1, child, 1u, &child_data) ==
              MOO_INPUT_ACCESS, "cross target relation rejected");
    }

    {
        MooInputHandle batch_nodes[2] = {root, child};
        uint64_t revisions[2] = {1u, 1u};
        MooA11yNodeData updates[2];
        updates[0] = node_data(MOO_A11Y_ROLE_WINDOW, "root2");
        updates[0].parent = MOO_INPUT_HANDLE_INVALID;
        updates[1] = node_data(MOO_A11Y_ROLE_TEXT_FIELD, "child2");
        updates[1].parent = MOO_INPUT_HANDLE_INVALID;
        updates[1].states = MOO_A11Y_STATE_PASSWORD;
        updates[1].actions = MOO_A11Y_ACTION_FOCUS;
        CHECK(moo_a11y_nodes_update_atomic(&f.core, app1, batch_nodes,
              revisions, updates, 2u) == MOO_INPUT_BAD_STATE,
              "atomic second root rejected");
        updates[0].parent = child;
        updates[1].parent = root;
        updates[1].states = MOO_A11Y_STATE_PASSWORD;
        updates[1].actions = MOO_A11Y_ACTION_FOCUS;
        CHECK(moo_a11y_nodes_update_atomic(&f.core, app1, batch_nodes,
              revisions, updates, 2u) == MOO_INPUT_BAD_STATE,
              "atomic overlay cycle rejected");
        updates[0].parent = MOO_INPUT_HANDLE_INVALID;
        CHECK(moo_a11y_nodes_update_atomic(&f.core, app1, batch_nodes,
              revisions, updates, 2u) == MOO_INPUT_OK,
              "atomic tree update");
        CHECK(moo_a11y_node_read(&f.core, reader, root, &out, &revision) ==
              MOO_INPUT_OK && revision == 2u, "atomic revision committed");
    }

    {
        MooA11yNodeData label_data = node_data(MOO_A11Y_ROLE_TEXT, "label");
        label_data.parent = root;
        MooA11yNodeData current;
        MooInputHandle label;
        uint64_t current_revision;
        CHECK(moo_a11y_node_create(&f.core, app1, target1, &label_data,
              &label) == MOO_INPUT_OK, "label relation node");
        CHECK(moo_a11y_node_read(&f.core, reader, child, &current,
              &current_revision) == MOO_INPUT_OK, "read child for relation");
        current.labelled_by = label;
        CHECK(moo_a11y_node_update(&f.core, app1, child, current_revision,
              &current) == MOO_INPUT_OK, "set labelled_by");
        CHECK(moo_a11y_node_destroy(&f.core, app1, label) ==
              MOO_INPUT_BAD_STATE, "referenced relation destroy rejected");
        current.labelled_by = MOO_INPUT_HANDLE_INVALID;
        CHECK(moo_a11y_node_update(&f.core, app1, child,
              current_revision + 1u, &current) == MOO_INPUT_OK,
              "clear labelled_by");
        CHECK(moo_a11y_node_destroy(&f.core, app1, label) == MOO_INPUT_OK,
              "unreferenced relation destroy");
    }

    CHECK(moo_a11y_action(&f.core, app1, child, MOO_A11Y_ACTION_FOCUS,
                          0, 1u, 1u) == MOO_INPUT_ACCESS,
          "ordinary client cannot automate");
    CHECK(moo_a11y_action(&f.core, automation, child, MOO_A11Y_ACTION_FOCUS,
                          0, 1u, 1u) == MOO_INPUT_OK, "authorized action");
    CHECK(drain(&f.core, app1, &event) == 1u &&
          event.type == MOO_INPUT_EVENT_A11Y_ACTION, "action routed to owner");
}

static void test_disconnect_isolation_and_preferences(void) {
    Fixture f;
    MooInputHandle a;
    MooInputHandle b;
    MooInputHandle ta;
    uint64_t epoch;
    init(&f, 16u);
    CHECK(moo_input_client_create(&f.core, 0u, &a) == MOO_INPUT_OK, "a");
    CHECK(moo_input_client_create(&f.core, 0u, &b) == MOO_INPUT_OK, "b");
    CHECK(moo_input_target_create_trusted(&f.core, a, 51u, 0u, &ta) == MOO_INPUT_OK,
          "ta");
    CHECK(moo_input_set_focus(&f.core, ta, 0u, 1u, 1u) == MOO_INPUT_OK,
          "focus a");
    drain(&f.core, a, 0);
    epoch = f.core.focus_epoch;
    CHECK(moo_input_client_disconnect(&f.core, b, 2u) == MOO_INPUT_OK,
          "disconnect unrelated");
    CHECK(f.core.focus == ta && f.core.focus_epoch == epoch,
          "unrelated disconnect preserves focus");
    CHECK(moo_input_set_preferences(&f.core, 1u, 1u, 3u, 3u) ==
          MOO_INPUT_OK, "preferences");
    CHECK(drain(&f.core, a, 0) == 1u, "preference event");
}

static void test_text_hash_privacy(void) {
    Fixture a;
    Fixture b;
    MooInputHandle ca;
    MooInputHandle cb;
    MooInputHandle ta;
    MooInputHandle tb;
    const uint8_t one[] = {'a'};
    const uint8_t two[] = {'b'};
    init(&a, 16u);
    init(&b, 16u);
    CHECK(moo_input_client_create(&a.core, 0u, &ca) == MOO_INPUT_OK, "hash ca");
    CHECK(moo_input_client_create(&b.core, 0u, &cb) == MOO_INPUT_OK, "hash cb");
    CHECK(moo_input_target_create_trusted(&a.core, ca, 61u,
          MOO_INPUT_TEXT_NORMAL, &ta) == MOO_INPUT_OK, "hash ta");
    CHECK(moo_input_target_create_trusted(&b.core, cb, 61u,
          MOO_INPUT_TEXT_NORMAL, &tb) == MOO_INPUT_OK, "hash tb");
    CHECK(moo_input_set_focus(&a.core, ta, 0u, 1u, 1u) == MOO_INPUT_OK,
          "hash focus a");
    CHECK(moo_input_set_focus(&b.core, tb, 0u, 1u, 1u) == MOO_INPUT_OK,
          "hash focus b");
    drain(&a.core, ca, 0);
    drain(&b.core, cb, 0);
    CHECK(moo_input_text_commit(&a.core, a.core.focus_epoch, 1u,
          one, 1u, 2u, 2u) == MOO_INPUT_OK, "hash commit a");
    CHECK(moo_input_text_commit(&b.core, b.core.focus_epoch, 1u,
          two, 1u, 2u, 2u) == MOO_INPUT_OK, "hash commit b");
    CHECK(moo_input_state_hash(&a.core) != moo_input_state_hash(&b.core),
          "normal text payload affects statehash");
}

static void test_sensitive_saturated_disconnect_atomicity(void) {
    Fixture f;
    Fixture before;
    MooInputHandle app;
    MooInputHandle foreign;
    MooInputHandle target;
    MooInputEvent event;
    const uint8_t secret[] = {'s', 'e', 'c'};
    init(&f, 2u);
    CHECK(moo_input_client_create(&f.core, 0u, &app) == MOO_INPUT_OK,
          "cleanup app");
    CHECK(moo_input_client_create(&f.core, 0u, &foreign) == MOO_INPUT_OK,
          "cleanup foreign");
    CHECK(moo_input_target_create_trusted(&f.core, app, UINT64_C(71),
          MOO_INPUT_TEXT_SENSITIVE, &target) == MOO_INPUT_OK,
          "cleanup sensitive target");
    CHECK(moo_input_set_focus(&f.core, target, 0u, UINT64_C(1),
          UINT64_C(1)) == MOO_INPUT_OK, "cleanup focus");
    CHECK(drain(&f.core, app, 0) == 1u, "cleanup initial focus drain");
    CHECK(moo_input_text_preedit(&f.core, f.core.focus_epoch, UINT64_C(1),
          secret, (uint32_t)sizeof(secret), 0u, (uint32_t)sizeof(secret),
          UINT64_C(2), UINT64_C(2)) == MOO_INPUT_OK,
          "sensitive preedit saturates quota");
    CHECK(f.clients[0].queued == 1u &&
          f.clients[0].reserved_cleanup == 1u,
          "sensitive preedit owns exact cleanup reservation");
    memcpy(&before, &f, sizeof(f));
    CHECK(moo_input_target_destroy(&f.core, foreign, target, UINT64_C(3)) ==
          MOO_INPUT_ACCESS && memcmp(&before, &f, sizeof(f)) == 0,
          "cross-owner destroy is byte-atomic");
    memcpy(&before, &f, sizeof(f));
    CHECK(moo_input_set_preferences(&f.core, 1u, 1u, UINT64_C(4),
          UINT64_C(4)) == MOO_INPUT_STALE_SERIAL &&
          memcmp(&before, &f, sizeof(f)) == 0,
          "stale serial reject is byte-atomic");
    memcpy(&before, &f, sizeof(f));
    CHECK(moo_input_set_preferences(&f.core, 1u, 1u, UINT64_MAX,
          UINT64_C(4)) == MOO_INPUT_INVALID &&
          memcmp(&before, &f, sizeof(f)) == 0,
          "MAX serial reject is byte-atomic");
    CHECK(moo_input_next_event(&f.core, app, &event) == MOO_INPUT_OK &&
          (event.flags & MOO_INPUT_EVENT_SENSITIVE) != 0u &&
          event.data.text.text.length == (uint32_t)sizeof(secret) &&
          memcmp(event.data.text.text.bytes, secret, sizeof(secret)) == 0 &&
          text_tail_zero(&event), "sensitive event canonical payload");
    CHECK(moo_input_text_preedit(&f.core, f.core.focus_epoch, UINT64_C(2),
          secret, (uint32_t)sizeof(secret), 0u, (uint32_t)sizeof(secret),
          UINT64_C(3), UINT64_C(3)) == MOO_INPUT_OK,
          "sensitive event re-saturates quota");
    CHECK(moo_input_client_disconnect(&f.core, app, UINT64_C(4)) ==
          MOO_INPUT_OK, "saturated disconnect consumes cleanup");
    CHECK(free_event_slots_canonical(&f),
          "disconnect scrubs sensitive event payload and metadata");
}

static void test_devtools_actual_redacted_producer(void) {
    Fixture f;
    MooInputHandle app;
    MooInputHandle reader;
    MooInputHandle target;
    MooInputHandle root;
    MooInputHandle password;
    MooA11yNodeData root_data;
    MooA11yNodeData password_data;
    MooA11yNodeData observed;
    MooUiHostParityInstrumentation instrumentation;
    MooUiHostParityDevtoolsInspection inspection;
    MooUiHostParityMeasurement measurement;
    const uint8_t secret[] = {'1', '2', '3', '4'};
    uint64_t revision = 0u;

    init(&f, 16u);
    CHECK(moo_input_client_create(&f.core, 0u, &app) == MOO_INPUT_OK,
          "devtools app");
    CHECK(moo_input_client_create(&f.core, MOO_INPUT_CLIENT_SCREEN_READER,
                                  &reader) == MOO_INPUT_OK,
          "devtools privileged reader");
    CHECK(moo_input_target_create_trusted(&f.core, app, 91u, 1u, &target) ==
              MOO_INPUT_OK,
          "devtools target");
    root_data = node_data(MOO_A11Y_ROLE_WINDOW, "root");
    CHECK(moo_a11y_node_create(&f.core, app, target, &root_data, &root) ==
              MOO_INPUT_OK,
          "devtools root");
    password_data = node_data(MOO_A11Y_ROLE_TEXT_FIELD, "secret-name");
    password_data.parent = root;
    password_data.states = MOO_A11Y_STATE_PASSWORD;
    password_data.actions = MOO_A11Y_ACTION_FOCUS |
        MOO_A11Y_ACTION_SET_VALUE;
    password_data.value.length = (uint32_t)sizeof(secret);
    memcpy(password_data.value.bytes, secret, sizeof(secret));
    CHECK(moo_a11y_node_create(&f.core, app, target, &password_data,
                               &password) == MOO_INPUT_OK,
          "devtools password node");
    CHECK(moo_a11y_node_read(&f.core, reader, password, &observed,
                             &revision) == MOO_INPUT_OK &&
          revision == 1u && observed.value.length == 0u,
          "devtools authoritative redacted revision");

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 101u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 101u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools producer instrumentation");
    inspection.core = &f.core;
    inspection.privileged_reader = app;
    inspection.node = password;
    inspection.expected_revision = revision;
    inspection.seeded_secret = secret;
    inspection.seeded_secret_length = (uint32_t)sizeof(secret);
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 101u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_trace_count == 0u,
          "ordinary app inspection denied without trace");
    inspection.privileged_reader = reader;
    inspection.expected_revision = revision + 1u;
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 101u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_trace_count == 0u,
          "revision drift rejected without trace");
    inspection.expected_revision = revision;
    inspection.node = root;
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 101u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_trace_count == 0u,
          "non-password node rejected");
    inspection.node = password;
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 101u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 101u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "two actual stable redacted inspections");
    CHECK(instrumentation.devtools_trace_count == 2u &&
          instrumentation.devtools_privacy_leaks == 0u &&
          instrumentation.devtools_traces[0].node == password &&
          instrumentation.devtools_traces[0].revision == revision &&
          instrumentation.devtools_traces[0].trace_hash != 0u &&
          instrumentation.devtools_traces[0].trace_hash ==
              instrumentation.devtools_traces[1].trace_hash,
          "stable node revision and trace hash exact");
    CHECK(moo_ui_host_parity_devtools_seal(
              &instrumentation, 101u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "actual devtools independent seal");
    memset(&measurement, 0, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 7u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
          measurement.sample_count == 2u &&
          measurement.value_a == 2u && measurement.value_b == 2u &&
          measurement.value_c == 0u,
          "actual devtools pass metrics");

    CHECK(moo_a11y_node_update(&f.core, app, password, revision,
                               &password_data) == MOO_INPUT_OK,
          "devtools revision update");
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 102u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 102u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools updated revision fixture");
    inspection.expected_revision = revision;
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 102u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_trace_count == 0u,
          "old revision rejected after update");
    inspection.expected_revision = revision + 1u;
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 102u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          instrumentation.devtools_traces[0].revision == revision + 1u,
          "new revision inspected exactly");

    CHECK(moo_a11y_node_destroy(&f.core, app, password) == MOO_INPUT_OK,
          "devtools stale node destroy");
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 103u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 103u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools stale fixture");
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
              &instrumentation, 103u, &inspection) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_trace_count == 0u,
          "stale node rejected without trace");
}

int main(void) {
    test_devtools_actual_redacted_producer();
    test_focus_key_ime();
    test_pointer_touch_stylus_and_quota();
    test_shortcut_and_modifiers();
    test_a11y_security_relations();
    test_disconnect_isolation_and_preferences();
    test_text_hash_privacy();
    test_sensitive_saturated_disconnect_atomicity();
    if (failures) {
        fprintf(stderr, "P016-O4-FAIL checks=%d failures=%d\n", checks, failures);
        return 1;
    }
    printf("P016-O4-INPUT-A11Y-OK checks=%d\n", checks);
    return 0;
}
