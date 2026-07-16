#include "moo_ui_host_effects_win32.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform;
    WCHAR service_pack[128];
} MooWin32VersionInfo;

typedef LONG (WINAPI *MooRtlGetVersionFn)(MooWin32VersionInfo *);
typedef HRESULT (WINAPI *MooDwmIsCompositionEnabledFn)(BOOL *);
typedef HRESULT (WINAPI *MooDwmSetWindowAttributeFn)(
    HWND, DWORD, const void *, DWORD);

typedef struct {
    int state;
    int flags;
    DWORD gradient_color;
    int animation_id;
} MooWin32AccentPolicy;

typedef struct {
    int attribute;
    void *data;
    SIZE_T size;
} MooWin32CompositionAttributeData;

typedef BOOL (WINAPI *MooSetWindowCompositionAttributeFn)(
    HWND, MooWin32CompositionAttributeData *);

enum {
    MOO_WIN32_DWMWA_USE_IMMERSIVE_DARK_MODE = 20,
    MOO_WIN32_DWMWA_WINDOW_CORNER_PREFERENCE = 33,
    MOO_WIN32_DWMWA_SYSTEMBACKDROP_TYPE = 38,
    MOO_WIN32_DWMWCP_DEFAULT = 0,
    MOO_WIN32_DWMWCP_ROUND = 2,
    MOO_WIN32_DWMSBT_NONE = 1,
    MOO_WIN32_DWMSBT_MAINWINDOW = 2,
    MOO_WIN32_WCA_ACCENT_POLICY = 19,
    MOO_WIN32_ACCENT_DISABLED = 0,
    MOO_WIN32_ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

static MooRtlGetVersionFn moo_win32_load_rtl_get_version(HMODULE module) {
    union {
        FARPROC raw;
        MooRtlGetVersionFn typed;
    } function;
    function.raw = GetProcAddress(module, "RtlGetVersion");
    return function.typed;
}

static MooDwmIsCompositionEnabledFn
moo_win32_load_dwm_is_composition_enabled(HMODULE module) {
    union {
        FARPROC raw;
        MooDwmIsCompositionEnabledFn typed;
    } function;
    function.raw = GetProcAddress(module, "DwmIsCompositionEnabled");
    return function.typed;
}

static MooDwmSetWindowAttributeFn
moo_win32_load_dwm_set_window_attribute(HMODULE module) {
    union {
        FARPROC raw;
        MooDwmSetWindowAttributeFn typed;
    } function;
    function.raw = GetProcAddress(module, "DwmSetWindowAttribute");
    return function.typed;
}

static MooSetWindowCompositionAttributeFn
moo_win32_load_set_window_composition_attribute(HMODULE module) {
    union {
        FARPROC raw;
        MooSetWindowCompositionAttributeFn typed;
    } function;
    function.raw = GetProcAddress(module, "SetWindowCompositionAttribute");
    return function.typed;
}

static MooUiHostWin32Result moo_win32_native_set_acrylic(
    HWND window, uint32_t enabled, int32_t *out_native_error) {
    HMODULE user32;
    MooSetWindowCompositionAttributeFn set_attribute;
    MooWin32AccentPolicy policy;
    MooWin32CompositionAttributeData data;
    BOOL result;
    if (window == 0 || out_native_error == 0)
        return MOO_UI_HOST_WIN32_RESULT_INVALID;
    *out_native_error = 0;
    user32 = LoadLibraryW(L"user32.dll");
    if (user32 == 0) {
        *out_native_error = (int32_t)GetLastError();
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }
    set_attribute =
        moo_win32_load_set_window_composition_attribute(user32);
    if (set_attribute == 0) {
        FreeLibrary(user32);
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }
    policy.state = enabled != 0u
        ? MOO_WIN32_ACCENT_ENABLE_ACRYLICBLURBEHIND
        : MOO_WIN32_ACCENT_DISABLED;
    policy.flags = 0;
    /* Neutral dark host tint; compositor pixels remain the semantic source. */
    policy.gradient_color = enabled != 0u ? (DWORD)0xcc202020UL : (DWORD)0;
    policy.animation_id = 0;
    data.attribute = MOO_WIN32_WCA_ACCENT_POLICY;
    data.data = &policy;
    data.size = (SIZE_T)sizeof(policy);
    SetLastError(ERROR_SUCCESS);
    result = set_attribute(window, &data);
    if (result == FALSE) *out_native_error = (int32_t)GetLastError();
    FreeLibrary(user32);
    return result != FALSE ? MOO_UI_HOST_WIN32_RESULT_OK
                           : MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR;
}

static void moo_win32_native_clear_dwm(
    MooDwmSetWindowAttributeFn set_attribute, HWND window) {
    const BOOL dark = FALSE;
    const DWORD corner = MOO_WIN32_DWMWCP_DEFAULT;
    const DWORD backdrop = MOO_WIN32_DWMSBT_NONE;
    if (set_attribute == 0 || window == 0) return;
    (void)set_attribute(window,
        (DWORD)MOO_WIN32_DWMWA_SYSTEMBACKDROP_TYPE,
        &backdrop, (DWORD)sizeof(backdrop));
    (void)set_attribute(window,
        (DWORD)MOO_WIN32_DWMWA_WINDOW_CORNER_PREFERENCE,
        &corner, (DWORD)sizeof(corner));
    (void)set_attribute(window,
        (DWORD)MOO_WIN32_DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark, (DWORD)sizeof(dark));
}

static MooUiHostWin32Result moo_win32_native_apply_dwm(
    void *user, const MooUiHostWin32Request *request,
    uint64_t allowed_host_capabilities,
    uint64_t *out_applied_host_capabilities,
    uint32_t *out_applied_backdrop, int32_t *out_native_error) {
    HMODULE dwm;
    MooDwmSetWindowAttributeFn set_attribute;
    HWND window;
    HRESULT result;
    uint64_t applied = UINT64_C(0);
    DWORD backdrop = MOO_WIN32_DWMSBT_NONE;
    DWORD corner = MOO_WIN32_DWMWCP_DEFAULT;
    BOOL dark = FALSE;
    uint32_t acrylic_active = 0u;
    (void)user;
    if (request == 0 || out_applied_host_capabilities == 0 ||
        out_applied_backdrop == 0 || out_native_error == 0)
        return MOO_UI_HOST_WIN32_RESULT_INVALID;
    *out_applied_host_capabilities = UINT64_C(0);
    *out_applied_backdrop = MOO_UI_HOST_WIN32_BACKDROP_NONE;
    *out_native_error = 0;
    window = (HWND)(uintptr_t)request->window;
    if (window == 0) return MOO_UI_HOST_WIN32_RESULT_INVALID;
    dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm == 0) {
        *out_native_error = (int32_t)GetLastError();
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }
    set_attribute = moo_win32_load_dwm_set_window_attribute(dwm);
    if (set_attribute == 0) {
        FreeLibrary(dwm);
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }

