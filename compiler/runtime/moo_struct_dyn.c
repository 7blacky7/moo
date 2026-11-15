/* P2-T49 (Plan 38c59c64 A2-A6): Strukturwerte in dynamischem Moo.
 * Grenzfunktionen fuer den Compiler-Adapter, Feldzugriff ueber die
 * Laufzeitbeschreibung v2, dynamischer Konstruktor, Ausgabe, Gleichheit.
 * Speicherkern (Box, Sicht, Validierung): moo_struct_box.c. */
#include "moo_struct_dyn.h"
#include "moo_buffer_index.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MooStructBox *struct_box_von(const MooValue *wert) {
    if (!wert || wert->tag != MOO_STRUCT) return NULL;
    return (MooStructBox *)moo_val_as_ptr(*wert);
}

static void struct_fehler(const char *format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    moo_throw(moo_error(text));
}

static const char *skalar_name(uint8_t skalar) {
    static const char *const namen[] = {
        "?", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64",
    };
    return skalar <= MOO_BUFFER_F64 ? namen[skalar] : "?";
}

static const MooStrukturFeld *feld_suchen(const MooStrukturTyp *typ, const char *name) {
    for (uint32_t i = 0; i < typ->feld_anzahl; ++i)
        if (strcmp(typ->felder[i].name, name) == 0) return &typ->felder[i];
    return NULL;
}

/* Bewusst strenger als Objekte: kein stilles nichts, keine neuen Felder (E4). */
static void feld_unbekannt(const MooStrukturTyp *typ, const char *name) {
    char liste[256];
    size_t n = 0;
    liste[0] = '\0';
    for (uint32_t i = 0; i < typ->feld_anzahl && n < sizeof(liste); ++i) {
        int w = snprintf(liste + n, sizeof(liste) - n, "%s%s", i ? ", " : "", typ->felder[i].name);
        if (w < 0) break;
        n += (size_t)w;
    }
    struct_fehler("Struktur %s hat kein Feld '%s' (Felder: %s)", typ->basis.name, name, liste);
}

/* Nur die beschriebenen Blaetter; Padding des Ziels bleibt unberuehrt. Quelle
 * und Ziel sind gleich oder disjunkt (gleicher Typ, kein Wert enthaelt sich). */
static void blaetter_kopieren(const MooStructDescriptor *basis, const unsigned char *quelle,
                              unsigned char *ziel) {
    for (size_t i = 0; i < basis->field_count; ++i) {
        const MooStructField *f = &basis->fields[i];
        memmove(ziel + f->offset, quelle + f->offset, f->size);
    }
}

void moo_struct_box_value(const MooStrukturTyp *typ, const void *quelle, MooValue *out) {
    if (!out) return;
    *out = moo_none();
    MooStructBox *box = NULL;
    MooStructStatus status = moo_struct_box_new_typ(typ, quelle, &box);
    if (status == MOO_STRUCT_ALLOC) {
        struct_fehler("Struktur: kein Speicher");
        return;
    }
    if (status != MOO_STRUCT_OK) {
        struct_fehler("Struktur: ungueltige Laufzeitbeschreibung");
        return;
    }
    out->tag = MOO_STRUCT;
    moo_val_set_ptr(out, box);
}

int32_t moo_struct_unbox_value(const MooValue *wert, const MooStrukturTyp *typ, void *ziel) {
    if (!wert || !ziel || !moo_struktur_typ_gueltig(typ)) {
        struct_fehler("Struktur: ungueltiger Aufruf an der nativen Grenze");
        return 0;
    }
    MooStructBox *box = struct_box_von(wert);
    if (!box) {
        struct_fehler("Struktur %s erwartet, erhalten: %s", typ->basis.name, moo_type_name(*wert));
        return 0;
    }
    const MooStrukturTyp *box_typ = moo_struct_box_typ(box);
    if (!box_typ) {
        struct_fehler("Struktur %s erwartet, erhalten: Struktur ohne Feldbeschreibung", typ->basis.name);
        return 0;
    }
    if (box_typ->typ_id != typ->typ_id) {
        struct_fehler("Struktur %s erwartet, erhalten: %s", typ->basis.name, box_typ->basis.name);
        return 0;
    }
    /* Gleiche typ_id = gleiches Layout: Blaetter der erwarteten Beschreibung. */
    memset(ziel, 0, typ->basis.size);
    blaetter_kopieren(&typ->basis, moo_struct_box_daten(box), ziel);
    return 1;
}

