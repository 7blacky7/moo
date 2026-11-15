#ifndef P2_M1_CPP_ADAPTER_H
#define P2_M1_CPP_ADAPTER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The handle is opaque. A null handle or a caught C++ exception returns -1. */
void *p2_cpp_create(int32_t value);
int32_t p2_cpp_value(void *handle);
int32_t p2_cpp_destroy(void *handle);
int32_t p2_cpp_example(int32_t value);
/* Sum copies up to 4096 elements; null is valid only for length zero. */
int64_t p2_cpp_vector_sum(const int32_t *values, uint32_t length);

/* P2-T23/T53 (Plan 36aca1f2 C1): opake C++-Klasse hinter extern-C-Funktionen.
 * Besitz: p2_zaehler_neu liefert ein Objekt, das genau einmal mit
 * p2_zaehler_frei freigegeben wird. Text und Vektoren laufen als (Zeiger, Laenge),
 * nie als NUL-terminierte Zeichenkette oder STL-Typ. Jede Funktion faengt alle
 * C++-Exceptions; Fehlercode -1 bzw. NULL, Meldung ueber p2_letzter_fehler. */
typedef struct P2Zaehler P2Zaehler;
P2Zaehler *p2_zaehler_neu(void);
int32_t p2_zaehler_frei(P2Zaehler *zaehler);
/* Haengt anzahl Werte an; mehr als 4096 Werte insgesamt -> -1 (std::length_error). */
int32_t p2_zaehler_addiere(P2Zaehler *zaehler, const int32_t *werte, uint64_t anzahl);
int64_t p2_zaehler_summe(const P2Zaehler *zaehler);
int32_t p2_zaehler_name_setzen(P2Zaehler *zaehler, const uint8_t *text, uint64_t laenge);
/* Kopiert hoechstens kapazitaet Bytes, liefert die benoetigte Laenge. */
uint64_t p2_zaehler_name(const P2Zaehler *zaehler, uint8_t *ziel, uint64_t kapazitaet);
uint64_t p2_letzter_fehler(uint8_t *ziel, uint64_t kapazitaet);
int64_t p2_zaehler_lebende(void);

/* P2-T49 (Plan 38c59c64 C5): Strukturwert by value ueber die C-ABI. */
typedef struct { float x; float y; int32_t gewicht; } P2Punkt;
P2Punkt p2_punkt_neu(float x, float y, int32_t gewicht);
/* x und y mal faktor, gewicht + 1. */
P2Punkt p2_punkt_skaliert(P2Punkt punkt, float faktor);
#ifdef __cplusplus
}
#endif
#endif
