#ifndef MOO_BUFFER_PRIV_H
#define MOO_BUFFER_PRIV_H
#include "moo_buffer.h"
#include "moo_rc.h"

/* Gemeinsames privates Layout fuer Speicherfreigabe, Teilen und Sperren.
 * Eine Ansicht friert denselben Speicher wie ihr Ursprung ein. */
typedef struct {
    size_t refs;
    size_t views;
    size_t pins;
    int32_t eingefroren;
    size_t elem_size;
    MooBufferScalarType scalar_type;
    uint64_t type_id;
    size_t len;
    size_t capacity;
    unsigned char* data;
} MooBufferStorage;

struct MooBuffer {
    int32_t refcount;
    MooBufferStorage* storage;
    size_t start;
    size_t len;
    int is_view;
};

static inline int moo_buffer_geteilt(const MooBuffer* b) {
    return b && (moo_rc_ist_geteilt(&b->refcount)
                 || moo_rc_lesen(&b->storage->eingefroren));
}
#endif
