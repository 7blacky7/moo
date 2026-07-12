#ifndef MOO_INPUT_PROTOCOL_H
#define MOO_INPUT_PROTOCOL_H

#include <stdint.h>

#define MOO_INPUT_PROTOCOL_VERSION 1u
#define MOO_INPUT_HANDLE_INVALID UINT64_C(0)
#define MOO_INPUT_TEXT_CAPACITY 64u
#define MOO_INPUT_LOGICAL_HID_BASE UINT32_C(0x00110000)

/* Fixed-width protocol values only. A wire codec must encode each field
 * explicitly; C layout, padding and host endianness are never wire format. */
typedef uint64_t MooInputHandle;

enum {
    MOO_INPUT_FEATURE_POINTER = UINT64_C(1) << 0,
    MOO_INPUT_FEATURE_TOUCH = UINT64_C(1) << 1,
    MOO_INPUT_FEATURE_STYLUS = UINT64_C(1) << 2,
    MOO_INPUT_FEATURE_KEYBOARD = UINT64_C(1) << 3,
    MOO_INPUT_FEATURE_IME = UINT64_C(1) << 4,
    MOO_INPUT_FEATURE_SHORTCUTS = UINT64_C(1) << 5,
    MOO_INPUT_FEATURE_ACCESSIBILITY = UINT64_C(1) << 6,
    MOO_INPUT_FEATURE_HIGH_CONTRAST = UINT64_C(1) << 7,
    MOO_INPUT_FEATURE_REDUCED_MOTION = UINT64_C(1) << 8,
    MOO_INPUT_FEATURE_AUTOMATION = UINT64_C(1) << 9
};

enum {
    MOO_INPUT_CLIENT_SCREEN_READER = UINT32_C(1) << 0,
    MOO_INPUT_CLIENT_AUTOMATION = UINT32_C(1) << 1
};

typedef enum {
    MOO_INPUT_OK = 0,
    MOO_INPUT_INVALID = 1,
    MOO_INPUT_LIMIT = 2,
    MOO_INPUT_STALE_HANDLE = 3,
    MOO_INPUT_ACCESS = 4,
    MOO_INPUT_BAD_STATE = 5,
    MOO_INPUT_WOULD_BLOCK = 6,
    MOO_INPUT_BAD_UTF8 = 7,
    MOO_INPUT_STALE_SERIAL = 8,
    MOO_INPUT_STALE_REVISION = 9,
    MOO_INPUT_UNSUPPORTED = 10
} MooInputResult;

typedef enum {
    MOO_INPUT_EVENT_INVALID = 0,
    MOO_INPUT_EVENT_FOCUS = 1,
    MOO_INPUT_EVENT_POINTER_ENTER = 2,
    MOO_INPUT_EVENT_POINTER_LEAVE = 3,
    MOO_INPUT_EVENT_POINTER_MOTION = 4,
    MOO_INPUT_EVENT_POINTER_BUTTON = 5,
    MOO_INPUT_EVENT_POINTER_AXIS = 6,
    MOO_INPUT_EVENT_TOUCH = 7,
    MOO_INPUT_EVENT_STYLUS = 8,
    MOO_INPUT_EVENT_KEY = 9,
    MOO_INPUT_EVENT_TEXT_PREEDIT = 10,
    MOO_INPUT_EVENT_TEXT_COMMIT = 11,
    MOO_INPUT_EVENT_TEXT_CANCEL = 12,
    MOO_INPUT_EVENT_A11Y_ACTION = 13,
    MOO_INPUT_EVENT_PREFERENCES = 14,
    MOO_INPUT_EVENT_SHORTCUT = 15
} MooInputEventType;

typedef enum { MOO_INPUT_RELEASED = 0, MOO_INPUT_PRESSED = 1 } MooInputButtonState;
typedef enum {
    MOO_INPUT_TOUCH_DOWN = 1,
    MOO_INPUT_TOUCH_MOVE = 2,
    MOO_INPUT_TOUCH_UP = 3,
    MOO_INPUT_TOUCH_CANCEL = 4
} MooInputTouchPhase;

