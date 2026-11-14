#include "moo_buffer.h"
#include "moo_buffer_value.h"

#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t refs;
    size_t views;
    size_t pins;
    size_t elem_size;
    MooBufferScalarType scalar_type;
    uint64_t type_id; /* P2-T48 D: Strukturidentitaet, 0 = keine Struktur */
    size_t len;
    size_t capacity;
    unsigned char* data;
} MooBufferStorage;

struct MooBuffer {
    int32_t refcount; /* moo_retain/moo_release require this as the first field */
    MooBufferStorage* storage;
    size_t start;
    size_t len;
    int is_view;
};

struct MooBufferPin { MooBufferStorage* storage; };

static void moo_buffer_storage_drop(MooBufferStorage* s) {
    if (--s->refs == 0) {
        free(s->data);
        free(s);
    }
}

MooBufferStatus moo_buffer_new(size_t elem_size, size_t elem_align, size_t len, MooBuffer** out) {
    if (!out) return MOO_BUFFER_INVALID;
    *out = NULL;
    if (!elem_size || !elem_align || (elem_align & (elem_align - 1)) ||
        elem_align > MOO_MAX_ALIGN || elem_size % elem_align)
        return MOO_BUFFER_INVALID;
    if (len > SIZE_MAX / elem_size) return MOO_BUFFER_OVERFLOW;
    MooBufferStorage* s = calloc(1, sizeof(*s));
    if (!s) return MOO_BUFFER_ALLOC;
    MooBuffer* b = malloc(sizeof(*b));
    if (!b) { free(s); return MOO_BUFFER_ALLOC; }
    if (len) {
        s->data = calloc(len, elem_size);
        if (!s->data) { free(b); free(s); return MOO_BUFFER_ALLOC; }
    }
    s->refs = 1; s->elem_size = elem_size; s->len = len; s->capacity = len;
    b->refcount = 1; b->storage = s; b->start = 0; b->len = len; b->is_view = 0;
    *out = b;
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_new_scalar(MooBufferScalarType type, size_t len, MooBuffer** out) {
    if (!out) return MOO_BUFFER_INVALID;
    *out = NULL;
    size_t size, align;
    switch (type) {
#define MOO_SCALAR_CASE(id, c_type) case id: size = sizeof(c_type); align = _Alignof(c_type); break
        MOO_SCALAR_CASE(MOO_BUFFER_I8, int8_t);
        MOO_SCALAR_CASE(MOO_BUFFER_U8, uint8_t);
        MOO_SCALAR_CASE(MOO_BUFFER_I16, int16_t);
        MOO_SCALAR_CASE(MOO_BUFFER_U16, uint16_t);
        MOO_SCALAR_CASE(MOO_BUFFER_I32, int32_t);
        MOO_SCALAR_CASE(MOO_BUFFER_U32, uint32_t);
        MOO_SCALAR_CASE(MOO_BUFFER_I64, int64_t);
        MOO_SCALAR_CASE(MOO_BUFFER_U64, uint64_t);
        case MOO_BUFFER_F32:
            if (sizeof(float) != 4 || FLT_RADIX != 2 || FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128)
                return MOO_BUFFER_INVALID;
            size = sizeof(float); align = _Alignof(float); break;
        case MOO_BUFFER_F64:
            if (sizeof(double) != 8 || FLT_RADIX != 2 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024)
                return MOO_BUFFER_INVALID;
            size = sizeof(double); align = _Alignof(double); break;
        default: return MOO_BUFFER_INVALID;
#undef MOO_SCALAR_CASE
    }
    MooBufferStatus status = moo_buffer_new(size, align, len, out);
    if (status == MOO_BUFFER_OK) (*out)->storage->scalar_type = type;
    return status;
}

MooBufferScalarType moo_buffer_scalar_type(const MooBuffer* b) {
    return b ? b->storage->scalar_type : MOO_BUFFER_SCALAR_NONE;
}

MooBufferStatus moo_buffer_new_struct(size_t elem_size, size_t elem_align, uint64_t type_id, size_t len, MooBuffer** out) {
    if (!out) return MOO_BUFFER_INVALID;
    *out = NULL;
    if (!type_id) return MOO_BUFFER_INVALID;
    MooBufferStatus status = moo_buffer_new(elem_size, elem_align, len, out);
    if (status == MOO_BUFFER_OK) (*out)->storage->type_id = type_id;
    return status;
}

uint64_t moo_buffer_struct_id(const MooBuffer* b) {
    return b ? b->storage->type_id : 0;
}

MooBufferStatus moo_buffer_get_scalar(const MooBuffer* b, MooBufferScalarType type, size_t index, void* out) {
    if (!b || !out) return MOO_BUFFER_INVALID;
    if (type == MOO_BUFFER_SCALAR_NONE || b->storage->scalar_type != type) return MOO_BUFFER_TYPE;
    return moo_buffer_get(b, index, out);
}

MooBufferStatus moo_buffer_set_scalar(MooBuffer* b, MooBufferScalarType type, size_t index, const void* value) {
    if (!b || !value) return MOO_BUFFER_INVALID;
    if (type == MOO_BUFFER_SCALAR_NONE || b->storage->scalar_type != type) return MOO_BUFFER_TYPE;
    return moo_buffer_set(b, index, value);
}


MooBufferStatus moo_buffer_value_new(MooBufferScalarType type, size_t len, MooValue* out) {
    if (!out) return MOO_BUFFER_INVALID;
    *out = (MooValue){MOO_NONE, 0};
    MooBuffer* buffer = NULL;
    MooBufferStatus status = moo_buffer_new_scalar(type, len, &buffer);
    if (status != MOO_BUFFER_OK) return status;
    out->tag = MOO_BUFFER;
    moo_val_set_ptr(out, buffer);
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_value_borrow(MooValue value, MooBufferScalarType expected, MooBuffer** out) {
    if (!out) return MOO_BUFFER_INVALID;
    *out = NULL;
    if (value.tag != MOO_BUFFER || !moo_val_as_ptr(value)) return MOO_BUFFER_INVALID;
    MooBuffer* buffer = moo_val_as_ptr(value);
    if (expected == MOO_BUFFER_SCALAR_NONE || moo_buffer_scalar_type(buffer) != expected)
        return MOO_BUFFER_TYPE;
    *out = buffer;
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_value_view(MooValue value, MooBufferScalarType expected,
                                      size_t start, size_t len, MooValue* out) {
    if (!out) return MOO_BUFFER_INVALID;
    *out = (MooValue){MOO_NONE, 0};
    MooBuffer* buffer = NULL;
    MooBufferStatus status = moo_buffer_value_borrow(value, expected, &buffer);
    if (status != MOO_BUFFER_OK) return status;
    MooBuffer* view = NULL;
    status = moo_buffer_view(buffer, start, len, &view);
    if (status != MOO_BUFFER_OK) return status;
    out->tag = MOO_BUFFER;
    moo_val_set_ptr(out, view);
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_view(MooBuffer* b, size_t start, size_t len, MooBuffer** out) {
    if (!b || !out) return MOO_BUFFER_INVALID;
    *out = NULL;
    if (start > b->len || len > b->len - start) return MOO_BUFFER_BOUNDS;
    MooBufferStorage* s = b->storage;
    if (s->refs == SIZE_MAX || s->views == SIZE_MAX) return MOO_BUFFER_OVERFLOW;
    MooBuffer* view = malloc(sizeof(*view));
    if (!view) return MOO_BUFFER_ALLOC;
    view->refcount = 1; view->storage = s; view->start = b->start + start; view->len = len; view->is_view = 1;
    ++s->refs; ++s->views; *out = view;
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_retain(MooBuffer* b) {
    if (!b) return MOO_BUFFER_INVALID;
    if (b->refcount == INT32_MAX) return MOO_BUFFER_OVERFLOW;
    ++b->refcount;
    return MOO_BUFFER_OK;
}

void moo_buffer_free(void* ptr) {
    MooBuffer* b = ptr;
    if (!b) return;
    MooBufferStorage* s = b->storage;
    if (b->is_view) --s->views;
    free(b);
    moo_buffer_storage_drop(s);
}

void moo_buffer_drop(MooBuffer* b) {
    if (b && --b->refcount == 0) moo_buffer_free(b);
}

size_t moo_buffer_len(const MooBuffer* b) { return b ? b->len : 0; }

MooBufferStatus moo_buffer_get(const MooBuffer* b, size_t index, void* out) {
    if (!b || !out) return MOO_BUFFER_INVALID;
    if (index >= b->len) return MOO_BUFFER_BOUNDS;
    memmove(out, b->storage->data + (b->start + index) * b->storage->elem_size, b->storage->elem_size);
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_set(MooBuffer* b, size_t index, const void* value) {
    if (!b || !value) return MOO_BUFFER_INVALID;
    if (index >= b->len) return MOO_BUFFER_BOUNDS;
    memmove(b->storage->data + (b->start + index) * b->storage->elem_size, value, b->storage->elem_size);
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_resize(MooBuffer* b, size_t len) {
    if (!b || b->is_view) return MOO_BUFFER_INVALID;
    MooBufferStorage* s = b->storage;
    if (len == s->len) return MOO_BUFFER_OK;
    if (s->views || s->pins) return MOO_BUFFER_PINNED;
    const size_t max_len = SIZE_MAX / s->elem_size;
    if (len > max_len) return MOO_BUFFER_OVERFLOW;
    if (!len) {
        free(s->data);
        s->data = NULL;
        s->capacity = 0;
    } else if (len > s->capacity) {
        size_t capacity = s->capacity ? s->capacity : 1;
        capacity = capacity > max_len / 2 ? max_len : capacity * 2;
        if (capacity < len) capacity = len;
        unsigned char* data = calloc(capacity, s->elem_size);
        if (!data) return MOO_BUFFER_ALLOC;
        if (s->len) memcpy(data, s->data, s->len * s->elem_size);
        free(s->data);
        s->data = data;
        s->capacity = capacity;
    } else if (len > s->len) {
        memset(s->data + s->len * s->elem_size, 0, (len - s->len) * s->elem_size);
    }
    s->len = len; b->len = len;
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_append_scalar(MooBuffer* b, MooBufferScalarType type, const void* value) {
    if (!b || !value || b->is_view) return MOO_BUFFER_INVALID;
    MooBufferStorage* s = b->storage;
    if (type == MOO_BUFFER_SCALAR_NONE || s->scalar_type != type) return MOO_BUFFER_TYPE;
    if (s->views || s->pins) return MOO_BUFFER_PINNED;
    if (s->len == SIZE_MAX) return MOO_BUFFER_OVERFLOW;
    /* Copy before resize: value may alias storage and growth may free it. */
    unsigned char copy[8];
    if (s->elem_size > sizeof(copy)) return MOO_BUFFER_INVALID;
    memcpy(copy, value, s->elem_size);
    size_t old_len = s->len;
    MooBufferStatus status = moo_buffer_resize(b, old_len + 1);
    if (status != MOO_BUFFER_OK) return status;
    memcpy(s->data + old_len * s->elem_size, copy, s->elem_size);
    return MOO_BUFFER_OK;
}

MooBufferStatus moo_buffer_pin(MooBuffer* b, MooBufferPin** out, void** data) {
    if (!b || !out || !data) return MOO_BUFFER_INVALID;
    *out = NULL; *data = NULL;
    MooBufferStorage* s = b->storage;
    if (s->refs == SIZE_MAX || s->pins == SIZE_MAX) return MOO_BUFFER_OVERFLOW;
    MooBufferPin* pin = malloc(sizeof(*pin));
    if (!pin) return MOO_BUFFER_ALLOC;
    pin->storage = s; ++s->refs; ++s->pins;
    *data = s->data ? s->data + b->start * s->elem_size : NULL;
    *out = pin;
    return MOO_BUFFER_OK;
}

void moo_buffer_unpin(MooBufferPin* pin) {
    if (!pin) return;
    MooBufferStorage* s = pin->storage;
    --s->pins; free(pin); moo_buffer_storage_drop(s);
}
