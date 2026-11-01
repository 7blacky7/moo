#ifndef MOO_STRUCT_TYPED_H
#define MOO_STRUCT_TYPED_H

#include "moo_struct_value.h"
#include "moo_buffer.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version 1: scalar IDs parallel base->fields. Descriptor, base, fields and
 * names must outlive every box; base pointer is the nominal identity token.
 * This first contract accepts unpacked i32 leaves only. */
#define MOO_STRUCT_TYPED_VERSION 1u
typedef struct {
    uint32_t version;
    const MooStructDescriptor *base;
    size_t field_count;
    const MooBufferScalarType *field_types;
} MooStructTypedDescriptor;

MooStructStatus moo_struct_typed_i32_new(const MooStructTypedDescriptor *type,
                                         const void *source, MooValue *out);
MooStructStatus moo_struct_typed_i32_get(MooValue value,
                                         const MooStructTypedDescriptor *type,
                                         size_t field, int32_t *out);
MooStructStatus moo_struct_typed_i32_set(MooValue value,
                                         const MooStructTypedDescriptor *type,
                                         size_t field, int32_t input);

#ifdef __cplusplus
}
#endif
#endif
