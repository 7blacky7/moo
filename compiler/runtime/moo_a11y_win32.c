#include "moo_a11y_win32.h"

#if defined(_WIN32)

#include <oleacc.h>
#include <oleauto.h>
#include <string.h>

struct MooA11yWin32Provider {
    IAccessible iface;
    volatile LONG references;
    volatile LONG detached;
    const MooInputCore *core;
    MooInputHandle screen_reader;
    MooInputHandle root;
    HWND window;
};

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_query_interface(
    IAccessible *iface, REFIID riid, void **out_object);
static ULONG STDMETHODCALLTYPE moo_a11y_win32_add_ref(IAccessible *iface);
static ULONG STDMETHODCALLTYPE moo_a11y_win32_release(IAccessible *iface);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_type_info_count(
    IAccessible *iface, UINT *out_count);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_type_info(
    IAccessible *iface, UINT index, LCID locale, ITypeInfo **out_info);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_ids_of_names(
    IAccessible *iface, REFIID riid, LPOLESTR *names, UINT name_count,
    LCID locale, DISPID *out_ids);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_invoke(
    IAccessible *iface, DISPID member, REFIID riid, LCID locale,
    WORD flags, DISPPARAMS *params, VARIANT *out_result,
    EXCEPINFO *out_exception, UINT *out_argument);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_parent(
    IAccessible *iface, IDispatch **out_parent);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_child_count(
    IAccessible *iface, LONG *out_count);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_child(
    IAccessible *iface, VARIANT child, IDispatch **out_dispatch);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_name(
    IAccessible *iface, VARIANT child, BSTR *out_name);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_value(
    IAccessible *iface, VARIANT child, BSTR *out_value);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_description(
    IAccessible *iface, VARIANT child, BSTR *out_description);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_role(
    IAccessible *iface, VARIANT child, VARIANT *out_role);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_state(
    IAccessible *iface, VARIANT child, VARIANT *out_state);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_help(
    IAccessible *iface, VARIANT child, BSTR *out_help);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_help_topic(
    IAccessible *iface, BSTR *out_file, VARIANT child, LONG *out_topic);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_keyboard_shortcut(
    IAccessible *iface, VARIANT child, BSTR *out_shortcut);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_focus(
    IAccessible *iface, VARIANT *out_focus);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_selection(
    IAccessible *iface, VARIANT *out_selection);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_default_action(
    IAccessible *iface, VARIANT child, BSTR *out_action);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_select(
    IAccessible *iface, LONG flags, VARIANT child);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_location(
    IAccessible *iface, LONG *out_left, LONG *out_top,
    LONG *out_width, LONG *out_height, VARIANT child);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_navigate(
    IAccessible *iface, LONG direction, VARIANT start, VARIANT *out_target);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_hit_test(
    IAccessible *iface, LONG x, LONG y, VARIANT *out_hit);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_do_default_action(
    IAccessible *iface, VARIANT child);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_put_name(
    IAccessible *iface, VARIANT child, BSTR name);
static HRESULT STDMETHODCALLTYPE moo_a11y_win32_put_value(
    IAccessible *iface, VARIANT child, BSTR value);

static IAccessibleVtbl moo_a11y_win32_vtable = {
    moo_a11y_win32_query_interface,
    moo_a11y_win32_add_ref,
    moo_a11y_win32_release,
    moo_a11y_win32_get_type_info_count,
    moo_a11y_win32_get_type_info,
    moo_a11y_win32_get_ids_of_names,
    moo_a11y_win32_invoke,
    moo_a11y_win32_get_parent,
    moo_a11y_win32_get_child_count,
    moo_a11y_win32_get_child,
    moo_a11y_win32_get_name,
    moo_a11y_win32_get_value,
    moo_a11y_win32_get_description,
    moo_a11y_win32_get_role,
    moo_a11y_win32_get_state,
    moo_a11y_win32_get_help,
    moo_a11y_win32_get_help_topic,
    moo_a11y_win32_get_keyboard_shortcut,
    moo_a11y_win32_get_focus,
    moo_a11y_win32_get_selection,
    moo_a11y_win32_get_default_action,
    moo_a11y_win32_select,
    moo_a11y_win32_location,
    moo_a11y_win32_navigate,
    moo_a11y_win32_hit_test,
    moo_a11y_win32_do_default_action,
    moo_a11y_win32_put_name,
    moo_a11y_win32_put_value
};

