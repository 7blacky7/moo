#ifndef MOO_INPUT_WIN32_H
#define MOO_INPUT_WIN32_H

#include "moo_input_core.h"

#ifdef _WIN32
#include <windows.h>

typedef struct {
    MooInputCore *core;
    MooInputHandle target;
    int32_t pointer_x;
    int32_t pointer_y;
    uint16_t pending_high_surrogate;
    uint32_t tracking_leave;
    uint32_t pending_leave;
    uint32_t releasing_capture;
} MooInputWin32Adapter;

uint64_t moo_input_win32_features(void);
MooInputResult moo_input_win32_refresh_preferences(
    MooInputWin32Adapter *adapter, uint64_t timestamp_ns);
MooInputResult moo_input_win32_init(MooInputWin32Adapter *adapter,
    MooInputCore *core, MooInputHandle target);
MooInputResult moo_input_win32_message(MooInputWin32Adapter *adapter,
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    uint64_t timestamp_ns, uint32_t *out_handled);
uint32_t moo_input_win32_physical_key(LPARAM key_lparam);

#endif
#endif
