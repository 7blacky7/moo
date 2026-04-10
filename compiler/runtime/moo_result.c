/// Result-Typ fuer moo — ok/err Wrapper ueber MooDict.
/// Speichert "__result_type" = "ok"/"err" und "__result_value" = Wert.

#include "moo_runtime.h"

// Vorwaertsdeklarationen der Dict-Funktionen
extern MooValue moo_dict_new(void);
extern void moo_dict_set(MooValue dict, MooValue key, MooValue value);
extern MooValue moo_dict_get(MooValue dict, MooValue key);
extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

static MooValue make_result(const char* type, MooValue value) {
    MooValue dict = moo_dict_new();
    MooValue type_key = moo_string_new("__result_type");
    MooValue type_val = moo_string_new(type);
    moo_dict_set(dict, type_key, type_val);

    MooValue val_key = moo_string_new("__result_value");
    moo_dict_set(dict, val_key, value);

    return dict;
}

static const char* get_result_type(MooValue result) {
    if (result.tag != MOO_DICT) return NULL;
    MooValue type_key = moo_string_new("__result_type");
    MooValue type_val = moo_dict_get(result, type_key);
    if (type_val.tag != MOO_STRING) return NULL;
    MooString* s = (MooString*)(uintptr_t)type_val.data;
    return s->chars;
}

MooValue moo_result_ok(MooValue value) {
    return make_result("ok", value);
}

MooValue moo_result_err(MooValue msg) {
    return make_result("err", msg);
}

MooValue moo_result_is_ok(MooValue result) {
    const char* type = get_result_type(result);
    return moo_bool(type != NULL && strcmp(type, "ok") == 0);
}

MooValue moo_result_is_err(MooValue result) {
    const char* type = get_result_type(result);
    return moo_bool(type != NULL && strcmp(type, "err") == 0);
}

MooValue moo_result_unwrap(MooValue result) {
    const char* type = get_result_type(result);
    if (type == NULL) {
        moo_throw(moo_string_new("unwrap auf Nicht-Result aufgerufen"));
        return moo_none();
    }
    if (strcmp(type, "err") == 0) {
        MooValue val_key = moo_string_new("__result_value");
        MooValue err_val = moo_dict_get(result, val_key);
        moo_throw(err_val);
        return moo_none();
    }
    MooValue val_key = moo_string_new("__result_value");
    return moo_dict_get(result, val_key);
}