static MooA11yWin32Provider *moo_a11y_win32_from_iface(
    IAccessible *iface) {
    return (MooA11yWin32Provider *)iface;
}

static HRESULT moo_a11y_win32_live_node(
    MooA11yWin32Provider *provider, MooA11yNodeData *out_node,
    HWND *out_window) {
    MooA11yNodeData node;
    uint64_t revision = UINT64_C(0);
    MooInputResult result;

    if (InterlockedCompareExchange(&provider->detached, 0, 0) != 0 ||
        provider->core == NULL)
        return CO_E_OBJNOTCONNECTED;
    result = moo_a11y_node_read(
        provider->core, provider->screen_reader,
        provider->root, &node, &revision);
    if (result != MOO_INPUT_OK)
        return CO_E_OBJNOTCONNECTED;
    if (node.role != MOO_A11Y_ROLE_BUTTON ||
        node.name.length == 0u ||
        node.name.length > MOO_INPUT_TEXT_CAPACITY)
        return E_FAIL;
    if (provider->window == NULL || !IsWindow(provider->window))
        return CO_E_OBJNOTCONNECTED;
    if (out_node != NULL)
        *out_node = node;
    if (out_window != NULL)
        *out_window = provider->window;
    return S_OK;
}

static HRESULT moo_a11y_win32_self(VARIANT child) {
    if (child.vt != VT_I4 || child.lVal != CHILDID_SELF)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_query_interface(
    IAccessible *iface, REFIID riid, void **out_object) {
    if (out_object == NULL)
        return E_POINTER;
    *out_object = NULL;
    if (riid == NULL)
        return E_INVALIDARG;
    if (!IsEqualIID(riid, &IID_IUnknown) &&
        !IsEqualIID(riid, &IID_IDispatch) &&
        !IsEqualIID(riid, &IID_IAccessible))
        return E_NOINTERFACE;
    *out_object = iface;
    (void)moo_a11y_win32_add_ref(iface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE moo_a11y_win32_add_ref(IAccessible *iface) {
    MooA11yWin32Provider *provider = moo_a11y_win32_from_iface(iface);
    return (ULONG)InterlockedIncrement(&provider->references);
}

static ULONG STDMETHODCALLTYPE moo_a11y_win32_release(IAccessible *iface) {
    MooA11yWin32Provider *provider = moo_a11y_win32_from_iface(iface);
    LONG references = InterlockedDecrement(&provider->references);
    if (references == 0)
        HeapFree(GetProcessHeap(), 0, provider);
    return (ULONG)references;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_type_info_count(
    IAccessible *iface, UINT *out_count) {
    (void)iface;
    if (out_count == NULL)
        return E_POINTER;
    *out_count = 0u;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_type_info(
    IAccessible *iface, UINT index, LCID locale, ITypeInfo **out_info) {
    (void)iface;
    (void)index;
    (void)locale;
    if (out_info == NULL)
        return E_POINTER;
    *out_info = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_ids_of_names(
    IAccessible *iface, REFIID riid, LPOLESTR *names, UINT name_count,
    LCID locale, DISPID *out_ids) {
    UINT index;
    (void)iface;
    (void)riid;
    (void)names;
    (void)locale;
    if (name_count != 0u && out_ids == NULL)
        return E_POINTER;
    for (index = 0u; index < name_count; ++index)
        out_ids[index] = DISPID_UNKNOWN;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_invoke(
    IAccessible *iface, DISPID member, REFIID riid, LCID locale,
    WORD flags, DISPPARAMS *params, VARIANT *out_result,
    EXCEPINFO *out_exception, UINT *out_argument) {
    (void)iface;
    (void)member;
    (void)riid;
    (void)locale;
    (void)flags;
    (void)params;
    if (out_result != NULL)
        VariantInit(out_result);
    if (out_exception != NULL)
        memset(out_exception, 0, sizeof(*out_exception));
    if (out_argument != NULL)
        *out_argument = 0u;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_parent(
    IAccessible *iface, IDispatch **out_parent) {
    MooA11yWin32Provider *provider = moo_a11y_win32_from_iface(iface);
    IAccessible *temporary = NULL;
    HWND window = NULL;
    HRESULT result;

    if (out_parent == NULL)
        return E_POINTER;
    *out_parent = NULL;
    result = moo_a11y_win32_live_node(provider, NULL, &window);
    if (FAILED(result))
        return result;
    result = AccessibleObjectFromWindow(
        window, OBJID_WINDOW, &IID_IAccessible, (void **)&temporary);
    if (result != S_OK || temporary == NULL) {
        if (temporary != NULL)
            temporary->lpVtbl->Release(temporary);
        return S_FALSE;
    }
    result = temporary->lpVtbl->QueryInterface(
        temporary, &IID_IDispatch, (void **)out_parent);
    temporary->lpVtbl->Release(temporary);
    if (FAILED(result)) {
        *out_parent = NULL;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_child_count(
    IAccessible *iface, LONG *out_count) {
    HRESULT result;
    if (out_count == NULL)
        return E_POINTER;
    *out_count = 0;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_child(
    IAccessible *iface, VARIANT child, IDispatch **out_dispatch) {
    HRESULT result;
    if (out_dispatch == NULL)
        return E_POINTER;
    *out_dispatch = NULL;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_name(
    IAccessible *iface, VARIANT child, BSTR *out_name) {
    MooA11yNodeData node;
    HRESULT result;
    int source_length;
    int wide_length;
    BSTR name;

    if (out_name == NULL)
        return E_POINTER;
    *out_name = NULL;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), &node, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    source_length = (int)node.name.length;
    wide_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)node.name.bytes,
        source_length, NULL, 0);
    if (wide_length <= 0)
        return E_FAIL;
    name = SysAllocStringLen(NULL, (UINT)wide_length);
    if (name == NULL)
        return E_OUTOFMEMORY;
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)node.name.bytes,
            source_length, name, wide_length) != wide_length) {
        SysFreeString(name);
        return E_FAIL;
    }
    *out_name = name;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_value(
    IAccessible *iface, VARIANT child, BSTR *out_value) {
    HRESULT result;
    if (out_value == NULL)
        return E_POINTER;
    *out_value = NULL;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_description(
    IAccessible *iface, VARIANT child, BSTR *out_description) {
    HRESULT result;
    if (out_description == NULL)
        return E_POINTER;
    *out_description = NULL;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_role(
    IAccessible *iface, VARIANT child, VARIANT *out_role) {
    HRESULT result;
    if (out_role == NULL)
        return E_POINTER;
    VariantInit(out_role);
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    out_role->vt = VT_I4;
    out_role->lVal = ROLE_SYSTEM_PUSHBUTTON;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_state(
    IAccessible *iface, VARIANT child, VARIANT *out_state) {
    MooA11yNodeData node;
    HRESULT result;
    if (out_state == NULL)
        return E_POINTER;
    VariantInit(out_state);
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), &node, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    out_state->vt = VT_I4;
    out_state->lVal =
        (node.states & MOO_A11Y_STATE_PASSWORD) != 0u
            ? STATE_SYSTEM_PROTECTED
            : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_help(
    IAccessible *iface, VARIANT child, BSTR *out_help) {
    HRESULT result;
    if (out_help == NULL)
        return E_POINTER;
    *out_help = NULL;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_help_topic(
    IAccessible *iface, BSTR *out_file, VARIANT child, LONG *out_topic) {
    HRESULT result;
    if (out_file != NULL)
        *out_file = NULL;
    if (out_topic != NULL)
        *out_topic = 0;
    if (out_file == NULL || out_topic == NULL)
        return E_POINTER;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_keyboard_shortcut(
    IAccessible *iface, VARIANT child, BSTR *out_shortcut) {
    HRESULT result;
    if (out_shortcut == NULL)
        return E_POINTER;
    *out_shortcut = NULL;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_focus(
    IAccessible *iface, VARIANT *out_focus) {
    HRESULT result;
    if (out_focus == NULL)
        return E_POINTER;
    VariantInit(out_focus);
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_selection(
    IAccessible *iface, VARIANT *out_selection) {
    HRESULT result;
    if (out_selection == NULL)
        return E_POINTER;
    VariantInit(out_selection);
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_get_default_action(
    IAccessible *iface, VARIANT child, BSTR *out_action) {
    HRESULT result;
    if (out_action == NULL)
        return E_POINTER;
    *out_action = NULL;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_select(
    IAccessible *iface, LONG flags, VARIANT child) {
    HRESULT result;
    (void)flags;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_location(
    IAccessible *iface, LONG *out_left, LONG *out_top,
    LONG *out_width, LONG *out_height, VARIANT child) {
    HWND window = NULL;
    RECT rectangle;
    POINT origin;
    HRESULT result;

    if (out_left != NULL)
        *out_left = 0;
    if (out_top != NULL)
        *out_top = 0;
    if (out_width != NULL)
        *out_width = 0;
    if (out_height != NULL)
        *out_height = 0;
    if (out_left == NULL || out_top == NULL ||
        out_width == NULL || out_height == NULL)
        return E_POINTER;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, &window);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    if (!GetClientRect(window, &rectangle))
        return E_FAIL;
    origin.x = rectangle.left;
    origin.y = rectangle.top;
    if (!ClientToScreen(window, &origin))
        return E_FAIL;
    *out_left = origin.x;
    *out_top = origin.y;
    *out_width = rectangle.right - rectangle.left;
    *out_height = rectangle.bottom - rectangle.top;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_navigate(
    IAccessible *iface, LONG direction, VARIANT start, VARIANT *out_target) {
    HRESULT result;
    if (out_target == NULL)
        return E_POINTER;
    VariantInit(out_target);
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(start);
    if (FAILED(result))
        return result;
    if (direction != NAVDIR_FIRSTCHILD &&
        direction != NAVDIR_LASTCHILD &&
        direction != NAVDIR_NEXT &&
        direction != NAVDIR_PREVIOUS &&
        direction != NAVDIR_UP &&
        direction != NAVDIR_DOWN &&
        direction != NAVDIR_LEFT &&
        direction != NAVDIR_RIGHT)
        return E_INVALIDARG;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_hit_test(
    IAccessible *iface, LONG x, LONG y, VARIANT *out_hit) {
    HWND window = NULL;
    RECT rectangle;
    POINT origin;
    HRESULT result;
    LONG right;
    LONG bottom;

    if (out_hit == NULL)
        return E_POINTER;
    VariantInit(out_hit);
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, &window);
    if (FAILED(result))
        return result;
    if (!GetClientRect(window, &rectangle))
        return E_FAIL;
    origin.x = rectangle.left;
    origin.y = rectangle.top;
    if (!ClientToScreen(window, &origin))
        return E_FAIL;
    right = origin.x + rectangle.right - rectangle.left;
    bottom = origin.y + rectangle.bottom - rectangle.top;
    if (x < origin.x || y < origin.y || x >= right || y >= bottom)
        return S_FALSE;
    out_hit->vt = VT_I4;
    out_hit->lVal = CHILDID_SELF;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_do_default_action(
    IAccessible *iface, VARIANT child) {
    HRESULT result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_put_name(
    IAccessible *iface, VARIANT child, BSTR name) {
    HRESULT result;
    (void)name;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE moo_a11y_win32_put_value(
    IAccessible *iface, VARIANT child, BSTR value) {
    HRESULT result;
    (void)value;
    result = moo_a11y_win32_live_node(
        moo_a11y_win32_from_iface(iface), NULL, NULL);
    if (FAILED(result))
        return result;
    result = moo_a11y_win32_self(child);
    if (FAILED(result))
        return result;
    return E_NOTIMPL;
}

MooInputResult moo_a11y_win32_provider_create(
    const MooInputCore *core, MooInputHandle screen_reader,
    MooInputHandle root, HWND window,
    MooA11yWin32Provider **out_provider) {
    MooA11yNodeData node;
    uint64_t revision = UINT64_C(0);
    MooA11yWin32Provider *created;
    MooInputResult result;

    if (out_provider == NULL)
        return MOO_INPUT_INVALID;
    *out_provider = NULL;
    if (core == NULL ||
        screen_reader == MOO_INPUT_HANDLE_INVALID ||
        root == MOO_INPUT_HANDLE_INVALID ||
        !IsWindow(window))
        return MOO_INPUT_INVALID;

    result = moo_a11y_node_read(
        core, screen_reader, root, &node, &revision);
    if (result != MOO_INPUT_OK)
        return result;
    if (node.role != MOO_A11Y_ROLE_BUTTON ||
        node.name.length == 0u ||
        node.name.length > MOO_INPUT_TEXT_CAPACITY)
        return MOO_INPUT_BAD_STATE;

    created = (MooA11yWin32Provider *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*created));
    if (created == NULL)
        return MOO_INPUT_LIMIT;

    created->iface.lpVtbl = &moo_a11y_win32_vtable;
    created->references = 1;
    created->detached = 0;
    created->core = core;
    created->screen_reader = screen_reader;
    created->root = root;
    created->window = window;
    *out_provider = created;
    return MOO_INPUT_OK;
}

MooInputResult moo_a11y_win32_provider_message(
    MooA11yWin32Provider *provider, HWND window, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT *out_lresult,
    uint32_t *out_handled) {
    MooA11yNodeData node;
    uint64_t revision = UINT64_C(0);
    MooInputResult result;

    if (out_lresult != NULL)
        *out_lresult = 0;
    if (out_handled != NULL)
        *out_handled = 0u;
    if (provider == NULL ||
        out_lresult == NULL ||
        out_handled == NULL)
        return MOO_INPUT_INVALID;
    if (window != provider->window)
        return MOO_INPUT_INVALID;
    if (message != WM_GETOBJECT ||
        (LONG)lparam != OBJID_CLIENT)
        return MOO_INPUT_OK;

    *out_handled = 1u;
    if (InterlockedCompareExchange(
            &provider->detached, 0, 0) != 0 ||
        provider->core == NULL)
        return MOO_INPUT_BAD_STATE;
    result = moo_a11y_node_read(
        provider->core, provider->screen_reader,
        provider->root, &node, &revision);
    if (result != MOO_INPUT_OK)
        return result;
    if (node.role != MOO_A11Y_ROLE_BUTTON ||
        node.name.length == 0u ||
        node.name.length > MOO_INPUT_TEXT_CAPACITY)
        return MOO_INPUT_BAD_STATE;
    *out_lresult = LresultFromObject(
        &IID_IAccessible, wparam, (IUnknown *)&provider->iface);
    return MOO_INPUT_OK;
}

void moo_a11y_win32_provider_destroy(
    MooA11yWin32Provider **provider) {
    MooA11yWin32Provider *current;

    if (provider == NULL)
        return;
    current = *provider;
    *provider = NULL;
    if (current == NULL)
        return;
    if (InterlockedExchange(&current->detached, 1) != 0)
        return;

    current->core = NULL;
    current->window = NULL;
    current->screen_reader = MOO_INPUT_HANDLE_INVALID;
    current->root = MOO_INPUT_HANDLE_INVALID;
    (void)moo_a11y_win32_release(&current->iface);
}

#endif
