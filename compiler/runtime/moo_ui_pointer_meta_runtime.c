#include "moo_runtime.h"
#include "moo_ui_pointer_meta.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t moo_ui_pointer_runtime_domain(void) {
#if defined(_WIN32)
    return MOO_UI_POINTER_DOMAIN_WIN32;
#elif defined(MOO_HAS_WAYLAND_UI)
    return MOO_UI_POINTER_DOMAIN_WAYLAND;
#elif defined(MOO_HAS_X11_UI)
    return MOO_UI_POINTER_DOMAIN_X11;
#else
    return MOO_UI_POINTER_DOMAIN_NONE;
#endif
}

static int moo_ui_pointer_runtime_u32(
    MooValue value,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t *out_value) {
    double number;
    uint32_t converted;
    if (!out_value || value.tag != MOO_NUMBER) return 0;
    number = MV_NUM(value);
    if (!isfinite(number) || number < (double)minimum ||
        number > (double)maximum) return 0;
    converted = (uint32_t)number;
    if ((double)converted != number) return 0;
    *out_value = converted;
    return 1;
}

static MooValue moo_ui_pointer_runtime_result(
    uint32_t domain,
    MooUiPointerMeta meta) {
    MooValue result = moo_dict_new();
    moo_dict_set(result, moo_string_new("domain"),
                 moo_number((double)domain));
    moo_dict_set(result, moo_string_new("mod"),
                 moo_number((double)meta.modifiers));
    moo_dict_set(result, moo_string_new("zeit"),
                 moo_number((double)meta.native_time_ms));
    moo_dict_set(result, moo_string_new("klickzahl"),
                 moo_number((double)meta.click_count));
    moo_dict_set(result, moo_string_new("gueltig"),
                 moo_bool(meta.valid != 0u));
    return result;
}

MooValue moo_ui_pointer_meta_hole(
    MooValue handle,
    MooValue phase,
    MooValue taste) {
    MooUiPointerMeta meta;
    uint32_t domain = moo_ui_pointer_runtime_domain();
    uint32_t phase_value;
    uint32_t button_value;
    uintptr_t handle_value;
    memset(&meta, 0, sizeof(meta));
    meta.click_count = 1u;
    if (domain == MOO_UI_POINTER_DOMAIN_NONE ||
        handle.tag != MOO_NUMBER || !moo_val_as_ptr(handle) ||
        !moo_ui_pointer_runtime_u32(
            phase, MOO_UI_POINTER_PHASE_PRESS,
            MOO_UI_POINTER_PHASE_RELEASE, &phase_value) ||
        !moo_ui_pointer_runtime_u32(taste, 1u, 3u, &button_value)) {
        return moo_ui_pointer_runtime_result(
            MOO_UI_POINTER_DOMAIN_NONE, meta);
    }
    handle_value = (uintptr_t)moo_val_as_ptr(handle);
    if (!moo_ui_pointer_take(
            domain, handle_value, phase_value, button_value, &meta)) {
        return moo_ui_pointer_runtime_result(
            MOO_UI_POINTER_DOMAIN_NONE, meta);
    }
    return moo_ui_pointer_runtime_result(domain, meta);
}
