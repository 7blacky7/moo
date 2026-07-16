#include "moo_ui_host_effects_cocoa.h"

#if defined(__APPLE__) && defined(__OBJC__)
#import <AppKit/AppKit.h>
#endif

static void moo_cocoa_zero(void *destination, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)destination;
    uint32_t index;
    for (index = 0u; index < size; ++index) bytes[index] = 0u;
}

static uint32_t moo_cocoa_request_valid(
    const MooUiHostCocoaRequest *request) {
    return request != 0 &&
        request->version == MOO_UI_HOST_EFFECTS_COCOA_VERSION &&
        request->window != MOO_UI_HOST_COCOA_WINDOW_INVALID &&
        request->material <= (uint32_t)MOO_UI_HOST_COCOA_MATERIAL_MENU &&
        request->blending <= (uint32_t)MOO_UI_HOST_COCOA_BLEND_WITHIN_WINDOW &&
        request->transparent_titlebar <= 1u && request->emphasized <= 1u &&
        request->fallback <= (uint32_t)MOO_UI_HOST_COCOA_FALLBACK_OPAQUE_WINDOW &&
        request->reserved == 0u &&
        (request->required_host_capabilities &
         ~(uint64_t)MOO_UI_HOST_COCOA_CAP_ALL) == UINT64_C(0);
}

static uint64_t moo_cocoa_requested_capabilities(
    const MooUiHostCocoaRequest *request) {
    uint64_t capabilities = request->required_host_capabilities;
    if (request->transparent_titlebar != 0u)
        capabilities |= MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR;
    if (request->material != (uint32_t)MOO_UI_HOST_COCOA_MATERIAL_NONE)
        capabilities |= MOO_UI_HOST_COCOA_CAP_VIBRANCY |
            MOO_UI_HOST_COCOA_CAP_REDUCE_TRANSPARENCY_AWARE;
    capabilities |= request->blending ==
            (uint32_t)MOO_UI_HOST_COCOA_BLEND_WITHIN_WINDOW
        ? MOO_UI_HOST_COCOA_CAP_WITHIN_WINDOW
        : MOO_UI_HOST_COCOA_CAP_BEHIND_WINDOW;
    return capabilities;
}

static MooUiHostCocoaResult moo_cocoa_state_result(
    const MooUiHostCocoaState *state) {
    if (state->native_state == (uint32_t)MOO_UI_HOST_COCOA_NATIVE_READY)
        return MOO_UI_HOST_COCOA_RESULT_OK;
    if (state->native_state == (uint32_t)MOO_UI_HOST_COCOA_NATIVE_LOST)
        return MOO_UI_HOST_COCOA_RESULT_LOST;
    if (state->native_state == (uint32_t)MOO_UI_HOST_COCOA_NATIVE_ERROR)
        return MOO_UI_HOST_COCOA_RESULT_SYSTEM_ERROR;
    return MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
}

static void moo_cocoa_outcome_base(
    const MooUiHostCocoaState *state, MooUiHostCocoaOutcome *outcome) {
    moo_cocoa_zero(outcome, (uint32_t)sizeof(*outcome));
    outcome->version = MOO_UI_HOST_EFFECTS_COCOA_VERSION;
    outcome->native_state = state->native_state;
    outcome->available_host_capabilities =
        state->native_state == (uint32_t)MOO_UI_HOST_COCOA_NATIVE_READY
            ? state->host_capabilities : UINT64_C(0);
    outcome->native_error = state->last_native_error;
}

static MooUiHostCocoaResult moo_cocoa_fallback(
    const MooUiHostCocoaRequest *request, MooUiHostCocoaOutcome *outcome,
    MooUiHostCocoaResult native_result, int32_t native_error) {
    outcome->fallback_used = request->fallback;
    outcome->native_error = native_error;
    return request->fallback != (uint32_t)MOO_UI_HOST_COCOA_FALLBACK_NONE
        ? MOO_UI_HOST_COCOA_RESULT_OK : native_result;
}

