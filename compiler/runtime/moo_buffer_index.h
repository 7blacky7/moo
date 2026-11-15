#ifndef MOO_BUFFER_INDEX_H
#define MOO_BUFFER_INDEX_H
#include "moo_runtime.h"
#include "moo_buffer.h"
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
/* P2-T50 A2: puffer_anhängen(p, wert). Container geliehen, value konsumiert.
 * Haengt ein Element an (exakt/bereichsgeprueft wie Indexzuweisung). Fangbare
 * Fehler: kein typisierter Puffer, Wert passt nicht, Ansicht (nicht
 * verlaengerbar), Puffer gesperrt (aktive Ansicht oder nativer Aufruf),
 * Speicher. Laenge und Elemente bleiben bei Fehler unveraendert. */
void moo_buffer_append_value(MooValue container, MooValue value);
/* P2-T50 A2: puffer_ansicht(p, start, laenge). Container geliehen, start und
 * len konsumiert. Ergebnis ist ein owning MOO_BUFFER, der denselben Speicher
 * teilt (Schreiben wirkt auf das Original); solange er lebt, ist jede
 * Laengenaenderung des Speichers gesperrt. Bereich ausserhalb, ungueltige
 * Zahlen, kein Puffer oder Speicherfehler: fangbarer Fehler und NONE. */
MooValue moo_buffer_view_value(MooValue container, MooValue start, MooValue len);
/* P2-T50 B5 (Plan bb3878ef E1): Pin fuer einen nativen Aufruf. value wird
 * geliehen (der Aufrufer haelt den Puffer am Leben), type ist der erwartete
 * MooBufferScalarType-Code. Erfolg: Pin (mit moo_buffer_unpin zu loesen),
 * Datenzeiger und Laenge des Ausschnitts (leer: NULL/0); waehrend des Pins ist jede
 * Laengenaenderung gesperrt. Kein Puffer bzw. falscher Elementtyp: fangbarer
 * Moo-Fehler und NULL. Nur Zeiger/Ganzzahlen an der ABI (kein MooValue by value). */
MooBufferPin *moo_buffer_native_pin(const MooValue *value, int32_t type, void **daten, uint64_t *laenge);
/* P2-T48 D (Plan a71947f7 D1): puffer_erstellen(Struktur, n). Groesse,
 * Ausrichtung und Struktur-ID kommen als Konstanten vom Compiler; len wird
 * konsumiert. Fehler (Laenge, Ausrichtung > MOO_MAX_ALIGN, Speicher): fangbar, NONE. */
MooValue moo_buffer_create_struct(uint64_t size, uint64_t align, uint64_t type_id, MooValue len);
/* Pin fuer puffer[Struktur]-Parameter: wie moo_buffer_native_pin, aber die
 * Struktur-ID muss exakt passen (andere Definition -> fangbarer Fehler, NULL). */
MooBufferPin *moo_buffer_native_pin_struct(const MooValue *value, uint64_t type_id, void **daten, uint64_t *laenge);
/* P2-T23/T53 A3 (Plan 36aca1f2 E5): Text <-> puffer[u8] als (Zeiger, Laenge),
 * keine NUL-Terminierung. puffer_aus_text(t): t geliehen, Ergebnis owning
 * puffer[u8] mit den UTF-8-Bytes. puffer_als_text(p[, n]): p geliehen, n
 * konsumiert (nichts = ganze Laenge); kein puffer[u8], n ausserhalb 0..Laenge
 * oder ungueltiges UTF-8 -> fangbarer Fehler und NONE. */
/* P2-T83 (Plan f3217c27 A1): aufrufgebundene Leihgabe fuer einen nativen
 * puffer[T]-Parameter. wert geliehen; elementtyp = MooBufferScalarType-Code
 * (0 bei Struktur), struktur_id != 0 fuer puffer[Struktur]. Quellen:
 *   MOO_BUFFER  - Typ/ID muss passen, Speicher gepinnt (Laenge gesperrt);
 *   MOO_TENSOR  - nur puffer[f32]: Host-Sicht via moo_tensor_host_sichern, kopierfrei;
 *   MOO_FRAME   - nur puffer[u8]: Pixel (height*stride Bytes), kopierfrei.
 * Erfolg: Leihe (mit moo_native_zurueck zu loesen), Daten/Laenge gesetzt.
 * Fehler: fangbarer Moo-Fehler mit erwartetem/erhaltenem Typ, NULL. Waehrend
 * des (synchronen) Aufrufs kann der Tensor weder freigegeben noch konvertiert
 * werden. Nur Zeiger/Ganzzahlen an der ABI. */
typedef struct MooNativeLeihe MooNativeLeihe;
MooNativeLeihe *moo_native_leihen(const MooValue *wert, int32_t elementtyp, uint64_t struktur_id,
                                  int32_t schreibbar, void **daten, uint64_t *laenge);
/* Pin loesen; schreibbarer Tensor: valid = MOO_V_DATA (store/GPU verworfen). */
void moo_native_zurueck(MooNativeLeihe *leihe);
MooValue moo_buffer_from_text(MooValue text);
MooValue moo_buffer_to_text(MooValue container, MooValue len);
/* P2-T49 A4 (Plan 38c59c64): gemeinsame Zahlwandlung fuer Strukturfelder, exakt
 * dieselben Regeln wie Pufferindex/-zuweisung. typ = MooBufferScalarType-Code;
 * ziel/quelle zeigen auf ein Element dieses Typs (Ausrichtung beliebig).
 * 1 = ok; 0 = Wert passt nicht (ziel unveraendert) bzw. 64-Bit-Wert nicht
 * exakt als Moo-Zahl darstellbar. */
int moo_zahl_in_element(int32_t typ, double zahl, void *ziel);
int moo_element_als_zahl(int32_t typ, const void *quelle, double *out);
#endif
