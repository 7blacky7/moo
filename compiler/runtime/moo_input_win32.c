#include "moo_input_win32.h"

#ifdef _WIN32

#include <imm.h>
#include <limits.h>
#include <windowsx.h>

uint64_t moo_input_win32_features(void) {
    return MOO_INPUT_FEATURE_POINTER | MOO_INPUT_FEATURE_KEYBOARD |
           MOO_INPUT_FEATURE_IME | MOO_INPUT_FEATURE_SHORTCUTS;
}

static uint64_t next_serial(const MooInputWin32Adapter *adapter) {
    if (!adapter || !adapter->core ||
        adapter->core->accepted_serial >= UINT64_MAX - 1u)
        return 0u;
    return adapter->core->accepted_serial + 1u;
}

MooInputResult moo_input_win32_init(MooInputWin32Adapter *adapter,
    MooInputCore *core, MooInputHandle target) {
    if (!adapter || !core || target == MOO_INPUT_HANDLE_INVALID)
        return MOO_INPUT_INVALID;
    adapter->core = core;
    adapter->target = target;
    adapter->pointer_x = 0;
    adapter->pointer_y = 0;
    adapter->pending_high_surrogate = 0u;
    adapter->tracking_leave = 0u;
    adapter->pending_leave = 0u;
    adapter->releasing_capture = 0u;
    return MOO_INPUT_OK;
}

static uint32_t set1_to_hid(uint32_t scan, uint32_t extended) {
    if (extended) {
        switch (scan) {
        case 0x1cu: return 88u;
        case 0x1du: return 228u;
        case 0x35u: return 84u;
        case 0x38u: return 230u;
        case 0x47u: return 74u;
        case 0x48u: return 82u;
        case 0x49u: return 75u;
        case 0x4bu: return 80u;
        case 0x4du: return 79u;
        case 0x4fu: return 77u;
        case 0x50u: return 81u;
        case 0x51u: return 78u;
        case 0x52u: return 73u;
        case 0x53u: return 76u;
        case 0x5bu: return 227u;
        case 0x5cu: return 231u;
        default: return 0u;
        }
    }
    if (scan >= 0x02u && scan <= 0x0bu)
        return 30u + (scan - 0x02u);
    if (scan >= 0x3bu && scan <= 0x44u)
        return 58u + (scan - 0x3bu);
    switch (scan) {
    case 0x01u: return 41u;
    case 0x0cu: return 45u;
    case 0x0du: return 46u;
    case 0x0eu: return 42u;
    case 0x0fu: return 43u;
    case 0x10u: return 20u;
    case 0x11u: return 26u;
    case 0x12u: return 8u;
    case 0x13u: return 21u;
    case 0x14u: return 23u;
    case 0x15u: return 28u;
    case 0x16u: return 24u;
    case 0x17u: return 12u;
    case 0x18u: return 18u;
    case 0x19u: return 19u;
    case 0x1au: return 47u;
    case 0x1bu: return 48u;
    case 0x1cu: return 40u;
    case 0x1du: return 224u;
    case 0x1eu: return 4u;
    case 0x1fu: return 22u;
    case 0x20u: return 7u;
    case 0x21u: return 9u;
    case 0x22u: return 10u;
    case 0x23u: return 11u;
    case 0x24u: return 13u;
    case 0x25u: return 14u;
    case 0x26u: return 15u;
    case 0x27u: return 51u;
    case 0x28u: return 52u;
    case 0x29u: return 53u;
    case 0x2au: return 225u;
    case 0x2bu: return 49u;
    case 0x2cu: return 29u;
    case 0x2du: return 27u;
    case 0x2eu: return 6u;
    case 0x2fu: return 25u;
    case 0x30u: return 5u;
    case 0x31u: return 17u;
    case 0x32u: return 16u;
    case 0x33u: return 54u;
    case 0x34u: return 55u;
    case 0x35u: return 56u;
    case 0x36u: return 229u;
    case 0x37u: return 85u;
    case 0x38u: return 226u;
    case 0x39u: return 44u;
    case 0x3au: return 57u;
    case 0x45u: return 83u;
    case 0x46u: return 71u;
    case 0x47u: return 95u;
    case 0x48u: return 96u;
    case 0x49u: return 97u;
    case 0x4au: return 86u;
    case 0x4bu: return 92u;
    case 0x4cu: return 93u;
    case 0x4du: return 94u;
    case 0x4eu: return 87u;
    case 0x4fu: return 89u;
    case 0x50u: return 90u;
    case 0x51u: return 91u;
    case 0x52u: return 98u;
    case 0x53u: return 99u;
    case 0x56u: return 100u;
    case 0x57u: return 68u;
    case 0x58u: return 69u;
    default: return 0u;
    }
}

