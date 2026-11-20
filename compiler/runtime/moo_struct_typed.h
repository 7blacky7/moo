#ifndef MOO_STRUCT_TYPED_H
#define MOO_STRUCT_TYPED_H

#include "moo_struct_value.h"
#include "moo_buffer.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version 1: scalar IDs parallel base->fields. Descriptor, base, fields and
 * names must outlive every box; base pointer is the nominal identity token.
 * This first contract accepts unpacked i32 leaves only. */
#define MOO_STRUCT_TYPED_VERSION 1u
typedef struct {
    uint32_t version;
    const MooStructDescriptor *base;
    size_t field_count;
    const MooBufferScalarType *field_types;
} MooStructTypedDescriptor;

MooStructStatus moo_struct_typed_i32_new(const MooStructTypedDescriptor *type,
                                         const void *source, MooValue *out);
MooStructStatus moo_struct_typed_i32_get(MooValue value,
                                         const MooStructTypedDescriptor *type,
                                         size_t field, int32_t *out);
MooStructStatus moo_struct_typed_i32_set(MooValue value,
                                         const MooStructTypedDescriptor *type,
                                         size_t field, int32_t input);

/* P2-T49 (Plan 38c59c64 E3): Version 2 - vom Compiler als Konstante erzeugte
 * Laufzeitbeschreibung eines Strukturwerts an der Grenze nativ <-> dynamisch.
 * Identitaet ist typ_id (NativeStructRegistry::struktur_id), nicht die Adresse.
 * basis listet die flachen Blaetter (Kopie, Padding bleibt 0); felder die
 * deklarierten Felder der obersten Ebene (Name, Ausgabe, Gleichheit).
 * Arrayfelder sind flach beschrieben: anzahl Elemente der Art element mit
 * Stride groesse / anzahl. Zeigerfelder sind nicht boxbar (E5). Version 1
 * bleibt unveraendert. Layout ist ABI des Compilers (codegen.rs). */
#define MOO_STRUKTUR_TYP_VERSION 2u
#define MOO_STRUKTUR_MAX_TIEFE 32u
enum { MOO_FELD_SKALAR = 1, MOO_FELD_BOOL = 2, MOO_FELD_STRUKTUR = 3, MOO_FELD_ARRAY = 4 };
typedef struct MooStrukturTyp MooStrukturTyp;
typedef struct {
    const char *name;
    uint32_t versatz;
    uint32_t groesse;
    uint8_t art;        /* MOO_FELD_* */
    uint8_t element;    /* Art je Element; ohne Array gleich art */
    uint8_t skalar;     /* MooBufferScalarType bei Skalar-Elementen, sonst 0 */
    uint8_t reserviert;
    uint32_t anzahl;    /* Elementanzahl, 1 ohne Array */
    const MooStrukturTyp *struktur; /* Elementstruktur bei MOO_FELD_STRUKTUR */
} MooStrukturFeld;
struct MooStrukturTyp {
    uint32_t version;
    uint32_t feld_anzahl;
    uint64_t typ_id;
    MooStructDescriptor basis;
    const MooStrukturFeld *felder;
};

/* 1 = gueltige Beschreibung (rekursiv, hoechstens MOO_STRUKTUR_MAX_TIEFE). */
int moo_struktur_typ_gueltig(const MooStrukturTyp *typ);
/* Kopierende Box mit Laufzeitbeschreibung (Padding 0). */
MooStructStatus moo_struct_box_new_typ(const MooStrukturTyp *typ, const void *source,
                                       MooStructBox **out);
/* Sicht auf eine eingebettete Struktur bei versatz: teilt den Speicher und haelt
 * die Wurzelbox am Leben (Sicht auf Sicht verweist direkt auf die Wurzel). */
MooStructStatus moo_struct_box_sicht(MooStructBox *besitzer, const MooStrukturTyp *typ,
                                     size_t versatz, MooStructBox **out);
/* NULL bei Boxen der Alt-API (ohne Feldbeschreibung). */
const MooStrukturTyp *moo_struct_box_typ(const MooStructBox *box);
unsigned char *moo_struct_box_daten(const MooStructBox *box);
/* P2-T88: Deskriptor am Speicher, von jeder Pufferansicht geerbt. wert geliehen;
 * ohne boxbare Beschreibung bleibt die bisherige native Nutzung möglich. */
void moo_buffer_struct_beschreiben(const MooValue *wert, const MooStrukturTyp *typ);

#ifdef __cplusplus
}
#endif
#endif
