#include "moo_ui_host_parity.h"
#include "moo_ui_host_parity_instrumentation.h"
#include "moo_ui_host_parity_win32_dwrite.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <imm.h>
#include <oleacc.h>

typedef HRESULT (WINAPI *MooGetDpiForMonitorFn)(
    HMONITOR monitor, int dpi_type, UINT *dpi_x, UINT *dpi_y);

typedef struct {
    MooGetDpiForMonitorFn get_dpi;
    uint32_t count;
    uint32_t min_dpi;
    uint32_t max_dpi;
    int32_t native_error;
} MooParityWin32DpiContext;

static MooGetDpiForMonitorFn moo_parity_load_get_dpi(HMODULE module) {
    union {
        FARPROC raw;
        MooGetDpiForMonitorFn typed;
    } function;
    function.raw = GetProcAddress(module, "GetDpiForMonitor");
    return function.typed;
}

static BOOL CALLBACK moo_parity_enum_monitor(
    HMONITOR monitor, HDC device, LPRECT rectangle, LPARAM data) {
    MooParityWin32DpiContext *context =
        (MooParityWin32DpiContext *)(uintptr_t)data;
    UINT dpi_x = 0u;
    UINT dpi_y = 0u;
    HRESULT result;
    (void)device;
    (void)rectangle;
    if (context == 0 || context->get_dpi == 0) return FALSE;
    result = context->get_dpi(monitor, 0, &dpi_x, &dpi_y);
    if (FAILED(result) || dpi_x != dpi_y ||
        dpi_x < (UINT)MOO_UI_HOST_PARITY_MIN_DPI ||
        dpi_x > (UINT)MOO_UI_HOST_PARITY_MAX_DPI) {
        context->native_error = (int32_t)result;
        return FALSE;
    }
    if (context->count == 0u || dpi_x < context->min_dpi)
        context->min_dpi = dpi_x;
    if (context->count == 0u || dpi_x > context->max_dpi)
        context->max_dpi = dpi_x;
    ++context->count;
    return TRUE;
}

