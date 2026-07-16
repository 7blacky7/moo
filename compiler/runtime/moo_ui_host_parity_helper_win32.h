#ifndef MOO_UI_HOST_PARITY_HELPER_WIN32_H
#define MOO_UI_HOST_PARITY_HELPER_WIN32_H

#include "moo_ui_host_parity_instrumentation.h"

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const wchar_t *executable_path;
    const wchar_t *crash_argument;
    const wchar_t *restart_argument;
    uint64_t expected_state_hash;
    uint32_t crash_exit_code;
    uint32_t timeout_ms;
} MooUiHostParityCrashProcessConfig;

MooUiHostParityResult moo_ui_host_parity_helper_win32_bind(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation);
MooUiHostParityResult moo_ui_host_parity_helper_win32_measure_reduced_idle(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    uint32_t duration_ms);
MooUiHostParityResult moo_ui_host_parity_helper_win32_run_crash_cycle(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    const MooUiHostParityCrashProcessConfig *config);

#ifdef __cplusplus
}
#endif
#endif
