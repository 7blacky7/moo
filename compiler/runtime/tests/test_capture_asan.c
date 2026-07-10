#include "../runtime/moo_capture_internal.h"

#include <stdio.h>
#include <stdlib.h>

static int checks = 0, failures = 0;
static int camera_closes = 0, microphone_closes = 0;
static int fail_camera_open = 0, fail_microphone_open = 0;

/* Nicht beteiligte Domain-Destruktoren fuer den isolierten moo_memory-Link. */
void moo_socket_free(void* p) { (void)p; }
void moo_thread_free(void* p) { (void)p; }
void moo_channel_free(void* p) { (void)p; }
void moo_db_free(void* p) { (void)p; }
void moo_db_stmt_free(void* p) { (void)p; }
void moo_window_free(void* p) { (void)p; }
void moo_web_free(void* p) { (void)p; }
void moo_voxel_free(void* p) { (void)p; }

#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

static MooValue fake_camera_list(void) { return moo_list_new(4); }
static bool fake_camera_open(MooKamera* c, const char* path, int32_t w,
                             int32_t h, double fps, bool exact) {
    (void)path; (void)exact;
    c->backend = malloc(1);
    if (fail_camera_open) {
        snprintf(c->last_error, sizeof(c->last_error), "fake camera open");
        return false;
    }
    c->width = w; c->height = h; c->fps = fps;
    c->state = MOO_CAPTURE_STREAMING;
    return true;
}
static MooValue fake_camera_frame(MooKamera* c, int32_t timeout) {
    (void)c; (void)timeout;
    uint8_t* pixels = malloc(4);
    pixels[0] = 1; pixels[1] = 2; pixels[2] = 3; pixels[3] = 255;
    return moo_frame_new_take(1, 1, pixels);
}
static void fake_camera_close(MooKamera* c) {
    if (c && c->backend) {
        free(c->backend); c->backend = NULL; camera_closes++;
    }
}
static bool fake_microphone_open(MooMikro* m, const char* device,
                                 int32_t rate, int32_t channels) {
    (void)device;
    m->backend = malloc(1);
    if (fail_microphone_open) {
        snprintf(m->last_error, sizeof(m->last_error), "fake microphone open");
        return false;
    }
    m->rate = rate; m->channels = channels; m->state = MOO_CAPTURE_STREAMING;
    return true;
}
static MooValue fake_microphone_read(MooMikro* m, int32_t samples,
                                     int32_t timeout) {
    (void)timeout;
    int32_t shape[1] = { samples };
    MooTensor* tensor = moo_tensor_raw(1, shape);
    if (!tensor) return moo_none();
    for (int32_t i = 0; i < samples; ++i) tensor->data[i] = 0.25f;
    MooValue tv = { MOO_TENSOR, 0 }; moo_val_set_ptr(&tv, tensor);
    MooValue out = moo_dict_new();
    moo_dict_set(out, moo_string_new("daten"), tv);
    moo_dict_set(out, moo_string_new("rate"), moo_number(m->rate));
    return out;
}
static void fake_microphone_close(MooMikro* m) {
    if (m && m->backend) {
        free(m->backend); m->backend = NULL; microphone_closes++;
    }
}

static MooValue none(void) { return moo_none(); }
static MooValue num(double n) { return moo_number(n); }

static void clear_expected_error(void) {
    CHECK(moo_try_check() != 0, "expected throw");
    moo_try_leave();
}

int main(void) {
    MooCaptureBackendOps ops = {
        fake_camera_list, fake_camera_open, fake_camera_frame, fake_camera_close,
        fake_microphone_open, fake_microphone_read, fake_microphone_close
    };
    moo_capture_set_backend_ops_for_tests(&ops);

    MooValue camera = moo_kamera_oeffnen(none(), none(), none(), none());
    CHECK(camera.tag == MOO_KAMERA, "camera handle");
    CHECK(MV_KAMERA(camera)->state == MOO_CAPTURE_STREAMING, "camera streaming");
    MooValue frame = moo_kamera_frame(camera, num(0));
    CHECK(frame.tag == MOO_FRAME, "camera frame");
    moo_release(frame);
    moo_retain(camera);
    moo_release(camera);
    CHECK(camera_closes == 0, "retained camera remains open");
    moo_kamera_schliessen(camera);
    moo_kamera_schliessen(camera);
    CHECK(camera_closes == 1, "camera close idempotent");
    moo_release(camera);
    CHECK(camera_closes == 1, "destructor does not re-close");

    MooValue microphone = moo_mikro_oeffnen(num(44100), num(2), none());
    CHECK(microphone.tag == MOO_MIKRO, "microphone handle");
    MooValue block = moo_mikro_lesen(microphone, num(8), num(0));
    CHECK(block.tag == MOO_DICT, "microphone block");
    moo_release(block);
    moo_release(microphone);
    CHECK(microphone_closes == 1, "microphone destructor closes");

    moo_try_enter();
    MooValue invalid = moo_kamera_frame(num(1), num(0));
    CHECK(invalid.tag == MOO_NONE, "invalid camera returns none");
    clear_expected_error();

    moo_try_enter();
    MooValue bad_timeout = moo_kamera_oeffnen(none(), num(9000), none(), none());
    CHECK(bad_timeout.tag == MOO_NONE, "bounds failure returns none");
    clear_expected_error();

    fail_camera_open = 1;
    moo_try_enter();
    MooValue failed_camera = moo_kamera_oeffnen(none(), none(), none(), none());
    CHECK(failed_camera.tag == MOO_NONE, "failed camera open");
    clear_expected_error();
    CHECK(camera_closes == 2, "partial camera open cleaned once");

    fail_microphone_open = 1;
    moo_try_enter();
    MooValue failed_microphone = moo_mikro_oeffnen(none(), none(), none());
    CHECK(failed_microphone.tag == MOO_NONE, "failed microphone open");
    clear_expected_error();
    CHECK(microphone_closes == 2, "partial microphone open cleaned once");

    moo_capture_reset_backend_ops_for_tests();
    printf("capture checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
