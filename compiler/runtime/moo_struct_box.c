#include "moo_struct_box.h"
#include "moo_buffer.h"
#include "moo_struct_value.h"
#include "moo_struct_typed.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct MooStructBox {
    int32_t refcount; /* Generic MooValue heap release requires first field. */
    const MooStructDescriptor* type;
    unsigned char* data;
};

static int moo_struct_valid(const MooStructDescriptor* t) {
    if (!t || !t->name || !t->size || !t->alignment ||
        (t->alignment & (t->alignment - 1)) ||
        t->alignment > MOO_MAX_ALIGN || t->size % t->alignment ||
        (t->field_count && !t->fields)) return 0;
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->fields[i];
        if (!f->name || !f->size || f->offset > t->size ||
            f->size > t->size - f->offset) return 0;
        for (size_t j = 0; j < i; ++j) {
            const MooStructField* g = &t->fields[j];
            if (strcmp(f->name, g->name) == 0 ||
                (f->offset < g->offset + g->size && g->offset < f->offset + f->size))
                return 0;
        }
    }
    return 1;
}


/* Typed metadata is kept separate so the original C descriptor ABI stays stable. */
static int moo_struct_typed_i32_valid(const MooStructTypedDescriptor* t) {
    if (!t || t->version != MOO_STRUCT_TYPED_VERSION || !moo_struct_valid(t->base) ||
        t->field_count == 0 || t->field_count != t->base->field_count ||
        !t->field_types || t->base->alignment < _Alignof(int32_t)) return 0;
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->base->fields[i];
        if (t->field_types[i] != MOO_BUFFER_I32 || f->size != sizeof(int32_t) ||
            f->offset % _Alignof(int32_t)) return 0;
    }
    return 1;
}

MooStructStatus moo_struct_typed_i32_new(const MooStructTypedDescriptor* type,
                                         const void* source, MooValue* out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = (MooValue){MOO_NONE, 0};
    if (!moo_struct_typed_i32_valid(type)) return MOO_STRUCT_INVALID;
    return moo_struct_value_new(type->base, source, out);
}

MooStructStatus moo_struct_typed_i32_get(MooValue value, const MooStructTypedDescriptor* type,
                                         size_t field, int32_t* out) {
    if (!out || !moo_struct_typed_i32_valid(type)) return MOO_STRUCT_INVALID;
    if (field >= type->field_count) return MOO_STRUCT_FIELD;
    MooStructBox* box = NULL;
    MooStructStatus status = moo_struct_value_borrow(value, type->base, &box);
    if (status != MOO_STRUCT_OK) return status;
    return moo_struct_box_get(box, type->base->fields[field].name, out, sizeof(*out));
}

MooStructStatus moo_struct_typed_i32_set(MooValue value, const MooStructTypedDescriptor* type,
                                         size_t field, int32_t input) {
    if (!moo_struct_typed_i32_valid(type)) return MOO_STRUCT_INVALID;
    if (field >= type->field_count) return MOO_STRUCT_FIELD;
    MooStructBox* box = NULL;
    MooStructStatus status = moo_struct_value_borrow(value, type->base, &box);
    if (status != MOO_STRUCT_OK) return status;
    return moo_struct_box_set(box, type->base->fields[field].name, &input, sizeof(input));
}

static const MooStructField* moo_struct_field(const MooStructBox* b, const char* name) {
    for (size_t i = 0; i < b->type->field_count; ++i)
        if (strcmp(b->type->fields[i].name, name) == 0) return &b->type->fields[i];
    return NULL;
}

MooStructStatus moo_struct_box_new(const MooStructDescriptor* t, const void* src, MooStructBox** out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = NULL;
    if (!src || !moo_struct_valid(t)) return MOO_STRUCT_INVALID;
    MooStructBox* b = malloc(sizeof(*b));
    if (!b) return MOO_STRUCT_ALLOC;
    b->data = calloc(1, t->size);
    if (!b->data) { free(b); return MOO_STRUCT_ALLOC; }
    b->refcount = 1; b->type = t;
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->fields[i];
        memcpy(b->data + f->offset, (const unsigned char*)src + f->offset, f->size);
    }
    *out = b;
    return MOO_STRUCT_OK;
}


MooStructStatus moo_struct_value_new(const MooStructDescriptor* type, const void* source, MooValue* out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = (MooValue){MOO_NONE, 0};
    MooStructBox* box = NULL;
    MooStructStatus status = moo_struct_box_new(type, source, &box);
    if (status != MOO_STRUCT_OK) return status;
    out->tag = MOO_STRUCT;
    moo_val_set_ptr(out, box);
    return MOO_STRUCT_OK;
}

MooStructStatus moo_struct_value_borrow(MooValue value, const MooStructDescriptor* type, MooStructBox** out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = NULL;
    if (!type || value.tag != MOO_STRUCT || !moo_val_as_ptr(value)) return MOO_STRUCT_INVALID;
    MooStructBox* box = moo_val_as_ptr(value);
    if (box->type != type) return MOO_STRUCT_TYPE;
    *out = box;
    return MOO_STRUCT_OK;
}

MooStructStatus moo_struct_box_retain(MooStructBox* b) {
    if (!b) return MOO_STRUCT_INVALID;
    if (b->refcount == INT32_MAX) return MOO_STRUCT_OVERFLOW;
    ++b->refcount;
    return MOO_STRUCT_OK;
}
void moo_struct_box_free(void* ptr) {
    MooStructBox* b = ptr;
    if (!b) return;
    free(b->data);
    free(b);
}
void moo_struct_box_drop(MooStructBox* b) {
    if (b && --b->refcount == 0) moo_struct_box_free(b);
}
MooStructStatus moo_struct_box_copy(const MooStructBox* b, const MooStructDescriptor* t, void* out) {
    if (!b || !t || !out) return MOO_STRUCT_INVALID;
    if (b->type != t) return MOO_STRUCT_TYPE;
    memset(out, 0, t->size);
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->fields[i];
        memcpy((unsigned char*)out + f->offset, b->data + f->offset, f->size);
    }
    return MOO_STRUCT_OK;
}
MooStructStatus moo_struct_box_get(const MooStructBox* b, const char* field, void* out, size_t size) {
    if (!b || !field || !out) return MOO_STRUCT_INVALID;
    const MooStructField* f = moo_struct_field(b, field);
    if (!f || f->size != size) return MOO_STRUCT_FIELD;
    memcpy(out, b->data + f->offset, size);
    return MOO_STRUCT_OK;
}
MooStructStatus moo_struct_box_set(MooStructBox* b, const char* field, const void* value, size_t size) {
    if (!b || !field || !value) return MOO_STRUCT_INVALID;
    const MooStructField* f = moo_struct_field(b, field);
    if (!f || f->size != size) return MOO_STRUCT_FIELD;
    memmove(b->data + f->offset, value, size);
    return MOO_STRUCT_OK;
}
