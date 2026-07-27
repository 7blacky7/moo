#define _POSIX_C_SOURCE 200809L
#include "moo_ui_pointer_meta.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#if defined(_MSC_VER)
#define MOO_UI_POINTER_TLS __declspec(thread)
#else
#define MOO_UI_POINTER_TLS _Thread_local
#endif

typedef struct {
    uintptr_t handle;
    MooUiPointerMeta meta;
    uint32_t domain;
    uint32_t phase;
    uint32_t button;
    uint32_t bound;
} MooUiPointerSnapshot;

static MOO_UI_POINTER_TLS MooUiPointerSnapshot moo_ui_pointer_snapshot;

static MooUiPointerMeta moo_ui_pointer_neutral(void) {
    MooUiPointerMeta meta;
    memset(&meta, 0, sizeof(meta));
    meta.click_count = 1u;
    return meta;
}

static int moo_ui_pointer_button_index(uint32_t button, size_t *out_index) {
    if (!out_index || button < 1u || button > 3u) return 0;
    *out_index = (size_t)(button - 1u);
    return 1;
}

static int moo_ui_pointer_thresholds_valid(MooUiPointerThresholds thresholds) {
    return thresholds.valid &&
           isfinite(thresholds.min_dx) && isfinite(thresholds.max_dx) &&
           isfinite(thresholds.min_dy) && isfinite(thresholds.max_dy) &&
           thresholds.min_dx <= thresholds.max_dx &&
           thresholds.min_dy <= thresholds.max_dy;
}

static int moo_ui_pointer_can_seed(
    double x,
    double y,
    uint32_t receive_time_valid,
    MooUiPointerThresholds thresholds) {
    return receive_time_valid && isfinite(x) && isfinite(y) &&
           moo_ui_pointer_thresholds_valid(thresholds);
}

static void moo_ui_pointer_seed(
    MooUiPointerTracker *tracker,
    uint32_t button,
    double x,
    double y,
    uint32_t native_time_ms,
    uint64_t receive_time_ms,
    uint32_t receive_time_valid,
    MooUiPointerThresholds thresholds) {
    memset(&tracker->candidate, 0, sizeof(tracker->candidate));
    if (!moo_ui_pointer_can_seed(x, y, receive_time_valid, thresholds)) return;
    tracker->candidate.x = x;
    tracker->candidate.y = y;
    tracker->candidate.receive_time_ms = receive_time_ms;
    tracker->candidate.native_time_ms = native_time_ms;
    tracker->candidate.button = button;
    tracker->candidate.valid = 1u;
}

void moo_ui_pointer_tracker_init(MooUiPointerTracker *tracker) {
    moo_ui_pointer_tracker_reset(tracker);
}

void moo_ui_pointer_tracker_reset(MooUiPointerTracker *tracker) {
    if (tracker) memset(tracker, 0, sizeof(*tracker));
}

MooUiPointerMeta moo_ui_pointer_press(
    MooUiPointerTracker *tracker,
    uint32_t button,
    double x,
    double y,
    uint32_t native_time_ms,
    uint64_t receive_time_ms,
    uint32_t receive_time_valid,
    MooUiPointerThresholds thresholds,
    uint32_t modifiers) {
    const uint64_t native_epoch_ms = UINT64_C(1) << 32;
    MooUiPointerMeta meta = moo_ui_pointer_neutral();
    size_t index;
    int duplicate;
    int matched = 0;

    if (!tracker || !moo_ui_pointer_button_index(button, &index)) return meta;

    meta.modifiers = modifiers & MOO_UI_POINTER_MOD_ALL;
    meta.native_time_ms = native_time_ms;
    meta.valid = 1u;

    duplicate = tracker->active[index].valid != 0u;
    if (duplicate) {
        memset(&tracker->active[index], 0, sizeof(tracker->active[index]));
        memset(&tracker->candidate, 0, sizeof(tracker->candidate));
    }

    if (!duplicate && tracker->candidate.valid &&
        tracker->candidate.button == button &&
        moo_ui_pointer_can_seed(x, y, receive_time_valid, thresholds) &&
        receive_time_ms >= tracker->candidate.receive_time_ms &&
        receive_time_ms - tracker->candidate.receive_time_ms < native_epoch_ms) {
        uint32_t native_delta =
            native_time_ms - tracker->candidate.native_time_ms;
        double dx = x - tracker->candidate.x;
        double dy = y - tracker->candidate.y;
        if (native_delta <= thresholds.interval_ms &&
            dx >= thresholds.min_dx && dx <= thresholds.max_dx &&
            dy >= thresholds.min_dy && dy <= thresholds.max_dy) {
            matched = 1;
        }
    }

    if (matched) {
        meta.click_count = 2u;
        memset(&tracker->candidate, 0, sizeof(tracker->candidate));
    } else {
        moo_ui_pointer_seed(tracker, button, x, y, native_time_ms,
                            receive_time_ms, receive_time_valid, thresholds);
    }

    tracker->active[index].meta = meta;
    tracker->active[index].valid = 1u;
    return meta;
}

MooUiPointerMeta moo_ui_pointer_release(
    MooUiPointerTracker *tracker,
    uint32_t button) {
    MooUiPointerMeta meta = moo_ui_pointer_neutral();
    size_t index;

    if (!tracker || !moo_ui_pointer_button_index(button, &index) ||
        !tracker->active[index].valid) {
        return meta;
    }

    meta = tracker->active[index].meta;
    memset(&tracker->active[index], 0, sizeof(tracker->active[index]));
    return meta;
}

