/* Headless HWIN host adapter tests: no HWND, desktop input or capture. */
#include "../moo_ui_host_effects_win32.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    MooUiHostWin32Result probe_result;
    MooUiHostWin32Result apply_result;
    uint64_t probe_capabilities;
    uint64_t applied_capabilities;
    uint32_t applied_backdrop;
    int32_t probe_error;
    int32_t apply_error;
    uint32_t probe_calls;
    uint32_t apply_calls;
    uint32_t reset_calls;
    uint64_t last_allowed;
    MooUiHostWin32Request last_request;
} FakeHost;

static int checks;
static int failures;
#define CHECK(condition, message) do {                                      \
    checks++;                                                               \
    if (!(condition)) {                                                     \
        failures++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
    }                                                                       \
} while (0)

static MooUiHostWin32Result fake_probe(
    void *user, uint64_t *out_capabilities, int32_t *out_error) {
    FakeHost *host = (FakeHost *)user;
    host->probe_calls++;
    *out_capabilities = host->probe_capabilities;
    *out_error = host->probe_error;
    return host->probe_result;
}

static MooUiHostWin32Result fake_apply(
    void *user, const MooUiHostWin32Request *request, uint64_t allowed,
    uint64_t *out_applied, uint32_t *out_backdrop, int32_t *out_error) {
    FakeHost *host = (FakeHost *)user;
    host->apply_calls++;
    host->last_allowed = allowed;
    host->last_request = *request;
    *out_applied = host->applied_capabilities;
    *out_backdrop = host->applied_backdrop;
    *out_error = host->apply_error;
    return host->apply_result;
}

static void fake_reset(void *user) {
    FakeHost *host = (FakeHost *)user;
    host->reset_calls++;
}

static MooUiHostWin32Ops fake_ops(void) {
    MooUiHostWin32Ops ops;
    ops.probe = fake_probe;
    ops.apply = fake_apply;
    ops.reset = fake_reset;
    return ops;
}

static void test_success_and_capability_truth(void) {
    const uint64_t caps =
        MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR |
        MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS |
        MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP |
        MOO_UI_HOST_WIN32_CAP_MICA;
    FakeHost host;
    MooUiHostWin32State state;
    MooUiHostWin32Ops ops = fake_ops();
    MooUiHostWin32Request request;
    MooUiHostWin32Outcome outcome;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.apply_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.probe_capabilities = caps | (UINT64_C(1) << 63);
    host.applied_capabilities = caps;
    host.applied_backdrop = MOO_UI_HOST_WIN32_BACKDROP_MICA;

    CHECK(moo_ui_host_effects_win32_init(&state, &ops, &host) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "ready probe must initialize");
    CHECK(host.probe_calls == 1u, "probe exactly once");
    CHECK(moo_ui_host_effects_win32_capabilities(&state) == caps,
          "unknown native capability bits must be masked");
    CHECK(moo_ui_host_effects_win32_generation(&state) == UINT64_C(1),
          "initial generation");

    request = moo_ui_host_effects_win32_request_default();
    request.window = (MooUiHostWin32Window)UINT32_C(0x1234);
    request.backdrop = MOO_UI_HOST_WIN32_BACKDROP_MICA;
    request.dark_title_bar = 1u;
    request.rounded_corners = 1u;
    request.required_host_capabilities = caps;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "fully supported native apply");
    CHECK(host.apply_calls == 1u && host.last_allowed == caps,
          "apply receives truthful masked host caps");
    CHECK(outcome.available_host_capabilities == caps &&
              outcome.applied_host_capabilities == caps &&
              outcome.missing_host_capabilities == UINT64_C(0),
          "outcome must separate available/applied/missing");
    CHECK(outcome.applied_backdrop == MOO_UI_HOST_WIN32_BACKDROP_MICA &&
              outcome.fallback_used == MOO_UI_HOST_WIN32_FALLBACK_NONE,
          "native mica without fallback");
    CHECK(memcmp(&host.last_request, &request, sizeof(request)) == 0,
          "request forwarded exactly");
    moo_ui_host_effects_win32_shutdown(&state);
    CHECK(host.reset_calls == 1u &&
              moo_ui_host_effects_win32_generation(&state) == UINT64_C(0),
          "shutdown resets injected host and state");
}

