#include "moo_handle.h"
#include "moo_rc.h"

enum { MOO_HANDLE_GUELTIG = 1, MOO_HANDLE_FREIGEGEBEN = 2 };

struct MooHandle {
    int32_t refcount; /* Generischer Heap-Release erwartet das als erstes Feld. */
    int32_t zustand;
    void* zeiger;
    uint64_t typ_id;
    size_t payload_length; /* 0 bei geliehenen Fremdobjekten. */
};

/* Die Griffe selbst sind threadgebunden; verschiedene Moo-Threads duerfen
 * gleichzeitig eigene Griffe erzeugen/schliessen. Der Gesamtzaehler ist atomar. */
static size_t moo_handle_offen = 0;

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
    h->payload_length = 0;
    moo_rc_size_inc(&moo_handle_offen);
    MooValue v;
    v.tag = MOO_HANDLE;
    moo_val_set_ptr(&v, h);
    return v;
}

void moo_handle_box_bytes(const void* bytes, size_t length, uint64_t typ_id, MooValue* out) {
    *out = moo_none();
    if (!bytes || length > SIZE_MAX - sizeof(MooHandle)) {
        moo_throw(moo_error("Griff: ungueltige Bytekopie"));
        return;
    }
    MooHandle* h = (MooHandle*)malloc(sizeof(*h) + length);
    if (!h) {
        moo_throw(moo_error("Griff: kein Speicher"));
        return;
    }
    h->refcount = 1;
    h->zustand = MOO_HANDLE_GUELTIG;
    h->zeiger = (void*)(h + 1);
    h->typ_id = typ_id;
    h->payload_length = length;
    memcpy(h + 1, bytes, length);
    moo_rc_size_inc(&moo_handle_offen);
    out->tag = MOO_HANDLE;
    moo_val_set_ptr(out, h);
}

int32_t moo_handle_payload(const MooValue* value, size_t length, void** out) {
    *out = NULL;
    MooHandle* h = handle_of(value);
    if (!h || !h->payload_length || h->payload_length != length) {
        moo_throw(moo_error("Griff: eigene Nutzdaten passender Groesse erwartet"));
        return 0;
    }
    /* Die eigene Bytekopie bleibt auch nach Close bis zum Box-Release stabil.
     * Hier keine mutable Zustandsdaten lesen: erst der Aufrufer prueft Owner. */
    *out = (void*)(h + 1);
    return 1;
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

int32_t moo_handle_unbox_owned(const MooValue* value, uint64_t typ_id, size_t length,
                               const void* owner, void** out) {
    *out = NULL;
    MooHandle* h = handle_of(value);
    if (!h || h->payload_length != length || length < sizeof(void*)) {
        moo_throw(moo_error("Griff: eigene threadgebundene Nutzdaten erwartet"));
        return 0;
    }
    /* Das erste Payload-Wort ist ein unveraenderlicher Owner-Cookie.
     * VOR dem Lesen von zustand/zeiger pruefen; fremde Threads duerfen diese
     * waehrend Close des Besitzers nicht einmal nicht-atomar lesen. */
    void* actual;
    memcpy(&actual, h + 1, sizeof(actual));
    if (actual != owner) {
        moo_throw(moo_error("Registrierungs-Griff gehoert zu einem anderen Moo-Thread"));
        return 0;
    }
    return moo_handle_unbox(value, typ_id, out);
}

void moo_handle_close(const MooValue* value) {
    MooHandle* h = handle_of(value);
    if (!h || h->zustand != MOO_HANDLE_GUELTIG) return;
    h->zustand = MOO_HANDLE_FREIGEGEBEN;
    h->zeiger = NULL;
    (void)moo_rc_size_dec(&moo_handle_offen);
}

int64_t moo_handle_live_count(void) {
    size_t count = moo_rc_size_lesen(&moo_handle_offen);
    return count > INT64_MAX ? INT64_MAX : (int64_t)count;
}

MooValue moo_handle_valid(MooValue value) {
    MooHandle* h = handle_of(&value);
    return moo_bool(h && h->zustand == MOO_HANDLE_GUELTIG);
}

MooValue moo_handle_open_count(void) {
    return moo_number((double)moo_handle_live_count());
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
