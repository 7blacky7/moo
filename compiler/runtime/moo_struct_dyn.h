#ifndef MOO_STRUCT_DYN_H
#define MOO_STRUCT_DYN_H

#include "moo_struct_typed.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* P2-T49 (Plan 38c59c64 A2-A6): Strukturwerte in dynamischem Moo.
 * An der Grenze wird kopiert (E1); dynamisch ist eine Box ein Referenzwert.
 * Compilerseitige Grenzfunktionen nutzen nur Zeiger/Ganzzahlen an der ABI. */

/* nativ -> dyn: kopiert *quelle in eine neue Box. out wird ueberschrieben
 * (darf keinen lebenden Wert enthalten); Fehler: fangbar, out = nichts. */
void moo_struct_box_value(const MooStrukturTyp *typ, const void *quelle, MooValue *out);
/* dyn -> nativ: *wert geliehen; kopiert die Blaetter nach ziel (Padding 0).
 * 1 = ok; sonst fangbarer Fehler (kein Strukturwert, andere typ_id), 0. */
int32_t moo_struct_unbox_value(const MooValue *wert, const MooStrukturTyp *typ, void *ziel);
/* s.name: s geliehen, Ergebnis owning. Verschachtelte Struktur -> Sicht. */
MooValue moo_struct_feld_get(MooValue s, const char *name);
/* s.name = wert: s geliehen, wert wird auf jedem Pfad verbraucht. */
void moo_struct_feld_set(MooValue s, const char *name, MooValue wert);
/* Name(a, b, ...) in dynamischem Code; argumente geliehen. anzahl 0 = Nullwert,
 * sonst genau feld_anzahl. Fehler: fangbar, out = nichts. */
void moo_struct_new_values(const MooStrukturTyp *typ, const MooValue *argumente,
                           int32_t anzahl, MooValue *out);
/* Ausgabe `Name(x: 1, y: 2.5)`; owning String. */
MooValue moo_struct_text(MooValue s);
/* Gleiche typ_id und alle Blaetter gleich (Float per ==, Padding ignoriert). */
bool moo_struct_gleich(MooValue a, MooValue b);

#ifdef __cplusplus
}
#endif
#endif
