/* Additiver deutscher Tokenizer. Alle Argumente geliehen, Ergebnisse +1.
 * trainiere erhält den vollständigen Inhalt einer TeX-Musterdatei.
 * UTF-8 ohne NUL; leerer Text und beschädigte Artefakte werfen.
 * IDs 0..255 sind Bytes; persistierte Regeln und IDs bleiben unverändert.
 */
#ifndef MOO_SILBEN_H
#define MOO_SILBEN_H
#include "moo_runtime.h"
MooValue moo_silben_trainiere(MooValue korpus, MooValue limit, MooValue muster);
MooValue moo_silben_kodiere(MooValue tok, MooValue text);
/* [start,end,art]: 0 Literal, 1 Muster-Wortsegment (Kandidat), 2 Bytefallback.
 * Art 2 kann Codepoints und Grapheme teilen; keine sprachliche Garantie. */
MooValue moo_silben_grenzen(MooValue tok, MooValue text);
MooValue moo_silben_dekodiere(MooValue tok, MooValue ids);
MooValue moo_silben_speichern(MooValue tok, MooValue pfad);
MooValue moo_silben_laden(MooValue pfad);
MooValue moo_silben_info(MooValue tok);
MooValue moo_silben_hash(MooValue tok);
MooValue moo_silben_hash64(MooValue tok);
#endif
