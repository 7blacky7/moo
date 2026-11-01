#ifndef MOO_STRUCT_BOX_H
#define MOO_STRUCT_BOX_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Descriptor and field names must remain valid for every box using them.
 * Only initialized, trivially copyable fields are supported; ownership-bearing
 * fields require a later descriptor contract. Descriptors must list padding-free
 * scalar leaves: nested aggregates and arrays containing padding must be flattened.
 * The caller guarantees this invariant. Offsets are target ABI offsets. */
typedef struct {
    const char* name;
    size_t offset;
    size_t size;
} MooStructField;

typedef struct {
    const char* name;
    size_t size;
    size_t alignment;
    size_t field_count;
    const MooStructField* fields;
} MooStructDescriptor;

typedef struct MooStructBox MooStructBox;
typedef enum {
    MOO_STRUCT_OK = 0, MOO_STRUCT_INVALID, MOO_STRUCT_OVERFLOW,
    MOO_STRUCT_ALLOC, MOO_STRUCT_TYPE, MOO_STRUCT_FIELD
} MooStructStatus;

/* Copies initialized fields into owned storage and zeroes padding. */
MooStructStatus moo_struct_box_new(const MooStructDescriptor* type, const void* source, MooStructBox** out);
MooStructStatus moo_struct_box_retain(MooStructBox* box);
void moo_struct_box_drop(MooStructBox* box);
void moo_struct_box_free(void* box);
/* Descriptors are identity tokens. Output padding is zeroed. */
MooStructStatus moo_struct_box_copy(const MooStructBox* box, const MooStructDescriptor* type, void* out);
MooStructStatus moo_struct_box_get(const MooStructBox* box, const char* field, void* out, size_t size);
MooStructStatus moo_struct_box_set(MooStructBox* box, const char* field, const void* value, size_t size);

#ifdef __cplusplus
}
#endif
#endif