static int element_lesen(const MooStrukturTyp *typ, const MooStrukturFeld *f, MooStructBox *box,
                         size_t versatz, MooValue *out) {
    const unsigned char *p = moo_struct_box_daten(box) + versatz;
    switch (f->element) {
    case MOO_FELD_BOOL:
        *out = moo_bool(*p != 0);
        return 1;
    case MOO_FELD_SKALAR: {
        double zahl = 0.0;
        if (!moo_element_als_zahl(f->skalar, p, &zahl)) {
            struct_fehler("Struktur %s.%s: 64-Bit-Wert nicht exakt als Moo-Zahl darstellbar",
                          typ->basis.name, f->name);
            return 0;
        }
        *out = moo_number(zahl);
        return 1;
    }
    case MOO_FELD_STRUKTUR: {
        /* Sicht: `a.innen.x = 5` wirkt auf a (E4). */
        MooStructBox *sicht = NULL;
        MooStructStatus status = moo_struct_box_sicht(box, f->struktur, versatz, &sicht);
        if (status != MOO_STRUCT_OK) {
            if (status == MOO_STRUCT_ALLOC) struct_fehler("Struktur: kein Speicher");
            else struct_fehler("Struktur %s.%s: Sicht nicht moeglich", typ->basis.name, f->name);
            return 0;
        }
        out->tag = MOO_STRUCT;
        moo_val_set_ptr(out, sicht);
        return 1;
    }
    default:
        struct_fehler("Struktur %s.%s: Feldart nicht unterstuetzt", typ->basis.name, f->name);
        return 0;
    }
}

/* wert geliehen; bei Fehler bleibt p unveraendert. */
static int element_schreiben(const MooStrukturTyp *typ, const MooStrukturFeld *f, unsigned char *p,
                             MooValue wert) {
    switch (f->element) {
    case MOO_FELD_BOOL:
        if (wert.tag != MOO_BOOL) {
            struct_fehler("Struktur %s.%s erwartet einen Wahrheitswert, erhalten: %s",
                          typ->basis.name, f->name, moo_type_name(wert));
            return 0;
        }
        *p = MV_BOOL(wert) ? 1u : 0u;
        return 1;
    case MOO_FELD_SKALAR:
        if (wert.tag != MOO_NUMBER) {
            struct_fehler("Struktur %s.%s erwartet eine Zahl (%s), erhalten: %s",
                          typ->basis.name, f->name, skalar_name(f->skalar), moo_type_name(wert));
            return 0;
        }
        if (!moo_zahl_in_element(f->skalar, moo_val_as_double(wert), p)) {
            struct_fehler("Struktur %s.%s: Wert %g passt nicht in %s (Ganzzahl im Bereich bzw. Kommazahl)",
                          typ->basis.name, f->name, moo_val_as_double(wert), skalar_name(f->skalar));
            return 0;
        }
        return 1;
    case MOO_FELD_STRUKTUR: {
        MooStructBox *quelle = struct_box_von(&wert);
        const MooStrukturTyp *quell_typ = moo_struct_box_typ(quelle);
        if (!quell_typ || quell_typ->typ_id != f->struktur->typ_id) {
            struct_fehler("Struktur %s.%s erwartet %s, erhalten: %s", typ->basis.name, f->name,
                          f->struktur->basis.name, quell_typ ? quell_typ->basis.name : moo_type_name(wert));
            return 0;
        }
        blaetter_kopieren(&f->struktur->basis, moo_struct_box_daten(quelle), p);
        return 1;
    }
    default:
        struct_fehler("Struktur %s.%s: Feldart nicht unterstuetzt", typ->basis.name, f->name);
        return 0;
    }
}

static void array_fehler(const MooStrukturTyp *typ, const MooStrukturFeld *f) {
    struct_fehler("Struktur %s.%s: Arrayfelder sind dynamisch noch nicht zugreifbar (nur in nativen Funktionen)",
                  typ->basis.name, f->name);
}

static const MooStrukturTyp *beschriebener_typ(MooValue s, MooStructBox **box, const char *wo) {
    *box = struct_box_von(&s);
    const MooStrukturTyp *typ = moo_struct_box_typ(*box);
    if (!typ) struct_fehler("%s: Struktur ohne Feldbeschreibung", wo);
    return typ;
}

MooValue moo_struct_feld_get(MooValue s, const char *name) {
    MooStructBox *box = NULL;
    const MooStrukturTyp *typ = beschriebener_typ(s, &box, "Feldzugriff");
    if (!typ) return moo_none();
    const MooStrukturFeld *f = name ? feld_suchen(typ, name) : NULL;
    if (!f) {
        feld_unbekannt(typ, name ? name : "");
        return moo_none();
    }
    if (f->art == MOO_FELD_ARRAY) {
        array_fehler(typ, f);
        return moo_none();
    }
    MooValue out = moo_none();
    element_lesen(typ, f, box, f->versatz, &out);
    return out;
}

