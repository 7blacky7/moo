#ifndef MOO_CAPTURE_WINDOWS_INTERNAL_H
#define MOO_CAPTURE_WINDOWS_INTERNAL_H
#include "moo_capture_internal.h"
#include <stddef.h>
#include <stdint.h>

#define MOO_WIN_CAPTURE_MAX_DEVICES 1024
#define MOO_WIN_CAPTURE_MAX_RECOVERIES 3

typedef enum {
    MOO_WIN_OK = 0,
    MOO_WIN_EMPTY = 1,
    MOO_WIN_TIMEOUT = 2,
    MOO_WIN_DISCONNECTED = 3,
    MOO_WIN_RECOVERABLE = 4,
    MOO_WIN_ERROR = 5
} MooWinCaptureResult;

typedef struct {
    char id[512];
    char name[256];
} MooWinCameraInfo;

typedef struct {
    uint8_t* bgra;
    size_t bytes;
    int32_t width;
    int32_t height;
    int32_t stride;
    int64_t timestamp_100ns;
} MooWinFramePacket;

typedef struct {
    const float* samples;
    int32_t frames;
    int32_t channels;
    uint32_t flags;
    void* token;
} MooWinAudioPacket;

/* Dünne Systemgrenze. Die Produktionsimplementierung lebt in
 * moo_capture_windows_system.c; Tests injizieren eine vollständige Fake-Tabelle.
 * Jeder erfolgreich gelieferte Frame/Audio-Puffer muss exakt einmal über die
 * passende release-Funktion zurückgegeben werden. */
typedef struct {
    int64_t (*monotonic_ms)(void);
    bool (*startup)(char* error, size_t cap);
    void (*shutdown)(void);
    MooWinCaptureResult (*camera_enumerate)(MooWinCameraInfo* out, int32_t cap,
                                            int32_t* total, char* error, size_t error_cap);
    MooWinCaptureResult (*camera_open)(const char* id, int32_t width, int32_t height,
                                       double fps, bool exact, void** session,
                                       int32_t* actual_width, int32_t* actual_height,
                                       double* actual_fps, int32_t* queue_bound,
                                       char* error, size_t error_cap);
    MooWinCaptureResult (*camera_wait)(void* session, int32_t timeout_ms,
                                      char* error, size_t error_cap);
    MooWinCaptureResult (*camera_next)(void* session, MooWinFramePacket* packet,
                                      char* error, size_t error_cap);
    void (*camera_release)(MooWinFramePacket* packet);
    void (*camera_close)(void* session);
    MooWinCaptureResult (*microphone_open)(const char* id, int32_t rate, int32_t channels,
                                           void** session, int32_t* actual_rate,
                                           int32_t* actual_channels, int32_t* period_frames,
                                           int32_t* buffer_frames, char* error, size_t error_cap);
    MooWinCaptureResult (*microphone_wait)(void* session, int32_t timeout_ms,
                                           char* error, size_t error_cap);
    MooWinCaptureResult (*microphone_next)(void* session, MooWinAudioPacket* packet,
                                           char* error, size_t error_cap);
    void (*microphone_release)(MooWinAudioPacket* packet);
    MooWinCaptureResult (*microphone_recover)(void* session, char* error, size_t error_cap);
    void (*microphone_close)(void* session);
} MooCaptureWindowsOps;

const MooCaptureWindowsOps* moo_capture_windows_system_ops(void);
void moo_capture_windows_set_ops_for_tests(const MooCaptureWindowsOps* ops);
void moo_capture_windows_reset_ops_for_tests(void);
#endif
