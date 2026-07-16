/* Headless HMAC host adapter tests: no NSWindow or desktop capture. */
#include "../moo_ui_host_effects_cocoa.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    MooUiHostCocoaResult probe_result;
    MooUiHostCocoaResult apply_result;
    uint64_t probe_capabilities;
    uint64_t applied_capabilities;
    uint32_t applied_material;
    int32_t probe_error;
    int32_t apply_error;
    uint32_t probe_calls;
    uint32_t apply_calls;
    uint32_t reset_calls;
} FakeCocoa;

static int checks;
static int failures;
#define CHECK(condition, message) do {                                      \
    checks++;                                                               \
    if (!(condition)) {                                                     \
        failures++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
    }                                                                       \
} while (0)

static MooUiHostCocoaResult fake_probe(
    void *user, uint64_t *out_capabilities, int32_t *out_error) {
    FakeCocoa *host = (FakeCocoa *)user;
    host->probe_calls++;
    *out_capabilities = host->probe_capabilities;
    *out_error = host->probe_error;
    return host->probe_result;
}

static MooUiHostCocoaResult fake_apply(
    void *user, const MooUiHostCocoaRequest *request, uint64_t allowed,
    uint64_t *out_applied, uint32_t *out_material, int32_t *out_error) {
    FakeCocoa *host = (FakeCocoa *)user;
    (void)request;
    (void)allowed;
    host->apply_calls++;
    *out_applied = host->applied_capabilities;
    *out_material = host->applied_material;
    *out_error = host->apply_error;
    return host->apply_result;
}

static void fake_reset(void *user) {
    ((FakeCocoa *)user)->reset_calls++;
}

static MooUiHostCocoaOps fake_ops(void) {
    MooUiHostCocoaOps ops;
    ops.probe = fake_probe;
    ops.apply = fake_apply;
    ops.reset = fake_reset;
    return ops;
}

static void test_success_truth(void) {
    const uint64_t caps =
        MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR |
        MOO_UI_HOST_COCOA_CAP_VIBRANCY |
        MOO_UI_HOST_COCOA_CAP_WITHIN_WINDOW |
        MOO_UI_HOST_COCOA_CAP_REDUCE_TRANSPARENCY_AWARE;
    FakeCocoa host;
    MooUiHostCocoaState state;
    MooUiHostCocoaOps ops = fake_ops();
    MooUiHostCocoaRequest request;
    MooUiHostCocoaOutcome outcome;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_COCOA_RESULT_OK;
    host.apply_result = MOO_UI_HOST_COCOA_RESULT_OK;
    host.probe_capabilities = caps | (UINT64_C(1) << 63);
    host.applied_capabilities = caps;
    host.applied_material = MOO_UI_HOST_COCOA_MATERIAL_SIDEBAR;
    CHECK(moo_ui_host_effects_cocoa_init(&state, &ops, &host) ==
              MOO_UI_HOST_COCOA_RESULT_OK,
          "ready Cocoa init");
    CHECK(moo_ui_host_effects_cocoa_capabilities(&state) == caps &&
              host.probe_calls == 1u,
          "mask unknown bits and probe once");
    request = moo_ui_host_effects_cocoa_request_default();
    request.window = (MooUiHostCocoaWindow)UINT32_C(11);
    request.material = MOO_UI_HOST_COCOA_MATERIAL_SIDEBAR;
    request.blending = MOO_UI_HOST_COCOA_BLEND_WITHIN_WINDOW;
    request.transparent_titlebar = 1u;
    request.required_host_capabilities = caps;
    CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_COCOA_RESULT_OK,
          "full Cocoa native success");
    CHECK(outcome.available_host_capabilities == caps &&
              outcome.applied_host_capabilities == caps &&
              outcome.missing_host_capabilities == UINT64_C(0) &&
              outcome.applied_material == MOO_UI_HOST_COCOA_MATERIAL_SIDEBAR &&
              outcome.fallback_used == MOO_UI_HOST_COCOA_FALLBACK_NONE,
          "truthful Cocoa outcome");
}

static void test_unavailable_and_native_stub(void) {
    FakeCocoa host;
    MooUiHostCocoaState state;
    MooUiHostCocoaOps ops = fake_ops();
    MooUiHostCocoaRequest request;
    MooUiHostCocoaOutcome outcome;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
    host.probe_capabilities = MOO_UI_HOST_COCOA_CAP_ALL;
    CHECK(moo_ui_host_effects_cocoa_init(&state, &ops, &host) ==
              MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE &&
              moo_ui_host_effects_cocoa_capabilities(&state) == UINT64_C(0),
          "unavailable advertises no native capability");
    request = moo_ui_host_effects_cocoa_request_default();
    request.window = (MooUiHostCocoaWindow)UINT32_C(12);
    request.material = MOO_UI_HOST_COCOA_MATERIAL_HUD;
    CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_COCOA_RESULT_OK && host.apply_calls == 0u &&
              outcome.fallback_used == MOO_UI_HOST_COCOA_FALLBACK_PORTABLE_CPU &&
              outcome.native_state == MOO_UI_HOST_COCOA_NATIVE_UNAVAILABLE,
          "unavailable uses CPU fallback without native call");
    request.fallback = MOO_UI_HOST_COCOA_FALLBACK_NONE;
    CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE,
          "no fallback exposes unavailable");
#if !defined(__APPLE__)
    CHECK(moo_ui_host_effects_cocoa_init_native(&state) ==
              MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE,
          "non-macOS native adapter is honestly unavailable");
#endif
}

