#include "moo_capture_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const MooCaptureBackendOps native_ops = {
    moo_capture_camera_list_native,
    moo_capture_camera_open_native,
    moo_capture_camera_frame_native,
    moo_capture_camera_close_native,
    moo_capture_microphone_open_native,
    moo_capture_microphone_read_native,
    moo_capture_microphone_close_native,
};
static MooCaptureBackendOps active_ops;
static bool active_ops_initialized = false;

static const MooCaptureBackendOps* capture_ops(void) {
    if (!active_ops_initialized) {
        active_ops = native_ops;
        active_ops_initialized = true;
    }
    return &active_ops;
}
void moo_capture_set_backend_ops_for_tests(const MooCaptureBackendOps* ops) {
    active_ops = ops ? *ops : native_ops;
    active_ops_initialized = true;
}
void moo_capture_reset_backend_ops_for_tests(void) {
    active_ops = native_ops;
    active_ops_initialized = true;
}

static MooValue capture_fail(const char* message) {
    MooValue error = moo_error(message);
    moo_throw(error);
    return moo_none();
}

static bool optional_i32(MooValue value, int32_t fallback, int32_t min,
                         int32_t max, const char* name, int32_t* out,
                         bool* supplied) {
    if (value.tag == MOO_NONE) {
        *out = fallback;
        if (supplied) *supplied = false;
        return true;
    }
    if (value.tag != MOO_NUMBER || !isfinite(MV_NUM(value))) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s muss eine endliche Zahl sein", name);
        capture_fail(msg);
        return false;
    }
    double number = MV_NUM(value);
    if (floor(number) != number || number < min || number > max) {
        char msg[192];
        snprintf(msg, sizeof(msg), "%s muss ganzzahlig zwischen %d und %d liegen",
                 name, min, max);
        capture_fail(msg);
        return false;
    }
    *out = (int32_t)number;
    if (supplied) *supplied = true;
    return true;
}

static bool optional_fps(MooValue value, double fallback, double* out,
                         bool* supplied) {
    if (value.tag == MOO_NONE) {
        *out = fallback;
        *supplied = false;
        return true;
    }
    if (value.tag != MOO_NUMBER || !isfinite(MV_NUM(value)) ||
        MV_NUM(value) <= 0.0 || MV_NUM(value) > 1000.0) {
        capture_fail("kamera_oeffnen: fps muss endlich und groesser 0 sein");
        return false;
    }
    *out = MV_NUM(value);
    *supplied = true;
    return true;
}

static bool optional_string(MooValue value, const char* fallback,
                            const char* name, const char** out) {
    if (value.tag == MOO_NONE) {
        *out = fallback;
        return true;
    }
    if (value.tag != MOO_STRING || !MV_STR(value) || !MV_STR(value)->chars) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s muss Text oder nichts sein", name);
        capture_fail(msg);
        return false;
    }
    *out = MV_STR(value)->chars;
    return true;
}

static bool camera_value(MooValue value, MooKamera** out) {
    if (value.tag != MOO_KAMERA || !moo_val_as_ptr(value)) {
        capture_fail("Kamera: ungueltiger Handle");
        return false;
    }
    *out = MV_KAMERA(value);
    return true;
}

static bool microphone_value(MooValue value, MooMikro** out) {
    if (value.tag != MOO_MIKRO || !moo_val_as_ptr(value)) {
        capture_fail("Mikrofon: ungueltiger Handle");
        return false;
    }
    *out = MV_MIKRO(value);
    return true;
}

MooValue moo_kamera_liste(void) {
    return capture_ops()->camera_list();
}

MooValue moo_kamera_oeffnen(MooValue path, MooValue width, MooValue height,
                            MooValue fps) {
    const char* selected_path = NULL;
    int32_t selected_width = 640;
    int32_t selected_height = 480;
    double selected_fps = 30.0;
    bool width_given = false, height_given = false, fps_given = false;

    if (!optional_string(path, NULL, "kamera_oeffnen: pfad", &selected_path) ||
        !optional_i32(width, 640, 1, MOO_CAPTURE_MAX_WIDTH,
                      "kamera_oeffnen: breite", &selected_width, &width_given) ||
        !optional_i32(height, 480, 1, MOO_CAPTURE_MAX_HEIGHT,
                      "kamera_oeffnen: hoehe", &selected_height, &height_given) ||
        !optional_fps(fps, 30.0, &selected_fps, &fps_given)) {
        return moo_none();
    }

    MooKamera* camera = (MooKamera*)calloc(1, sizeof(MooKamera));
    if (!camera) return capture_fail("kamera_oeffnen: Speicher voll");
    camera->refcount = 1;
    camera->state = MOO_CAPTURE_OPEN;
    bool exact = width_given && height_given && fps_given;
    if (!capture_ops()->camera_open(camera, selected_path, selected_width,
                                        selected_height, selected_fps, exact)) {
        MooValue error = moo_error(camera->last_error[0] ? camera->last_error :
                                   "kamera_oeffnen: unbekannter Backendfehler");
        capture_ops()->camera_close(camera);
        free(camera);
        moo_throw(error);
        return moo_none();
    }
    if (camera->state != MOO_CAPTURE_STREAMING) {
        capture_ops()->camera_close(camera);
        free(camera);
        return capture_fail("kamera_oeffnen: Backend startete keinen Stream");
    }
    MooValue value = { MOO_KAMERA, 0 };
    moo_val_set_ptr(&value, camera);
    return value;
}