uint32_t moo_input_win32_physical_key(LPARAM key_lparam) {
    uint32_t scan = ((uint32_t)(uintptr_t)key_lparam >> 16u) & 0xffu;
    uint32_t extended = ((uint32_t)(uintptr_t)key_lparam >> 24u) & 1u;
    return set1_to_hid(scan, extended);
}

static MooInputResult utf16_to_utf8(const WCHAR *wide, uint32_t wide_length,
    uint8_t *utf8, uint32_t *out_length) {
    int length;
    if (!wide || !utf8 || !out_length || wide_length > INT_MAX)
        return MOO_INPUT_INVALID;
    if (wide_length == 0u) {
        *out_length = 0u;
        return MOO_INPUT_OK;
    }
    SetLastError(ERROR_SUCCESS);
    length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
        (int)wide_length, (char *)utf8, (int)MOO_INPUT_TEXT_CAPACITY,
        0, 0);
    if (length <= 0)
        return GetLastError() == ERROR_INSUFFICIENT_BUFFER ?
               MOO_INPUT_LIMIT : MOO_INPUT_BAD_UTF8;
    *out_length = (uint32_t)length;
    return MOO_INPUT_OK;
}

static MooInputResult commit_utf16(MooInputWin32Adapter *adapter,
    const WCHAR *wide, uint32_t wide_length, uint64_t timestamp_ns) {
    uint8_t utf8[MOO_INPUT_TEXT_CAPACITY];
    uint32_t length;
    uint64_t serial;
    MooInputResult result = utf16_to_utf8(wide, wide_length, utf8, &length);
    if (result != MOO_INPUT_OK) return result;
    serial = next_serial(adapter);
    if (serial == 0u) return MOO_INPUT_LIMIT;
    return moo_input_text_commit(adapter->core, adapter->core->focus_epoch,
        adapter->core->ime_revision + 1u, utf8, length, serial, timestamp_ns);
}

static MooInputResult ime_composition(MooInputWin32Adapter *adapter,
    HWND window, LPARAM flags, uint64_t timestamp_ns) {
    HIMC context;
    LONG bytes;
    WCHAR wide[MOO_INPUT_TEXT_CAPACITY];
    uint8_t utf8[MOO_INPUT_TEXT_CAPACITY];
    uint32_t length;
    uint32_t selection;
    uint64_t serial;
    MooInputResult result;
    context = ImmGetContext(window);
    if (!context) return MOO_INPUT_UNSUPPORTED;
    if ((flags & GCS_RESULTSTR) != 0) {
        bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR,
                                         wide, sizeof(wide));
        ImmReleaseContext(window, context);
        if (bytes < 0 || (bytes & 1) != 0) return MOO_INPUT_BAD_UTF8;
        if ((size_t)bytes > sizeof(wide)) return MOO_INPUT_LIMIT;
        return commit_utf16(adapter, wide, (uint32_t)bytes / 2u, timestamp_ns);
    }
    if ((flags & GCS_COMPSTR) == 0) {
        ImmReleaseContext(window, context);
        return MOO_INPUT_OK;
    }
    bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, wide, sizeof(wide));
    if (bytes < 0 || (bytes & 1) != 0 ||
        (size_t)bytes > sizeof(wide)) {
        ImmReleaseContext(window, context);
        return bytes < 0 || (bytes & 1) != 0 ? MOO_INPUT_BAD_UTF8 :
                                               MOO_INPUT_LIMIT;
    }
    {
        LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, 0, 0);
        uint8_t prefix_utf8[MOO_INPUT_TEXT_CAPACITY];
        uint32_t prefix_length = 0u;
        uint32_t wide_count = (uint32_t)bytes / 2u;
        if (cursor < 0 || (uint32_t)cursor > wide_count) cursor = 0;
        result = utf16_to_utf8(wide, wide_count, utf8, &length);
        if (result == MOO_INPUT_OK && cursor > 0)
            result = utf16_to_utf8(wide, (uint32_t)cursor,
                                   prefix_utf8, &prefix_length);
        selection = prefix_length;
    }
    ImmReleaseContext(window, context);
    if (result != MOO_INPUT_OK) return result;
    serial = next_serial(adapter);
    if (serial == 0u) return MOO_INPUT_LIMIT;
    return moo_input_text_preedit(adapter->core, adapter->core->focus_epoch,
        adapter->core->ime_revision + 1u, utf8, length,
        selection, selection, serial, timestamp_ns);
}

