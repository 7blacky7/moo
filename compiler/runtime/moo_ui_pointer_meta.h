#ifndef MOO_UI_POINTER_META_H
#define MOO_UI_POINTER_META_H

#include "moo_input_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MOO_UI_POINTER_DOMAIN_NONE = 0u,
    MOO_UI_POINTER_DOMAIN_X11 = 1u,
    MOO_UI_POINTER_DOMAIN_WAYLAND = 2u,
    MOO_UI_POINTER_DOMAIN_WIN32 = 3u
};

enum {
    MOO_UI_POINTER_PHASE_NONE = 0u,
    MOO_UI_POINTER_PHASE_PRESS = 1u,
    MOO_UI_POINTER_PHASE_RELEASE = 2u
};

#define MOO_UI_POINTER_MOD_ALL \
    (MOO_INPUT_MOD_SHIFT | MOO_INPUT_MOD_CONTROL | MOO_INPUT_MOD_ALT | \
     MOO_INPUT_MOD_SUPER | MOO_INPUT_MOD_CAPS_LOCK | \
     MOO_INPUT_MOD_NUM_LOCK | MOO_INPUT_MOD_ALT_GR)

typedef struct {
    uint32_t modifiers;
    uint32_t native_time_ms;
    uint32_t click_count;
    uint32_t valid;
} MooUiPointerMeta;

typedef struct {
    uint32_t interval_ms;
    double min_dx;
    double max_dx;
    double min_dy;
    double max_dy;
    uint32_t valid;
} MooUiPointerThresholds;

typedef struct {
    double x;
    double y;
    uint64_t receive_time_ms;
    uint32_t native_time_ms;
    uint32_t button;
    uint32_t valid;
} MooUiPointerCandidate;

typedef struct {
    MooUiPointerMeta meta;
    uint32_t valid;
} MooUiPointerActivePress;

typedef struct {
    MooUiPointerCandidate candidate;
    MooUiPointerActivePress active[3];
} MooUiPointerTracker;

void moo_ui_pointer_tracker_init(MooUiPointerTracker *tracker);
void moo_ui_pointer_tracker_reset(MooUiPointerTracker *tracker);

MooUiPointerMeta moo_ui_pointer_press(
    MooUiPointerTracker *tracker,
    uint32_t button,
    double x,
    double y,
    uint32_t native_time_ms,
    uint64_t receive_time_ms,
    uint32_t receive_time_valid,
    MooUiPointerThresholds thresholds,
    uint32_t modifiers);

MooUiPointerMeta moo_ui_pointer_release(
    MooUiPointerTracker *tracker,
    uint32_t button);

int moo_ui_pointer_monotonic_ms(uint64_t *out_ms);
MooUiPointerThresholds moo_ui_pointer_linux_thresholds(void);
MooUiPointerThresholds moo_ui_pointer_rectangle_thresholds(
    uint32_t interval_ms,
    uint32_t width,
    uint32_t height);

uint32_t moo_ui_pointer_modifiers(
    uint32_t shift,
    uint32_t control,
    uint32_t alt,
    uint32_t super_key,
    uint32_t caps_lock,
    uint32_t num_lock,
    uint32_t alt_gr);

uint32_t moo_ui_pointer_modifiers_from_state(
    uint32_t state,
    uint32_t shift_mask,
    uint32_t control_mask,
    uint32_t alt_mask,
    uint32_t super_mask,
    uint32_t caps_lock_mask,
    uint32_t num_lock_mask,
    uint32_t alt_gr_mask);

void moo_ui_pointer_publish(
    uint32_t domain,
    uintptr_t handle,
    uint32_t phase,
    uint32_t button,
    MooUiPointerMeta meta);

int moo_ui_pointer_take(
    uint32_t domain,
    uintptr_t handle,
    uint32_t phase,
    uint32_t button,
    MooUiPointerMeta *out_meta);

void moo_ui_pointer_clear(uint32_t domain, uintptr_t handle);

#ifdef __cplusplus
}
#endif
#endif
