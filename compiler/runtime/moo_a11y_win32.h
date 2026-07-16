#ifndef MOO_A11Y_WIN32_H
#define MOO_A11Y_WIN32_H

#include "moo_input_core.h"

#if defined(_WIN32)
#include <windows.h>

typedef struct MooA11yWin32Provider MooA11yWin32Provider;

MooInputResult moo_a11y_win32_provider_create(
    const MooInputCore *core, MooInputHandle screen_reader,
    MooInputHandle root, HWND window,
    MooA11yWin32Provider **out_provider);
MooInputResult moo_a11y_win32_provider_message(
    MooA11yWin32Provider *provider, HWND window, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT *out_lresult,
    uint32_t *out_handled);
void moo_a11y_win32_provider_destroy(MooA11yWin32Provider **provider);

#endif
#endif
