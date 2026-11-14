#ifndef MOO_BUFFER_INDEX_H
#define MOO_BUFFER_INDEX_H
#include "moo_runtime.h"
/* Container borrowed; index and set value are consumed on all paths.
 * Get returns an owning MooValue; failures throw and return NONE.
 * P2-T50: alle Skalartypen (i8..u64, f32, f64). Ganzzahlen werden nur exakt
 * und im Wertebereich gespeichert; 64-Bit-Elemente werden nur geliefert, wenn
 * sie exakt als Moo-Zahl (double, |v| <= 2^53) darstellbar sind. */
MooValue moo_buffer_index_get(MooValue container, MooValue index);
void moo_buffer_index_set(MooValue container, MooValue index, MooValue value);
/* P2-T50: Konstruktor fuer puffer_erstellen(T, n). type ist ein
 * MooBufferScalarType-Code (1..10), len wird konsumiert. Ergebnis ist ein
 * owning MOO_BUFFER mit n Null-Elementen; bei negativer/gebrochener Laenge,
 * unbekanntem Typ oder Speicherfehler fangbarer Fehler und NONE. */
MooValue moo_buffer_create(int32_t type, MooValue len);
#endif
