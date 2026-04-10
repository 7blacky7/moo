#include "moo_runtime.h"

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

MooValue moo_error(const char* msg) {
    MooValue v;
    v.tag = MOO_ERROR;
    moo_val_set_ptr(&v, strdup(msg));
    return v;
}

bool moo_is_number(MooValue v) { return v.tag == MOO_NUMBER; }
bool moo_is_string(MooValue v) { return v.tag == MOO_STRING; }
bool moo_is_bool(MooValue v)   { return v.tag == MOO_BOOL; }
bool moo_is_none(MooValue v)   { return v.tag == MOO_NONE; }
bool moo_is_list(MooValue v)   { return v.tag == MOO_LIST; }
bool moo_is_dict(MooValue v)   { return v.tag == MOO_DICT; }

bool moo_is_truthy(MooValue v) {
    switch (v.tag) {
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
        case MOO_NUMBER: return "Zahl";
        case MOO_STRING: return "Text";
        case MOO_BOOL:   return "Wahrheitswert";
        case MOO_NONE:   return "Nichts";
        case MOO_LIST:   return "Liste";
        case MOO_DICT:   return "Woerterbuch";
        case MOO_FUNC:   return "Funktion";
        case MOO_OBJECT: return "Objekt";
        case MOO_ERROR:  return "Fehler";
        default:         return "Unbekannt";
    }
}

double moo_as_number(MooValue v) {
    if (v.tag == MOO_NUMBER) return MV_NUM(v);
    if (v.tag == MOO_BOOL) return MV_BOOL(v) ? 1.0 : 0.0;
    return 0.0;
}

bool moo_as_bool(MooValue v) {
    return moo_is_truthy(v);
}

MooValue moo_type_of(MooValue v) {
    return moo_string_new(moo_type_name(v));
}
