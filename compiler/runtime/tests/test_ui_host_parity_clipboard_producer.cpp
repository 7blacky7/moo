#include "../moo_ui_host_parity_clipboard_producer_win32.h"
extern "C" {
#include "../moo_ui_host_parity_instrumentation_internal.h"
}

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
static unsigned launcher_calls;
static unsigned record_calls;
static unsigned seal_calls;
static MooUiHostParityResult launcher_result;
static MooUiHostParityResult record_result;
static MooUiHostParityResult seal_result;
static MooUiHostParityClipboardWorkerMetrics launcher_metrics;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", (message)); \
    } \
} while (0)

static int metrics_zero(const MooUiHostParityClipboardWorkerMetrics *metrics) {
    return metrics->clipboard_roundtrips == 0u &&
        metrics->dragdrop_sequences == 0u &&
        metrics->integrity_mismatches == 0u && metrics->native_error == 0;
}

static void reset_stubs(void) {
    launcher_calls = 0u;
    record_calls = 0u;
    seal_calls = 0u;
    launcher_result = MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    record_result = MOO_UI_HOST_PARITY_RESULT_OK;
    seal_result = MOO_UI_HOST_PARITY_RESULT_OK;
    memset(&launcher_metrics, 0, sizeof(launcher_metrics));
}

extern "C" MooUiHostParityResult
moo_ui_host_parity_clipboard_win32_launcher_run(
    const MooUiHostParityClipboardLauncherConfig *,
    MooUiHostParityClipboardWorkerMetrics *metrics) {
    ++launcher_calls;
    *metrics = launcher_metrics;
    return launcher_result;
}

extern "C" MooUiHostParityResult moo_ui_host_parity_helper_record_clipboard(
    MooUiHostParityInstrumentation *, uint64_t,
    uint32_t roundtrips, uint32_t dragdrops, uint32_t mismatches) {
    ++record_calls;
    CHECK(roundtrips == 1u && dragdrops == 1u && mismatches == 0u,
        "record receives exact complete metrics");
    return record_result;
}

extern "C" MooUiHostParityResult moo_ui_host_parity_helper_seal_clipboard(
    MooUiHostParityInstrumentation *, uint64_t) {
    ++seal_calls;
    return seal_result;
}

int main(void) {
    MooUiHostParityInstrumentation instrumentation;
    MooUiHostParityClipboardLauncherConfig config;
    MooUiHostParityClipboardWorkerMetrics metrics;
    MooUiHostParityResult result;
    memset(&instrumentation, 0, sizeof(instrumentation));
    memset(&config, 0, sizeof(config));

    reset_stubs();
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_produce(
        nullptr, 1u, &config, &metrics);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_INVALID && metrics_zero(&metrics) &&
          launcher_calls == 0u && record_calls == 0u && seal_calls == 0u,
          "invalid producer input is exact zero without launch or seal");

    reset_stubs();
    launcher_metrics.clipboard_roundtrips = 1u;
    launcher_metrics.native_error = 1460;
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_produce(
        &instrumentation, 1u, &config, &metrics);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE && metrics_zero(&metrics) &&
          launcher_calls == 1u && record_calls == 0u && seal_calls == 0u,
          "launcher failure discards dirty metrics and stays unsealed");

    reset_stubs();
    launcher_result = MOO_UI_HOST_PARITY_RESULT_OK;
    launcher_metrics.clipboard_roundtrips = 1u;
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_produce(
        &instrumentation, 1u, &config, &metrics);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE && metrics_zero(&metrics) &&
          record_calls == 0u && seal_calls == 0u,
          "partial successful metrics remain exact-zero unsupported");

    reset_stubs();
    launcher_result = MOO_UI_HOST_PARITY_RESULT_OK;
    launcher_metrics.clipboard_roundtrips = 1u;
    launcher_metrics.dragdrop_sequences = 1u;
    record_result = MOO_UI_HOST_PARITY_RESULT_INVALID;
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_produce(
        &instrumentation, 1u, &config, &metrics);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_INVALID && metrics_zero(&metrics) &&
          record_calls == 1u && seal_calls == 0u,
          "record failure remains exact zero and does not seal");

    reset_stubs();
    launcher_result = MOO_UI_HOST_PARITY_RESULT_OK;
    launcher_metrics.clipboard_roundtrips = 1u;
    launcher_metrics.dragdrop_sequences = 1u;
    seal_result = MOO_UI_HOST_PARITY_RESULT_INVALID;
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_produce(
        &instrumentation, 1u, &config, &metrics);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_INVALID && metrics_zero(&metrics) &&
          record_calls == 1u && seal_calls == 1u,
          "seal failure remains exact zero");

    reset_stubs();
    launcher_result = MOO_UI_HOST_PARITY_RESULT_OK;
    launcher_metrics.clipboard_roundtrips = 1u;
    launcher_metrics.dragdrop_sequences = 1u;
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_produce(
        &instrumentation, 1u, &config, &metrics);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_OK &&
          metrics.clipboard_roundtrips == 1u &&
          metrics.dragdrop_sequences == 1u &&
          metrics.integrity_mismatches == 0u && metrics.native_error == 0 &&
          record_calls == 1u && seal_calls == 1u,
          "only complete metrics record, seal and become visible");

    if (failures != 0u) {
        fprintf(stderr, "P016 N9 CLIPBOARD PRODUCER FAIL: %u/%u\n",
            failures, checks);
        return 1;
    }
    printf("P016 N9 CLIPBOARD PRODUCER OK: checks=%u\n", checks);
    return 0;
}
