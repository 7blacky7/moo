#include "moo_ui_host_parity.h"

#if defined(__APPLE__)
static MooUiHostParityResult moo_parity_cocoa_probe(
    void *user, uint32_t domain, MooUiHostParityMeasurement *measurement) {
    (void)user;
    (void)domain;
    if (measurement == 0) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    /* AppKit availability is not native parity evidence.  Keep every domain
     * unsupported until an actual macOS conformance runner supplies samples. */
    measurement->evidence =
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED;
    measurement->sample_count = 0u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}
#endif

MooUiHostParityResult moo_ui_host_parity_init_cocoa(
    MooUiHostParityState *state) {
#if defined(__APPLE__)
    MooUiHostParityOps ops;
    ops.probe = moo_parity_cocoa_probe;
    ops.reset = 0;
    return moo_ui_host_parity_init(state, &ops, 0);
#else
    (void)state;
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#endif
}