static MooInputResult char_message(MooInputWin32Adapter *adapter,
    WCHAR code_unit, uint64_t timestamp_ns) {
    WCHAR pair[2];
    if (code_unit >= 0xd800u && code_unit <= 0xdbffu) {
        if (adapter->pending_high_surrogate != 0u) {
            adapter->pending_high_surrogate = code_unit;
            return MOO_INPUT_BAD_UTF8;
        }
        adapter->pending_high_surrogate = code_unit;
        return MOO_INPUT_OK;
    }
    if (code_unit >= 0xdc00u && code_unit <= 0xdfffu) {
        if (adapter->pending_high_surrogate == 0u)
            return MOO_INPUT_BAD_UTF8;
        MooInputResult result;
        pair[0] = adapter->pending_high_surrogate;
        pair[1] = code_unit;
        result = commit_utf16(adapter, pair, 2u, timestamp_ns);
        if (result == MOO_INPUT_OK)
            adapter->pending_high_surrogate = 0u;
        return result;
    }
    if (adapter->pending_high_surrogate != 0u) {
        MooInputResult result;
        pair[0] = 0xfffdu;
        pair[1] = code_unit;
        result = commit_utf16(adapter, pair, 2u, timestamp_ns);
        if (result == MOO_INPUT_OK)
            adapter->pending_high_surrogate = 0u;
        return result;
    }
    pair[0] = code_unit;
    return commit_utf16(adapter, pair, 1u, timestamp_ns);
}

