#ifndef MOO_UI_HOST_EFFECTS_WIN32_H
#define MOO_UI_HOST_EFFECTS_WIN32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOO_UI_HOST_EFFECTS_WIN32_VERSION UINT32_C(1)

/*
 * Host-window decoration capabilities only.  These bits describe what the
 * current HWND presenter can request from DWM/User32.  They MUST NOT be ORed
 * into MOO_COMP_FEATURE_* or MOO_COMP_EFFECT_*: none of them promises
 * per-surface corner, shadow, backdrop, colour or animation semantics.
 */
enum {
    MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR = UINT64_C(1) << 0,
    MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS = UINT64_C(1) << 1,
    MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP = UINT64_C(1) << 2,
    MOO_UI_HOST_WIN32_CAP_MICA = UINT64_C(1) << 3,
    MOO_UI_HOST_WIN32_CAP_ACRYLIC = UINT64_C(1) << 4
};
#define MOO_UI_HOST_WIN32_CAP_ALL \
    (MOO_UI_HOST_WIN32_CAP_DARK_TITLE_BAR | \
     MOO_UI_HOST_WIN32_CAP_ROUNDED_CORNERS | \
     MOO_UI_HOST_WIN32_CAP_SYSTEM_BACKDROP | \
     MOO_UI_HOST_WIN32_CAP_MICA | MOO_UI_HOST_WIN32_CAP_ACRYLIC)

typedef uintptr_t MooUiHostWin32Window;
#define MOO_UI_HOST_WIN32_WINDOW_INVALID ((MooUiHostWin32Window)0)

typedef enum {
    MOO_UI_HOST_WIN32_RESULT_OK = 0,
    MOO_UI_HOST_WIN32_RESULT_INVALID = 1,
    MOO_UI_HOST_WIN32_RESULT_UNAVAILABLE = 2,
    MOO_UI_HOST_WIN32_RESULT_SYSTEM_ERROR = 3,
    MOO_UI_HOST_WIN32_RESULT_LOST = 4
} MooUiHostWin32Result;

typedef enum {
    MOO_UI_HOST_WIN32_NATIVE_UNINITIALIZED = 0,
    MOO_UI_HOST_WIN32_NATIVE_READY = 1,
    MOO_UI_HOST_WIN32_NATIVE_UNAVAILABLE = 2,
    MOO_UI_HOST_WIN32_NATIVE_ERROR = 3,
    MOO_UI_HOST_WIN32_NATIVE_LOST = 4
} MooUiHostWin32NativeState;

typedef enum {
    MOO_UI_HOST_WIN32_BACKDROP_NONE = 0,
    MOO_UI_HOST_WIN32_BACKDROP_MICA = 1,
    MOO_UI_HOST_WIN32_BACKDROP_ACRYLIC = 2
} MooUiHostWin32Backdrop;

typedef enum {
    MOO_UI_HOST_WIN32_FALLBACK_NONE = 0,
    /* Keep compositor CPU semantics and make no native-window promise. */
    MOO_UI_HOST_WIN32_FALLBACK_PORTABLE_CPU = 1,
    /* Use an ordinary opaque host window; compositor pixels remain valid. */
    MOO_UI_HOST_WIN32_FALLBACK_OPAQUE_WINDOW = 2
} MooUiHostWin32Fallback;

typedef struct {
    uint32_t version;
    uint32_t backdrop;
    MooUiHostWin32Window window;
    uint64_t required_host_capabilities;
    uint32_t dark_title_bar;
    uint32_t rounded_corners;
    uint32_t fallback;
    uint32_t reserved;
} MooUiHostWin32Request;

typedef struct {
    uint32_t version;
    uint32_t native_state;
    uint64_t available_host_capabilities;
    uint64_t applied_host_capabilities;
    uint64_t missing_host_capabilities;
    uint32_t applied_backdrop;
    uint32_t fallback_used;
    int32_t native_error;
    uint32_t reserved;
} MooUiHostWin32Outcome;

/*
 * All callbacks are injectable.  Host tests therefore exercise lifecycle and
 * fallback behaviour without creating a desktop window or taking mouse/input
 * ownership.  Native callbacks use HWND only inside the guarded Win32 body.
 */
typedef MooUiHostWin32Result (*MooUiHostWin32ProbeFn)(
    void *user, uint64_t *out_host_capabilities, int32_t *out_native_error);
typedef MooUiHostWin32Result (*MooUiHostWin32ApplyFn)(
    void *user, const MooUiHostWin32Request *request,
    uint64_t allowed_host_capabilities,
    uint64_t *out_applied_host_capabilities,
    uint32_t *out_applied_backdrop, int32_t *out_native_error);
typedef void (*MooUiHostWin32ResetFn)(void *user);

typedef struct {
    MooUiHostWin32ProbeFn probe;
    MooUiHostWin32ApplyFn apply;
    MooUiHostWin32ResetFn reset;
} MooUiHostWin32Ops;

typedef struct {
    MooUiHostWin32Ops ops;
    void *user;
    uint64_t host_capabilities;
    uint64_t generation;
    uint32_t native_state;
    int32_t last_native_error;
} MooUiHostWin32State;

MooUiHostWin32Request moo_ui_host_effects_win32_request_default(void);
MooUiHostWin32Result moo_ui_host_effects_win32_init(
    MooUiHostWin32State *state, const MooUiHostWin32Ops *ops, void *user);
MooUiHostWin32Result moo_ui_host_effects_win32_init_native(
    MooUiHostWin32State *state);
MooUiHostWin32Result moo_ui_host_effects_win32_apply(
    MooUiHostWin32State *state, const MooUiHostWin32Request *request,
    MooUiHostWin32Outcome *outcome);
MooUiHostWin32Result moo_ui_host_effects_win32_mark_lost(
    MooUiHostWin32State *state, int32_t native_error);
MooUiHostWin32Result moo_ui_host_effects_win32_recover(
    MooUiHostWin32State *state);
void moo_ui_host_effects_win32_shutdown(MooUiHostWin32State *state);
uint64_t moo_ui_host_effects_win32_capabilities(
    const MooUiHostWin32State *state);
uint64_t moo_ui_host_effects_win32_generation(
    const MooUiHostWin32State *state);

#ifdef __cplusplus
}
#endif
#endif
