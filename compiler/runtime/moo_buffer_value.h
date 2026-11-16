#ifndef MOO_BUFFER_VALUE_H
#define MOO_BUFFER_VALUE_H

#include "moo_runtime.h"
#include "moo_buffer.h"

/* out must not contain a live owning value. On success it receives one owning
 * MOO_BUFFER reference. On failure it is NONE and no ownership is transferred. */
MooBufferStatus moo_buffer_value_new(MooBufferScalarType type, size_t len, MooValue* out);
/* On success, out is borrowed from value; no retain occurs.
 * The caller must keep value alive and pin it for any exported raw pointer.
 * On failure, out is NULL. */
MooBufferStatus moo_buffer_value_borrow(MooValue value, MooBufferScalarType expected, MooBuffer** out);
/* As with new, out must not contain an owning value. The input is borrowed;
 * on success out owns a view sharing storage, on failure it is NONE. */
MooBufferStatus moo_buffer_value_view(MooValue value, MooBufferScalarType expected,
                                      size_t start, size_t len, MooValue* out);
/* P2-T114 B (übergib): 1, wenn value ein ungeteilter Puffer im Alleinbesitz
 * ist: genau eine Referenz, keine Sicht, Speicher ohne weitere Halter, keine
 * Sichten und keine Pins. */
int moo_buffer_alleinbesitz(MooValue value);

#endif