static MooUiHostParityResult moo_parity_win32_probe_dpi(
    MooUiHostParityMeasurement *measurement) {
    HMODULE shcore;
    MooParityWin32DpiContext context;
    BOOL enumerated;
    if (measurement == 0) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    context.get_dpi = 0;
    context.count = 0u;
    context.min_dpi = 0u;
    context.max_dpi = 0u;
    context.native_error = 0;
    shcore = LoadLibraryW(L"shcore.dll");
    if (shcore == 0) {
        measurement->native_error = (int32_t)GetLastError();
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    }
    context.get_dpi = moo_parity_load_get_dpi(shcore);
    if (context.get_dpi == 0) {
        FreeLibrary(shcore);
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    }
    enumerated = EnumDisplayMonitors(
        0, 0, moo_parity_enum_monitor, (LPARAM)(uintptr_t)&context);
    FreeLibrary(shcore);
    if (enumerated == FALSE || context.count == 0u) {
        measurement->native_error = context.native_error != 0
            ? context.native_error : (int32_t)GetLastError();
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    measurement->evidence = (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS;
    measurement->sample_count = context.count;
    measurement->value_a = (uint64_t)context.count;
    measurement->value_b = (uint64_t)context.min_dpi;
    measurement->value_c = (uint64_t)context.max_dpi;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

typedef struct {
    WNDPROC original_proc;
    uint32_t key_down;
    uint32_t key_character;
    uint32_t key_up;
    uint32_t pointer_move;
    uint32_t pointer_down;
    uint32_t pointer_up;
} MooParityWin32InputContext;

static LRESULT CALLBACK moo_parity_input_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    MooParityWin32InputContext *context =
        (MooParityWin32InputContext *)(uintptr_t)
            GetWindowLongPtrW(window, GWLP_USERDATA);
    if (context != 0) {
        switch (message) {
        case WM_KEYDOWN: ++context->key_down; return 0;
        case WM_CHAR: ++context->key_character; return 0;
        case WM_KEYUP: ++context->key_up; return 0;
        case WM_MOUSEMOVE: ++context->pointer_move; return 0;
        case WM_LBUTTONDOWN: ++context->pointer_down; return 0;
        case WM_LBUTTONUP: ++context->pointer_up; return 0;
        default: break;
        }
    }
    if (context != 0 && context->original_proc != 0)
        return CallWindowProcW(
            context->original_proc, window, message, wparam, lparam);
    return DefWindowProcW(window, message, wparam, lparam);
}

static void moo_parity_input_cleanup(HWND window,
    MooParityWin32InputContext *context, HIMC private_context,
    HIMC previous_context) {
    union {
        WNDPROC typed;
        LONG_PTR raw;
    } original;
    if (private_context != 0) {
        if (window != 0)
            (void)ImmAssociateContext(window, previous_context);
        ImmDestroyContext(private_context);
    }
    if (window == 0) return;
    if (context != 0 && context->original_proc != 0) {
        original.typed = context->original_proc;
        (void)SetWindowLongPtrW(window, GWLP_WNDPROC, original.raw);
        context->original_proc = 0;
    }
    (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    DestroyWindow(window);
}

static MooUiHostParityResult moo_parity_win32_probe_input_ime(
    MooUiHostParityMeasurement *measurement) {
    WCHAR composition[] = L"Moo";
    MooParityWin32InputContext context;
    union {
        WNDPROC typed;
        LONG_PTR raw;
    } replacement;
    union {
        WNDPROC typed;
        LONG_PTR raw;
    } original;
    HWND window = 0;
    HIMC private_context = 0;
    HIMC previous_context = 0;
    HKL layout;
    MSG message;
    WCHAR buffer[8];
    LONG bytes;
    uint32_t dispatched = 0u;
    uint32_t input_sequences = 0u;
    int32_t native_error = 0;
    if (measurement == 0) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    ZeroMemory(&context, sizeof(context));
    window = CreateWindowExW(0u, L"EDIT", L"", WS_OVERLAPPED,
        0, 0, 1, 1, 0, 0, GetModuleHandleW(0), 0);
    if (window == 0) {
        native_error = (int32_t)GetLastError();
        goto system_error;
    }
    (void)SetWindowLongPtrW(window, GWLP_USERDATA,
        (LONG_PTR)(uintptr_t)&context);
    if ((MooParityWin32InputContext *)(uintptr_t)
            GetWindowLongPtrW(window, GWLP_USERDATA) != &context) {
        native_error = (int32_t)ERROR_INVALID_DATA;
        goto system_error;
    }
    replacement.typed = moo_parity_input_window_proc;
    SetLastError(ERROR_SUCCESS);
    original.raw = SetWindowLongPtrW(window, GWLP_WNDPROC, replacement.raw);
    if (original.raw == 0) {
        native_error = (int32_t)GetLastError();
        goto system_error;
    }
    context.original_proc = original.typed;
    if (PostMessageW(window, WM_KEYDOWN, (WPARAM)'A',
            (LPARAM)UINT32_C(0x001e0001)) == FALSE ||
        PostMessageW(window, WM_CHAR, (WPARAM)L'a',
            (LPARAM)UINT32_C(0x001e0001)) == FALSE ||
        PostMessageW(window, WM_KEYUP, (WPARAM)'A',
            (LPARAM)UINT32_C(0xc01e0001)) == FALSE ||
        PostMessageW(window, WM_MOUSEMOVE, 0u,
            (LPARAM)UINT32_C(0x00010001)) == FALSE ||
        PostMessageW(window, WM_LBUTTONDOWN, (WPARAM)MK_LBUTTON,
            (LPARAM)UINT32_C(0x00010001)) == FALSE ||
        PostMessageW(window, WM_LBUTTONUP, 0u,
            (LPARAM)UINT32_C(0x00010001)) == FALSE) {
        native_error = (int32_t)GetLastError();
        goto system_error;
    }
    while (PeekMessageW(&message, window, 0u, 0u, PM_REMOVE) != FALSE) {
        DispatchMessageW(&message);
        ++dispatched;
        if (dispatched > 16u) {
            native_error = (int32_t)ERROR_INVALID_DATA;
            goto system_error;
        }
    }
    if (dispatched != 6u || context.key_down != 1u ||
        context.key_character != 1u || context.key_up != 1u ||
        context.pointer_move != 1u || context.pointer_down != 1u ||
        context.pointer_up != 1u) {
        native_error = (int32_t)ERROR_INVALID_DATA;
        goto system_error;
    }
    input_sequences = 2u;
    layout = GetKeyboardLayout(0u);
    if (ImmIsIME(layout) == FALSE) goto unsupported;
    private_context = ImmCreateContext();
    if (private_context == 0) goto unsupported;
    previous_context = ImmAssociateContext(window, private_context);
    if (ImmSetCompositionStringW(private_context, SCS_SETSTR,
            composition, (DWORD)(sizeof(composition) - sizeof(composition[0])), 0, 0u) == FALSE)
        goto unsupported;
    ZeroMemory(buffer, sizeof(buffer));
    bytes = ImmGetCompositionStringW(
        private_context, GCS_COMPSTR, buffer, (DWORD)sizeof(buffer));
    if (bytes != (LONG)(3u * sizeof(WCHAR)) ||
        buffer[0] != L'M' || buffer[1] != L'o' || buffer[2] != L'o')
        goto unsupported;
    if (ImmNotifyIME(private_context, NI_COMPOSITIONSTR,
            CPS_COMPLETE, 0u) == FALSE)
        goto unsupported;
    while (PeekMessageW(&message, window, 0u, 0u, PM_REMOVE) != FALSE)
        DispatchMessageW(&message);
    ZeroMemory(buffer, sizeof(buffer));
    bytes = ImmGetCompositionStringW(
        private_context, GCS_RESULTSTR, buffer, (DWORD)sizeof(buffer));
    if (bytes != (LONG)(3u * sizeof(WCHAR)) ||
        buffer[0] != L'M' || buffer[1] != L'o' || buffer[2] != L'o')
        goto unsupported;
    moo_parity_input_cleanup(
        window, &context, private_context, previous_context);
    measurement->evidence = (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS;
    measurement->sample_count = 4u;
    measurement->value_a = (uint64_t)input_sequences;
    measurement->value_b = UINT64_C(1);
    measurement->value_c = UINT64_C(1);
    return MOO_UI_HOST_PARITY_RESULT_OK;

unsupported:
    moo_parity_input_cleanup(
        window, &context, private_context, previous_context);
    measurement->evidence =
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED;
    measurement->sample_count = 0u;
    measurement->value_a = UINT64_C(0);
    measurement->value_b = UINT64_C(0);
    measurement->value_c = UINT64_C(0);
    measurement->native_error = 0;
    return MOO_UI_HOST_PARITY_RESULT_OK;

system_error:
    moo_parity_input_cleanup(
        window, &context, private_context, previous_context);
    measurement->native_error = native_error != 0
        ? native_error : (int32_t)ERROR_GEN_FAILURE;
    return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
}

static MooUiHostParityResult moo_parity_win32_probe_accessibility(
    MooUiHostParityMeasurement *measurement) {
    HWND window = 0;
    IAccessible *accessible = 0;
    VARIANT child;
    VARIANT role;
    BSTR name = 0;
    HRESULT initialized;
    HRESULT result;
    HRESULT role_result;
    HRESULT name_result;
    HRESULT action_result;
    uint32_t co_uninitialize = 0u;
    if (measurement == 0) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    initialized = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initialized)) {
        co_uninitialize = 1u;
    } else if (initialized != RPC_E_CHANGED_MODE) {
        measurement->native_error = (int32_t)initialized;
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    window = CreateWindowExW(0u, L"BUTTON", L"Moo parity accessible",
        WS_OVERLAPPED, 0, 0, 120, 32, 0, 0, GetModuleHandleW(0), 0);
    if (window == 0) {
        measurement->native_error = (int32_t)GetLastError();
        if (co_uninitialize != 0u) CoUninitialize();
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    result = AccessibleObjectFromWindow(
        window, (DWORD)OBJID_CLIENT, &IID_IAccessible,
        (void **)&accessible);
    if (FAILED(result) || accessible == 0) goto unavailable;
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = CHILDID_SELF;
    VariantInit(&role);
    role_result = IAccessible_get_accRole(accessible, child, &role);
    name_result = IAccessible_get_accName(accessible, child, &name);
    action_result = IAccessible_accDoDefaultAction(accessible, child);
    if (FAILED(role_result) || role.vt != VT_I4 ||
        role.lVal != ROLE_SYSTEM_PUSHBUTTON ||
        FAILED(name_result) || name == 0 || SysStringLen(name) == 0u ||
        FAILED(action_result)) {
        result = FAILED(role_result) ? role_result :
            (FAILED(name_result) ? name_result : action_result);
        VariantClear(&role);
        SysFreeString(name);
        IAccessible_Release(accessible);
        DestroyWindow(window);
        if (co_uninitialize != 0u) CoUninitialize();
        measurement->native_error = (int32_t)result;
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    }
    VariantClear(&role);
    SysFreeString(name);
    IAccessible_Release(accessible);
    DestroyWindow(window);
    if (co_uninitialize != 0u) CoUninitialize();
    measurement->evidence = (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS;
    measurement->sample_count = 3u;
    measurement->value_a = UINT64_C(1);
    measurement->value_b = UINT64_C(1);
    measurement->value_c = UINT64_C(1);
    return MOO_UI_HOST_PARITY_RESULT_OK;

unavailable:
    if (accessible != 0) IAccessible_Release(accessible);
    DestroyWindow(window);
    if (co_uninitialize != 0u) CoUninitialize();
    measurement->native_error = (int32_t)result;
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
}

static MooUiHostParityResult moo_parity_win32_probe(
    void *user, uint32_t domain, MooUiHostParityMeasurement *measurement) {
    if (measurement == 0) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    if (domain == 0u)
        return moo_ui_host_parity_win32_measure_typography(measurement);
    if (domain == 1u)
        return moo_parity_win32_probe_input_ime(measurement);
    if (domain == 2u)
        return moo_parity_win32_probe_accessibility(measurement);
    if (domain == 3u) return moo_parity_win32_probe_dpi(measurement);
    if (domain >= 4u && domain <= 7u && user != 0)
        return moo_ui_host_parity_instrumentation_probe(
            (const MooUiHostParityInstrumentation *)user,
            domain, measurement);
    /* Domains without a real measurement remain explicitly unsupported. */
    measurement->evidence =
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED;
    measurement->sample_count = 0u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}
#endif

MooUiHostParityResult moo_ui_host_parity_init_win32_instrumented(
    MooUiHostParityState *state,
    struct MooUiHostParityInstrumentation *instrumentation) {
#if defined(_WIN32)
    MooUiHostParityOps ops;
    ops.probe = moo_parity_win32_probe;
    ops.reset = 0;
    return moo_ui_host_parity_init(state, &ops, instrumentation);
#else
    (void)state;
    (void)instrumentation;
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#endif
}

MooUiHostParityResult moo_ui_host_parity_init_win32(
    MooUiHostParityState *state) {
    return moo_ui_host_parity_init_win32_instrumented(state, 0);
}
