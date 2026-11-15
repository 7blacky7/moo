#include "moo_handle.h"

enum { MOO_HANDLE_GUELTIG = 1, MOO_HANDLE_FREIGEGEBEN = 2 };

struct MooHandle {
    int32_t refcount; /* Generischer Heap-Release erwartet das als erstes Feld. */
    int32_t zustand;
    void* zeiger;
    uint64_t typ_id;
};

/* Thread-confined wie die uebrige Laufzeit-Heapverwaltung. */
static int64_t moo_handle_offen = 0;

static MooHandle* handle_of(const MooValue* value) {
    if (!value || value->tag != MOO_HANDLE) return NULL;
    return (MooHandle*)moo_val_as_ptr(*value);
}

MooValue moo_handle_box(void* zeiger, uint64_t typ_id) {
    if (!zeiger) return moo_none();
    MooHandle* h = (MooHandle*)malloc(sizeof(*h));
    if (!h) {
        moo_throw(moo_error("Griff: kein Speicher"));
        return moo_none();
    }
    h->refcount = 1;
    h->zustand = MOO_HANDLE_GUELTIG;
    h->zeiger = zeiger;
    h->typ_id = typ_id;
    moo_handle_offen++;
    MooValue v;
    v.tag = MOO_HANDLE;
    moo_val_set_ptr(&v, h);
    return v;
}

int32_t moo_handle_unbox(const MooValue* value, uint64_t typ_id, void** out) {
    if (out) *out = NULL;
    if (!value || !out) {
        moo_throw(moo_error("Griff: ungueltiger Aufruf"));
        return 0;
    }
    if (value->tag == MOO_NONE) return 1;
    MooHandle* h = handle_of(value);
    if (!h) {
        char text[96];
        snprintf(text, sizeof(text), "Griff erwartet, erhalten: %s", moo_type_name(*value));
        moo_throw(moo_error(text));
        return 0;
    }
    if (h->zustand != MOO_HANDLE_GUELTIG) {
        moo_throw(moo_error("Griff wurde bereits freigegeben"));
        return 0;
    }
    if (h->typ_id != typ_id) {
        moo_throw(moo_error("Griff hat einen anderen Zeigertyp als der Parameter erwartet"));
        return 0;
    }
    *out = h->zeiger;
    return 1;
}

void moo_handle_close(const MooValue* value) {
    MooHandle* h = handle_of(value);
    if (!h || h->zustand != MOO_HANDLE_GUELTIG) return;
    h->zustand = MOO_HANDLE_FREIGEGEBEN;
    h->zeiger = NULL;
    moo_handle_offen--;
}

int64_t moo_handle_live_count(void) {
    return moo_handle_offen;
}

MooValue moo_handle_valid(MooValue value) {
    MooHandle* h = handle_of(&value);
    return moo_bool(h && h->zustand == MOO_HANDLE_GUELTIG);
}

MooValue moo_handle_open_count(void) {
    return moo_number((double)moo_handle_offen);
}

bool moo_handle_equal(MooValue a, MooValue b) {
    MooHandle* x = handle_of(&a);
    MooHandle* y = handle_of(&b);
    if (!x || !y) return false;
    if (x == y) return true;
    return x->zustand == MOO_HANDLE_GUELTIG && y->zustand == MOO_HANDLE_GUELTIG
        && x->zeiger == y->zeiger && x->typ_id == y->typ_id;
}

const char* moo_handle_text(MooValue value) {
    MooHandle* h = handle_of(&value);
    return (h && h->zustand == MOO_HANDLE_GUELTIG) ? "<Griff>" : "<Griff freigegeben>";
}
