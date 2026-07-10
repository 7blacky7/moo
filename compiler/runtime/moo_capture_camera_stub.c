#include "moo_capture_internal.h"

static MooValue unavailable(void) {
    MooValue error = moo_error("Kamera-Capture ist in diesem Build nicht verfuegbar (Linux + libv4l2/libv4lconvert Development-Pakete erforderlich)");
    moo_throw(error);
    return moo_none();
}

MooValue moo_capture_camera_list_native(void) { return unavailable(); }
bool moo_capture_camera_open_native(MooKamera* camera, const char* path,
                                    int32_t width, int32_t height, double fps,
                                    bool require_exact) {
    (void)camera; (void)path; (void)width; (void)height; (void)fps;
    (void)require_exact;
    unavailable();
    return false;
}
MooValue moo_capture_camera_frame_native(MooKamera* camera, int32_t timeout_ms) {
    (void)camera; (void)timeout_ms;
    return unavailable();
}
void moo_capture_camera_close_native(MooKamera* camera) {
    if (camera) camera->backend = NULL;
}