    if (request->backdrop == MOO_UI_HOST_WIN32_BACKDROP_ACRYLIC) {
        MooUiHostWin32Result acrylic_result =
            moo_win32_native_set_acrylic(window, 1u, out_native_error);
        if (acrylic_result != MOO_UI_HOST_WIN32_RESULT_OK) {
            FreeLibrary(dwm);
            return acrylic_result;
        }
        acrylic_active = 1u;
        applied |= MOO_UI_HOST_WIN32_CAP_ACRYLIC;
        *out_applied_backdrop = MOO_UI_HOST_WIN32_BACKDROP_ACRYLIC;
    }

    if (request->backdrop == MOO_UI_HOST_WIN32_BACKDROP_MICA) {
        backdrop = MOO_WIN32_DWMSBT_MAINWINDOW;
        result = set_attribute(window,
            (DWORD)MOO_WIN32_DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop, (DWORD)sizeof(backdrop));
        if (FAILED(result)) goto system_error;
        applied |= MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP |
                   MOO_UI_HOST_WIN32_CAP_MICA;
        *out_applied_backdrop = MOO_UI_HOST_WIN32_BACKDROP_MICA;
    } else {
        result = set_attribute(window,
            (DWORD)MOO_WIN32_DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop, (DWORD)sizeof(backdrop));
        if (FAILED(result) &&
            (allowed_host_capabilities &
             MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP) != UINT64_C(0))
            goto system_error;
    }

