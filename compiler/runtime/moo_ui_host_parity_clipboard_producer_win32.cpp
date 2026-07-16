#include "moo_ui_host_parity_clipboard_producer_win32.h"
extern "C" {
#include "moo_ui_host_parity_instrumentation_internal.h"
}

namespace {
static void zero_metrics(MooUiHostParityClipboardWorkerMetrics *metrics) {
    metrics->clipboard_roundtrips = 0u;
    metrics->dragdrop_sequences = 0u;
    metrics->integrity_mismatches = 0u;
    metrics->native_error = 0;
}
}

extern "C" MooUiHostParityResult
moo_ui_host_parity_clipboard_win32_produce(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    const MooUiHostParityClipboardLauncherConfig *config,
    MooUiHostParityClipboardWorkerMetrics *metrics) {
    MooUiHostParityClipboardWorkerMetrics observed;
    MooUiHostParityResult result;
    if (metrics == nullptr) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    zero_metrics(metrics);
    if (instrumentation == nullptr || generation == 0u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    zero_metrics(&observed);
    result = moo_ui_host_parity_clipboard_win32_launcher_run(
        config, &observed);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (observed.clipboard_roundtrips != 1u ||
        observed.dragdrop_sequences != 1u ||
        observed.integrity_mismatches != 0u || observed.native_error != 0)
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    result = moo_ui_host_parity_helper_record_clipboard(
        instrumentation, generation, observed.clipboard_roundtrips,
        observed.dragdrop_sequences, observed.integrity_mismatches);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    result = moo_ui_host_parity_helper_seal_clipboard(
        instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    *metrics = observed;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}
