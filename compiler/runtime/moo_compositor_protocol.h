#ifndef MOO_COMPOSITOR_PROTOCOL_H
#define MOO_COMPOSITOR_PROTOCOL_H

#include <stdint.h>

#include "moo_compositor_effects_protocol.h"

/* V1 values are permanent. Negotiation selects the highest common version. */
#define MOO_COMP_PROTOCOL_VERSION_V1 1u
#define MOO_COMP_PROTOCOL_VERSION_V2 2u
#define MOO_COMP_PROTOCOL_MIN_VERSION MOO_COMP_PROTOCOL_VERSION_V1
#define MOO_COMP_PROTOCOL_MAX_VERSION MOO_COMP_PROTOCOL_VERSION_V2
#define MOO_COMP_PROTOCOL_CURRENT_VERSION MOO_COMP_PROTOCOL_MAX_VERSION
/* Legacy V1 dispatcher alias; keep until its explicit V1/V2 handshake lands. */
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
    MOO_COMP_FEATURE_CORNER_CLIP = UINT64_C(1) << 7,
    MOO_COMP_FEATURE_SHADOW = UINT64_C(1) << 8,
    MOO_COMP_FEATURE_BACKDROP_BLUR = UINT64_C(1) << 9,
    MOO_COMP_FEATURE_SATURATION = UINT64_C(1) << 10,
    MOO_COMP_FEATURE_TINT = UINT64_C(1) << 11,
    MOO_COMP_FEATURE_NOISE = UINT64_C(1) << 12,
    MOO_COMP_FEATURE_AFFINE_2D = UINT64_C(1) << 13,
    MOO_COMP_FEATURE_ANIMATION = UINT64_C(1) << 14,
    MOO_COMP_FEATURE_CLIPBOARD = UINT64_C(1) << 16,
    MOO_COMP_FEATURE_DRAG_AND_DROP = UINT64_C(1) << 17
};

#define MOO_COMP_FEATURES_V1 \
    (MOO_COMP_FEATURE_SURFACES | MOO_COMP_FEATURE_DAMAGE | \
     MOO_COMP_FEATURE_Z_ORDER | MOO_COMP_FEATURE_INTEGER_SCALE | \
     MOO_COMP_FEATURE_OPACITY | MOO_COMP_FEATURE_FRAME_CALLBACK | \
     MOO_COMP_FEATURE_CURSOR)

#define MOO_COMP_FEATURES_EFFECTS_V2 \
    (MOO_COMP_FEATURE_CORNER_CLIP | MOO_COMP_FEATURE_SHADOW | \
     MOO_COMP_FEATURE_BACKDROP_BLUR | MOO_COMP_FEATURE_SATURATION | \
     MOO_COMP_FEATURE_TINT | MOO_COMP_FEATURE_NOISE | \
     MOO_COMP_FEATURE_AFFINE_2D | MOO_COMP_FEATURE_ANIMATION)

#define MOO_COMP_FEATURES_V2 \
    (MOO_COMP_FEATURES_V1 | MOO_COMP_FEATURES_EFFECTS_V2)

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
    MOO_COMP_REQUEST_DND_CANCEL = 43,
    MOO_COMP_REQUEST_SURFACE_SET_EFFECTS = 44,
    MOO_COMP_REQUEST_SURFACE_ANIMATE_EFFECT = 45,
    MOO_COMP_REQUEST_SURFACE_CANCEL_ANIMATION = 46
} MooCompRequestOpcode;

typedef enum {
    MOO_COMP_EVENT_INVALID = 0,
    MOO_COMP_EVENT_BUFFER_RELEASE = 1,
    MOO_COMP_EVENT_FRAME_DONE = 2,
    MOO_COMP_EVENT_EFFECT_STATUS = 3,
    MOO_COMP_EVENT_ANIMATION_DONE = 4
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

typedef enum {
    MOO_COMP_ANIMATION_DONE_COMPLETED = 1,
    MOO_COMP_ANIMATION_DONE_CANCELLED = 2,
    MOO_COMP_ANIMATION_DONE_REPLACED = 3,
    MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED = 4,
    MOO_COMP_ANIMATION_DONE_REDUCED_MOTION = 5
} MooCompAnimationDoneStatus;

/*
 * Additive V2 messages. MooCompRequest and MooCompEvent above remain the
 * complete, ABI-stable V1 value records and never grow. A V2 codec selects the
 * record by header.opcode, verifies header.version==V2 and the exact
 * byte_length below, then encodes/decodes every nested field in declaration
 * order. It never copies struct bytes or padding.
 */
#define MOO_COMP_REQUEST_SET_EFFECTS_V2_BYTE_LENGTH UINT32_C(128)
#define MOO_COMP_REQUEST_ANIMATE_EFFECT_V2_BYTE_LENGTH UINT32_C(144)
#define MOO_COMP_REQUEST_CANCEL_ANIMATION_V2_BYTE_LENGTH UINT32_C(40)
#define MOO_COMP_EVENT_EFFECT_STATUS_V2_BYTE_LENGTH UINT32_C(232)
#define MOO_COMP_EVENT_ANIMATION_DONE_V2_BYTE_LENGTH UINT32_C(40)

typedef struct {
    MooCompMessageHeader header;
    MooCompHandle surface;
    MooCompEffectState effects;
} MooCompRequestSetEffectsV2;

typedef struct {
    MooCompMessageHeader header;
    MooCompHandle surface;
    MooCompAnimationDesc animation;
} MooCompRequestAnimateEffectV2;

typedef struct {
    MooCompMessageHeader header;
    MooCompHandle surface;
    uint64_t token;
} MooCompRequestCancelAnimationV2;

/*
 * Dedicated V2 event records make degradation and completion observable
 * without changing the 40-byte V1 MooCompEvent. result is a MooCompResult.
 * An accepted animation token produces exactly one AnimationDoneV2 event.
 */
typedef struct {
    uint32_t type;
    uint32_t result;
    MooCompHandle surface;
    uint64_t commit_sequence;
    uint64_t timestamp_ns;
    MooCompEffectStatus effects;
} MooCompEventEffectStatusV2;

typedef struct {
    uint32_t type;
    uint32_t status;
    MooCompHandle surface;
    uint64_t token;
    uint64_t commit_sequence;
    uint64_t timestamp_ns;
} MooCompEventAnimationDoneV2;

#endif