    if ((allowed_host_capabilities &
         MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS) != UINT64_C(0)) {
        corner = request->rounded_corners != 0u
            ? MOO_WIN32_DWMWCP_ROUND : MOO_WIN32_DWMWCP_DEFAULT;
        result = set_attribute(window,
            (DWORD)MOO_WIN32_DWMWA_WINDOW_CORNER_PREFERENCE,
            &corner, (DWORD)sizeof(corner));
        if (FAILED(result)) goto system_error;
        if (request->rounded_corners != 0u)
            applied |= MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS;
    }
    if ((allowed_host_capabilities &
         MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR) != UINT64_C(0)) {
        dark = request->dark_title_bar != 0u ? TRUE : FALSE;
        result = set_attribute(window,
            (DWORD)MOO_WIN32_DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark, (DWORD)sizeof(dark));
        if (FAILED(result)) goto system_error;
        if (request->dark_title_bar != 0u)
            applied |= MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR;
    }
    FreeLibrary(dwm);
    *out_applied_host_capabilities = applied;
    return MOO_UI_HOST_WIN32_RESULT_OK;

system_error:
    *out_native_error = (int32_t)result;
    if (acrylic_active != 0u) {
        int32_t clear_error = 0;
        (void)moo_win32_native_set_acrylic(window, 0u, &clear_error);
    }
    moo_win32_native_clear_dwm(set_attribute, window);
    FreeLibrary(dwm);
    *out_applied_host_capabilities = UINT64_C(0);
    *out_applied_backdrop = MOO_UI_HOST_WIN32_BACKDROP_NONE;
    return MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR;
}

static uint32_t moo_win32_native_build(uint32_t *out_build) {
    HMODULE ntdll;
    MooRtlGetVersionFn get_version;
    MooWin32VersionInfo version;
    LONG status;
    uint32_t index;
    if (out_build == 0) return 0u;
    *out_build = 0u;
    ntdll = LoadLibraryW(L"ntdll.dll");
    if (ntdll == 0) return 0u;
    get_version = moo_win32_load_rtl_get_version(ntdll);
    if (get_version == 0) {
        FreeLibrary(ntdll);
        return 0u;
    }
    for (index = 0u; index < (uint32_t)sizeof(version); ++index)
        ((volatile uint8_t *)&version)[index] = 0u;
    version.size = (ULONG)sizeof(version);
    status = get_version(&version);
    FreeLibrary(ntdll);
    if (status != 0 || version.build > UINT32_MAX) return 0u;
    *out_build = (uint32_t)version.build;
    return 1u;
}

static MooUiHostWin32Result moo_win32_native_probe(
    void *user, uint64_t *out_host_capabilities, int32_t *out_native_error) {
    HMODULE dwm;
    HMODULE user32;
    MooDwmIsCompositionEnabledFn composition_enabled;
    FARPROC set_window_attribute;
    FARPROC set_window_composition_attribute;
    BOOL enabled = FALSE;
    HRESULT result;
    uint32_t build = 0u;
    uint64_t capabilities = UINT64_C(0);
    (void)user;
    if (out_host_capabilities == 0 || out_native_error == 0)
        return MOO_UI_HOST_WIN32_RESULT_INVALID;
    *out_host_capabilities = UINT64_C(0);
    *out_native_error = 0;
    if (!moo_win32_native_build(&build))
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm == 0) {
        *out_native_error = (int32_t)GetLastError();
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }
    composition_enabled =
        moo_win32_load_dwm_is_composition_enabled(dwm);
    set_window_attribute = GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (composition_enabled == 0 || set_window_attribute == 0) {
        FreeLibrary(dwm);
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }
    result = composition_enabled(&enabled);
    if (result < 0) {
        *out_native_error = (int32_t)result;
        FreeLibrary(dwm);
        return MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR;
    }
    if (enabled == FALSE) {
        FreeLibrary(dwm);
        return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }
    if (build >= UINT32_C(18362))
        capabilities |= MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR;
    if (build >= UINT32_C(22000))
        capabilities |= MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS |
                        MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP |
                        MOO_UI_HOST_WIN32_CAP_MICA;
    user32 = LoadLibraryW(L"user32.dll");
    set_window_composition_attribute = user32 == 0 ? 0 :
        GetProcAddress(user32, "SetWindowCompositionAttribute");
    if (build >= UINT32_C(17134) &&
        set_window_composition_attribute != 0)
        capabilities |= MOO_UI_HOST_WIN32_CAP_ACRYLIC;
    if (user32 != 0) FreeLibrary(user32);
    FreeLibrary(dwm);
    *out_host_capabilities = capabilities;
    return capabilities == UINT64_C(0)
        ? MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE
        : MOO_UI_HOST_WIN32_RESULT_OK;
}
#endif