static void test_unavailable_fallbacks(void) {
    FakeHost host;
    MooUiHostWin32State state;
    MooUiHostWin32Ops ops = fake_ops();
    MooUiHostWin32Request request;
    MooUiHostWin32Outcome outcome;
    const uint64_t wanted = MOO_UI_HOST_WIN32_CAP_ACRYLIC;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    host.probe_capabilities = MOO_UI_HOST_WIN32_CAP_ALL;
    CHECK(moo_ui_host_effects_win32_init(&state, &ops, &host) ==
              MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE,
          "unavailable probe stays explicit");
    CHECK(moo_ui_host_effects_win32_capabilities(&state) == UINT64_C(0),
          "unavailable host advertises zero capabilities");

    request = moo_ui_host_effects_win32_request_default();
    request.window = (MooUiHostWin32Window)UINT32_C(1);
    request.backdrop = MOO_UI_HOST_WIN32_BACKDROP_ACRYLIC;
    request.required_host_capabilities = wanted;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "default portable CPU fallback keeps presentation viable");
    CHECK(host.apply_calls == 0u, "unavailable host callback not invoked");
    CHECK(outcome.native_state == MOO_UI_HOST_WIN32_NATIVE_UNAVAILABLE &&
              outcome.available_host_capabilities == UINT64_C(0) &&
              outcome.applied_host_capabilities == UINT64_C(0) &&
              (outcome.missing_host_capabilities & wanted) == wanted &&
              outcome.fallback_used ==
                  MOO_UI_HOST_WIN32_FALLBACK_PORTABLE_CPU,
          "unavailable outcome is truthful and explicit");

    request.fallback = MOO_UI_HOST_WIN32_FALLBACK_OPAQUE_WINDOW;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_OK &&
              outcome.fallback_used ==
                  MOO_UI_HOST_WIN32_FALLBACK_OPAQUE_WINDOW,
          "opaque window fallback selectable");
    request.fallback = MOO_UI_HOST_WIN32_FALLBACK_NONE;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE,
          "no-fallback caller receives unavailable");
}

static void test_invalid_is_fail_closed(void) {
    FakeHost host;
    MooUiHostWin32State state;
    MooUiHostWin32Ops ops = fake_ops();
    MooUiHostWin32Request request;
    MooUiHostWin32Outcome before;
    MooUiHostWin32Outcome outcome;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.apply_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.probe_capabilities = MOO_UI_HOST_WIN32_CAP_ALL;
    CHECK(moo_ui_host_effects_win32_init(&state, &ops, &host) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "invalid fixture init");
    request = moo_ui_host_effects_win32_request_default();
    request.window = MOO_UI_HOST_WIN32_WINDOW_INVALID;
    memset(&outcome, 0xa5, sizeof(outcome));
    before = outcome;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_INVALID,
          "invalid window rejected");
    CHECK(host.apply_calls == 0u &&
              memcmp(&outcome, &before, sizeof(outcome)) == 0,
          "invalid request mutates neither host nor outcome");
}

