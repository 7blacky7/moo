#ifndef MOO_HANDLE_H
#define MOO_HANDLE_H
#include "moo_runtime.h"
/* P2-T23/T53 (Plan 36aca1f2 A1): Griff = refgezaehlter Laufzeitwert fuer einen
 * fremden Zeiger (z. B. ein C++-Objekt hinter einem extern-C-Adapter).
 * - Nur durch die Rueckgabe einer nativen Funktion herstellbar (nicht aus Zahlen).
 * - typ_id = Struktur-ID des Zeigerziels der nativen Signatur (ptr[Name]);
 *   0 = ptr[void] ("untypisiert", passt nur auf ptr[void]).
 * - Zustand gueltig|freigegeben: nach @verbraucht ist jeder weitere Gebrauch
 *   (auch ueber Aliase) ein fangbarer Moo-Fehler VOR dem Fremdaufruf.
 * - Kein Finalizer: die Box gibt nie das fremde Objekt frei. Offene (nicht
 *   freigegebene) Griffe zaehlt moo_handle_live_count - auch solche, deren Box
 *   schon weg ist (= Leck des fremden Objekts).
 * Thread-confined wie Puffer. Nur Zeiger/Ganzzahlen an der Unbox-/Close-ABI. */

typedef struct MooHandle MooHandle;

/* NULL -> moo_none(), sonst owning MOO_HANDLE (+1). */
MooValue moo_handle_box(void* zeiger, uint64_t typ_id);
/* value geliehen. nichts -> *out = NULL, 1. Griff passender Typ-ID im Zustand
 * gueltig -> *out = Zeiger, 1. Sonst fangbarer Fehler (kein Griff, falscher
 * Typ, bereits freigegeben), *out = NULL, 0. */
int32_t moo_handle_unbox(const MooValue* value, uint64_t typ_id, void** out);
/* value geliehen. Markiert einen gueltigen Griff als freigegeben (idempotent;
 * nichts/kein Griff: ohne Wirkung). */
void moo_handle_close(const MooValue* value);
/* Freigabe der Box: moo_memory.c (free, kein Finalizer). */
/* Anzahl erzeugter, noch nicht freigegebener Griffe. */
int64_t moo_handle_live_count(void);
/* Dyn-Builtins: griff_gültig(h) (h geliehen) -> Wahrheitswert; griffe_offen() -> Zahl. */
MooValue moo_handle_valid(MooValue value);
MooValue moo_handle_open_count(void);
/* Fuer moo_eq/moo_to_string. */
bool moo_handle_equal(MooValue a, MooValue b);
const char* moo_handle_text(MooValue value);
#endif