static void moo_win32_zero(void *destination, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)destination;
    uint32_t index;
    for (index = 0u; index < size; ++index) bytes[index] = 0u;
}

static uint32_t moo_win32_backdrop_valid(uint32_t backdrop) {
    return backdrop <= (uint32_t)MOO_UI_HOST_WIN32_BACKDROP_ACRYLIC;
}

static uint32_t moo_win32_fallback_valid(uint32_t fallback) {
    return fallback <= (uint32_t)MOO_UI_HOST_WIN32_FALLBACK_OPAQUE_WINDOW;
}

static uint64_t moo_win32_requested_capabilities(
    const MooUiHostWin32Request *request) {
    uint64_t capabilities = request->required_host_capabilities;
    if (request->dark_title_bar != 0u)
        capabilities |= MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR;
    if (request->rounded_corners != 0u)
        capabilities |= MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS;
    if (request->backdrop == (uint32_t)MOO_UI_HOST_WIN32_BACKDROP_MICA)
        capabilities |= MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP |
                        MOO_UI_HOST_WIN32_CAP_MICA;
    if (request->backdrop == (uint32_t)MOO_UI_HOST_WIN32_BACKDROP_ACRYLIC)
        capabilities |= MOO_UI_HOST_WIN32_CAP_ACRYLIC;
    return capabilities;
}

static MooUiHostWin32Result moo_win32_state_result(
    const MooUiHostWin32State *state) {
    if (state->native_state == (uint32_t)MOO_UI_HOST_WIN32_NATIVE_READY)
        return MOO_UI_HOST_WIN32_RESULT_OK;
    if (state->native_state == (uint32_t)MOO_UI_HOST_WIN32_NATIVE_LOST)
        return MOO_UI_HOST_WIN32_RESULT_LOST;
    if (state->native_state == (uint32_t)MOO_UI_HOST_WIN32_NATIVE_ERROR)
        return MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR;
    return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
}

static void moo_win32_outcome_base(
    const MooUiHostWin32State *state, MooUiHostWin32Outcome *outcome) {
    moo_win32_zero(outcome, (uint32_t)sizeof(*outcome));
    outcome->version = MOO_UI_HOST_EFFECTS_WIN32_VERSION;
    outcome->native_state = state->native_state;
    outcome->available_host_capabilities =
        state->native_state == (uint32_t)MOO_UI_HOST_WIN32_NATIVE_READY
            ? state->host_capabilities : UINT64_C(0);
    outcome->native_error = state->last_native_error;
}

static MooUiHostWin32Result moo_win32_use_fallback(
    const MooUiHostWin32Request *request, MooUiHostWin32Outcome *outcome,
    MooUiHostWin32Result native_result, int32_t native_error) {
    outcome->fallback_used = request->fallback;
    outcome->native_error = native_error;
    if (request->fallback != (uint32_t)MOO_UI_HOST_WIN32_FALLBACK_NONE)
        return MOO_UI_HOST_WIN32_RESULT_OK;
    return native_result;
}

static MooUiHostWin32Result moo_win32_probe_state(
    MooUiHostWin32State *state) {
    uint64_t capabilities = UINT64_C(0);
    int32_t native_error = 0;
    MooUiHostWin32Result result;
    result = state->ops.probe(state->user, &capabilities, &native_error);
    state->host_capabilities = UINT64_C(0);
    state->last_native_error = native_error;
    if (result == MOO_UI_HOST_WIN32_RESULT_OK) {
        state->host_capabilities =
            capabilities & (uint64_t)MOO_UI_HOST_WIN32_CAP_ALL;
        state->native_state = (uint32_t)MOO_UI_HOST_WIN32_NATIVE_READY;
    } else if (result == MOO_UI_HOST_WIN32_RESULT_LOST) {
        state->native_state = (uint32_t)MOO_UI_HOST_WIN32_NATIVE_LOST;
    } else if (result == MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR) {
        state->native_state = (uint32_t)MOO_UI_HOST_WIN32_NATIVE_ERROR;
    } else {
        state->native_state = (uint32_t)MOO_UI_HOST_WIN32_NATIVE_UNAVAILABLE;
        result = MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    }
    return result;
}