void moo_struct_feld_set(MooValue s, const char *name, MooValue wert) {
    MooStructBox *box = NULL;
    const MooStrukturTyp *typ = beschriebener_typ(s, &box, "Feldzuweisung");
    const MooStrukturFeld *f = (typ && name) ? feld_suchen(typ, name) : NULL;
    if (typ && !f) feld_unbekannt(typ, name ? name : "");
    else if (f && f->art == MOO_FELD_ARRAY) array_fehler(typ, f);
    else if (f) element_schreiben(typ, f, moo_struct_box_daten(box) + f->versatz, wert);
    moo_release(wert);
}

void moo_struct_new_values(const MooStrukturTyp *typ, const MooValue *argumente, int32_t anzahl,
                           MooValue *out) {
    if (!out) return;
    *out = moo_none();
    if (!moo_struktur_typ_gueltig(typ) || anzahl < 0 || (anzahl > 0 && !argumente)) {
        struct_fehler("Struktur: ungueltiger Konstruktoraufruf");
        return;
    }
    /* Stelligkeit wie native_system.rs construct_struct_into: keine Argumente =
     * Nullwert, sonst genau ein Wert je Feld. */
    if (anzahl != 0 && (uint32_t)anzahl != typ->feld_anzahl) {
        struct_fehler("Struktur %s erwartet %u Werte (oder keinen fuer den Nullwert), bekommen %d",
                      typ->basis.name, (unsigned)typ->feld_anzahl, (int)anzahl);
        return;
    }
    unsigned char *daten = calloc(1, typ->basis.size);
    if (!daten) {
        struct_fehler("Struktur: kein Speicher");
        return;
    }
    for (int32_t i = 0; i < anzahl; ++i) {
        const MooStrukturFeld *f = &typ->felder[i];
        if (f->art == MOO_FELD_ARRAY) {
            struct_fehler("Struktur %s.%s: Arrayfelder sind dynamisch noch nicht setzbar (%s() liefert den Nullwert)",
                          typ->basis.name, f->name, typ->basis.name);
            free(daten);
            return;
        }
        if (!element_schreiben(typ, f, daten + f->versatz, argumente[i])) {
            free(daten);
            return;
        }
    }
    moo_struct_box_value(typ, daten, out);
    free(daten);
}

/* ---- Ausgabe (E7) ---- */
typedef struct {
    char *daten;
    size_t laenge;
    size_t kapazitaet;
    int fehler;
} StrukturText;

static void text_an(StrukturText *t, const char *s) {
    size_t n = strlen(s);
    if (t->fehler) return;
    if (n >= SIZE_MAX - t->laenge) { t->fehler = 1; return; }
    if (t->laenge + n + 1 > t->kapazitaet) {
        size_t neu = t->kapazitaet ? t->kapazitaet : 64;
        while (neu < t->laenge + n + 1) {
            if (neu > SIZE_MAX / 2) { t->fehler = 1; return; }
            neu *= 2;
        }
        char *p = realloc(t->daten, neu);
        if (!p) { t->fehler = 1; return; }
        t->daten = p;
        t->kapazitaet = neu;
    }
    memcpy(t->daten + t->laenge, s, n + 1);
    t->laenge += n;
}

/* Wie moo_to_string fuer Zahlen, ohne UB-Cast bei NaN/Unendlich/Ueberlauf. */
static void zahl_text(char *buf, size_t n, double d) {
    if (d == d && d >= -9.2e18 && d <= 9.2e18 && d == (double)(int64_t)d)
        snprintf(buf, n, "%lld", (long long)(int64_t)d);
    else
        snprintf(buf, n, "%g", d);
}

