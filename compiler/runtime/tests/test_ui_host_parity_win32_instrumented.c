#include "../moo_input_core.h"
#include "../moo_ui_host_parity_devtools.h"
#include "../moo_ui_host_parity_helper_win32.h"
#include "../moo_ui_host_parity_instrumentation_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
int main(void) {
    puts("P016 O6 WIN32 INSTRUMENTED DEVTOOLS SKIP: 77");
    return 77;
}
#else

#define CLIENT_CAP UINT32_C(4)
#define TARGET_CAP UINT32_C(4)
#define EVENT_CAP UINT32_C(16)
#define NODE_CAP UINT32_C(8)

static unsigned checks;
static unsigned failures;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL:%u:%s\n", __LINE__, (message)); \
    } \
} while (0)

typedef struct {
    MooInputCore core;
    MooInputClientSlot clients[CLIENT_CAP];
    MooInputTargetSlot targets[TARGET_CAP];
    MooInputEventSlot events[EVENT_CAP];
    MooA11yNodeSlot nodes[NODE_CAP];
} Fixture;

static uint64_t test_features(void) {
    return MOO_INPUT_FEATURE_ACCESSIBILITY |
        MOO_INPUT_FEATURE_AUTOMATION;
}

static void fixture_init(Fixture *fixture) {
    MooInputConfig config;
    memset(fixture, 0, sizeof(*fixture));
    config.max_events_per_client = UINT32_C(8);
    config.max_a11y_depth = UINT32_C(4);
    config.features = test_features();
    CHECK(moo_input_init(
        &fixture->core, &config,
        fixture->clients, CLIENT_CAP,
        fixture->targets, TARGET_CAP,
        fixture->events, EVENT_CAP,
        fixture->nodes, NODE_CAP) == MOO_INPUT_OK,
        "input fixture init");
}

static MooA11yNodeData node_data(uint32_t role, const char *name) {
    MooA11yNodeData data;
    size_t length = strlen(name);
    memset(&data, 0, sizeof(data));
    data.role = role;
    data.bounds.width = 100;
    data.bounds.height = 20;
    if (length > MOO_INPUT_TEXT_CAPACITY)
        length = MOO_INPUT_TEXT_CAPACITY;
    data.name.length = (uint32_t)length;
    memcpy(data.name.bytes, name, length);
    return data;
}

static void prepare_measurement(MooUiHostParityMeasurement *measurement) {
    memset(measurement, 0, sizeof(*measurement));
    measurement->version = MOO_UI_HOST_PARITY_VERSION;
    measurement->domain = UINT32_C(7);
}

static void probe_devtools(
    MooUiHostParityState *state, MooUiHostParityMeasurement *measurement) {
    prepare_measurement(measurement);
    CHECK(state->ops.probe != 0, "public init installs probe");
    if (state->ops.probe != 0) {
        CHECK(state->ops.probe(state->user, UINT32_C(7), measurement) ==
            MOO_UI_HOST_PARITY_RESULT_OK, "DEVTOOLS-only probe");
    }
}

static void probe_clipboard(
    MooUiHostParityState *state, MooUiHostParityMeasurement *measurement) {
    memset(measurement, 0, sizeof(*measurement));
    measurement->version = MOO_UI_HOST_PARITY_VERSION;
    measurement->domain = UINT32_C(4);
    CHECK(state->ops.probe != 0, "public init installs clipboard probe");
    if (state->ops.probe != 0) {
        CHECK(state->ops.probe(state->user, UINT32_C(4), measurement) ==
            MOO_UI_HOST_PARITY_RESULT_OK, "CLIPBOARD-only probe");
    }
}

static void check_unsupported(
    const MooUiHostParityMeasurement *measurement, const char *message) {
    CHECK(measurement->evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED, message);
    CHECK(measurement->sample_count == 0u, "unsupported sample count");
    CHECK(measurement->value_a == UINT64_C(0) &&
        measurement->value_b == UINT64_C(0) &&
        measurement->value_c == UINT64_C(0),
        "unsupported values zero");
    CHECK(measurement->native_error == 0, "unsupported native error clear");
}

