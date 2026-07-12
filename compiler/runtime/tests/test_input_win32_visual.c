#include "../moo_input_win32.h"

#ifdef _WIN32

#include <windowsx.h>

static MooInputCore g_core;
static MooInputClientSlot g_clients[2];
static MooInputTargetSlot g_targets[2];
static MooInputEventSlot g_events[32];
static MooA11yNodeSlot g_nodes[2];
static MooInputWin32Adapter g_adapter;
static MooInputHandle g_client;
static uint32_t g_ready;
static uint32_t g_passed;
static uint32_t g_event_count;
static WCHAR g_detail[160];

static uint32_t drain_events(void) {
    MooInputEvent event;
    uint32_t count = 0u;
    while (moo_input_next_event(&g_core, g_client, &event) == MOO_INPUT_OK)
        count++;
    return count;
}

static LRESULT CALLBACK visual_wndproc(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam) {
    if (g_ready) {
        uint32_t handled = 0u;
        MooInputResult result = moo_input_win32_message(
            &g_adapter, window, message, wparam, lparam,
            (uint64_t)GetTickCount64() * UINT64_C(1000000), &handled);
        if (handled) {
            if (result != MOO_INPUT_OK) {
                g_passed = 0u;
                wsprintfW(g_detail, L"Adapter error %u on message 0x%04x",
                          (unsigned)result, (unsigned)message);
                InvalidateRect(window, 0, TRUE);
            }
            return 0;
        }
    }
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT client;
        HDC dc = BeginPaint(window, &paint);
        HBRUSH background = CreateSolidBrush(RGB(18, 24, 38));
        HBRUSH badge = CreateSolidBrush(g_passed ? RGB(28, 160, 92)
                                                 : RGB(190, 55, 55));
        HFONT title_font = CreateFontW(30, 0, 0, 0, FW_BOLD, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT body_font = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        GetClientRect(window, &client);
        FillRect(dc, &client, background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(238, 242, 255));
        SelectObject(dc, title_font);
        TextOutW(dc, 32, 28, L"Moo Native Input", 16);
        SelectObject(dc, body_font);
        SetTextColor(dc, RGB(158, 174, 205));
        TextOutW(dc, 34, 72, L"Win32 VM visual integration gate", 32);
        SetRect(&client, 32, 116, 610, 178);
        FillRect(dc, &client, badge);
        SetTextColor(dc, RGB(255, 255, 255));
        SelectObject(dc, title_font);
        TextOutW(dc, 52, 130, g_passed ? L"PASS" : L"FAIL",
                 g_passed ? 4 : 4);
        SelectObject(dc, body_font);
        SetTextColor(dc, RGB(210, 221, 242));
        TextOutW(dc, 34, 210, g_detail, lstrlenW(g_detail));
        TextOutW(dc, 34, 252,
            L"Real HWND + WndProc | pointer capture | UTF-16 | event queue",
            62);
        SetTextColor(dc, RGB(116, 205, 255));
        TextOutW(dc, 34, 300,
            L"Toolkit-free: no GTK, no browser, native Windows messages",
            57);
        DeleteObject(title_font);
        DeleteObject(body_font);
        DeleteObject(badge);
        DeleteObject(background);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_TIMER:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command) {
    MooInputConfig config;
    MooInputHandle target;
    WNDCLASSW window_class;
    HWND window;
    MSG message;
    MooInputResult result;
    uint32_t before_release;
    (void)previous;
    (void)command_line;

    ZeroMemory(&g_core, sizeof(g_core));
    ZeroMemory(&window_class, sizeof(window_class));
    config.max_events_per_client = 24u;
    config.max_a11y_depth = 2u;
    config.features = moo_input_win32_features();
    result = moo_input_init(&g_core, &config, g_clients, 2u, g_targets, 2u,
                            g_events, 32u, g_nodes, 2u);
    if (result != MOO_INPUT_OK) return 10;
    if (moo_input_client_create(&g_core, 0u, &g_client) != MOO_INPUT_OK)
        return 11;
    if (moo_input_target_create_trusted(&g_core, g_client, 1u,
            MOO_INPUT_TEXT_NORMAL, &target) != MOO_INPUT_OK)
        return 12;
    if (moo_input_win32_init(&g_adapter, &g_core, target) != MOO_INPUT_OK)
        return 13;

    window_class.lpfnWndProc = visual_wndproc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(0, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = L"MooInputVisualGate";
    if (!RegisterClassW(&window_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 14;
    window = CreateWindowExW(0, window_class.lpszClassName,
        L"Moo Native UI - Windows VM Visual Test", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 430, 0, 0, instance, 0);
    if (!window) return 15;

    g_passed = 1u;
    g_ready = 1u;
    ShowWindow(window, show_command);
    UpdateWindow(window);
    SetFocus(window);
    SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(96, 144));
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(96, 144));
    before_release = g_core.pointer_buttons;
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(96, 144));
    SendMessageW(window, WM_CHAR, 0x00e4u, 0);
    g_event_count = drain_events();

    if (before_release == 0u || g_core.pointer_buttons != 0u ||
        g_core.pointer_target == MOO_INPUT_HANDLE_INVALID ||
        g_adapter.releasing_capture != 0u || g_event_count < 6u) {
        g_passed = 0u;
        wsprintfW(g_detail,
            L"events=%u buttons-before=%u buttons-after=%u target=%s",
            (unsigned)g_event_count, (unsigned)before_release,
            (unsigned)g_core.pointer_buttons,
            g_core.pointer_target == MOO_INPUT_HANDLE_INVALID
                ? L"invalid" : L"live");
    } else {
        wsprintfW(g_detail,
            L"%u native input events verified; capture released cleanly",
            (unsigned)g_event_count);
    }
    InvalidateRect(window, 0, TRUE);
    UpdateWindow(window);
    SetTimer(window, 1u, 120000u, 0);

    while (GetMessageW(&message, 0, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return g_passed ? 0 : 1;
}

#else
int main(void) { return 77; }
#endif