static void test_error_loss_recovery_and_partial(void) {
    FakeCocoa host;
    MooUiHostCocoaState state;
    MooUiHostCocoaOps ops = fake_ops();
    MooUiHostCocoaRequest request;
    MooUiHostCocoaOutcome outcome;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_COCOA_RESULT_OK;
    host.probe_capabilities = MOO_UI_HOST_COCOA_CAP_ALL;
    host.apply_result = MOO_UI_HOST_COCOA_RESULT_SYSTEM_ERROR;
    host.apply_error = -31;
    host.applied_capabilities = MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR;
    CHECK(moo_ui_host_effects_cocoa_init(&state, &ops, &host) ==
              MOO_UI_HOST_COCOA_RESULT_OK,
          "error fixture init");
    request = moo_ui_host_effects_cocoa_request_default();
    request.window = (MooUiHostCocoaWindow)UINT32_C(13);
    request.material = MOO_UI_HOST_COCOA_MATERIAL_CONTENT;
    request.transparent_titlebar = 1u;
    request.fallback = MOO_UI_HOST_COCOA_FALLBACK_OPAQUE_WINDOW;
    CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_COCOA_RESULT_OK &&
              state.native_state == MOO_UI_HOST_COCOA_NATIVE_ERROR &&
              outcome.native_error == -31 &&
              outcome.applied_host_capabilities ==
                  MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR &&
              outcome.fallback_used == MOO_UI_HOST_COCOA_FALLBACK_OPAQUE_WINDOW,
          "system error preserves partial evidence and opaque fallback");
    host.apply_result = MOO_UI_HOST_COCOA_RESULT_OK;
    host.apply_error = 0;
    host.applied_capabilities = MOO_UI_HOST_COCOA_CAP_ALL;
    CHECK(moo_ui_host_effects_cocoa_recover(&state) ==
              MOO_UI_HOST_COCOA_RESULT_OK && host.reset_calls == 1u &&
              moo_ui_host_effects_cocoa_generation(&state) == UINT64_C(2),
          "recover reset/probe/generation");
    host.apply_result = MOO_UI_HOST_COCOA_RESULT_LOST;
    host.apply_error = -41;
    host.applied_capabilities = UINT64_C(0);
    request.fallback = MOO_UI_HOST_COCOA_FALLBACK_PORTABLE_CPU;
    CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_COCOA_RESULT_OK &&
              state.native_state == MOO_UI_HOST_COCOA_NATIVE_LOST &&
              moo_ui_host_effects_cocoa_capabilities(&state) == UINT64_C(0) &&
              moo_ui_host_effects_cocoa_generation(&state) == UINT64_C(3) &&
              outcome.fallback_used == MOO_UI_HOST_COCOA_FALLBACK_PORTABLE_CPU,
          "lost revokes caps and selects CPU fallback");
    {
        uint32_t calls = host.apply_calls;
        CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
                  MOO_UI_HOST_COCOA_RESULT_OK && host.apply_calls == calls,
              "lost state never re-enters AppKit apply");
    }
    host.probe_result = MOO_UI_HOST_COCOA_RESULT_OK;
    host.apply_result = MOO_UI_HOST_COCOA_RESULT_OK;
    host.applied_capabilities =
        MOO_UI_HOST_COCOA_CAP_VIBRANCY |
        MOO_UI_HOST_COCOA_CAP_REDUCE_TRANSPARENCY_AWARE |
        MOO_UI_HOST_COCOA_CAP_BEHIND_WINDOW;
    CHECK(moo_ui_host_effects_cocoa_recover(&state) ==
              MOO_UI_HOST_COCOA_RESULT_OK,
          "second recovery");
    request.transparent_titlebar = 1u;
    CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_COCOA_RESULT_OK &&
              (outcome.missing_host_capabilities &
               MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR) != UINT64_C(0) &&
              outcome.fallback_used == MOO_UI_HOST_COCOA_FALLBACK_PORTABLE_CPU,
          "partial OK is degraded, never promoted to full support");
}

static void test_invalid_atomic(void) {
    FakeCocoa host;
    MooUiHostCocoaState state;
    MooUiHostCocoaOps ops = fake_ops();
    MooUiHostCocoaRequest request;
    MooUiHostCocoaOutcome before;
    MooUiHostCocoaOutcome outcome;
    memset(&host, 0, sizeof(host));
    host.probe_result = MOO_UI_HOST_COCOA_RESULT_OK;
    host.probe_capabilities = MOO_UI_HOST_COCOA_CAP_ALL;
    host.apply_result = MOO_UI_HOST_COCOA_RESULT_OK;
    CHECK(moo_ui_host_effects_cocoa_init(&state, &ops, &host) ==
              MOO_UI_HOST_COCOA_RESULT_OK,
          "invalid fixture init");
    request = moo_ui_host_effects_cocoa_request_default();
    request.window = MOO_UI_HOST_COCOA_WINDOW_INVALID;
    memset(&outcome, 0xa5, sizeof(outcome));
    before = outcome;
    CHECK(moo_ui_host_effects_cocoa_apply(&state, &request, &outcome) ==
              MOO_UI_HOST_COCOA_RESULT_INVALID && host.apply_calls == 0u &&
              memcmp(&before, &outcome, sizeof(outcome)) == 0,
          "invalid Cocoa request is fail-closed and atomic");
}

int main(void) {
    test_success_truth();
    test_unavailable_and_native_stub();
    test_error_loss_recovery_and_partial();
    test_invalid_atomic();
    if (failures != 0) {
        fprintf(stderr, "P016 HMAC HOST RED: %d/%d failed\n", failures, checks);
        return 1;
    }
    printf("P016 HMAC HOST GREEN: %d checks\n", checks);
    return 0;
}
