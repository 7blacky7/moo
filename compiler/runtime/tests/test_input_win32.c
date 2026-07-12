#include "../moo_input_win32.h"

#ifdef _WIN32

#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL:%d:%s\n", __LINE__, (m)); return 1; } } while (0)

static uint32_t drain(MooInputCore *core, MooInputHandle client,
                      MooInputEvent *last) {
    MooInputEvent event;
    uint32_t count = 0u;
    while (moo_input_next_event(core, client, &event) == MOO_INPUT_OK) {
        if (last) *last = event;
        count++;
    }
    return count;
}

static LPARAM key_lp(uint32_t scan, uint32_t extended,
                     uint32_t previous, uint32_t released) {
    uint32_t value = (scan & 0xffu) << 16u;
    value |= (extended & 1u) << 24u;
    value |= (previous & 1u) << 30u;
    value |= (released & 1u) << 31u;
    return (LPARAM)(uintptr_t)value;
}

int main(void) {
    MooInputCore core;
    MooInputClientSlot clients[2];
    MooInputTargetSlot targets[2];
    MooInputEventSlot events[32];
    MooA11yNodeSlot nodes[2];
    MooInputConfig config;
    MooInputHandle client;
    MooInputHandle target;
    MooInputWin32Adapter adapter;
    MooInputEvent event;
    uint32_t handled;
    uint64_t serial_before;
    HWND window;
    WNDCLASSW window_class;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(0);
    window_class.lpszClassName = L"MooInputO4Test";
    CHECK(RegisterClassW(&window_class) != 0 ||
          GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "register window");
    window = CreateWindowExW(0, window_class.lpszClassName, L"Moo O4",
        WS_OVERLAPPEDWINDOW, 0, 0, 160, 100, 0, 0,
        window_class.hInstance, 0);
    CHECK(window != 0, "create native window");

    config.max_events_per_client = 24u;
    config.max_a11y_depth = 2u;
    config.features = moo_input_win32_features();
    CHECK((config.features & (MOO_INPUT_FEATURE_TOUCH |
          MOO_INPUT_FEATURE_STYLUS | MOO_INPUT_FEATURE_ACCESSIBILITY)) == 0u,
          "unsupported capabilities stay off");
    CHECK(moo_input_init(&core, &config, clients, 2u, targets, 2u,
          events, 32u, nodes, 2u) == MOO_INPUT_OK, "core init");
    CHECK(moo_input_client_create(&core, 0u, &client) == MOO_INPUT_OK,
          "client");
    CHECK(moo_input_target_create_trusted(&core, client, 1u,
          MOO_INPUT_TEXT_NORMAL, &target) == MOO_INPUT_OK, "target");
    CHECK(moo_input_win32_init(&adapter, &core, target) == MOO_INPUT_OK,
          "adapter init");

    CHECK(moo_input_win32_physical_key(key_lp(0x1eu, 0u, 0u, 0u)) == 4u,
          "set1 A to HID A");
    CHECK(moo_input_win32_physical_key(key_lp(0x4bu, 1u, 0u, 0u)) == 80u,
          "extended left to HID left");
    CHECK(moo_input_win32_physical_key(key_lp(0x46u, 0u, 0u, 0u)) == 71u,
          "scroll lock HID");
    CHECK(moo_input_win32_physical_key(key_lp(0x47u, 0u, 0u, 0u)) == 95u,
          "keypad7 HID");
    CHECK(moo_input_win32_physical_key(key_lp(0x57u, 0u, 0u, 0u)) == 68u &&
          moo_input_win32_physical_key(key_lp(0x58u, 0u, 0u, 0u)) == 69u,
          "F11 F12 HID");

    CHECK(moo_input_win32_message(&adapter, window, WM_SETFOCUS, 0, 0,
          1u, &handled) == MOO_INPUT_OK && handled, "set focus");
    CHECK(drain(&core, client, &event) == 1u &&
          event.type == MOO_INPUT_EVENT_FOCUS, "focus event");

    CHECK(moo_input_win32_message(&adapter, window, WM_MOUSEMOVE, 0,
          MAKELPARAM(10, 20), 2u, &handled) == MOO_INPUT_OK, "mouse move");
    CHECK(drain(&core, client, &event) == 2u &&
          event.type == MOO_INPUT_EVENT_POINTER_MOTION, "enter and motion");
    CHECK(moo_input_win32_message(&adapter, window, WM_LBUTTONDOWN, 0,
          MAKELPARAM(10, 20), 3u, &handled) == MOO_INPUT_OK && handled,
          "button down");
    CHECK(moo_input_win32_message(&adapter, window, WM_LBUTTONUP, 0,
          MAKELPARAM(10, 20), 4u, &handled) == MOO_INPUT_OK && handled,
          "button up");
    CHECK(drain(&core, client, 0) == 2u, "button events");
    CHECK(moo_input_win32_message(&adapter, window, WM_LBUTTONDOWN, 0,
          MAKELPARAM(10, 20), 4u, &handled) == MOO_INPUT_OK,
          "capture cancel seed");
    CHECK(moo_input_win32_message(&adapter, window, WM_CANCELMODE, 0, 0,
          4u, &handled) == MOO_INPUT_OK, "cancel mode");
    CHECK(drain(&core, client, &event) == 3u &&
          event.type == MOO_INPUT_EVENT_POINTER_LEAVE &&
          (event.flags & MOO_INPUT_EVENT_SYNTHETIC) != 0u,
          "cancel mode release leave");

    CHECK(moo_input_win32_message(&adapter, window, WM_KEYDOWN, 'A',
          key_lp(0x1eu, 0u, 0u, 0u), 5u, &handled) == MOO_INPUT_OK,
          "key down");
    CHECK(moo_input_win32_message(&adapter, window, WM_KEYDOWN, 'A',
          key_lp(0x1eu, 0u, 1u, 0u), 6u, &handled) == MOO_INPUT_OK,
          "key repeat");
    CHECK(moo_input_win32_message(&adapter, window, WM_KEYUP, 'A',
          key_lp(0x1eu, 0u, 1u, 1u), 7u, &handled) == MOO_INPUT_OK,
          "key up");
    CHECK(drain(&core, client, &event) == 3u &&
          event.type == MOO_INPUT_EVENT_KEY &&
          event.data.key.state == MOO_INPUT_RELEASED, "key events");

    CHECK(moo_input_win32_message(&adapter, window, WM_CHAR, 0x00e4u, 0,
          8u, &handled) == MOO_INPUT_OK && handled, "bmp char");
    CHECK(drain(&core, client, &event) == 1u &&
          event.type == MOO_INPUT_EVENT_TEXT_COMMIT &&
          event.data.text.text.length == 2u &&
          event.data.text.text.bytes[0] == 0xc3u &&
          event.data.text.text.bytes[1] == 0xa4u, "utf8 bmp commit");

    serial_before = core.accepted_serial;
    CHECK(moo_input_win32_message(&adapter, window, WM_CHAR, 0xd83du, 0,
          9u, &handled) == MOO_INPUT_OK, "high surrogate pending");
    CHECK(core.accepted_serial == serial_before &&
          drain(&core, client, 0) == 0u, "pending surrogate no event");
    CHECK(moo_input_win32_message(&adapter, window, WM_CHAR, 0xde00u, 0,
          10u, &handled) == MOO_INPUT_OK, "low surrogate commit");
    CHECK(drain(&core, client, &event) == 1u &&
          event.data.text.text.length == 4u &&
          event.data.text.text.bytes[0] == 0xf0u, "emoji utf8 commit");

    serial_before = core.accepted_serial;
    CHECK(moo_input_win32_message(&adapter, window, WM_CHAR, 0xde00u, 0,
          11u, &handled) == MOO_INPUT_BAD_UTF8, "orphan low rejected");
    CHECK(core.accepted_serial == serial_before, "bad char no serial consume");
    CHECK(moo_input_win32_message(&adapter, window, WM_CHAR, 0xd83du, 0,
          11u, &handled) == MOO_INPUT_OK, "second pair high1");
    CHECK(moo_input_win32_message(&adapter, window, WM_CHAR, 0xd83du, 0,
          11u, &handled) == MOO_INPUT_BAD_UTF8, "double high rejected");
    CHECK(adapter.pending_high_surrogate == 0xd83du,
          "new high remains retryable");
    CHECK(moo_input_win32_message(&adapter, window, WM_CHAR, 0xde00u, 0,
          11u, &handled) == MOO_INPUT_OK, "replacement pair committed");
    drain(&core, client, 0);

    CHECK(moo_input_text_preedit(&core, core.focus_epoch,
          core.ime_revision + 1u, (const uint8_t *)"x", 1u, 0u, 1u,
          core.accepted_serial + 1u, 12u) == MOO_INPUT_OK, "seed preedit");
    drain(&core, client, 0);
    CHECK(moo_input_win32_message(&adapter, window, WM_IME_ENDCOMPOSITION,
          0, 0, 13u, &handled) == MOO_INPUT_OK && handled, "ime end cancel");
    CHECK(drain(&core, client, &event) == 1u &&
          event.type == MOO_INPUT_EVENT_TEXT_CANCEL, "ime cancel event");

    CHECK(moo_input_win32_message(&adapter, window, WM_KILLFOCUS, 0, 0,
          14u, &handled) == MOO_INPUT_OK && handled, "kill focus");
    CHECK(core.focus == MOO_INPUT_HANDLE_INVALID, "focus cleared");
    CHECK(moo_input_win32_message(&adapter, window, WM_NCDESTROY, 0, 0,
          15u, &handled) == MOO_INPUT_OK, "native destroy cleanup");
    CHECK(DestroyWindow(window) != 0, "destroy native window");
    puts("P016-O4-WIN32-ADAPTER-OK");
    return 0;
}

#else
int main(void) { return 77; }
#endif
