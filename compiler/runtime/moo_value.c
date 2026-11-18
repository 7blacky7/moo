#include "moo_runtime.h"

#ifdef MOO_UEFI_COLLECTIONS
#include "moo_bare_wert.h"
#else
MooValue moo_number(double n) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_double(&v, n);
    return v;
}

MooValue moo_bool(bool b) {
    MooValue v;
    v.tag = MOO_BOOL;
    moo_val_set_bool(&v, b);
    return v;
}

MooValue moo_none(void) {
    MooValue v;
    v.tag = MOO_NONE;
    v.data = 0;
    return v;
}
#endif

MooValue moo_error(const char* msg) {
    MooError* e = (MooError*)malloc(sizeof(MooError));
    if (!e) return moo_none();
    e->refcount = 1;
    e->chars = strdup(msg ? msg : "");
    if (!e->chars) { free(e); return moo_none(); }
    MooValue v;
    v.tag = MOO_ERROR;
    moo_val_set_ptr(&v, e);
    return v;
}

bool moo_is_number(MooValue v) {
#ifdef MOO_UEFI_COLLECTIONS
    return moo_wert_ist_zahl(v.tag);
#else
    return v.tag == MOO_NUMBER;
#endif
}
bool moo_is_string(MooValue v) { return v.tag == MOO_STRING; }
bool moo_is_bool(MooValue v)   { return v.tag == MOO_BOOL; }
#ifndef MOO_UEFI_COLLECTIONS
bool moo_is_none(MooValue v)   { return v.tag == MOO_NONE; }
#endif
bool moo_is_list(MooValue v)   { return v.tag == MOO_LIST; }
bool moo_is_dict(MooValue v)   { return v.tag == MOO_DICT; }

bool moo_is_truthy(MooValue v) {
    switch (v.tag) {
#ifdef MOO_UEFI_COLLECTIONS
        case MOO_WERT_TAG_I64:
        case MOO_WERT_TAG_U64: return v.data != 0;
#endif
        case MOO_NUMBER: return MV_NUM(v) != 0.0;
        case MOO_STRING: return MV_STR(v)->length > 0;
        case MOO_BOOL:   return MV_BOOL(v);
        case MOO_NONE:   return false;
        case MOO_LIST:   return MV_LIST(v)->length > 0;
        case MOO_DICT:   return MV_DICT(v)->count > 0;
        default:         return true;
    }
}

const char* moo_type_name(MooValue v) {
    switch (v.tag) {
#ifdef MOO_UEFI_COLLECTIONS
        case MOO_WERT_TAG_I64:
        case MOO_WERT_TAG_U64:
#endif
        case MOO_NUMBER: return "Zahl";
        case MOO_STRING: return "Text";
        case MOO_BOOL:   return "Wahrheitswert";
        case MOO_NONE:   return "Nichts";
        case MOO_LIST:   return "Liste";
        case MOO_DICT:   return "Woerterbuch";
        case MOO_FUNC:   return "Funktion";
        case MOO_OBJECT: return "Objekt";
        case MOO_ERROR:  return "Fehler";
        case MOO_DATABASE: return "Datenbank";
        case MOO_SURFACE: return "Oberflaeche";
        case MOO_BUFFER: return "Puffer";
        case MOO_STRUCT: return "Struktur";
        case MOO_HANDLE: return "Griff";
        case MOO_ATOM:   return "Atom";
        case MOO_SPERRE: return "Sperre";
        default:         return "Unbekannt";
    }
}

double moo_as_number(MooValue v) {
#ifdef MOO_UEFI_COLLECTIONS
    if (moo_wert_ist_zahl(v.tag)) return MOO_WERT_DOUBLE(v);
#else
    if (v.tag == MOO_NUMBER) return MV_NUM(v);
#endif
    if (v.tag == MOO_BOOL) return MV_BOOL(v) ? 1.0 : 0.0;
    return 0.0;
}

bool moo_as_bool(MooValue v) {
    return moo_is_truthy(v);
}

MooValue moo_type_of(MooValue v) {
    return moo_string_new(moo_type_name(v));
}
