#define COBJMACROS
#include "../moo_a11y_win32.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
int main(void) {
    puts("P016 O4 WIN32 MSAA ROOT SKIP: 77");
    return 77;
}
#else
#include <oleacc.h>
#include <oleauto.h>
#include <wchar.h>

#define CLIENT_CAP UINT32_C(4)
#define TARGET_CAP UINT32_C(2)
#define EVENT_CAP UINT32_C(8)
#define NODE_CAP UINT32_C(4)

static unsigned checks;
static unsigned failures;
static MooA11yWin32Provider *provider;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL:%u:%s\n", __LINE__, (message)); \
    } \
} while (0)

typedef struct {
    MooInputCore core;
    MooInputClientSlot clients[CLIENT_CAP];
    MooInputTargetSlot targets[TARGET_CAP];
    MooInputEventSlot events[EVENT_CAP];
    MooA11yNodeSlot nodes[NODE_CAP];
} Fixture;

static LRESULT CALLBACK root_wndproc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (provider != NULL) {
        LRESULT result = 0;
        uint32_t handled = 0u;
        MooInputResult status = moo_a11y_win32_provider_message(
            provider, window, message, wparam, lparam, &result, &handled);
        if (status == MOO_INPUT_OK && handled != 0u)
            return result;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void set_text(MooInputText *text, const char *bytes) {
    size_t length = strlen(bytes);
    memset(text, 0, sizeof(*text));
    if (length > MOO_INPUT_TEXT_CAPACITY)
        length = MOO_INPUT_TEXT_CAPACITY;
    text->length = (uint32_t)length;
    memcpy(text->bytes, bytes, length);
}

static void check_create_rejected(
    const MooInputCore *core, MooInputHandle requester,
    MooInputHandle root, HWND window, const char *message) {
    MooA11yWin32Provider *candidate =
        (MooA11yWin32Provider *)(uintptr_t)1;
    MooInputResult result = moo_a11y_win32_provider_create(
        core, requester, root, window, &candidate);
    CHECK(result != MOO_INPUT_OK, message);
    CHECK(candidate == NULL, "failed create clears out_provider");
    if (result == MOO_INPUT_OK && candidate != NULL &&
        candidate != (MooA11yWin32Provider *)(uintptr_t)1)
        moo_a11y_win32_provider_destroy(&candidate);
}

static void check_navigation(IAccessible *accessible, VARIANT self) {
    const LONG directions[] = {
        NAVDIR_FIRSTCHILD, NAVDIR_LASTCHILD, NAVDIR_NEXT, NAVDIR_PREVIOUS,
        NAVDIR_UP, NAVDIR_DOWN, NAVDIR_LEFT, NAVDIR_RIGHT
    };
    size_t i;
    for (i = 0u; i < sizeof(directions) / sizeof(directions[0]); ++i) {
        VARIANT result;
        VariantInit(&result);
        result.vt = VT_I4;
        result.lVal = 123;
        CHECK(IAccessible_accNavigate(
            accessible, directions[i], self, &result) == S_FALSE,
            "root navigation has no peer or child");
        CHECK(result.vt == VT_EMPTY,
            "absent navigation initializes VT_EMPTY");
    }
}

int main(void) {
    Fixture fixture;
    MooInputConfig config;
    MooInputHandle app = MOO_INPUT_HANDLE_INVALID;
    MooInputHandle ordinary = MOO_INPUT_HANDLE_INVALID;
    MooInputHandle automation = MOO_INPUT_HANDLE_INVALID;
    MooInputHandle reader = MOO_INPUT_HANDLE_INVALID;
    MooInputHandle target = MOO_INPUT_HANDLE_INVALID;
    MooInputHandle root = MOO_INPUT_HANDLE_INVALID;
    MooA11yNodeData node;
    WNDCLASSW window_class;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND window_a = NULL;
    HWND window_b = NULL;
    IAccessible *first = NULL;
    IAccessible *second = NULL;
    IUnknown *first_identity = NULL;
    IUnknown *second_identity = NULL;
    IDispatch *dispatch = NULL;
    IDispatch *parent = NULL;
    HRESULT com_status;
    VARIANT self;
    VARIANT role;
    VARIANT state;
    BSTR name = NULL;
    BSTR value = NULL;
    LRESULT native_result;
    uint32_t native_handled;
    RECT client;
    POINT origin;
    LONG left = 0;
    LONG top = 0;
    LONG width = 0;
    LONG height = 0;
    int com_initialized = 0;

    com_status = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CHECK(SUCCEEDED(com_status), "UI thread enters STA");
    if (FAILED(com_status))
        return 10;
    com_initialized = 1;

    memset(&fixture, 0, sizeof(fixture));
    config.max_events_per_client = UINT32_C(8);
    config.max_a11y_depth = UINT32_C(4);
    config.features = MOO_INPUT_FEATURE_ACCESSIBILITY |
        MOO_INPUT_FEATURE_AUTOMATION;
    CHECK(moo_input_init(
        &fixture.core, &config,
        fixture.clients, CLIENT_CAP,
        fixture.targets, TARGET_CAP,
        fixture.events, EVENT_CAP,
        fixture.nodes, NODE_CAP) == MOO_INPUT_OK,
        "input core init");
    CHECK(moo_input_client_create(
        &fixture.core, 0u, &app) == MOO_INPUT_OK,
        "application client");
    CHECK(moo_input_client_create(
        &fixture.core, 0u, &ordinary) == MOO_INPUT_OK,
        "ordinary requester");
    CHECK(moo_input_client_create(
        &fixture.core, MOO_INPUT_CLIENT_AUTOMATION, &automation) ==
        MOO_INPUT_OK, "automation requester");
    CHECK(moo_input_client_create(
        &fixture.core, MOO_INPUT_CLIENT_SCREEN_READER, &reader) ==
        MOO_INPUT_OK, "privileged screen reader");
    CHECK(moo_input_target_create_trusted(
        &fixture.core, app, UINT64_C(401), MOO_INPUT_TEXT_SENSITIVE,
        &target) == MOO_INPUT_OK, "trusted target");

    memset(&node, 0, sizeof(node));
    node.role = MOO_A11Y_ROLE_BUTTON;
    node.states = MOO_A11Y_STATE_PASSWORD;
    node.bounds.width = 240;
    node.bounds.height = 90;
    set_text(&node.name, "Moo Root Button");
    set_text(&node.value, "secret-value");
    CHECK(moo_a11y_node_create(
        &fixture.core, app, target, &node, &root) == MOO_INPUT_OK,
        "button root node");

    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = root_wndproc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"MooA11yRootContract";
    CHECK(RegisterClassW(&window_class) != 0 ||
        GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
        "register hidden test class");
    window_a = CreateWindowExW(
        0, window_class.lpszClassName, L"Moo A11y Root A",
        WS_OVERLAPPEDWINDOW, 10, 20, 320, 180,
        NULL, NULL, instance, NULL);
    window_b = CreateWindowExW(
        0, window_class.lpszClassName, L"Moo A11y Root B",
        WS_OVERLAPPEDWINDOW, 40, 50, 200, 120,
        NULL, NULL, instance, NULL);
    CHECK(window_a != NULL && window_b != NULL,
        "create two never-shown HWNDs");
    if (window_a == NULL || window_b == NULL)
        goto cleanup;

    check_create_rejected(
        &fixture.core, ordinary, root, window_a,
        "ordinary requester cannot create provider");
    check_create_rejected(
        &fixture.core, automation, root, window_a,
        "automation requester cannot create provider");
    check_create_rejected(
        &fixture.core, reader, root, NULL,
        "NULL HWND rejected");
    check_create_rejected(
        &fixture.core, reader, root, (HWND)(uintptr_t)1,
        "non-window HWND rejected");
    check_create_rejected(
        &fixture.core, MOO_INPUT_HANDLE_INVALID, root, window_a,
        "invalid requester rejected");
    check_create_rejected(
        &fixture.core, reader, UINT64_MAX, window_a,
        "invalid root rejected");
    CHECK(moo_input_client_disconnect(
        &fixture.core, automation, UINT64_C(1)) == MOO_INPUT_OK,
        "automation requester disconnected");
    check_create_rejected(
        &fixture.core, automation, root, window_a,
        "stale requester rejected");
    CHECK(moo_a11y_win32_provider_create(
        &fixture.core, reader, root, window_a, NULL) == MOO_INPUT_INVALID,
        "NULL out_provider rejected");

    CHECK(moo_a11y_win32_provider_create(
        &fixture.core, reader, root, window_a, &provider) == MOO_INPUT_OK,
        "reader alone creates provider bound to HWND A");
    CHECK(provider != NULL, "provider returned");
    if (provider == NULL)
        goto cleanup;

    native_result = (LRESULT)123;
    native_handled = UINT32_C(123);
    CHECK(moo_a11y_win32_provider_message(
        provider, window_b, WM_GETOBJECT, 0,
        (LPARAM)(LONG)OBJID_CLIENT,
        &native_result, &native_handled) == MOO_INPUT_INVALID,
        "mismatched HWND rejected");
    CHECK(native_result == 0 && native_handled == 0u,
        "mismatched HWND outputs cleared");
    native_result = (LRESULT)123;
    native_handled = UINT32_C(123);
    CHECK(moo_a11y_win32_provider_message(
        provider, window_a, WM_NULL, 0, 0,
        &native_result, &native_handled) == MOO_INPUT_OK &&
        native_result == 0 && native_handled == 0u,
        "irrelevant message unhandled");
    native_result = (LRESULT)123;
    native_handled = UINT32_C(123);
    CHECK(moo_a11y_win32_provider_message(
        provider, window_a, WM_GETOBJECT, 0,
        (LPARAM)(LONG)OBJID_WINDOW,
        &native_result, &native_handled) == MOO_INPUT_OK &&
        native_result == 0 && native_handled == 0u,
        "non-client object request unhandled");

    CHECK(AccessibleObjectFromWindow(
        window_a, OBJID_CLIENT, &IID_IAccessible, (void **)&first) == S_OK,
        "actual WM_GETOBJECT roundtrip");
    CHECK(first != NULL, "first IAccessible");
    if (first == NULL)
        goto cleanup;
    CHECK(AccessibleObjectFromWindow(
        window_a, OBJID_CLIENT, &IID_IAccessible, (void **)&second) == S_OK,
        "fresh repeated WM_GETOBJECT roundtrip");
    CHECK(second != NULL, "second IAccessible");

    CHECK(IAccessible_QueryInterface(
        first, &IID_IDispatch, (void **)&dispatch) == S_OK &&
        dispatch != NULL, "IAccessible exposes IDispatch");
    CHECK(IAccessible_QueryInterface(
        first, &IID_IUnknown, (void **)&first_identity) == S_OK,
        "first identity");
    if (second != NULL) {
        CHECK(IAccessible_QueryInterface(
            second, &IID_IUnknown, (void **)&second_identity) == S_OK,
            "second identity");
        CHECK(first_identity != NULL && second_identity != NULL,
            "repeated requests expose valid COM identities");
    }

    VariantInit(&self);
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    VariantInit(&role);
    VariantInit(&state);
    CHECK(IAccessible_get_accName(first, self, &name) == S_OK &&
        name != NULL && lstrcmpW(name, L"Moo Root Button") == 0,
        "exact root name BSTR");
    CHECK(name == NULL || wcsstr(name, L"secret-value") == NULL,
        "name excludes secret");
    CHECK(IAccessible_get_accRole(first, self, &role) == S_OK &&
        role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_PUSHBUTTON,
        "button role");
    CHECK(IAccessible_get_accState(first, self, &state) == S_OK &&
        state.vt == VT_I4 &&
        (state.lVal & STATE_SYSTEM_PROTECTED) != 0,
        "password maps to protected state");
    CHECK(IAccessible_get_accValue(first, self, &value) != S_OK &&
        value == NULL, "value remains unclaimed and secret-free");

    CHECK(GetClientRect(window_a, &client) != 0,
        "expected client rectangle");
    origin.x = client.left;
    origin.y = client.top;
    CHECK(ClientToScreen(window_a, &origin) != 0,
        "expected screen origin");
    CHECK(IAccessible_accLocation(
        first, &left, &top, &width, &height, self) == S_OK,
        "root location");
    CHECK(left == origin.x && top == origin.y &&
        width == client.right - client.left &&
        height == client.bottom - client.top,
        "location equals current client screen rectangle");

    {
        VARIANT hit;
        VariantInit(&hit);
        CHECK(IAccessible_accHitTest(
            first, origin.x + width / 2, origin.y + height / 2, &hit) == S_OK &&
            hit.vt == VT_I4 && hit.lVal == CHILDID_SELF,
            "inside hit returns self");
        VariantClear(&hit);
        hit.vt = VT_I4;
        hit.lVal = 123;
        CHECK(IAccessible_accHitTest(
            first, origin.x - 1, origin.y - 1, &hit) == S_FALSE &&
            hit.vt == VT_EMPTY, "outside upper-left hit is empty");
        hit.vt = VT_I4;
        hit.lVal = 123;
        CHECK(IAccessible_accHitTest(
            first, origin.x + width, origin.y + height / 2, &hit) == S_FALSE &&
            hit.vt == VT_EMPTY, "right edge is outside half-open rect");
        hit.vt = VT_I4;
        hit.lVal = 123;
        CHECK(IAccessible_accHitTest(
            first, origin.x + width / 2, origin.y + height, &hit) == S_FALSE &&
            hit.vt == VT_EMPTY, "bottom edge is outside half-open rect");
    }

    CHECK(IAccessible_get_accParent(first, &parent) == S_OK &&
        parent != NULL, "standard window parent proxy");
    check_navigation(first, self);
    {
        VARIANT result;
        VariantInit(&result);
        result.vt = VT_I4;
        result.lVal = 123;
        CHECK(IAccessible_accNavigate(
            first, (LONG)0x7fffffff, self, &result) ==
            E_INVALIDARG && result.vt == VT_EMPTY,
            "invalid navigation direction is initialized");
    }
    {
        VARIANT invalid_start;
        VARIANT result;
        VariantInit(&invalid_start);
        VariantInit(&result);
        result.vt = VT_I4;
        result.lVal = 123;
        CHECK(IAccessible_accNavigate(
            first, NAVDIR_FIRSTCHILD, invalid_start, &result) ==
            E_INVALIDARG && result.vt == VT_EMPTY,
            "invalid navigation start is initialized");
    }
    {
        VARIANT child;
        LONG invalid_left = 1;
        LONG invalid_top = 2;
        LONG invalid_width = 3;
        LONG invalid_height = 4;
        VariantInit(&child);
        child.vt = VT_I4;
        child.lVal = 1;
        CHECK(IAccessible_accLocation(
            first, &invalid_left, &invalid_top,
            &invalid_width, &invalid_height, child) == E_INVALIDARG,
            "invalid location child rejected");
        CHECK(invalid_left == 0 && invalid_top == 0 &&
            invalid_width == 0 && invalid_height == 0,
            "invalid child location outputs cleared");
        invalid_left = 1;
        invalid_top = 2;
        invalid_width = 3;
        invalid_height = 4;
        VariantClear(&child);
        child.vt = VT_BSTR;
        child.bstrVal = SysAllocString(L"bad");
        CHECK(IAccessible_accLocation(
            first, &invalid_left, &invalid_top,
            &invalid_width, &invalid_height, child) == E_INVALIDARG,
            "wrong location VARIANT type rejected");
        CHECK(invalid_left == 0 && invalid_top == 0 &&
            invalid_width == 0 && invalid_height == 0,
            "wrong-type location outputs cleared");
        VariantClear(&child);
    }

    if (second != NULL) {
        IAccessible_Release(second);
        second = NULL;
    }
    if (parent != NULL) {
        IDispatch_Release(parent);
        parent = NULL;
    }
    moo_a11y_win32_provider_destroy(&provider);
    CHECK(provider == NULL, "destroy nulls provider");
    DestroyWindow(window_a);
    window_a = NULL;

    {
        BSTR detached_name = (BSTR)(uintptr_t)1;
        CHECK(IAccessible_get_accName(
            first, self, &detached_name) == CO_E_OBJNOTCONNECTED &&
            detached_name == NULL,
            "detached name is safe and initialized");
        if (detached_name != NULL &&
            detached_name != (BSTR)(uintptr_t)1)
            SysFreeString(detached_name);
    }
    left = 1;
    top = 2;
    width = 3;
    height = 4;
    CHECK(IAccessible_accLocation(
        first, &left, &top, &width, &height, self) ==
        CO_E_OBJNOTCONNECTED &&
        left == 0 && top == 0 && width == 0 && height == 0,
        "detached location is safe and initialized");
    {
        VARIANT hit;
        VariantInit(&hit);
        hit.vt = VT_I4;
        hit.lVal = 123;
        CHECK(IAccessible_accHitTest(
            first, origin.x, origin.y, &hit) == CO_E_OBJNOTCONNECTED &&
            hit.vt == VT_EMPTY, "detached hit-test is safe and initialized");
    }
    {
        IDispatch *detached_parent = (IDispatch *)(uintptr_t)1;
        CHECK(IAccessible_get_accParent(first, &detached_parent) ==
            CO_E_OBJNOTCONNECTED && detached_parent == NULL,
            "detached parent is safe and initialized");
        if (detached_parent != NULL &&
            detached_parent != (IDispatch *)(uintptr_t)1)
            IDispatch_Release(detached_parent);
    }
    {
        VARIANT result;
        VariantInit(&result);
        result.vt = VT_I4;
        result.lVal = 123;
        CHECK(IAccessible_accNavigate(
            first, NAVDIR_FIRSTCHILD, self, &result) ==
            CO_E_OBJNOTCONNECTED && result.vt == VT_EMPTY,
            "detached navigation is safe and initialized");
    }

cleanup:
    if (provider != NULL)
        moo_a11y_win32_provider_destroy(&provider);
    if (dispatch != NULL)
        IDispatch_Release(dispatch);
    if (parent != NULL)
        IDispatch_Release(parent);
    if (first_identity != NULL)
        IUnknown_Release(first_identity);
    if (second_identity != NULL)
        IUnknown_Release(second_identity);
    if (first != NULL)
        IAccessible_Release(first);
    if (second != NULL)
        IAccessible_Release(second);
    if (name != NULL)
        SysFreeString(name);
    if (value != NULL)
        SysFreeString(value);
    if (window_a != NULL)
        DestroyWindow(window_a);
    if (window_b != NULL)
        DestroyWindow(window_b);
    UnregisterClassW(window_class.lpszClassName, instance);
    if (com_initialized)
        CoUninitialize();

    if (failures != 0u) {
        fprintf(stderr, "P016-O4-A11Y-WIN32-ROOT-FAIL checks=%u failures=%u\n",
            checks, failures);
        return 1;
    }
    printf("P016-O4-A11Y-WIN32-ROOT-OK checks=%u hidden=1 no_show=1 "
           "wm_getobject=1 retained_detach=1 privacy=1\n", checks);
    return 0;
}
#endif
