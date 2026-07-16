#ifndef MOO_UI_HOST_EFFECTS_COCOA_H
#define MOO_UI_HOST_EFFECTS_COCOA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOO_UI_HOST_EFFECTS_COCOA_VERSION UINT32_C(1)

/*
 * NSWindow/NSVisualEffectView host decoration capabilities only.  They never
 * imply MOO_COMP_EFFECT_* per-surface semantics and must not be exported as
 * portable compositor feature bits.
 */
enum {
    MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR = UINT64_C(1) << 0,
    MOO_UI_HOST_COCOA_CAP_VIBRANCY = UINT64_C(1) << 1,
    MOO_UI_HOST_COCOA_CAP_BEHIND_WINDOW = UINT64_C(1) << 2,
    MOO_UI_HOST_COCOA_CAP_WITHIN_WINDOW = UINT64_C(1) << 3,
    MOO_UI_HOST_COCOA_CAP_REDUCE_TRANSPARENCY_AWARE = UINT64_C(1) << 4
};
#define MOO_UI_HOST_COCOA_CAP_ALL \
    (MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR | \
     MOO_UI_HOST_COCOA_CAP_VIBRANCY | \
     MOO_UI_HOST_COCOA_CAP_BEHIND_WINDOW | \
     MOO_UI_HOST_COCOA_CAP_WITHIN_WINDOW | \
     MOO_UI_HOST_COCOA_CAP_REDUCE_TRANSPARENCY_AWARE)

typedef uintptr_t MooUiHostCocoaWindow;
#define MOO_UI_HOST_COCOA_WINDOW_INVALID ((MooUiHostCocoaWindow)0)

typedef enum {
    MOO_UI_HOST_COCOA_RESULT_OK = 0,
    MOO_UI_HOST_COCOA_RESULT_INVALID = 1,
    MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE = 2,
    MOO_UI_HOST_COCOA_RESULT_SYSTEM_ERROR = 3,
    MOO_UI_HOST_COCOA_RESULT_LOST = 4
} MooUiHostCocoaResult;

typedef enum {
    MOO_UI_HOST_COCOA_NATIVE_UNINITIALIZED = 0,
    MOO_UI_HOST_COCOA_NATIVE_READY = 1,
    MOO_UI_HOST_COCOA_NATIVE_UNAVAILABLE = 2,
    MOO_UI_HOST_COCOA_NATIVE_ERROR = 3,
    MOO_UI_HOST_COCOA_NATIVE_LOST = 4
} MooUiHostCocoaNativeState;

typedef enum {
    MOO_UI_HOST_COCOA_MATERIAL_NONE = 0,
    MOO_UI_HOST_COCOA_MATERIAL_CONTENT = 1,
    MOO_UI_HOST_COCOA_MATERIAL_SIDEBAR = 2,
    MOO_UI_HOST_COCOA_MATERIAL_HUD = 3,
    MOO_UI_HOST_COCOA_MATERIAL_MENU = 4
} MooUiHostCocoaMaterial;

typedef enum {
    MOO_UI_HOST_COCOA_BLEND_BEHIND_WINDOW = 0,
    MOO_UI_HOST_COCOA_BLEND_WITHIN_WINDOW = 1
} MooUiHostCocoaBlending;

typedef enum {
    MOO_UI_HOST_COCOA_FALLBACK_NONE = 0,
    MOO_UI_HOST_COCOA_FALLBACK_PORTABLE_CPU = 1,
    MOO_UI_HOST_COCOA_FALLBACK_OPAQUE_WINDOW = 2
} MooUiHostCocoaFallback;

typedef struct {
    uint32_t version;
    uint32_t material;
    MooUiHostCocoaWindow window;
    uint64_t required_host_capabilities;
    uint32_t blending;
    uint32_t transparent_titlebar;
    uint32_t emphasized;
    uint32_t fallback;
    uint32_t reserved;
} MooUiHostCocoaRequest;

typedef struct {
    uint32_t version;
    uint32_t native_state;
    uint64_t available_host_capabilities;
    uint64_t applied_host_capabilities;
    uint64_t missing_host_capabilities;
    uint32_t applied_material;
    uint32_t fallback_used;
    int32_t native_error;
    uint32_t reserved;
} MooUiHostCocoaOutcome;

typedef MooUiHostCocoaResult (*MooUiHostCocoaProbeFn)(
    void *user, uint64_t *out_host_capabilities, int32_t *out_native_error);
typedef MooUiHostCocoaResult (*MooUiHostCocoaApplyFn)(
    void *user, const MooUiHostCocoaRequest *request,
    uint64_t allowed_host_capabilities,
    uint64_t *out_applied_host_capabilities,
    uint32_t *out_applied_material, int32_t *out_native_error);
typedef void (*MooUiHostCocoaResetFn)(void *user);

typedef struct {
    MooUiHostCocoaProbeFn probe;
    MooUiHostCocoaApplyFn apply;
    MooUiHostCocoaResetFn reset;
} MooUiHostCocoaOps;

typedef struct {
    MooUiHostCocoaOps ops;
    void *user;
    uint64_t host_capabilities;
    uint64_t generation;
    uint32_t native_state;
    int32_t last_native_error;
} MooUiHostCocoaState;

MooUiHostCocoaRequest moo_ui_host_effects_cocoa_request_default(void);
MooUiHostCocoaResult moo_ui_host_effects_cocoa_init(
    MooUiHostCocoaState *state, const MooUiHostCocoaOps *ops, void *user);
MooUiHostCocoaResult moo_ui_host_effects_cocoa_init_native(
    MooUiHostCocoaState *state);
MooUiHostCocoaResult moo_ui_host_effects_cocoa_apply(
    MooUiHostCocoaState *state, const MooUiHostCocoaRequest *request,
    MooUiHostCocoaOutcome *outcome);
MooUiHostCocoaResult moo_ui_host_effects_cocoa_mark_lost(
    MooUiHostCocoaState *state, int32_t native_error);
MooUiHostCocoaResult moo_ui_host_effects_cocoa_recover(
    MooUiHostCocoaState *state);
void moo_ui_host_effects_cocoa_shutdown(MooUiHostCocoaState *state);
uint64_t moo_ui_host_effects_cocoa_capabilities(
    const MooUiHostCocoaState *state);
uint64_t moo_ui_host_effects_cocoa_generation(
    const MooUiHostCocoaState *state);

#ifdef __cplusplus
}
#endif
#endif
