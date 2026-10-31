#ifndef MOO_TOK_SEGMENT_H
#define MOO_TOK_SEGMENT_H

#include <stddef.h>
#include <stdint.h>
#include "moo_runtime.h"

/* Gibt die benötigte Anzahl zurück; schreibt bis max_grenzen Offsets.
 * Für len > 0 sind Grenzen streng aufsteigend, beginnen nach 0 und enden
 * bei len. Für len == 0 ist die Anzahl 0. grenzen darf NULL sein. */
size_t moo_tok_segmente(const uint8_t *buf, size_t len, size_t *grenzen, size_t max_grenzen);

/* Artefakt geliehen; MOO_NONE wählt den Wortgrenzen-Fallback. */
size_t moo_tok_segmente_ctx(const uint8_t *buf, size_t len, size_t *grenzen, size_t max_grenzen, MooValue silben_artefakt);

#endif