static MooUiHostCocoaResult moo_cocoa_probe_state(
    MooUiHostCocoaState *state) {
    uint64_t capabilities = UINT64_C(0);
    int32_t native_error = 0;
    MooUiHostCocoaResult result =
        state->ops.probe(state->user, &capabilities, &native_error);
    state->host_capabilities = UINT64_C(0);
    state->last_native_error = native_error;
    if (result == MOO_UI_HOST_COCOA_RESULT_OK) {
        state->host_capabilities =
            capabilities & (uint64_t)MOO_UI_HOST_COCOA_CAP_ALL;
        state->native_state = (uint32_t)MOO_UI_HOST_COCOA_NATIVE_READY;
    } else if (result == MOO_UI_HOST_COCOA_RESULT_LOST) {
        state->native_state = (uint32_t)MOO_UI_HOST_COCOA_NATIVE_LOST;
    } else if (result == MOO_UI_HOST_COCOA_RESULT_SYSTEM_ERROR) {
        state->native_state = (uint32_t)MOO_UI_HOST_COCOA_NATIVE_ERROR;
    } else {
        state->native_state = (uint32_t)MOO_UI_HOST_COCOA_NATIVE_UNAVAILABLE;
        result = MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
    }
    return result;
}

#if defined(__APPLE__) && defined(__OBJC__)
static const NSInteger MOO_COCOA_EFFECT_VIEW_TAG = (NSInteger)0x4d4f4f;

static MooUiHostCocoaResult moo_cocoa_native_probe(
    void *user, uint64_t *out_capabilities, int32_t *out_error) {
    (void)user;
    if (out_capabilities == 0 || out_error == 0)
        return MOO_UI_HOST_COCOA_RESULT_INVALID;
    *out_capabilities = UINT64_C(0);
    *out_error = 0;
    if (NSClassFromString(@"NSVisualEffectView") == Nil)
        return MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
    *out_capabilities = MOO_UI_HOST_COCOA_CAP_ALL;
    return MOO_UI_HOST_COCOA_RESULT_OK;
}

static NSVisualEffectMaterial moo_cocoa_material(uint32_t material) {
    switch (material) {
        case MOO_UI_HOST_COCOA_MATERIAL_SIDEBAR:
            return NSVisualEffectMaterialSidebar;
        case MOO_UI_HOST_COCOA_MATERIAL_HUD:
            return NSVisualEffectMaterialHUDWindow;
        case MOO_UI_HOST_COCOA_MATERIAL_MENU:
            return NSVisualEffectMaterialMenu;
        default:
            return NSVisualEffectMaterialContentBackground;
    }
}

static NSVisualEffectView *moo_cocoa_effect_view(NSView *content) {
    NSView *view;
    for (view in [content subviews])
        if ([view tag] == MOO_COCOA_EFFECT_VIEW_TAG &&
            [view isKindOfClass:[NSVisualEffectView class]])
            return (NSVisualEffectView *)view;
    return nil;
}

static void moo_cocoa_native_clear(NSWindow *window) {
    NSView *content = [window contentView];
    NSVisualEffectView *effect = content == nil ? nil :
        moo_cocoa_effect_view(content);
    if (effect != nil) [effect removeFromSuperview];
    [window setTitlebarAppearsTransparent:NO];
}

