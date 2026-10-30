#ifndef MOO_CHAR_FIXED_H
#define MOO_CHAR_FIXED_H
#include "moo_runtime.h"

/* Frozen character tokenizer. Borrowed arguments; returned values own one ref.
 * Artifact MOOCF001: LE version=1, flags=0, vocab count, then (count-256)
 * LE Unicode scalar values in token-ID order; a 32-byte SHA-256 checksum
 * covers header and entries. IDs 0..255 are raw bytes.
 * Training chooses frequent non-ASCII scalars, ties by UTF-8 byte order.
 * Valid input is nonempty UTF-8 without NUL. No normalization. Encode never
 * changes the artifact; unknown scalars use their original UTF-8 bytes.
 * Limit is integer 256..65536. IDs are exact float integers.
 * hash = SHA-256 of the complete artifact; hash64 = FNV-1a-64 of same bytes.
 * Save replaces destination atomically where supported, without durability
 * guarantee across power loss. Windows paths are UTF-8 and converted to UTF-16.
 */
MooValue moo_char_fixed_trainiere(MooValue korpus, MooValue limit);
MooValue moo_char_fixed_kodiere(MooValue tok, MooValue text);
MooValue moo_char_fixed_dekodiere(MooValue tok, MooValue ids);
MooValue moo_char_fixed_speichern(MooValue tok, MooValue pfad);
MooValue moo_char_fixed_laden(MooValue pfad);
MooValue moo_char_fixed_info(MooValue tok);
MooValue moo_char_fixed_hash(MooValue tok);
MooValue moo_char_fixed_hash64(MooValue tok);
#endif
