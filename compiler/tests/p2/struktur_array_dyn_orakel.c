/* P2-T88: echte Runtime, keine Ersatz-Refcounts oder Leihfunktionen. */
#include "../../runtime/moo_struct_dyn.h"
#include "../../runtime/moo_buffer_value.h"
#include "../../runtime/moo_buffer_priv.h"
#include <assert.h>
#include <stdint.h>

static const MooStructField blaetter[] = {
    {"zahlen[0]", 0, 4}, {"zahlen[1]", 4, 4}, {"zahlen[2]", 8, 4}
};
static const MooStrukturFeld felder[] = {
    {"zahlen", 0, 12, MOO_FELD_ARRAY, MOO_FELD_SKALAR, MOO_BUFFER_I32, 0, 3, NULL}
};
static const MooStrukturTyp typ = {
    MOO_STRUKTUR_TYP_VERSION, 1, UINT64_C(0x88778877),
    {"Orakel", 12, 4, 3, blaetter}, felder
};

int32_t struktur_array_orakel(void) {
    int32_t werte[] = {1, 2, 3};
    MooValue owner;
    moo_struct_box_value(&typ, werte, &owner);
    assert(owner.tag == MOO_STRUCT);
    MooValue a = moo_struct_feld_get(owner, "zahlen");
    MooBuffer *buffer = moo_val_as_ptr(a), *sub = NULL;
    assert(moo_buffer_view(buffer, 1, 2, &sub) == MOO_BUFFER_OK);
    MooBufferPin *pin = NULL;
    void *data = NULL;
    assert(moo_buffer_pin(sub, &pin, &data) == MOO_BUFFER_OK);
    moo_release(owner);
    moo_release(a);
    moo_buffer_drop(sub);
    /* Nur noch der Pin besitzt den Speicher und mittelbar die Struktur. */
    assert(((int32_t*)data)[0] == 2 && ((int32_t*)data)[1] == 3);
    ((int32_t*)data)[0] = 42;
    moo_buffer_unpin(pin);

    moo_struct_box_value(&typ, werte, &owner);
    a = moo_struct_feld_get(owner, "zahlen");
    buffer = moo_val_as_ptr(a);
    assert(moo_buffer_view(buffer, 0, 3, &sub) == MOO_BUFFER_OK);
    MooValue b = {MOO_BUFFER, 0};
    moo_val_set_ptr(&b, sub);
    /* Zwei Handles teilen EINE Besitzerreferenz im Speicher. Der externe
     * owner darf deshalb nicht als ausschliesslicher Sperreninhalt gelten. */
    MooValue items[] = {a, b};
    MooList liste = {0};
    liste.refcount = 1; liste.length = liste.capacity = 2; liste.items = items;
    MooValue graph = {MOO_LIST, 0};
    moo_val_set_ptr(&graph, &liste);
    moo_sperre_unterelemente_sichern(graph);
    MooStructBox *box = moo_val_as_ptr(owner);
    assert(moo_rc_ist_geteilt(&box->refcount));
    assert(moo_buffer_geteilt(buffer));
    /* Auch eine nach dem Teilen erzeugte Sicht erbt den Schreibschutz. */
    MooValue neu = moo_struct_feld_get(owner, "zahlen");
    assert(moo_buffer_geteilt(moo_val_as_ptr(neu)));
    moo_release(neu); moo_release(a); moo_release(b); moo_release(owner);
    return 0;
}
