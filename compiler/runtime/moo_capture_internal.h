#ifndef MOO_CAPTURE_INTERNAL_H
#define MOO_CAPTURE_INTERNAL_H

#include "moo_runtime.h"

#define MOO_CAPTURE_MAX_WIDTH 8192
#define MOO_CAPTURE_MAX_HEIGHT 8192
#define MOO_CAPTURE_MAX_FRAME_BYTES ((size_t)256 * 1024 * 1024)
#define MOO_CAPTURE_MAX_BUFFERS 16
#define MOO_CAPTURE_MAX_AUDIO_SAMPLES 16777216
#define MOO_CAPTURE_MAX_TIMEOUT_MS 60000
#define MOO_CAPTURE_DEFAULT_TIMEOUT_MS 1000

typedef enum {
    MOO_CAPTURE_OPEN = 0,
    MOO_CAPTURE_STREAMING = 1,
    MOO_CAPTURE_BROKEN = 2,
    MOO_CAPTURE_CLOSED = 3
} MooCaptureState;

struct MooKamera {
    int32_t refcount;
    MooCaptureState state;
    void* backend;
    int32_t width;
    int32_t height;
    double fps;
    char last_error[256];
};

struct MooMikro {
    int32_t refcount;
    MooCaptureState state;
    void* backend;
    int32_t rate;
    int32_t channels;
    int32_t period_frames;
    int32_t buffer_frames;
    char last_error[256];
};

/* Plattformadapter. Fehler werden als eigener MOO_ERROR vor Cleanup erzeugt. */
MooValue moo_capture_camera_list_native(void);
bool moo_capture_camera_open_native(MooKamera* camera, const char* path,
                                    int32_t width, int32_t height, double fps,
                                    bool require_exact);
MooValue moo_capture_camera_frame_native(MooKamera* camera, int32_t timeout_ms);
void moo_capture_camera_close_native(MooKamera* camera);

bool moo_capture_microphone_open_native(MooMikro* microphone, const char* device,
                                        int32_t rate, int32_t channels);
MooValue moo_capture_microphone_read_native(MooMikro* microphone,
                                            int32_t samples, int32_t timeout_ms);
void moo_capture_microphone_close_native(MooMikro* microphone);

typedef struct {
    MooValue (*camera_list)(void);
    bool (*camera_open)(MooKamera*, const char*, int32_t, int32_t, double, bool);
    MooValue (*camera_frame)(MooKamera*, int32_t);
    void (*camera_close)(MooKamera*);
    bool (*microphone_open)(MooMikro*, const char*, int32_t, int32_t);
    MooValue (*microphone_read)(MooMikro*, int32_t, int32_t);
    void (*microphone_close)(MooMikro*);
} MooCaptureBackendOps;

/* Nur fuer deterministische C1-Gates; kein oeffentliches Moo-Builtin. */
void moo_capture_set_backend_ops_for_tests(const MooCaptureBackendOps* ops);
void moo_capture_reset_backend_ops_for_tests(void);

#endif
