#include "moo_capture_internal.h"

static MooValue unavailable(void) {
    MooValue error = moo_error("Mikrofon-Capture ist in diesem Build nicht verfuegbar (Linux + libasound Development-Paket erforderlich)");
    moo_throw(error);
    return moo_none();
}

bool moo_capture_microphone_open_native(MooMikro* microphone, const char* device,
                                        int32_t rate, int32_t channels) {
    (void)microphone; (void)device; (void)rate; (void)channels;
    unavailable();
    return false;
}
MooValue moo_capture_microphone_read_native(MooMikro* microphone,
                                            int32_t samples, int32_t timeout_ms) {
    (void)microphone; (void)samples; (void)timeout_ms;
    return unavailable();
}
void moo_capture_microphone_close_native(MooMikro* microphone) {
    if (microphone) microphone->backend = NULL;
}
