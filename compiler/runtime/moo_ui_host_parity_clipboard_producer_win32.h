#ifndef MOO_UI_HOST_PARITY_CLIPBOARD_PRODUCER_WIN32_H
#define MOO_UI_HOST_PARITY_CLIPBOARD_PRODUCER_WIN32_H

#include "moo_ui_host_parity_clipboard_launcher_win32.h"
#include "moo_ui_host_parity_instrumentation.h"

#ifdef __cplusplus
extern "C" {
#endif

MooUiHostParityResult moo_ui_host_parity_clipboard_win32_produce(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    const MooUiHostParityClipboardLauncherConfig *config,
    MooUiHostParityClipboardWorkerMetrics *metrics);

#ifdef __cplusplus
}
#endif

#endif
