#include "moo_runtime.h"

MooValue moo_object_new(const char* class_name) {
    MooValue v;
    v.tag = MOO_OBJECT;
    MooObject* obj = moo_alloc(sizeof(MooObject));
    obj->class_name = strdup(class_name);
    obj->prop_count = 0;
    obj->prop_capacity = 8;
    obj->properties = moo_alloc(sizeof(MooProperty) * obj->prop_capacity);
    obj->parent = NULL;
    v.data.object = obj;
    return v;
}

static int32_t find_property(MooObject* obj, const char* name) {
    for (int32_t i = 0; i < obj->prop_count; i++) {
        if (strcmp(obj->properties[i].name->chars, name) == 0) return i;
    }
    return -1;
}

MooValue moo_object_get(MooValue obj_val, const char* prop) {
    MooObject* obj = obj_val.data.object;
    int32_t idx = find_property(obj, prop);
    if (idx >= 0) return obj->properties[idx].value;
    if (obj->parent) {
        MooValue parent_val;
        parent_val.tag = MOO_OBJECT;
        parent_val.data.object = obj->parent;
        return moo_object_get(parent_val, prop);
    }
    return moo_none();
}

void moo_object_set(MooValue obj_val, const char* prop, MooValue value) {
    MooObject* obj = obj_val.data.object;
    int32_t idx = find_property(obj, prop);
    if (idx >= 0) {
        obj->properties[idx].value = value;
        return;
    }
    if (obj->prop_count >= obj->prop_capacity) {
        obj->prop_capacity *= 2;
        obj->properties = moo_realloc(obj->properties, sizeof(MooProperty) * obj->prop_capacity);
    }
    MooString* name_str = moo_alloc(sizeof(MooString));
    int32_t len = strlen(prop);
    name_str->chars = strdup(prop);
    name_str->length = len;
    name_str->capacity = len + 1;
    obj->properties[obj->prop_count].name = name_str;
    obj->properties[obj->prop_count].value = value;
    obj->prop_count++;
}

void moo_object_set_parent(MooValue obj, MooValue parent) {
    if (obj.tag == MOO_OBJECT && parent.tag == MOO_OBJECT) {
        obj.data.object->parent = parent.data.object;
    }
}