static MooUiHostCocoaResult moo_cocoa_native_apply(
    void *user, const MooUiHostCocoaRequest *request,
    uint64_t allowed_capabilities, uint64_t *out_applied,
    uint32_t *out_material, int32_t *out_error) {
    NSWindow *window;
    NSView *content;
    NSVisualEffectView *effect;
    uint64_t applied = UINT64_C(0);
    (void)user;
    if (request == 0 || out_applied == 0 || out_material == 0 ||
        out_error == 0)
        return MOO_UI_HOST_COCOA_RESULT_INVALID;
    *out_applied = UINT64_C(0);
    *out_material = MOO_UI_HOST_COCOA_MATERIAL_NONE;
    *out_error = 0;
    window = (__bridge NSWindow *)(void *)(uintptr_t)request->window;
    if (window == nil) return MOO_UI_HOST_COCOA_RESULT_INVALID;
    content = [window contentView];
    if (content == nil) {
        *out_error = -1;
        return MOO_UI_HOST_COCOA_RESULT_SYSTEM_ERROR;
    }
    if ([[NSWorkspace sharedWorkspace]
            accessibilityDisplayShouldReduceTransparency]) {
        moo_cocoa_native_clear(window);
        return MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
    }
    [window setTitlebarAppearsTransparent:
        request->transparent_titlebar != 0u ? YES : NO];
    if (request->transparent_titlebar != 0u)
        applied |= MOO_UI_HOST_COCOA_CAP_TRANSPARENT_TITLEBAR;
    effect = moo_cocoa_effect_view(content);
    if (request->material == MOO_UI_HOST_COCOA_MATERIAL_NONE) {
        if (effect != nil) [effect removeFromSuperview];
        *out_applied = applied;
        return MOO_UI_HOST_COCOA_RESULT_OK;
    }
    if (effect == nil) {
        effect = [[NSVisualEffectView alloc] initWithFrame:[content bounds]];
        if (effect == nil) {
            moo_cocoa_native_clear(window);
            *out_error = -2;
            return MOO_UI_HOST_COCOA_RESULT_SYSTEM_ERROR;
        }
        [effect setTag:MOO_COCOA_EFFECT_VIEW_TAG];
        [effect setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [content addSubview:effect positioned:NSWindowBelow relativeTo:nil];
#if !__has_feature(objc_arc)
        [effect release];
#endif
    }
    [effect setMaterial:moo_cocoa_material(request->material)];
    [effect setBlendingMode:
        request->blending == MOO_UI_HOST_COCOA_BLEND_WITHIN_WINDOW
            ? NSVisualEffectBlendingModeWithinWindow
            : NSVisualEffectBlendingModeBehindWindow];
    [effect setState:request->emphasized != 0u
        ? NSVisualEffectStateActive : NSVisualEffectStateFollowsWindowActiveState];
    applied |= MOO_UI_HOST_COCOA_CAP_VIBRANCY |
               MOO_UI_HOST_COCOA_CAP_REDUCE_TRANSPARENCY_AWARE;
    applied |= request->blending == MOO_UI_HOST_COCOA_BLEND_WITHIN_WINDOW
        ? MOO_UI_HOST_COCOA_CAP_WITHIN_WINDOW
        : MOO_UI_HOST_COCOA_CAP_BEHIND_WINDOW;
    applied &= allowed_capabilities;
    *out_applied = applied;
    *out_material = request->material;
    return MOO_UI_HOST_COCOA_RESULT_OK;
}

static void moo_cocoa_native_reset(void *user) { (void)user; }
#else
static MooUiHostCocoaResult moo_cocoa_native_probe(
    void *user, uint64_t *out_capabilities, int32_t *out_error) {
    (void)user;
    if (out_capabilities != 0) *out_capabilities = UINT64_C(0);
    if (out_error != 0) *out_error = 0;
    return MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
}

static MooUiHostCocoaResult moo_cocoa_native_apply(
    void *user, const MooUiHostCocoaRequest *request,
    uint64_t allowed_capabilities, uint64_t *out_applied,
    uint32_t *out_material, int32_t *out_error) {
    (void)user; (void)request; (void)allowed_capabilities;
    if (out_applied != 0) *out_applied = UINT64_C(0);
    if (out_material != 0) *out_material = MOO_UI_HOST_COCOA_MATERIAL_NONE;
    if (out_error != 0) *out_error = 0;
    return MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
}

static void moo_cocoa_native_reset(void *user) { (void)user; }
#endif

MooUiHostCocoaRequest moo_ui_host_effects_cocoa_request_default(void) {
    MooUiHostCocoaRequest request;
    moo_cocoa_zero(&request, (uint32_t)sizeof(request));
    request.version = MOO_UI_HOST_EFFECTS_COCOA_VERSION;
    request.fallback = MOO_UI_HOST_COCOA_FALLBACK_PORTABLE_CPU;
    return request;
}

MooUiHostCocoaResult moo_ui_host_effects_cocoa_init(
    MooUiHostCocoaState *state, const MooUiHostCocoaOps *ops, void *user) {
    if (state == 0 || ops == 0 || ops->probe == 0 || ops->apply == 0)
        return MOO_UI_HOST_COCOA_RESULT_INVALID;
    moo_cocoa_zero(state, (uint32_t)sizeof(*state));
    state->ops = *ops;
    state->user = user;
    state->generation = UINT64_C(1);
    return moo_cocoa_probe_state(state);
}

MooUiHostCocoaResult moo_ui_host_effects_cocoa_init_native(
    MooUiHostCocoaState *state) {
    MooUiHostCocoaOps ops;
    ops.probe = moo_cocoa_native_probe;
    ops.apply = moo_cocoa_native_apply;
    ops.reset = moo_cocoa_native_reset;
    return moo_ui_host_effects_cocoa_init(state, &ops, 0);
}

MooUiHostCocoaResult moo_ui_host_effects_cocoa_apply(
    MooUiHostCocoaState *state, const MooUiHostCocoaRequest *request,
    MooUiHostCocoaOutcome *outcome) {
    uint64_t requested;
    uint64_t applied = UINT64_C(0);
    uint32_t material = MOO_UI_HOST_COCOA_MATERIAL_NONE;
    int32_t native_error = 0;
    MooUiHostCocoaResult result;
    if (state == 0 || outcome == 0 || state->ops.probe == 0 ||
        state->ops.apply == 0 || !moo_cocoa_request_valid(request))
        return MOO_UI_HOST_COCOA_RESULT_INVALID;
    moo_cocoa_outcome_base(state, outcome);
    requested = moo_cocoa_requested_capabilities(request);
    outcome->missing_host_capabilities =
        requested & ~outcome->available_host_capabilities;
    result = moo_cocoa_state_result(state);
    if (result != MOO_UI_HOST_COCOA_RESULT_OK ||
        outcome->missing_host_capabilities != UINT64_C(0)) {
        if (result == MOO_UI_HOST_COCOA_RESULT_OK)
            result = MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
        return moo_cocoa_fallback(
            request, outcome, result, state->last_native_error);
    }
    result = state->ops.apply(state->user, request,
        outcome->available_host_capabilities, &applied, &material,
        &native_error);
    applied &= (uint64_t)MOO_UI_HOST_COCOA_CAP_ALL;
    outcome->applied_host_capabilities = applied;
    outcome->applied_material = material;
    outcome->native_error = native_error;
    outcome->missing_host_capabilities = requested & ~applied;
    state->last_native_error = native_error;
    if (result == MOO_UI_HOST_COCOA_RESULT_OK &&
        outcome->missing_host_capabilities == UINT64_C(0))
        return MOO_UI_HOST_COCOA_RESULT_OK;
    if (result == MOO_UI_HOST_COCOA_RESULT_LOST)
        (void)moo_ui_host_effects_cocoa_mark_lost(state, native_error);
    else if (result == MOO_UI_HOST_COCOA_RESULT_SYSTEM_ERROR)
        state->native_state = MOO_UI_HOST_COCOA_NATIVE_ERROR;
    else if (result == MOO_UI_HOST_COCOA_RESULT_OK)
        result = MOO_UI_HOST_COCOA_RESULT_UNAVAILABLE;
    outcome->native_state = state->native_state;
    return moo_cocoa_fallback(request, outcome, result, native_error);
}

MooUiHostCocoaResult moo_ui_host_effects_cocoa_mark_lost(
    MooUiHostCocoaState *state, int32_t native_error) {
    if (state == 0 || state->ops.probe == 0 || state->ops.apply == 0)
        return MOO_UI_HOST_COCOA_RESULT_INVALID;
    state->native_state = MOO_UI_HOST_COCOA_NATIVE_LOST;
    state->host_capabilities = UINT64_C(0);
    state->last_native_error = native_error;
    if (state->generation != UINT64_MAX) state->generation += UINT64_C(1);
    return MOO_UI_HOST_COCOA_RESULT_LOST;
}

MooUiHostCocoaResult moo_ui_host_effects_cocoa_recover(
    MooUiHostCocoaState *state) {
    if (state == 0 || state->ops.probe == 0 || state->ops.apply == 0)
        return MOO_UI_HOST_COCOA_RESULT_INVALID;
    if (state->ops.reset != 0) state->ops.reset(state->user);
    if (state->generation != UINT64_MAX) state->generation += UINT64_C(1);
    state->native_state = MOO_UI_HOST_COCOA_NATIVE_UNINITIALIZED;
    return moo_cocoa_probe_state(state);
}

void moo_ui_host_effects_cocoa_shutdown(MooUiHostCocoaState *state) {
    if (state == 0) return;
    if (state->ops.reset != 0) state->ops.reset(state->user);
    moo_cocoa_zero(state, (uint32_t)sizeof(*state));
}

uint64_t moo_ui_host_effects_cocoa_capabilities(
    const MooUiHostCocoaState *state) {
    if (state == 0 || state->native_state != MOO_UI_HOST_COCOA_NATIVE_READY)
        return UINT64_C(0);
    return state->host_capabilities;
}

uint64_t moo_ui_host_effects_cocoa_generation(
    const MooUiHostCocoaState *state) {
    return state == 0 ? UINT64_C(0) : state->generation;
}