enum {
    MOO_INPUT_EVENT_SENSITIVE = 1u << 0,
    MOO_INPUT_EVENT_SYNTHETIC = 1u << 1
};

typedef enum {
    MOO_INPUT_TEXT_NONE = 0,
    MOO_INPUT_TEXT_NORMAL = 1,
    MOO_INPUT_TEXT_SENSITIVE = 2
} MooInputTextMode;

enum {
    MOO_INPUT_MOD_SHIFT = 1u << 0,
    MOO_INPUT_MOD_CONTROL = 1u << 1,
    MOO_INPUT_MOD_ALT = 1u << 2,
    MOO_INPUT_MOD_SUPER = 1u << 3,
    MOO_INPUT_MOD_CAPS_LOCK = 1u << 4,
    MOO_INPUT_MOD_NUM_LOCK = 1u << 5,
    MOO_INPUT_MOD_ALT_GR = 1u << 6
};

typedef enum {
    MOO_A11Y_ROLE_NONE = 0,
    MOO_A11Y_ROLE_WINDOW = 1,
    MOO_A11Y_ROLE_GROUP = 2,
    MOO_A11Y_ROLE_TEXT = 3,
    MOO_A11Y_ROLE_BUTTON = 4,
    MOO_A11Y_ROLE_CHECKBOX = 5,
    MOO_A11Y_ROLE_SLIDER = 6,
    MOO_A11Y_ROLE_TEXT_FIELD = 7,
    MOO_A11Y_ROLE_LIST = 8,
    MOO_A11Y_ROLE_LIST_ITEM = 9,
    MOO_A11Y_ROLE_IMAGE = 10
} MooA11yRole;

enum {
    MOO_A11Y_STATE_DISABLED = 1u << 0,
    MOO_A11Y_STATE_FOCUSED = 1u << 1,
    MOO_A11Y_STATE_CHECKED = 1u << 2,
    MOO_A11Y_STATE_SELECTED = 1u << 3,
    MOO_A11Y_STATE_EXPANDED = 1u << 4,
    MOO_A11Y_STATE_PASSWORD = 1u << 5,
    MOO_A11Y_STATE_HIDDEN = 1u << 6
};

enum {
    MOO_A11Y_ACTION_FOCUS = 1u << 0,
    MOO_A11Y_ACTION_ACTIVATE = 1u << 1,
    MOO_A11Y_ACTION_INCREMENT = 1u << 2,
    MOO_A11Y_ACTION_DECREMENT = 1u << 3,
    MOO_A11Y_ACTION_SET_VALUE = 1u << 4,
    MOO_A11Y_ACTION_SET_SELECTION = 1u << 5
};

typedef struct { int32_t x, y, width, height; } MooInputRect;

typedef struct {
    uint32_t length;
    uint8_t bytes[MOO_INPUT_TEXT_CAPACITY];
} MooInputText;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t serial;
    uint64_t timestamp_ns;
    uint64_t focus_epoch;
    MooInputHandle target;
    union {
        struct { uint32_t focused, reason; } focus;
        struct { int32_t x, y; int32_t dx, dy; } pointer;
        struct { uint32_t button, state; int32_t x, y; } button;
        struct { int32_t x_120, y_120; } axis;
        struct { uint32_t id, phase; int32_t x, y; uint32_t pressure; } touch;
        struct { int32_t x, y; uint32_t pressure; int32_t tilt_x, tilt_y; uint32_t buttons; } stylus;
        struct { uint32_t physical, logical, state, modifiers, repeat; } key;
        struct { MooInputText text; uint32_t selection_start, selection_end; uint64_t revision; } text;
        struct { MooInputHandle node; uint32_t action, reserved; int64_t value; } a11y;
        struct { uint32_t high_contrast, reduced_motion; } preferences;
        struct { uint64_t id; uint32_t physical, modifiers; } shortcut;
    } data;
} MooInputEvent;

typedef struct {
    MooInputHandle parent;
    MooInputHandle labelled_by;
    MooInputHandle described_by;
    MooInputHandle controls;
    uint32_t role;
    uint32_t states;
    uint32_t actions;
    MooInputRect bounds;
    MooInputText name;
    MooInputText value;
    MooInputText description;
} MooA11yNodeData;

#endif
