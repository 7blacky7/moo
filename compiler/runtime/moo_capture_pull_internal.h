#ifndef MOO_CAPTURE_PULL_INTERNAL_H
#define MOO_CAPTURE_PULL_INTERNAL_H
#include "moo_capture_internal.h"
#include <stddef.h>
#include <stdint.h>

#define MOO_PULL_CAPTURE_MAX_DEVICES 1024
#define MOO_PULL_CAPTURE_MAX_RECOVERIES 3

typedef enum {
    MOO_PULL_OK = 0,
    MOO_PULL_EMPTY = 1,
    MOO_PULL_TIMEOUT = 2,
    MOO_PULL_DISCONNECTED = 3,
    MOO_PULL_RECOVERABLE = 4,
    MOO_PULL_ERROR = 5
} MooPullResult;

typedef struct {
    char id[512];
    char name[256];
} MooPullCameraInfo;

typedef struct {
    uint8_t* bgra;
    size_t bytes;
    int32_t width;
    int32_t height;
    int32_t stride;
    int64_t timestamp_100ns;
} MooPullFramePacket;

typedef struct {
    const float* samples;
    int32_t frames;
    int32_t channels;
    uint32_t flags;
    void* token;
} MooPullAudioPacket;

/* Dünne Systemgrenze. Die Produktionsimplementierungen leben in den
 * target-spezifischen *_system-Dateien; Tests injizieren eine vollständige Fake-Tabelle.
 * Jeder erfolgreich gelieferte Frame/Audio-Puffer muss exakt einmal über die
 * passende release-Funktion zurückgegeben werden. */
typedef struct {
    int64_t (*monotonic_ms)(void);
    bool (*startup)(char* error, size_t cap);
    void (*shutdown)(void);
    MooPullResult (*camera_enumerate)(MooPullCameraInfo* out, int32_t cap,
                                            int32_t* total, char* error, size_t error_cap);
    MooPullResult (*camera_open)(const char* id, int32_t width, int32_t height,
                                       double fps, bool exact, void** session,
                                       int32_t* actual_width, int32_t* actual_height,
                                       double* actual_fps, int32_t* queue_bound,
                                       char* error, size_t error_cap);
    MooPullResult (*camera_wait)(void* session, int32_t timeout_ms,
                                      char* error, size_t error_cap);
    MooPullResult (*camera_next)(void* session, MooPullFramePacket* packet,
                                      char* error, size_t error_cap);
    void (*camera_release)(MooPullFramePacket* packet);
    void (*camera_close)(void* session);
    MooPullResult (*microphone_open)(const char* id, int32_t rate, int32_t channels,
                                           void** session, int32_t* actual_rate,
                                           int32_t* actual_channels, int32_t* period_frames,
                                           int32_t* buffer_frames, char* error, size_t error_cap);
    MooPullResult (*microphone_wait)(void* session, int32_t timeout_ms,
                                           char* error, size_t error_cap);
    MooPullResult (*microphone_next)(void* session, MooPullAudioPacket* packet,
                                           char* error, size_t error_cap);
    void (*microphone_release)(MooPullAudioPacket* packet);
    MooPullResult (*microphone_recover)(void* session, char* error, size_t error_cap);
    void (*microphone_close)(void* session);
} MooCapturePullOps;

const MooCapturePullOps* moo_capture_pull_system_ops(void);
void moo_capture_pull_set_ops_for_tests(const MooCapturePullOps* ops);
void moo_capture_pull_reset_ops_for_tests(void);
#endif
