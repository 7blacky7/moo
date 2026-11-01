#ifndef MOO_BUFFER_INDEX_H
#define MOO_BUFFER_INDEX_H
#include "moo_runtime.h"
/* Container borrowed; index and set value are consumed on all paths.
 * Get returns an owning MooValue; failures throw and return NONE. */
MooValue moo_buffer_index_get(MooValue container, MooValue index);
void moo_buffer_index_set(MooValue container, MooValue index, MooValue value);
#endif