MooValue moo_kamera_frame(MooValue handle, MooValue timeout) {
    MooKamera* camera = NULL;
    int32_t timeout_ms = MOO_CAPTURE_DEFAULT_TIMEOUT_MS;
    if (!camera_value(handle, &camera) ||
        !optional_i32(timeout, MOO_CAPTURE_DEFAULT_TIMEOUT_MS, 0,
                      MOO_CAPTURE_MAX_TIMEOUT_MS, "kamera_frame: timeout_ms",
                      &timeout_ms, NULL)) {
        return moo_none();
    }
    if (camera->state == MOO_CAPTURE_CLOSED)
        return capture_fail("kamera_frame: geschlossener Handle");
    if (camera->state == MOO_CAPTURE_BROKEN)
        return capture_fail("kamera_frame: Geraet getrennt oder Handle BROKEN");
    if (camera->state != MOO_CAPTURE_STREAMING)
        return capture_fail("kamera_frame: Kamera streamt nicht");
    return capture_ops()->camera_frame(camera, timeout_ms);
}

MooValue moo_kamera_schliessen(MooValue handle) {
    MooKamera* camera = NULL;
    if (!camera_value(handle, &camera)) return moo_none();
    if (camera->state != MOO_CAPTURE_CLOSED) {
        capture_ops()->camera_close(camera);
        camera->state = MOO_CAPTURE_CLOSED;
    }
    return moo_none();
}

void moo_kamera_free(void* ptr) {
    MooKamera* camera = (MooKamera*)ptr;
    if (!camera) return;
    if (camera->state != MOO_CAPTURE_CLOSED) {
        capture_ops()->camera_close(camera);
        camera->state = MOO_CAPTURE_CLOSED;
    }
    free(camera);
}

MooValue moo_mikro_oeffnen(MooValue rate, MooValue channels, MooValue device) {
    int32_t requested_rate = 48000;
    int32_t requested_channels = 1;
    const char* selected_device = "default";
    if (!optional_i32(rate, 48000, 1, 768000, "mikro_oeffnen: rate",
                      &requested_rate, NULL) ||
        !optional_i32(channels, 1, 1, 2, "mikro_oeffnen: kanaele",
                      &requested_channels, NULL) ||
        !optional_string(device, "default", "mikro_oeffnen: geraet",
                         &selected_device)) {
        return moo_none();
    }

    MooMikro* microphone = (MooMikro*)calloc(1, sizeof(MooMikro));
    if (!microphone) return capture_fail("mikro_oeffnen: Speicher voll");
    microphone->refcount = 1;
    microphone->state = MOO_CAPTURE_OPEN;
    if (!capture_ops()->microphone_open(microphone, selected_device,
                                             requested_rate,
                                             requested_channels)) {
        MooValue error = moo_error(microphone->last_error[0] ? microphone->last_error :
                                   "mikro_oeffnen: unbekannter Backendfehler");
        capture_ops()->microphone_close(microphone);
        free(microphone);
        moo_throw(error);
        return moo_none();
    }
    if (microphone->state != MOO_CAPTURE_STREAMING) {
        capture_ops()->microphone_close(microphone);
        free(microphone);
        return capture_fail("mikro_oeffnen: Backend startete keinen Stream");
    }
    MooValue value = { MOO_MIKRO, 0 };
    moo_val_set_ptr(&value, microphone);
    return value;
}

MooValue moo_mikro_lesen(MooValue handle, MooValue samples, MooValue timeout) {
    MooMikro* microphone = NULL;
    int32_t sample_count = 0;
    int32_t timeout_ms = MOO_CAPTURE_DEFAULT_TIMEOUT_MS;
    if (!microphone_value(handle, &microphone) ||
        !optional_i32(samples, 0, 1, MOO_CAPTURE_MAX_AUDIO_SAMPLES,
                      "mikro_lesen: n_samples", &sample_count, NULL) ||
        !optional_i32(timeout, MOO_CAPTURE_DEFAULT_TIMEOUT_MS, 0,
                      MOO_CAPTURE_MAX_TIMEOUT_MS, "mikro_lesen: timeout_ms",
                      &timeout_ms, NULL)) {
        return moo_none();
    }
    if (microphone->state == MOO_CAPTURE_CLOSED)
        return capture_fail("mikro_lesen: geschlossener Handle");
    if (microphone->state == MOO_CAPTURE_BROKEN)
        return capture_fail("mikro_lesen: Geraet getrennt oder Handle BROKEN");
    if (microphone->state != MOO_CAPTURE_STREAMING)
        return capture_fail("mikro_lesen: Mikrofon streamt nicht");
    return capture_ops()->microphone_read(microphone, sample_count,
                                              timeout_ms);
}

MooValue moo_mikro_schliessen(MooValue handle) {
    MooMikro* microphone = NULL;
    if (!microphone_value(handle, &microphone)) return moo_none();
    if (microphone->state != MOO_CAPTURE_CLOSED) {
        capture_ops()->microphone_close(microphone);
        microphone->state = MOO_CAPTURE_CLOSED;
    }
    return moo_none();
}

void moo_mikro_free(void* ptr) {
    MooMikro* microphone = (MooMikro*)ptr;
    if (!microphone) return;
    if (microphone->state != MOO_CAPTURE_CLOSED) {
        capture_ops()->microphone_close(microphone);
        microphone->state = MOO_CAPTURE_CLOSED;
    }
    free(microphone);
}