MooInputResult moo_input_win32_message(MooInputWin32Adapter *adapter,
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    uint64_t timestamp_ns, uint32_t *out_handled) {
    uint64_t serial;
    if (!adapter || !adapter->core || !out_handled)
        return MOO_INPUT_INVALID;
    *out_handled = 0u;
    serial = next_serial(adapter);
    switch (message) {
    case WM_SETFOCUS:
        if (serial == 0u) return MOO_INPUT_LIMIT;
        *out_handled = 1u;
        return moo_input_set_focus(adapter->core, adapter->target, 1u,
                                   serial, timestamp_ns);
    case WM_KILLFOCUS:
        if (serial == 0u) return MOO_INPUT_LIMIT;
        adapter->pending_high_surrogate = 0u;
        *out_handled = 1u;
        return moo_input_set_focus(adapter->core, MOO_INPUT_HANDLE_INVALID,
                                   2u, serial, timestamp_ns);
    case WM_MOUSEMOVE: {
        int32_t x = GET_X_LPARAM(lparam);
        int32_t y = GET_Y_LPARAM(lparam);
        int32_t dx = x - adapter->pointer_x;
        int32_t dy = y - adapter->pointer_y;
        MooInputResult result;
        uint32_t began_tracking = 0u;
        if (serial == 0u) return MOO_INPUT_LIMIT;
        if (window && !adapter->tracking_leave) {
            TRACKMOUSEEVENT tracking;
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = window;
            tracking.dwHoverTime = 0u;
            if (!TrackMouseEvent(&tracking)) return MOO_INPUT_BAD_STATE;
            adapter->tracking_leave = 1u;
            began_tracking = 1u;
        }
        result = moo_input_pointer_motion(adapter->core, adapter->target,
                                          x, y, dx, dy, serial, timestamp_ns);
        if (result == MOO_INPUT_OK) {
            adapter->pointer_x = x;
            adapter->pointer_y = y;
            adapter->pending_leave = 0u;
        } else if (began_tracking) {
            TRACKMOUSEEVENT cancel_tracking;
            cancel_tracking.cbSize = sizeof(cancel_tracking);
            cancel_tracking.dwFlags = TME_LEAVE | TME_CANCEL;
            cancel_tracking.hwndTrack = window;
            cancel_tracking.dwHoverTime = 0u;
            (void)TrackMouseEvent(&cancel_tracking);
            adapter->tracking_leave = 0u;
        }
        return result;
    }
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP: {
        uint32_t button = (message == WM_LBUTTONDOWN ||
                           message == WM_LBUTTONUP) ? 0u :
                          (message == WM_MBUTTONDOWN ||
                           message == WM_MBUTTONUP) ? 1u : 2u;
        uint32_t state = (message == WM_LBUTTONDOWN ||
                          message == WM_MBUTTONDOWN ||
                          message == WM_RBUTTONDOWN)
                       ? MOO_INPUT_PRESSED : MOO_INPUT_RELEASED;
        if (serial == 0u) return MOO_INPUT_LIMIT;
        HWND previous_capture = 0;
        MooInputResult result;
        if (adapter->core->pointer_target == MOO_INPUT_HANDLE_INVALID)
            return MOO_INPUT_BAD_STATE;
        if (window && state == MOO_INPUT_PRESSED) {
            previous_capture = SetCapture(window);
            if (GetCapture() != window) return MOO_INPUT_BAD_STATE;
        }
        result = moo_input_pointer_button(adapter->core, button, state,
            GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam),
            serial, timestamp_ns);
        if (result != MOO_INPUT_OK && window && state == MOO_INPUT_PRESSED) {
            if (previous_capture) (void)SetCapture(previous_capture);
            else (void)ReleaseCapture();
        }
        if (result == MOO_INPUT_OK && window && state == MOO_INPUT_RELEASED &&
            adapter->core->pointer_buttons == 0u) {
            adapter->releasing_capture = 1u;
            (void)ReleaseCapture();
            adapter->releasing_capture = 0u;
            if (adapter->pending_leave) {
                serial = next_serial(adapter);
                if (serial == 0u) return MOO_INPUT_LIMIT;
                result = moo_input_pointer_cancel(adapter->core, serial,
                                                  timestamp_ns);
                if (result == MOO_INPUT_OK) adapter->pending_leave = 0u;
            }
        }
        if (result == MOO_INPUT_OK) *out_handled = 1u;
        return result;
    }
    case WM_MOUSELEAVE:
        adapter->tracking_leave = 0u;
        *out_handled = 1u;
        if (adapter->core->pointer_buttons != 0u) {
            adapter->pending_leave = 1u;
            return MOO_INPUT_OK;
        }
        if (serial == 0u) return MOO_INPUT_LIMIT;
        return moo_input_pointer_cancel(adapter->core, serial, timestamp_ns);
    case WM_CAPTURECHANGED:
        if ((HWND)lparam == window) return MOO_INPUT_OK;
        *out_handled = 1u;
        if (adapter->releasing_capture) return MOO_INPUT_OK;
        if (serial == 0u) return MOO_INPUT_LIMIT;
        adapter->pending_leave = 0u;
        return moo_input_pointer_cancel(adapter->core, serial, timestamp_ns);
    case WM_CANCELMODE:
    case WM_NCDESTROY:
        adapter->tracking_leave = 0u;
        adapter->pending_leave = 0u;
        adapter->pending_high_surrogate = 0u;
        if (serial == 0u) return MOO_INPUT_LIMIT;
        *out_handled = 1u;
        return moo_input_pointer_cancel(adapter->core, serial, timestamp_ns);
    case WM_MOUSEWHEEL:
        if (serial == 0u) return MOO_INPUT_LIMIT;
        *out_handled = 1u;
        return moo_input_pointer_axis(adapter->core, 0,
            -(int32_t)GET_WHEEL_DELTA_WPARAM(wparam),
            serial, timestamp_ns);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        uint32_t physical = moo_input_win32_physical_key(lparam);
        uint32_t state = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
                       ? MOO_INPUT_PRESSED : MOO_INPUT_RELEASED;
        uint32_t repeat = state == MOO_INPUT_PRESSED &&
                          (((uint32_t)(uintptr_t)lparam >> 30u) & 1u);
        if (physical == 0u) return MOO_INPUT_UNSUPPORTED;
        if (serial == 0u) return MOO_INPUT_LIMIT;
        return moo_input_key(adapter->core, physical,
                             MOO_INPUT_LOGICAL_HID_BASE + physical,
                             state, repeat, serial, timestamp_ns);
    }
    case WM_CHAR:
        *out_handled = 1u;
        return char_message(adapter, (WCHAR)wparam, timestamp_ns);
    case WM_IME_COMPOSITION:
        *out_handled = 1u;
        return ime_composition(adapter, window, lparam, timestamp_ns);
    case WM_IME_ENDCOMPOSITION:
        adapter->pending_high_surrogate = 0u;
        if (!adapter->core->ime_active) return MOO_INPUT_OK;
        if (serial == 0u) return MOO_INPUT_LIMIT;
        *out_handled = 1u;
        return moo_input_text_cancel(adapter->core,
            adapter->core->focus_epoch, adapter->core->ime_revision + 1u,
            serial, timestamp_ns);
    default:
        return MOO_INPUT_UNSUPPORTED;
    }
}

#endif