#if !defined(_WIN32)
static MooUiHostWin32Result moo_win32_native_unavailable_probe(
    void *user, uint64_t *out_host_capabilities, int32_t *out_native_error) {
    (void)user;
    if (out_host_capabilities != 0) *out_host_capabilities = UINT64_C(0);
    if (out_native_error != 0) *out_native_error = 0;
    return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
}

static MooUiHostWin32Result moo_win32_native_unavailable_apply(
    void *user, const MooUiHostWin32Request *request,
    uint64_t allowed_host_capabilities,
    uint64_t *out_applied_host_capabilities,
    uint32_t *out_applied_backdrop, int32_t *out_native_error) {
    (void)user;
    (void)request;
    (void)allowed_host_capabilities;
    if (out_applied_host_capabilities != 0)
        *out_applied_host_capabilities = UINT64_C(0);
    if (out_applied_backdrop != 0)
        *out_applied_backdrop =
            (uint32_t)MOO_UI_HOST_WIN32_BACKDROP_NONE;
    if (out_native_error != 0) *out_native_error = 0;
    return MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
}
#endif

MooUiHostWin32Request moo_ui_host_effects_win32_request_default(void) {
    MooUiHostWin32Request request;
    moo_win32_zero(&request, (uint32_t)sizeof(request));
    request.version = MOO_UI_HOST_EFFECTS_WIN32_VERSION;
    request.fallback = (uint32_t)MOO_UI_HOST_WIN32_FALLBACK_PORTABLE_CPU;
    return request;
}

MooUiHostWin32Result moo_ui_host_effects_win32_init(
    MooUiHostWin32State *state, const MooUiHostWin32Ops *ops, void *user) {
    if (state == 0 || ops == 0 || ops->probe == 0 || ops->apply == 0)
        return MOO_UI_HOST_WIN32_RESULT_INVALID;
    moo_win32_zero(state, (uint32_t)sizeof(*state));
    state->ops = *ops;
    state->user = user;
    state->generation = UINT64_C(1);
    state->native_state =
        (uint32_t)MOO_UI_HOST_WIN32_NATIVE_UNINITIALIZED;
    return moo_win32_probe_state(state);
}

MooUiHostWin32Result moo_ui_host_effects_win32_init_native(
    MooUiHostWin32State *state) {
    MooUiHostWin32Ops ops;
    moo_win32_zero(&ops, (uint32_t)sizeof(ops));
#if defined(_WIN32)
    ops.probe = moo_win32_native_probe;
    ops.apply = moo_win32_native_apply_dwm;
#else
    ops.probe = moo_win32_native_unavailable_probe;
    ops.apply = moo_win32_native_unavailable_apply;
#endif
    return moo_ui_host_effects_win32_init(state, &ops, 0);
}

