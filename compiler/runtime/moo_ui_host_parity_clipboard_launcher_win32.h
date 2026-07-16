#ifndef MOO_UI_HOST_PARITY_CLIPBOARD_LAUNCHER_WIN32_H
#define MOO_UI_HOST_PARITY_CLIPBOARD_LAUNCHER_WIN32_H
#include "moo_ui_host_parity_clipboard_win32.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE UINT32_C(28)
typedef struct {
    const wchar_t *executable_path;
    const uint8_t *payload;
    uint32_t payload_length;
    uint32_t timeout_ms;
} MooUiHostParityClipboardLauncherConfig;
MooUiHostParityResult moo_ui_host_parity_clipboard_wire_encode(
    const MooUiHostParityClipboardWorkerMetrics *metrics,
    uint8_t wire[MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE]);
MooUiHostParityResult moo_ui_host_parity_clipboard_win32_launcher_run(
    const MooUiHostParityClipboardLauncherConfig *config,
    MooUiHostParityClipboardWorkerMetrics *metrics);
#ifdef __cplusplus
}
#endif
#endif
