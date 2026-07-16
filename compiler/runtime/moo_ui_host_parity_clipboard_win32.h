#ifndef MOO_UI_HOST_PARITY_CLIPBOARD_WIN32_H
#define MOO_UI_HOST_PARITY_CLIPBOARD_WIN32_H
#include "moo_ui_host_parity.h"
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD UINT32_C(256)
typedef struct {
    const wchar_t *expected_window_station;
    const wchar_t *expected_desktop;
    const uint8_t *payload;
    uint32_t payload_length;
} MooUiHostParityClipboardWorkerConfig;
typedef struct {
    uint32_t clipboard_roundtrips;
    uint32_t dragdrop_sequences;
    uint32_t integrity_mismatches;
    int32_t native_error;
} MooUiHostParityClipboardWorkerMetrics;
MooUiHostParityResult moo_ui_host_parity_clipboard_win32_worker_run(
    const MooUiHostParityClipboardWorkerConfig *config,
    MooUiHostParityClipboardWorkerMetrics *metrics);
#if defined(_WIN32) && defined(MOO_UI_HOST_PARITY_TESTING)
int moo_ui_host_parity_clipboard_win32_test_medium_matches(
    uintptr_t global_handle, const uint8_t *payload, uint32_t length);
#endif
#ifdef __cplusplus
}
#endif
#endif