MooUiHostWin32Result moo_ui_host_effects_win32_apply(
    MooUiHostWin32State *state, const MooUiHostWin32Request *request,
    MooUiHostWin32Outcome *outcome) {
    uint64_t requested_capabilities;
    uint64_t available_capabilities;
    uint64_t missing_capabilities;
    uint64_t applied_capabilities = UINT64_C(0);
    uint32_t applied_backdrop =
        (uint32_t)MOO_UI_HOST_WIN32_BACKDROP_NONE;
    int32_t native_error = 0;
    MooUiHostWin32Result state_result;
    MooUiHostWin32Result result;
    if (state == 0 || request == 0 || outcome == 0 ||
        state->ops.probe == 0 || state->ops.apply == 0 ||
        request->version != MOO_UI_HOST_EFFECTS_WIN32_VERSION ||
        request->window == MOO_UI_HOST_WIN32_WINDOW_INVALID ||
        request->reserved != 0u || request->dark_title_bar > 1u ||
        request->rounded_corners > 1u ||
        !moo_win32_backdrop_valid(request->backdrop) ||
        !moo_win32_fallback_valid(request->fallback) ||
        (request->required_host_capabilities &
         ~(uint64_t)MOO_UI_HOST_WIN32_CAP_ALL) != UINT64_C(0))
        return MOO_UI_HOST_WIN32_RESULT_INVALID;

    moo_win32_outcome_base(state, outcome);
    requested_capabilities = moo_win32_requested_capabilities(request);
    available_capabilities = outcome->available_host_capabilities;
    missing_capabilities = requested_capabilities & ~available_capabilities;
    outcome->missing_host_capabilities = missing_capabilities;
    state_result = moo_win32_state_result(state);
    if (state_result != MOO_UI_HOST_WIN32_RESULT_OK ||
        missing_capabilities != UINT64_C(0)) {
        if (state_result == MOO_UI_HOST_WIN32_RESULT_OK)
            state_result = MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
        return moo_win32_use_fallback(
            request, outcome, state_result, state->last_native_error);
    }

    result = state->ops.apply(
        state->user, request, available_capabilities,
        &applied_capabilities, &applied_backdrop, &native_error);
    applied_capabilities &= (uint64_t)MOO_UI_HOST_WIN32_CAP_ALL;
    outcome->applied_host_capabilities = applied_capabilities;
    outcome->applied_backdrop = applied_backdrop;
    outcome->native_error = native_error;
    outcome->missing_host_capabilities =
        requested_capabilities & ~applied_capabilities;
    state->last_native_error = native_error;

    if (result == MOO_UI_HOST_WIN32_RESULT_OK &&
        outcome->missing_host_capabilities == UINT64_C(0))
        return MOO_UI_HOST_WIN32_RESULT_OK;
    if (result == MOO_UI_HOST_WIN32_RESULT_LOST)
        (void)moo_ui_host_effects_win32_mark_lost(state, native_error);
    else if (result == MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR)
        state->native_state = (uint32_t)MOO_UI_HOST_WIN32_NATIVE_ERROR;
    else if (result == MOO_UI_HOST_WIN32_RESULT_OK)
        result = MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE;
    outcome->native_state = state->native_state;
    return moo_win32_use_fallback(request, outcome, result, native_error);
}

MooUiHostWin32Result moo_ui_host_effects_win32_mark_lost(
    MooUiHostWin32State *state, int32_t native_error) {
    if (state == 0 || state->ops.probe == 0 || state->ops.apply == 0)
        return MOO_UI_HOST_WIN32_RESULT_INVALID;
    state->native_state = (uint32_t)MOO_UI_HOST_WIN32_NATIVE_LOST;
    state->host_capabilities = UINT64_C(0);
    state->last_native_error = native_error;
    if (state->generation != UINT64_MAX) state->generation += UINT64_C(1);
    return MOO_UI_HOST_WIN32_RESULT_LOST;
}

MooUiHostWin32Result moo_ui_host_effects_win32_recover(
    MooUiHostWin32State *state) {
    if (state == 0 || state->ops.probe == 0 || state->ops.apply == 0)
        return MOO_UI_HOST_WIN32_RESULT_INVALID;
    if (state->ops.reset != 0) state->ops.reset(state->user);
    if (state->generation != UINT64_MAX) state->generation += UINT64_C(1);
    state->native_state =
        (uint32_t)MOO_UI_HOST_WIN32_NATIVE_UNINITIALIZED;
    return moo_win32_probe_state(state);
}

void moo_ui_host_effects_win32_shutdown(MooUiHostWin32State *state) {
    if (state == 0) return;
    if (state->ops.reset != 0) state->ops.reset(state->user);
    moo_win32_zero(state, (uint32_t)sizeof(*state));
}

uint64_t moo_ui_host_effects_win32_capabilities(
    const MooUiHostWin32State *state) {
    if (state == 0 ||
        state->native_state != (uint32_t)MOO_UI_HOST_WIN32_NATIVE_READY)
        return UINT64_C(0);
    return state->host_capabilities;
}

uint64_t moo_ui_host_effects_win32_generation(
    const MooUiHostWin32State *state) {
    return state == 0 ? UINT64_C(0) : state->generation;
}