static void test_error_loss_and_recovery(void) {
    FakeHost host;
    MooUiHostWin32State state;
    MooUiHostWin32Ops ops = fake_ops();
    MooUiHostWin32Request request;
    MooUiHostWin32Outcome outcome;
    const uint64_t wanted =
        MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR |
        MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP |
        MOO_UI_HOST_WIN32_CAP_MICA;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.apply_result = MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR;
    host.probe_capabilities = MOO_UI_HOST_WIN32_CAP_ALL;
    host.applied_capabilities = MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR;
    host.applied_backdrop = MOO_UI_HOST_WIN32_BACKDROP_NONE;
    host.apply_error = -55;
    CHECK(moo_ui_host_effects_win32_init(&state, &ops, &host) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "error fixture init");
    request = moo_ui_host_effects_win32_request_default();
    request.window = (MooUiHostWin32Window)UINT32_C(7);
    request.backdrop = MOO_UI_HOST_WIN32_BACKDROP_MICA;
    request.dark_title_bar = 1u;
    request.required_host_capabilities = wanted;
    request.fallback = MOO_UI_HOST_WIN32_FALLBACK_OPAQUE_WINDOW;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "system error must select requested opaque fallback");
    CHECK(state.native_state == MOO_UI_HOST_WIN32_NATIVE_ERROR &&
              outcome.native_state == MOO_UI_HOST_WIN32_NATIVE_ERROR &&
              outcome.native_error == -55 &&
              outcome.applied_host_capabilities ==
                  MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR &&
              outcome.fallback_used ==
                  MOO_UI_HOST_WIN32_FALLBACK_OPAQUE_WINDOW,
          "system error preserves truthful partial evidence");

    host.probe_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.probe_error = 0;
    CHECK(moo_ui_host_effects_win32_recover(&state) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "recover reprobes after native error");
    CHECK(host.reset_calls == 1u && host.probe_calls == 2u &&
              state.native_state == MOO_UI_HOST_WIN32_NATIVE_READY &&
              moo_ui_host_effects_win32_generation(&state) == UINT64_C(2),
          "recover reset/probe/generation exactly once");

    host.apply_result = MOO_UI_HOST_WIN32_RESULT_LOST;
    host.applied_capabilities = UINT64_C(0);
    host.apply_error = -77;
    request.fallback = MOO_UI_HOST_WIN32_FALLBACK_PORTABLE_CPU;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "device loss must fall back to portable CPU");
    CHECK(state.native_state == MOO_UI_HOST_WIN32_NATIVE_LOST &&
              moo_ui_host_effects_win32_capabilities(&state) == UINT64_C(0) &&
              moo_ui_host_effects_win32_generation(&state) == UINT64_C(3) &&
              outcome.native_state == MOO_UI_HOST_WIN32_NATIVE_LOST &&
              outcome.native_error == -77 &&
              outcome.fallback_used ==
                  MOO_UI_HOST_WIN32_FALLBACK_PORTABLE_CPU,
          "lost state revokes capabilities and advances generation");
    {
        const uint32_t apply_calls = host.apply_calls;
        CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
                  MOO_UI_HOST_WIN32_RESULT_OK &&
                  host.apply_calls == apply_calls,
              "lost state never calls native apply again");
    }

    host.probe_result = MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    CHECK(moo_ui_host_effects_win32_recover(&state) ==
              MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE,
          "failed recovery remains explicitly unavailable");
    CHECK(state.native_state == MOO_UI_HOST_WIN32_NATIVE_UNAVAILABLE &&
              moo_ui_host_effects_win32_generation(&state) == UINT64_C(4),
          "unavailable recovery still advances attempt generation");
    state.generation = UINT64_MAX;
    CHECK(moo_ui_host_effects_win32_mark_lost(&state, -99) ==
              MOO_UI_HOST_WIN32_RESULT_LOST &&
              state.generation == UINT64_MAX,
          "generation saturates instead of wrapping");
}

static void test_partial_success_degrades(void) {
    FakeHost host;
    MooUiHostWin32State state;
    MooUiHostWin32Ops ops = fake_ops();
    MooUiHostWin32Request request;
    MooUiHostWin32Outcome outcome;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.apply_result = MOO_UI_HOST_WIN32_RESULT_OK;
    host.probe_capabilities = MOO_UI_HOST_WIN32_CAP_ALL;
    host.applied_capabilities = MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR;
    CHECK(moo_ui_host_effects_win32_init(&state, &ops, &host) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "partial fixture init");
    request = moo_ui_host_effects_win32_request_default();
    request.window = (MooUiHostWin32Window)UINT32_C(9);
    request.dark_title_bar = 1u;
    request.rounded_corners = 1u;
    CHECK(moo_ui_host_effects_win32_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_WIN32_RESULT_OK,
          "partial native success must use fallback");
    CHECK(state.native_state == MOO_UI_HOST_WIN32_NATIVE_READY &&
              outcome.applied_host_capabilities ==
                  MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR &&
              outcome.missing_host_capabilities ==
                  MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS &&
              outcome.fallback_used ==
                  MOO_UI_HOST_WIN32_FALLBACK_PORTABLE_CPU,
          "partial apply is visible and never promoted to full capability");
}

int main(void) {
    test_success_and_capability_truth();
    test_unavailable_fallbacks();
    test_invalid_is_fail_closed();
    test_error_loss_and_recovery();
    test_partial_success_degrades();
    if (failures != 0) {
        fprintf(stderr, "P016 HWIN HOST RED: %d/%d failed\n", failures, checks);
        return 1;
    }
    printf("P016 HWIN HOST GREEN: %d checks\n", checks);
    return 0;
}