/* Ganzzahlen exakt aus dem Speicher (auch 64 Bit jenseits 2^53). */
static void skalar_text(char *buf, size_t n, uint8_t skalar, const unsigned char *p) {
    switch (skalar) {
    case MOO_BUFFER_I8: { int8_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%d", (int)v); return; }
    case MOO_BUFFER_U8: { uint8_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%u", (unsigned)v); return; }
    case MOO_BUFFER_I16: { int16_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%d", (int)v); return; }
    case MOO_BUFFER_U16: { uint16_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%u", (unsigned)v); return; }
    case MOO_BUFFER_I32: { int32_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%ld", (long)v); return; }
    case MOO_BUFFER_U32: { uint32_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%lu", (unsigned long)v); return; }
    case MOO_BUFFER_I64: { int64_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%lld", (long long)v); return; }
    case MOO_BUFFER_U64: { uint64_t v; memcpy(&v, p, sizeof(v)); snprintf(buf, n, "%llu", (unsigned long long)v); return; }
    case MOO_BUFFER_F32: { float v; memcpy(&v, p, sizeof(v)); zahl_text(buf, n, (double)v); return; }
    case MOO_BUFFER_F64: { double v; memcpy(&v, p, sizeof(v)); zahl_text(buf, n, v); return; }
    default: snprintf(buf, n, "?"); return;
    }
}

static void struktur_text(StrukturText *t, const MooStrukturTyp *typ, const unsigned char *daten,
                          uint32_t tiefe);

static void element_text(StrukturText *t, const MooStrukturFeld *f, const unsigned char *p,
                         uint32_t tiefe) {
    char buf[64];
    switch (f->element) {
    case MOO_FELD_BOOL:
        text_an(t, *p ? "wahr" : "falsch");
        return;
    case MOO_FELD_STRUKTUR:
        if (tiefe < MOO_STRUKTUR_MAX_TIEFE) struktur_text(t, f->struktur, p, tiefe + 1);
        else text_an(t, "...");
        return;
    default:
        skalar_text(buf, sizeof(buf), f->skalar, p);
        text_an(t, buf);
        return;
    }
}

static void struktur_text(StrukturText *t, const MooStrukturTyp *typ, const unsigned char *daten,
                          uint32_t tiefe) {
    text_an(t, typ->basis.name);
    text_an(t, "(");
    for (uint32_t i = 0; i < typ->feld_anzahl; ++i) {
        const MooStrukturFeld *f = &typ->felder[i];
        if (i) text_an(t, ", ");
        text_an(t, f->name);
        text_an(t, ": ");
        if (f->art == MOO_FELD_ARRAY) {
            size_t schritt = f->groesse / f->anzahl;
            text_an(t, "[");
            for (uint32_t k = 0; k < f->anzahl; ++k) {
                if (k) text_an(t, ", ");
                element_text(t, f, daten + f->versatz + (size_t)k * schritt, tiefe);
            }
            text_an(t, "]");
        } else {
            element_text(t, f, daten + f->versatz, tiefe);
        }
    }
    text_an(t, ")");
}

MooValue moo_struct_text(MooValue s) {
    MooStructBox *box = struct_box_von(&s);
    const MooStrukturTyp *typ = moo_struct_box_typ(box);
    if (!typ) return moo_string_new("<Struktur>");
    StrukturText t = {NULL, 0, 0, 0};
    struktur_text(&t, typ, moo_struct_box_daten(box), 0);
    MooValue r = moo_string_new((t.fehler || !t.daten) ? "<Struktur>" : t.daten);
    free(t.daten);
    return r;
}

/* ---- Gleichheit (E7) ---- */
static int struktur_gleich(const MooStrukturTyp *typ, const unsigned char *a, const unsigned char *b,
                           uint32_t tiefe);

static int element_gleich(const MooStrukturFeld *f, const unsigned char *a, const unsigned char *b,
                          uint32_t tiefe) {
    switch (f->element) {
    case MOO_FELD_BOOL:
        return (*a != 0) == (*b != 0);
    case MOO_FELD_STRUKTUR:
        return tiefe < MOO_STRUKTUR_MAX_TIEFE && struktur_gleich(f->struktur, a, b, tiefe + 1);
    default:
        if (f->skalar == MOO_BUFFER_F32) {
            float x, y;
            memcpy(&x, a, sizeof(x)); memcpy(&y, b, sizeof(y));
            return x == y;
        }
        if (f->skalar == MOO_BUFFER_F64) {
            double x, y;
            memcpy(&x, a, sizeof(x)); memcpy(&y, b, sizeof(y));
            return x == y;
        }
        return memcmp(a, b, f->groesse / f->anzahl) == 0;
    }
}

static int struktur_gleich(const MooStrukturTyp *typ, const unsigned char *a, const unsigned char *b,
                           uint32_t tiefe) {
    for (uint32_t i = 0; i < typ->feld_anzahl; ++i) {
        const MooStrukturFeld *f = &typ->felder[i];
        size_t schritt = f->groesse / f->anzahl;
        for (uint32_t k = 0; k < f->anzahl; ++k) {
            size_t versatz = f->versatz + (size_t)k * schritt;
            if (!element_gleich(f, a + versatz, b + versatz, tiefe)) return 0;
        }
    }
    return 1;
}

bool moo_struct_gleich(MooValue a, MooValue b) {
    MooStructBox *x = struct_box_von(&a);
    MooStructBox *y = struct_box_von(&b);
    if (!x || !y) return false;
    const MooStrukturTyp *tx = moo_struct_box_typ(x);
    const MooStrukturTyp *ty = moo_struct_box_typ(y);
    if (!tx || !ty) return x == y;
    if (tx->typ_id != ty->typ_id) return false;
    return struktur_gleich(tx, moo_struct_box_daten(x), moo_struct_box_daten(y), 0) != 0;
}
