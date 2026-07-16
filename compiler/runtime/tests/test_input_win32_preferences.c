#include "../moo_input_win32.h"

#ifdef _WIN32

#include <stdio.h>

/* Tests-first declaration: production adds this public adapter entry point. */
MooInputResult moo_input_win32_refresh_preferences(
    MooInputWin32Adapter *adapter, uint64_t timestamp_ns);

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL:%d:%s\n", __LINE__, (message));              \
        return 1;                                                           \
    }                                                                       \
} while (0)

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

int main(void) {
    MooInputCore core;
    MooInputClientSlot clients[1];
    MooInputTargetSlot targets[1];
    MooInputEventSlot events[4];
    MooA11yNodeSlot nodes[1];
    MooInputConfig config;
    MooInputHandle client;
    MooInputHandle target;
    MooInputWin32Adapter adapter;
    MooInputEvent event;
    HIGHCONTRASTW native_high_contrast;
    BOOL native_client_area_animation = FALSE;
    uint32_t expected_high_contrast;
    uint32_t expected_reduced_motion;
    uint32_t handled;
    uint64_t serial_before;
    const uint64_t timestamp_ns = UINT64_C(4242);

    ZeroMemory(&native_high_contrast, sizeof(native_high_contrast));
    native_high_contrast.cbSize = sizeof(native_high_contrast);
    CHECK(SystemParametersInfoW(SPI_GETHIGHCONTRAST,
          sizeof(native_high_contrast), &native_high_contrast, 0) != 0,
          "read high contrast");
    CHECK(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
          &native_client_area_animation, 0) != 0,
          "read client area animation");
    expected_high_contrast =
        (native_high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0u ? 1u : 0u;
    expected_reduced_motion = native_client_area_animation != FALSE ? 0u : 1u;

    config.max_events_per_client = 4u;
    config.max_a11y_depth = 1u;
    config.features = moo_input_win32_features();
    CHECK((config.features & (MOO_INPUT_FEATURE_HIGH_CONTRAST |
          MOO_INPUT_FEATURE_REDUCED_MOTION)) ==
          (MOO_INPUT_FEATURE_HIGH_CONTRAST |
          MOO_INPUT_FEATURE_REDUCED_MOTION),
          "preference feature bits");
    CHECK(moo_input_init(&core, &config, clients, 1u, targets, 1u,
          events, 4u, nodes, 1u) == MOO_INPUT_OK, "core init");
    CHECK(moo_input_client_create(&core, 0u, &client) == MOO_INPUT_OK,
          "one live client");
    CHECK(moo_input_target_create_trusted(&core, client, 1u,
          MOO_INPUT_TEXT_NORMAL, &target) == MOO_INPUT_OK, "target");
    CHECK(moo_input_win32_init(&adapter, &core, target) == MOO_INPUT_OK,
          "adapter init");

    serial_before = core.accepted_serial;
    CHECK(moo_input_win32_refresh_preferences(0, timestamp_ns) ==
          MOO_INPUT_INVALID, "null adapter fails closed");
    CHECK(core.accepted_serial == serial_before &&
          core.high_contrast == 0u && core.reduced_motion == 0u &&
          drain(&core, client, 0) == 0u,
          "invalid refresh has no state event or serial effect");

    CHECK(moo_input_win32_refresh_preferences(&adapter, timestamp_ns) ==
          MOO_INPUT_OK, "refresh preferences");
    CHECK(core.accepted_serial == serial_before + 1u,
          "serial increments exactly once");
    CHECK(core.high_contrast == expected_high_contrast &&
          core.reduced_motion == expected_reduced_motion,
          "native boolean values equal core state");
    CHECK(drain(&core, client, &event) == 1u,
          "one live client receives exactly one event");
    CHECK(event.type == MOO_INPUT_EVENT_PREFERENCES &&
          event.target == MOO_INPUT_HANDLE_INVALID &&
          event.serial == serial_before + 1u &&
          event.timestamp_ns == timestamp_ns &&
          event.data.preferences.high_contrast == expected_high_contrast &&
          event.data.preferences.reduced_motion == expected_reduced_motion,
          "exact preferences event");

    serial_before = core.accepted_serial;
    handled = 99u;
    CHECK(moo_input_win32_message(&adapter, 0, WM_SETTINGCHANGE, 0, 0,
          timestamp_ns + UINT64_C(1), &handled) == MOO_INPUT_OK,
          "setting change refresh");
    CHECK(handled == 1u, "setting change handled");
    CHECK(core.accepted_serial == serial_before + 1u &&
          core.high_contrast == expected_high_contrast &&
          core.reduced_motion == expected_reduced_motion,
          "setting change updates once with native values");
    CHECK(drain(&core, client, &event) == 1u,
          "setting change emits exactly one event");
    CHECK(event.type == MOO_INPUT_EVENT_PREFERENCES &&
          event.target == MOO_INPUT_HANDLE_INVALID &&
          event.serial == serial_before + 1u &&
          event.timestamp_ns == timestamp_ns + UINT64_C(1) &&
          event.data.preferences.high_contrast == expected_high_contrast &&
          event.data.preferences.reduced_motion == expected_reduced_motion,
          "exact setting change preferences event");

    puts("P016-O4-WIN32-PREFERENCES-OK");
    return 0;
}

#else
int main(void) { return 77; }
#endif