static void check_pass(const MooUiHostParityMeasurement *measurement) {
    CHECK(measurement->evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS,
        "sealed instrumentation passes DEVTOOLS");
    CHECK(measurement->sample_count == 2u, "two inspection samples");
    CHECK(measurement->value_a == UINT64_C(2), "two stable inspections");
    CHECK(measurement->value_b == UINT64_C(2), "two trace events");
    CHECK(measurement->value_c == UINT64_C(0), "zero privacy leaks");
    CHECK(measurement->native_error == 0, "pass native error clear");
}

static void check_clipboard_pass(
    const MooUiHostParityMeasurement *measurement) {
    CHECK(measurement->evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS,
        "sealed instrumentation passes CLIPBOARD");
    CHECK(measurement->sample_count == 2u, "clipboard sample modes");
    CHECK(measurement->value_a == UINT64_C(1), "one clipboard roundtrip");
    CHECK(measurement->value_b == UINT64_C(1), "one drag/drop sequence");
    CHECK(measurement->value_c == UINT64_C(0), "zero integrity mismatches");
    CHECK(measurement->native_error == 0, "clipboard native error clear");
}

int main(void) {
    Fixture fixture;
    MooInputHandle app;
    MooInputHandle reader;
    MooInputHandle target;
    MooInputHandle root;
    MooInputHandle password;
    MooA11yNodeData root_node;
    MooA11yNodeData password_node;
    MooA11yNodeData observed;
    MooUiHostParityInstrumentation sealed;
    MooUiHostParityInstrumentation unsealed;
    MooUiHostParityDevtoolsInspection inspection;
    MooUiHostParityState state;
    MooUiHostParityMeasurement default_measurement;
    MooUiHostParityMeasurement default_clipboard_measurement;
    MooUiHostParityMeasurement unsealed_measurement;
    MooUiHostParityMeasurement unsealed_clipboard_measurement;
    MooUiHostParityMeasurement sealed_measurement;
    MooUiHostParityMeasurement sealed_clipboard_measurement;
    const uint8_t secret[] = {'1', '2', '3', '4'};
    uint64_t revision = UINT64_C(0);

    fixture_init(&fixture);
    CHECK(moo_input_client_create(&fixture.core, 0u, &app) ==
        MOO_INPUT_OK, "app client");
    CHECK(moo_input_client_create(
        &fixture.core, MOO_INPUT_CLIENT_SCREEN_READER, &reader) ==
        MOO_INPUT_OK, "privileged screen reader");
    CHECK(moo_input_target_create_trusted(
        &fixture.core, app, UINT64_C(91), UINT64_C(1), &target) ==
        MOO_INPUT_OK, "trusted input target");

    root_node = node_data(MOO_A11Y_ROLE_WINDOW, "root");
    CHECK(moo_a11y_node_create(
        &fixture.core, app, target, &root_node, &root) ==
        MOO_INPUT_OK, "root accessibility node");
    password_node = node_data(MOO_A11Y_ROLE_TEXT_FIELD, "secret-name");
    password_node.parent = root;
    password_node.states = MOO_A11Y_STATE_PASSWORD;
    password_node.actions =
        MOO_A11Y_ACTION_FOCUS | MOO_A11Y_ACTION_SET_VALUE;
    password_node.value.length = (uint32_t)sizeof(secret);
    memcpy(password_node.value.bytes, secret, sizeof(secret));
    CHECK(moo_a11y_node_create(
        &fixture.core, app, target, &password_node, &password) ==
        MOO_INPUT_OK, "password accessibility node");
    CHECK(moo_a11y_node_read(
        &fixture.core, reader, password, &observed, &revision) ==
        MOO_INPUT_OK && revision == UINT64_C(1) &&
        observed.value.length == 0u,
        "authoritative screen-reader view redacts password");

    CHECK(moo_ui_host_parity_instrumentation_init(&sealed, UINT64_C(1)) ==
        MOO_UI_HOST_PARITY_RESULT_OK, "sealed instrumentation init");
    CHECK(moo_ui_host_parity_helper_win32_bind(&sealed, UINT64_C(1)) ==
        MOO_UI_HOST_PARITY_RESULT_OK, "public Win32 helper bind");
    inspection.core = &fixture.core;
    inspection.privileged_reader = reader;
    inspection.node = password;
    inspection.expected_revision = revision;
    inspection.seeded_secret = secret;
    inspection.seeded_secret_length = (uint32_t)sizeof(secret);
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
        &sealed, UINT64_C(1), &inspection) ==
        MOO_UI_HOST_PARITY_RESULT_OK, "first redacted inspection");
    CHECK(moo_ui_host_parity_devtools_inspect_password_node(
        &sealed, UINT64_C(1), &inspection) ==
        MOO_UI_HOST_PARITY_RESULT_OK, "second redacted inspection");
    CHECK(sealed.devtools_trace_count == 2u &&
        sealed.devtools_traces[0].revision == revision &&
        sealed.devtools_traces[0].trace_hash != UINT64_C(0) &&
        sealed.devtools_traces[0].trace_hash ==
            sealed.devtools_traces[1].trace_hash &&
        sealed.devtools_privacy_leaks == 0u,
        "stable redacted producer traces");
    CHECK(moo_ui_host_parity_devtools_seal(&sealed, UINT64_C(1)) ==
        MOO_UI_HOST_PARITY_RESULT_OK, "public devtools seal");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &sealed, UINT64_C(1), 1u, 1u, 0u) ==
        MOO_UI_HOST_PARITY_RESULT_OK, "clipboard producer record");
    CHECK(moo_ui_host_parity_helper_seal_clipboard(
              &sealed, UINT64_C(1)) ==
        MOO_UI_HOST_PARITY_RESULT_OK, "clipboard producer seal");

    CHECK(moo_ui_host_parity_instrumentation_init(
        &unsealed, UINT64_C(2)) == MOO_UI_HOST_PARITY_RESULT_OK &&
        moo_ui_host_parity_helper_win32_bind(&unsealed, UINT64_C(2)) ==
            MOO_UI_HOST_PARITY_RESULT_OK,
        "unsealed instrumentation fixture");

    CHECK(moo_ui_host_parity_init_win32(&state) ==
        MOO_UI_HOST_PARITY_RESULT_OK && state.user == 0,
        "public default init leaves producer unbound");
    probe_devtools(&state, &default_measurement);
    check_unsupported(&default_measurement, "default init unsupported");
    probe_clipboard(&state, &default_clipboard_measurement);
    check_unsupported(
        &default_clipboard_measurement, "default clipboard unsupported");
    moo_ui_host_parity_shutdown(&state);

    CHECK(moo_ui_host_parity_init_win32_instrumented(&state, &unsealed) ==
        MOO_UI_HOST_PARITY_RESULT_OK && state.user == &unsealed,
        "public unsealed instrumented init binds producer");
    probe_devtools(&state, &unsealed_measurement);
    check_unsupported(&unsealed_measurement, "unsealed unsupported");
    probe_clipboard(&state, &unsealed_clipboard_measurement);
    check_unsupported(
        &unsealed_clipboard_measurement, "unsealed clipboard unsupported");
    moo_ui_host_parity_shutdown(&state);

    CHECK(moo_ui_host_parity_init_win32_instrumented(&state, &sealed) ==
        MOO_UI_HOST_PARITY_RESULT_OK && state.user == &sealed,
        "public sealed instrumented init binds producer");
    probe_devtools(&state, &sealed_measurement);
    check_pass(&sealed_measurement);
    probe_clipboard(&state, &sealed_clipboard_measurement);
    check_clipboard_pass(&sealed_clipboard_measurement);
    moo_ui_host_parity_shutdown(&state);

    if (failures != 0u) {
        fprintf(stderr,
            "P016 O6 WIN32 INSTRUMENTED DEVTOOLS FAIL: checks=%u failures=%u\n",
            checks, failures);
        return 1;
    }
    printf(
        "P016 O6 WIN32 INSTRUMENTED DEVTOOLS GREEN: "
        "sealed=%u/%u/%llu/%llu/%llu default=%u/%u unsealed=%u/%u checks=%u\n",
        sealed_measurement.evidence, sealed_measurement.sample_count,
        (unsigned long long)sealed_measurement.value_a,
        (unsigned long long)sealed_measurement.value_b,
        (unsigned long long)sealed_measurement.value_c,
        default_measurement.evidence, default_measurement.sample_count,
        unsealed_measurement.evidence, unsealed_measurement.sample_count,
        checks);
    return 0;
}
#endif
