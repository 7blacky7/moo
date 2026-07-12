#ifndef MOO_COMPOSITOR_PROTOCOL_H
#define MOO_COMPOSITOR_PROTOCOL_H

#include <stdint.h>

#define MOO_COMP_PROTOCOL_VERSION 1u
#define MOO_COMP_HANDLE_INVALID UINT64_C(0)
#define MOO_COMP_SCALE_MIN 1u
#define MOO_COMP_SCALE_MAX 4u

typedef uint64_t MooCompHandle;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} MooCompRect;

typedef enum {
    MOO_COMP_OK = 0,
    MOO_COMP_INVALID = 1,
    MOO_COMP_LIMIT = 2,
    MOO_COMP_STALE_HANDLE = 3,
    MOO_COMP_ACCESS = 4,
    MOO_COMP_WRONG_KIND = 5,
    MOO_COMP_BAD_STATE = 6,
    MOO_COMP_BAD_BUFFER = 7,
    MOO_COMP_WOULD_BLOCK = 8,
    MOO_COMP_UNSUPPORTED = 9,
    MOO_COMP_VERSION = 10
} MooCompResult;

typedef enum {
    MOO_COMP_FORMAT_INVALID = 0,
    MOO_COMP_FORMAT_RGBA8888 = 1
} MooCompFormat;

enum {
    MOO_COMP_FEATURE_SURFACES = UINT64_C(1) << 0,
    MOO_COMP_FEATURE_DAMAGE = UINT64_C(1) << 1,
    MOO_COMP_FEATURE_Z_ORDER = UINT64_C(1) << 2,
    MOO_COMP_FEATURE_INTEGER_SCALE = UINT64_C(1) << 3,
    MOO_COMP_FEATURE_OPACITY = UINT64_C(1) << 4,
    MOO_COMP_FEATURE_FRAME_CALLBACK = UINT64_C(1) << 5,
    MOO_COMP_FEATURE_CURSOR = UINT64_C(1) << 6,
    MOO_COMP_FEATURE_CLIPBOARD = UINT64_C(1) << 16,
    MOO_COMP_FEATURE_DRAG_AND_DROP = UINT64_C(1) << 17
};

#define MOO_COMP_FEATURES_V1     (MOO_COMP_FEATURE_SURFACES | MOO_COMP_FEATURE_DAMAGE |      MOO_COMP_FEATURE_Z_ORDER | MOO_COMP_FEATURE_INTEGER_SCALE |      MOO_COMP_FEATURE_OPACITY | MOO_COMP_FEATURE_FRAME_CALLBACK |      MOO_COMP_FEATURE_CURSOR)

typedef enum {
    MOO_COMP_REQUEST_INVALID = 0,
    MOO_COMP_REQUEST_SURFACE_CREATE = 1,
    MOO_COMP_REQUEST_SURFACE_DESTROY = 2,
    MOO_COMP_REQUEST_SURFACE_ATTACH = 3,
    MOO_COMP_REQUEST_SURFACE_DAMAGE_BUFFER = 4,
    MOO_COMP_REQUEST_SURFACE_SET_SCALE = 5,
    MOO_COMP_REQUEST_SURFACE_SET_OPACITY = 6,
    MOO_COMP_REQUEST_SURFACE_FRAME = 7,
    MOO_COMP_REQUEST_SURFACE_COMMIT = 8,
    MOO_COMP_REQUEST_BUFFER_CREATE = 16,
    MOO_COMP_REQUEST_BUFFER_DESTROY = 17,
    MOO_COMP_REQUEST_CURSOR_SET_BUFFER = 24,
    MOO_COMP_REQUEST_CURSOR_HIDE = 25,
    MOO_COMP_REQUEST_CLIPBOARD_SET = 32,
    MOO_COMP_REQUEST_CLIPBOARD_OFFER = 33,
    MOO_COMP_REQUEST_CLIPBOARD_RECEIVE = 34,
    MOO_COMP_REQUEST_DND_BEGIN = 40,
    MOO_COMP_REQUEST_DND_ACCEPT = 41,
    MOO_COMP_REQUEST_DND_DROP = 42,
    MOO_COMP_REQUEST_DND_CANCEL = 43
} MooCompRequestOpcode;

typedef enum {
    MOO_COMP_EVENT_INVALID = 0,
    MOO_COMP_EVENT_BUFFER_RELEASE = 1,
    MOO_COMP_EVENT_FRAME_DONE = 2
} MooCompEventType;

typedef enum {
    MOO_COMP_FRAME_PRESENTED = 1,
    MOO_COMP_FRAME_SUPERSEDED = 2,
    MOO_COMP_FRAME_OCCLUDED = 3,
    MOO_COMP_FRAME_CANCELLED = 4
} MooCompFrameStatus;

/*
 * These fixed-width records are protocol values, not a wire encoding.
 * A future IPC codec must encode every field explicitly; it must never copy
 * this C representation or any union padding onto the wire.
 */
typedef struct {
    uint32_t version;
    uint32_t opcode;
    uint32_t byte_length;
    uint32_t reserved;
    uint64_t serial;
} MooCompMessageHeader;

typedef struct {
    MooCompHandle surface;
    MooCompHandle buffer;
} MooCompRequestAttach;

typedef struct {
    MooCompHandle surface;
    MooCompRect rect;
} MooCompRequestDamage;

typedef struct {
    MooCompHandle surface;
    uint32_t value;
    uint32_t reserved;
} MooCompRequestValue;

typedef struct {
    MooCompHandle surface;
    uint64_t token;
} MooCompRequestFrame;

typedef struct {
    MooCompHandle buffer;
    int32_t hotspot_x;
    int32_t hotspot_y;
    uint32_t scale;
    uint32_t reserved;
} MooCompRequestCursor;

typedef struct {
    MooCompMessageHeader header;
    union {
        MooCompRequestAttach attach;
        MooCompRequestDamage damage;
        MooCompRequestValue value;
        MooCompRequestFrame frame;
        MooCompRequestCursor cursor;
        uint64_t reserved_words[8];
    } payload;
} MooCompRequest;

typedef struct {
    uint32_t type;
    uint32_t status;
    MooCompHandle object;
    uint64_t token;
    uint64_t present_sequence;
    uint64_t timestamp_ns;
} MooCompEvent;

#endif