int moo_ui_pointer_monotonic_ms(uint64_t *out_ms) {
    if (!out_ms) return 0;
#if defined(_WIN32)
    *out_ms = (uint64_t)GetTickCount64();
    return 1;
#else
    {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
            now.tv_sec < 0 || now.tv_nsec < 0) {
            return 0;
        }
        *out_ms = (uint64_t)now.tv_sec * UINT64_C(1000) +
                  (uint64_t)now.tv_nsec / UINT64_C(1000000);
        return 1;
    }
#endif
}

MooUiPointerThresholds moo_ui_pointer_linux_thresholds(void) {
    MooUiPointerThresholds thresholds;
    thresholds.interval_ms = 400u;
    thresholds.min_dx = -5.0;
    thresholds.max_dx = 5.0;
    thresholds.min_dy = -5.0;
    thresholds.max_dy = 5.0;
    thresholds.valid = 1u;
    return thresholds;
}

MooUiPointerThresholds moo_ui_pointer_rectangle_thresholds(
    uint32_t interval_ms,
    uint32_t width,
    uint32_t height) {
    MooUiPointerThresholds thresholds;
    memset(&thresholds, 0, sizeof(thresholds));
    if (!interval_ms || !width || !height) return thresholds;
    thresholds.interval_ms = interval_ms;
    thresholds.min_dx = -(double)(width / 2u);
    thresholds.max_dx = (double)(width - width / 2u - 1u);
    thresholds.min_dy = -(double)(height / 2u);
    thresholds.max_dy = (double)(height - height / 2u - 1u);
    thresholds.valid = 1u;
    return thresholds;
}

uint32_t moo_ui_pointer_modifiers(
    uint32_t shift,
    uint32_t control,
    uint32_t alt,
    uint32_t super_key,
    uint32_t caps_lock,
    uint32_t num_lock,
    uint32_t alt_gr) {
    uint32_t modifiers = 0u;
    if (shift) modifiers |= MOO_INPUT_MOD_SHIFT;
    if (control) modifiers |= MOO_INPUT_MOD_CONTROL;
    if (alt) modifiers |= MOO_INPUT_MOD_ALT;
    if (super_key) modifiers |= MOO_INPUT_MOD_SUPER;
    if (caps_lock) modifiers |= MOO_INPUT_MOD_CAPS_LOCK;
    if (num_lock) modifiers |= MOO_INPUT_MOD_NUM_LOCK;
    if (alt_gr) modifiers |= MOO_INPUT_MOD_ALT_GR | MOO_INPUT_MOD_ALT;
    return modifiers;
}

uint32_t moo_ui_pointer_modifiers_from_state(
    uint32_t state,
    uint32_t shift_mask,
    uint32_t control_mask,
    uint32_t alt_mask,
    uint32_t super_mask,
    uint32_t caps_lock_mask,
    uint32_t num_lock_mask,
    uint32_t alt_gr_mask) {
    return moo_ui_pointer_modifiers(
        state & shift_mask, state & control_mask, state & alt_mask,
        state & super_mask, state & caps_lock_mask,
        state & num_lock_mask, state & alt_gr_mask);
}

static int moo_ui_pointer_domain_valid(uint32_t domain) {
    return domain == MOO_UI_POINTER_DOMAIN_X11 ||
           domain == MOO_UI_POINTER_DOMAIN_WAYLAND ||
           domain == MOO_UI_POINTER_DOMAIN_WIN32;
}

static void moo_ui_pointer_snapshot_reset(void) {
    memset(&moo_ui_pointer_snapshot, 0, sizeof(moo_ui_pointer_snapshot));
}

void moo_ui_pointer_publish(
    uint32_t domain,
    uintptr_t handle,
    uint32_t phase,
    uint32_t button,
    MooUiPointerMeta meta) {
    moo_ui_pointer_snapshot_reset();
    if (!moo_ui_pointer_domain_valid(domain)) return;
    moo_ui_pointer_snapshot.handle = handle;
    moo_ui_pointer_snapshot.meta = meta;
    moo_ui_pointer_snapshot.domain = domain;
    moo_ui_pointer_snapshot.phase = phase;
    moo_ui_pointer_snapshot.button = button;
    moo_ui_pointer_snapshot.bound = 1u;
}

int moo_ui_pointer_take(
    uint32_t domain,
    uintptr_t handle,
    uint32_t phase,
    uint32_t button,
    MooUiPointerMeta *out_meta) {
    if (out_meta) *out_meta = moo_ui_pointer_neutral();
    if (!moo_ui_pointer_domain_valid(domain) ||
        !moo_ui_pointer_snapshot.bound ||
        moo_ui_pointer_snapshot.domain != domain ||
        moo_ui_pointer_snapshot.handle != handle ||
        moo_ui_pointer_snapshot.phase != phase ||
        moo_ui_pointer_snapshot.button != button) {
        return 0;
    }
    if (out_meta) *out_meta = moo_ui_pointer_snapshot.meta;
    moo_ui_pointer_snapshot_reset();
    return 1;
}

void moo_ui_pointer_clear(uint32_t domain, uintptr_t handle) {
    if (moo_ui_pointer_snapshot.bound &&
        moo_ui_pointer_snapshot.domain == domain &&
        moo_ui_pointer_snapshot.handle == handle) {
        moo_ui_pointer_snapshot_reset();
    }
}
