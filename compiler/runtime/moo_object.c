#include "moo_runtime.h"

MooValue moo_object_new(const char* class_name) {
    MooValue v;
    v.tag = MOO_OBJECT;
    MooObject* obj = moo_alloc(sizeof(MooObject));
    obj->refcount = 1;
    obj->frozen = false;
    obj->class_name = strdup(class_name);
    obj->prop_count = 0;
    obj->prop_capacity = 8;
    obj->properties = moo_alloc(sizeof(MooProperty) * obj->prop_capacity);
    obj->parent = NULL;
    moo_val_set_ptr(&v, obj);
    return v;
}

static int32_t find_property(MooObject* obj, const char* name) {
    for (int32_t i = 0; i < obj->prop_count; i++)
        if (strcmp(obj->properties[i].name->chars, name) == 0) return i;
    return -1;
}

MooValue moo_object_get(MooValue obj_val, const char* prop) {
    // Dot-Access-Aequivalenz: g.key soll auch auf Dicts funktionieren.
    // Ohne Tag-Dispatch wuerde MV_OBJ(dict) den MooDict*-Pointer als
    // MooObject* fehlinterpretieren und beim Lesen der prop_count/
    // properties-Felder in Garbage laufen (Segfault), besonders wenn der
    // Dict pointer-tagged MooValues (HWND/GtkWidget/fn-Handle) enthaelt.
    // Siehe moo_index_get fuer das analoge Dispatch-Muster.
    if (obj_val.tag == MOO_DICT) {
        MooValue key = moo_string_new(prop);
        MooValue result = moo_dict_get(obj_val, key);
        // moo_dict_get uebernimmt die key-Ref (transfer), wir muessen
        // nichts mehr freigeben.
        return result;
    }
    if (obj_val.tag != MOO_OBJECT) {
        return moo_none();
    }
    MooObject* obj = MV_OBJ(obj_val);
    int32_t idx = find_property(obj, prop);
    if (idx >= 0) {
        MooValue v = obj->properties[idx].value;
        moo_retain(v);  // Owning-Rueckgabe
        return v;
    }
    if (obj->parent) {
        MooValue parent_val;
        parent_val.tag = MOO_OBJECT;
        moo_val_set_ptr(&parent_val, obj->parent);
        return moo_object_get(parent_val, prop);
    }
    return moo_none();
}

void moo_object_set(MooValue obj_val, const char* prop, MooValue value) {
    // Dot-Access-Aequivalenz: g.key = v soll auch auf Dicts funktionieren.
    // Sonst wuerde MV_OBJ(dict) in frozen/prop_count/properties-Feldern
    // am falschen Offset lesen und in moo_dict_set/-writes crashen.
    if (obj_val.tag == MOO_DICT) {
        MooValue key = moo_string_new(prop);
        // moo_dict_set uebernimmt Ownership von key und value (transfer).
        moo_dict_set(obj_val, key, value);
        return;
    }
    if (obj_val.tag != MOO_OBJECT) {
        // Unbekannter Typ — value-Ref freigeben (Caller hatte transferiert)
        moo_release(value);
        return;
    }
    MooObject* obj = MV_OBJ(obj_val);
    if (obj->frozen) { moo_throw(moo_string_new("Objekt ist eingefroren!")); return; }
    int32_t idx = find_property(obj, prop);
    if (idx >= 0) {
        // Transfer: neuen Caller-Ref uebernehmen, alten freigeben.
        MooValue old = obj->properties[idx].value;
        obj->properties[idx].value = value;
        moo_release(old);
        return;
    }
    if (obj->prop_count >= obj->prop_capacity) {
        obj->prop_capacity *= 2;
        obj->properties = moo_realloc(obj->properties, sizeof(MooProperty) * obj->prop_capacity);
    }
    MooString* name_str = moo_alloc(sizeof(MooString));
    name_str->refcount = 1;
    int32_t len = strlen(prop);
    name_str->chars = strdup(prop);
    name_str->length = len;
    name_str->capacity = len + 1;
    // Transfer: Object uebernimmt Caller-Ref.
    obj->properties[obj->prop_count].name = name_str;
    obj->properties[obj->prop_count].value = value;
    obj->prop_count++;
}

void moo_object_set_parent(MooValue obj, MooValue parent) {
    if (obj.tag == MOO_OBJECT && parent.tag == MOO_OBJECT)
        MV_OBJ(obj)->parent = MV_OBJ(parent);
}

// Gibt den statischen Klassennamen eines Objekts zurueck ("" wenn kein Objekt).
// Vom Codegen fuer dynamic method dispatch genutzt.
const char* moo_object_class_name(MooValue obj) {
    if (obj.tag != MOO_OBJECT) return "";
    MooObject* o = MV_OBJ(obj);
    if (!o || !o->class_name) return "";
    return o->class_name;
}

// === Event-System ===
// Events werden als "__event_<name>" Property gespeichert (MooList von Callbacks)

void moo_event_on(MooValue obj, MooValue event_name, MooValue callback) {
    if (obj.tag != MOO_OBJECT || event_name.tag != MOO_STRING) return;
    const char* name = MV_STR(event_name)->chars;

    // Property-Key: "__event_<name>"
    char key[256];
    snprintf(key, sizeof(key), "__event_%s", name);

    MooValue list = moo_object_get(obj, key);
    if (list.tag != MOO_LIST) {
        // Neue Event-Liste erstellen
        list = moo_list_new(4);
        // object_set transferiert Caller-Ref → extra retain damit wir
        // gleich noch list_append aufrufen koennen.
        moo_retain(list);
        moo_object_set(obj, key, list);
    }
    // list_append transferiert callback-Ref.
    moo_list_append(list, callback);
    moo_release(list);  // unsere Owning-Ref aus moo_object_get/new freigeben
}

void moo_event_emit(MooValue obj, MooValue event_name) {
    if (obj.tag != MOO_OBJECT || event_name.tag != MOO_STRING) return;
    const char* name = MV_STR(event_name)->chars;

    char key[256];
    snprintf(key, sizeof(key), "__event_%s", name);

    MooValue list = moo_object_get(obj, key);
    if (list.tag != MOO_LIST) return;

    MooList* callbacks = MV_LIST(list);
    for (int32_t i = 0; i < callbacks->length; i++) {
        MooValue cb = callbacks->items[i];
        if (cb.tag == MOO_FUNC) {
            MooFunc* fn = MV_FUNC(cb);
            // Closure- vs. Plain-Call-Konvention (vgl. moo_thread.c)
            if (fn->n_captured > 0) {
                typedef MooValue (*MooTramp1)(MooFunc*, MooValue);
                MooTramp1 tramp = (MooTramp1)fn->fn_ptr;
                tramp(fn, event_name);
            } else {
                typedef MooValue (*MooFn1)(MooValue);
                MooFn1 func = (MooFn1)fn->fn_ptr;
                func(event_name);
            }
        }
    }
}
