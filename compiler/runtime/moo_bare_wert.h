/**
 * moo_bare_wert.h — Gemeinsamer Zahlzugriff fuer MooValues im Bare-/UEFI-Profil
 * (P7-T19). Freestanding: nur <stdint.h>/<stdbool.h>, kein libc.
 *
 * Zahlwerte im Bare-Profil haben drei Tags:
 *   0  (MOO_NUMBER) data = IEEE-754-binary64-Bits
 *   30 (i64)        data = Zweierkomplement-Bits, exakt (P7-T17, @nativ-Rueckgaben)
 *   31 (u64)        data = vorzeichenlose Bits, exakt
 * Tag 30/31 sind in moo_runtime.h reserviert. JEDE C-Funktion des Bare/UEFI-
 * Profils, die einen Zahlwert entpackt, geht ueber diesen Header — nicht ueber
 * ein eigenes memcpy auf double (das liefert fuer 30/31 Unsinn).
 *
 * Die Funktionen nehmen (tag, data) statt MooValue, weil die Uebersetzungs-
 * einheiten MooValue verschieden typedefen ({u64,u64} bzw. {i64,i64}); die
 * Makros MOO_WERT_* nehmen jeden MooValue mit Feldern tag/data.
 *
 *   moo_wert_double  — Zahl als double, kein Trap (Arithmetik-Rueckfallweg).
 *   moo_wert_zahl    — wie oben, aber Trap, wenn der Wert keine Zahl ist.
 *                      Fuer Builtins, deren Vertrag eine Zahl verlangt: Bare
 *                      hat keinen fangbaren Fehlerkanal, ein Typfehler darf
 *                      nicht still als Adresse/Laenge weiterlaufen.
 *   moo_wert_ganz    — exakte Ganzzahlsicht in [-2^63, 2^64), sonst false.
 *   moo_wert_u64     — exakte nicht negative Ganzzahl (Adressen, Laengen).
 */
#ifndef MOO_BARE_WERT_H
#define MOO_BARE_WERT_H

#include <stdint.h>
#include <stdbool.h>

#define MOO_WERT_TAG_ZAHL 0u
#define MOO_WERT_TAG_I64  30u
#define MOO_WERT_TAG_U64  31u

/* Ein Vergleich statt zwei: Tags 30/31 sind die einzigen >= 30 im Bare-Profil
 * (0..3 sonst). Ein Treffer bei einem fremden Tag >= 30 ist nur ein Umweg,
 * kein Fehler: moo_wert_ganz prueft danach exakt. */
#define MOO_WERT_VIELLEICHT_BREIT(tag) ((uint64_t)(tag) >= MOO_WERT_TAG_I64)

static inline bool moo_wert_breit(uint64_t tag) {
    return tag == MOO_WERT_TAG_I64 || tag == MOO_WERT_TAG_U64;
}
static inline bool moo_wert_ist_zahl(uint64_t tag) {
    return tag == MOO_WERT_TAG_ZAHL || moo_wert_breit(tag);
}

static inline double moo_wert_double(uint64_t tag, uint64_t data) {
    if (__builtin_expect(MOO_WERT_VIELLEICHT_BREIT(tag), 0)) {
        if (tag == MOO_WERT_TAG_I64) return (double)(int64_t)data;
        if (tag == MOO_WERT_TAG_U64) return (double)data;
    }
    double d;
    __builtin_memcpy(&d, &data, sizeof d);
    return d;
}

static inline double moo_wert_zahl(uint64_t tag, uint64_t data) {
    if (__builtin_expect(!moo_wert_ist_zahl(tag), 0)) __builtin_trap();
    return moo_wert_double(tag, data);
}

/* Ganzzahlsicht: *bits = Zweierkomplement, *neg = mathematisch negativ.
 * Bereich vor jedem Cast geprueft (kein UB). */
static inline bool moo_wert_ganz(uint64_t tag, uint64_t data, uint64_t* bits, bool* neg) {
    if (tag == MOO_WERT_TAG_U64) { *bits = data; *neg = false; return true; }
    if (tag == MOO_WERT_TAG_I64) { *bits = data; *neg = (int64_t)data < 0; return true; }
    if (tag != MOO_WERT_TAG_ZAHL) return false;
    double d;
    __builtin_memcpy(&d, &data, sizeof d);
    if (!(d >= -9223372036854775808.0 && d < 18446744073709551616.0)) return false;
    if (d < 9223372036854775808.0) {
        int64_t i = (int64_t)d;
        if ((double)i != d) return false;
        *bits = (uint64_t)i;
        *neg = i < 0;
        return true;
    }
    uint64_t u = (uint64_t)d;
    if ((double)u != d) return false;
    *bits = u;
    *neg = false;
    return true;
}

static inline bool moo_wert_u64(uint64_t tag, uint64_t data, uint64_t* out) {
    uint64_t b;
    bool n;
    if (!moo_wert_ganz(tag, data, &b, &n) || n) return false;
    *out = b;
    return true;
}

#define MOO_WERT_DOUBLE(v) moo_wert_double((uint64_t)(v).tag, (uint64_t)(v).data)
#define MOO_WERT_ZAHL(v)   moo_wert_zahl((uint64_t)(v).tag, (uint64_t)(v).data)
#define MOO_WERT_IST_ZAHL(v) moo_wert_ist_zahl((uint64_t)(v).tag)

#endif /* MOO_BARE_WERT_H */
