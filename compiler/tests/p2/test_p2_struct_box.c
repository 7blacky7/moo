#include "../../runtime/moo_struct_box.h"
#include "../../runtime/moo_struct_typed.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t a; uint32_t b; } Record;
static const MooStructField fields[] = {
    {"a", offsetof(Record, a), sizeof(uint8_t)},
    {"b", offsetof(Record, b), sizeof(uint32_t)}
};
static const MooStructDescriptor record = {"Record", sizeof(Record), _Alignof(Record), 2, fields};
static const MooStructDescriptor other = {"Record", sizeof(Record), _Alignof(Record), 2, fields};

/* P2-T49 (Plan 38c59c64 A1/C4): Laufzeitbeschreibung v2 wie vom Compiler
 * erzeugt (lueckenlose Blaetter zusammengefasst) und Sicht-Lebensdauer. */
typedef struct { float x; float y; } Innen;
typedef struct { uint32_t id; Innen pos; double masse; } Teil;
static const MooStructField innen_blaetter[] = {{"x", 0, sizeof(Innen)}};
static const MooStrukturFeld innen_felder[] = {
    {"x", offsetof(Innen, x), 4, MOO_FELD_SKALAR, MOO_FELD_SKALAR, MOO_BUFFER_F32, 0, 1, NULL},
    {"y", offsetof(Innen, y), 4, MOO_FELD_SKALAR, MOO_FELD_SKALAR, MOO_BUFFER_F32, 0, 1, NULL},
};
static const MooStrukturTyp innen_typ = {
    MOO_STRUKTUR_TYP_VERSION, 2, 0x1111u,
    {"Innen", sizeof(Innen), _Alignof(Innen), 1, innen_blaetter}, innen_felder,
};
static const MooStructField teil_blaetter[] = {
    {"id", offsetof(Teil, id), offsetof(Teil, pos) + sizeof(Innen)},
    {"masse", offsetof(Teil, masse), sizeof(double)},
};
static const MooStrukturFeld teil_felder[] = {
    {"id", offsetof(Teil, id), 4, MOO_FELD_SKALAR, MOO_FELD_SKALAR, MOO_BUFFER_U32, 0, 1, NULL},
    {"pos", offsetof(Teil, pos), sizeof(Innen), MOO_FELD_STRUKTUR, MOO_FELD_STRUKTUR, 0, 0, 1, &innen_typ},
    {"masse", offsetof(Teil, masse), 8, MOO_FELD_SKALAR, MOO_FELD_SKALAR, MOO_BUFFER_F64, 0, 1, NULL},
};
static const MooStrukturTyp teil_typ = {
    MOO_STRUKTUR_TYP_VERSION, 3, 0x2222u,
    {"Teil", sizeof(Teil), _Alignof(Teil), 2, teil_blaetter}, teil_felder,
};

