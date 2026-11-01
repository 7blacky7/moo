#ifndef MOO_STRUCT_VALUE_H
#define MOO_STRUCT_VALUE_H

#include "moo_runtime.h"
#include "moo_struct_box.h"

/* out must not contain a live owning value. Success returns one owning
 * MOO_STRUCT reference; failure sets out to NONE. Descriptor lifetime must
 * exceed all boxes and its address is the type identity token. */
MooStructStatus moo_struct_value_new(const MooStructDescriptor* type, const void* source, MooValue* out);
/* Success borrows a box while value is alive; no retain or transfer occurs.
 * The exact canonical descriptor is required; on failure out is NULL. */
MooStructStatus moo_struct_value_borrow(MooValue value, const MooStructDescriptor* type, MooStructBox** out);

#endif
