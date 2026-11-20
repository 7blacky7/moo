#ifndef MOO_BUFFER_H
#define MOO_BUFFER_H

#include <stddef.h>
#include <stdint.h>

/* Groesste von malloc/calloc garantierte Ausrichtung. MSVC kennt max_align_t
 * im C-Modus nicht; dessen malloc liefert 2*sizeof(void*) (x64: 16). */
#if defined(_MSC_VER) && !defined(__clang__)
#define MOO_MAX_ALIGN ((size_t)(2 * sizeof(void*)))
#else
#define MOO_MAX_ALIGN ((size_t)_Alignof(max_align_t))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Thread-confined, trivially copyable elements. Each returned handle is owned.
 * An active view or pin prevents every length change of the shared storage.
 * A pin keeps the storage alive even after all buffer handles are released.
 * Element pointers are borrowed until the corresponding pin is released. */
typedef struct MooBuffer MooBuffer;
typedef struct MooBufferPin MooBufferPin;
typedef enum {
    MOO_BUFFER_OK = 0,
    MOO_BUFFER_BOUNDS,
    MOO_BUFFER_OVERFLOW,
    MOO_BUFFER_ALLOC,
    MOO_BUFFER_PINNED,
    MOO_BUFFER_INVALID,
    MOO_BUFFER_TYPE,
    MOO_BUFFER_ALIGNMENT
} MooBufferStatus;

/* Stable IDs for explicitly supported trivial scalar storage. Values are ABI. */
typedef enum {
    MOO_BUFFER_SCALAR_NONE = 0,
    MOO_BUFFER_I8 = 1, MOO_BUFFER_U8 = 2,
    MOO_BUFFER_I16 = 3, MOO_BUFFER_U16 = 4,
    MOO_BUFFER_I32 = 5, MOO_BUFFER_U32 = 6,
    MOO_BUFFER_I64 = 7, MOO_BUFFER_U64 = 8,
    MOO_BUFFER_F32 = 9, MOO_BUFFER_F64 = 10
} MooBufferScalarType;

/* Typed APIs require exact scalar identity; views retain their storage's ID.
 * Managed elements and structure elements are not supported by this API. */
MooBufferStatus moo_buffer_new_scalar(MooBufferScalarType type, size_t len, MooBuffer** out);
/* NONE means an untyped raw buffer or a null handle. */
MooBufferScalarType moo_buffer_scalar_type(const MooBuffer* buffer);
MooBufferStatus moo_buffer_get_scalar(const MooBuffer* buffer, MooBufferScalarType type, size_t index, void* out);
MooBufferStatus moo_buffer_set_scalar(MooBuffer* buffer, MooBufferScalarType type, size_t index, const void* value);
/* Append copies one scalar. Failure preserves length and all old elements. */
MooBufferStatus moo_buffer_append_scalar(MooBuffer* buffer, MooBufferScalarType type, const void* value);

/* P2-T48 D (Plan a71947f7 D1): Strukturpuffer. type_id != 0 ist die vom Compiler
 * berechnete Strukturidentitaet (FNV-1a ueber das kanonische Layout); Ansichten
 * erben sie. Skalar-APIs sehen einen Strukturpuffer als untypisiert (NONE). */
MooBufferStatus moo_buffer_new_struct(size_t elem_size, size_t elem_align, uint64_t type_id, size_t len, MooBuffer** out);
/* 0 = kein Strukturpuffer bzw. NULL. */
uint64_t moo_buffer_struct_id(const MooBuffer* buffer);

/* elem_align must be a power of two <= MOO_MAX_ALIGN, dividing elem_size. */
MooBufferStatus moo_buffer_new(size_t elem_size, size_t elem_align, size_t len, MooBuffer** out);
MooBufferStatus moo_buffer_view(MooBuffer* buffer, size_t start, size_t len, MooBuffer** out);
/* Retain/drop apply only to the owned handle; pins have separate ownership. */
MooBufferStatus moo_buffer_retain(MooBuffer* buffer);
void moo_buffer_drop(MooBuffer* buffer);
/* Destruction hook for moo_release after it decrements the first refcount to 0. */
void moo_buffer_free(void* buffer);
size_t moo_buffer_len(const MooBuffer* buffer);
MooBufferStatus moo_buffer_get(const MooBuffer* buffer, size_t index, void* out);
MooBufferStatus moo_buffer_set(MooBuffer* buffer, size_t index, const void* value);
MooBufferStatus moo_buffer_resize(MooBuffer* buffer, size_t len);
MooBufferStatus moo_buffer_pin(MooBuffer* buffer, MooBufferPin** out, void** data);
void moo_buffer_unpin(MooBufferPin* pin);

#ifdef __cplusplus
}
#endif

#endif
