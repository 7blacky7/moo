#ifndef MOO_STRUCT_BOX_PRIV_H
#define MOO_STRUCT_BOX_PRIV_H

#include "moo_struct_box.h"
#include <stdint.h>

/* Privates Layout: moo_memory.c braucht es, um eine Sicht mitsamt ihrer
 * Wurzelbox beim Teilen zu markieren. Kein Bestandteil der oeffentlichen API. */
struct MooStrukturTyp;
struct MooStructBox {
    int32_t refcount;
    const MooStructDescriptor* type;
    unsigned char* data;
    const struct MooStrukturTyp* typ;
    MooStructBox* besitzer;
};

#endif