static void test_v2_und_sicht(void) {
    assert(moo_struktur_typ_gueltig(&innen_typ) && moo_struktur_typ_gueltig(&teil_typ));
    MooStrukturTyp falsch = teil_typ;
    falsch.version = 1;
    assert(!moo_struktur_typ_gueltig(&falsch));
    MooStrukturFeld felder[3];
    memcpy(felder, teil_felder, sizeof(felder));
    falsch = teil_typ;
    falsch.felder = felder;
    felder[2].art = felder[2].element = 5; /* Zeiger: nicht boxbar (E5) */
    assert(!moo_struktur_typ_gueltig(&falsch));
    memcpy(felder, teil_felder, sizeof(felder));
    felder[2].name = "id"; /* doppelter Feldname */
    assert(!moo_struktur_typ_gueltig(&falsch));
    memcpy(felder, teil_felder, sizeof(felder));
    felder[1].groesse = 4; /* passt nicht zur Elementstruktur */
    assert(!moo_struktur_typ_gueltig(&falsch));
    memcpy(felder, teil_felder, sizeof(felder));
    felder[0].groesse = 8; felder[0].art = MOO_FELD_ARRAY; felder[0].anzahl = 2; /* [u32; 2] */
    assert(moo_struktur_typ_gueltig(&falsch));
    felder[0].anzahl = 3; /* 8 nicht durch 3 teilbar */
    assert(!moo_struktur_typ_gueltig(&falsch));

    Teil t;
    memset(&t, 0xab, sizeof(t)); /* Padding-Muster darf nicht in die Box */
    t.id = 7; t.pos.x = 1.5f; t.pos.y = 2.0f; t.masse = 10.0;
    MooStructBox *box = NULL, *sicht = NULL, *sicht2 = NULL, *falsche = NULL;
    assert(moo_struct_box_new_typ(&teil_typ, &t, &box) == MOO_STRUCT_OK);
    assert(moo_struct_box_typ(box) == &teil_typ);
    unsigned char *daten = moo_struct_box_daten(box);
    for (size_t i = offsetof(Teil, pos) + sizeof(Innen); i < offsetof(Teil, masse); ++i)
        assert(daten[i] == 0);
    assert(moo_struct_box_sicht(box, &teil_typ, 8, &falsche) == MOO_STRUCT_INVALID && !falsche);
    assert(moo_struct_box_sicht(box, &innen_typ, offsetof(Teil, pos), &sicht) == MOO_STRUCT_OK);
    assert(moo_struct_box_typ(sicht) == &innen_typ);
    /* Sicht auf Sicht verweist auf dieselbe Wurzel und denselben Speicher. */
    assert(moo_struct_box_sicht(sicht, &innen_typ, 0, &sicht2) == MOO_STRUCT_OK);
    float neu = 5.0f;
    memcpy(moo_struct_box_daten(sicht2) + offsetof(Innen, x), &neu, sizeof(neu));
    Teil kopie;
    memcpy(&kopie, moo_struct_box_daten(box), sizeof(kopie));
    assert(kopie.pos.x == 5.0f && kopie.id == 7);
    /* Besitzer zuerst freigeben: die Sichten halten die Wurzel am Leben. */
    moo_struct_box_drop(box);
    float gelesen = 0.0f;
    memcpy(&gelesen, moo_struct_box_daten(sicht), sizeof(gelesen));
    assert(gelesen == 5.0f);
    moo_struct_box_drop(sicht);
    memcpy(&gelesen, moo_struct_box_daten(sicht2) + offsetof(Innen, y), sizeof(gelesen));
    assert(gelesen == 2.0f);
    moo_struct_box_drop(sicht2); /* gibt die Wurzel frei (LSan: kein Leck) */

    Record r = {1, 2};
    MooStructBox *alt = NULL;
    assert(moo_struct_box_new(&record, &r, &alt) == MOO_STRUCT_OK);
    assert(moo_struct_box_typ(alt) == NULL); /* Alt-API: ohne Feldbeschreibung */
    moo_struct_box_drop(alt);
}

int main(void) {
    Record input = {7, 42}, result;
    MooStructBox* box = NULL;
    uint32_t n = 0, replacement = 99;
    assert(moo_struct_box_new(&record, &input, &box) == MOO_STRUCT_OK);
    input.b = 1;
    assert(moo_struct_box_get(box, "b", &n, sizeof(n)) == MOO_STRUCT_OK && n == 42);
    assert(moo_struct_box_set(box, "b", &replacement, sizeof(replacement)) == MOO_STRUCT_OK);
    assert(moo_struct_box_get(box, "missing", &n, sizeof(n)) == MOO_STRUCT_FIELD);
    assert(moo_struct_box_get(box, "b", &n, 1) == MOO_STRUCT_FIELD);
    assert(moo_struct_box_copy(box, &other, &result) == MOO_STRUCT_TYPE);
    memset(&result, 0xff, sizeof(result));
    assert(moo_struct_box_copy(box, &record, &result) == MOO_STRUCT_OK);
    assert(result.a == 7 && result.b == 99);
    for (size_t i = sizeof(result.a); i < offsetof(Record, b); ++i)
        assert(((unsigned char*)&result)[i] == 0); /* padding is not copied */
    assert(moo_struct_box_retain(box) == MOO_STRUCT_OK);
    moo_struct_box_drop(box);
    assert(moo_struct_box_get(box, "b", &n, sizeof(n)) == MOO_STRUCT_OK && n == 99);
    moo_struct_box_drop(box);

    MooStructField bad = {"x", sizeof(Record) - 1, 2};
    MooStructDescriptor invalid = {"bad", sizeof(Record), _Alignof(Record), 1, &bad};
    box = NULL;
    assert(moo_struct_box_new(&invalid, &input, &box) == MOO_STRUCT_INVALID && !box);
    MooStructField duplicate[] = {{"x", 0, 1}, {"x", 1, 1}};
    invalid.fields = duplicate;
    invalid.field_count = 2;
    assert(moo_struct_box_new(&invalid, &input, &box) == MOO_STRUCT_INVALID && !box);
    MooStructField overlap[] = {{"x", 0, 2}, {"y", 1, 1}};
    invalid.fields = overlap;
    assert(moo_struct_box_new(&invalid, &input, &box) == MOO_STRUCT_INVALID && !box);
    test_v2_und_sicht();
    return 0;
}
