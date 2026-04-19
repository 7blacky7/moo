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
    // moo_dict_get konsumiert den Key bereits intern (release_key_str_if_fresh),
    // deshalb HIER KEIN release(type_key) — das waere Double-Free (tcache).
    MooValue type_val = moo_dict_get(result, type_key);
    if (type_val.tag != MOO_STRING) {
        // Auch Nicht-String-Rueckgabe muss released werden (dict_get +1).
        moo_release(type_val);
        return NULL;
    }
    MooString* s = (MooString*)(uintptr_t)type_val.data;
    // RB8: dict_get hat type_val mit retain (+1) zurueckgegeben. Das Dict
    // haelt seine eigene +1 → nach release bleibt der String via Dict-Ref
    // lebendig. s->chars bleibt gueltig bis der Result-Dict freigegeben wird.
    moo_release(type_val);
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
        // val_key wird von dict_get intern released — hier nicht anfassen.
        MooValue val_key = moo_string_new("__result_value");
        MooValue err_val = moo_dict_get(result, val_key);
        // RB8: err_val hat +1 aus dict_get; throw speichert ohne retain.
        // Nicht releasen — sonst haengt moo_last_error auf rc=0 sobald
        // der Result-Dict freigegeben wird. Kleiner Leak in try-catch
        // akzeptiert (Fehler sind selten; bei fatal exit irrelevant).
        moo_throw(err_val);
        return moo_none();
    }
    // RB8: val_key wird von dict_get intern released. val behaelt sein
    // +1 aus dict_get und wird zum Caller transferiert (owning return).
    MooValue val_key = moo_string_new("__result_value");
    return moo_dict_get(result, val_key);
}
