#include "moo_runtime.h"

MooValue moo_to_string(MooValue v) {
    char buf[64];
    switch (v.tag) {
        case MOO_NUMBER: {
            double n = v.data.number;
            if (n == (int64_t)n)
                snprintf(buf, sizeof(buf), "%lld", (long long)n);
            else
                snprintf(buf, sizeof(buf), "%g", n);
            return moo_string_new(buf);
        }
        case MOO_STRING: return v;
        case MOO_BOOL:
            return moo_string_new(v.data.boolean ? "wahr" : "falsch");
        case MOO_NONE:
            return moo_string_new("nichts");
        case MOO_LIST: {
            MooList* l = v.data.list;
            MooValue result = moo_string_new("[");
            for (int32_t i = 0; i < l->length; i++) {
                if (i > 0) result = moo_string_concat(result, moo_string_new(", "));
                MooValue item_str = moo_to_string(l->items[i]);
                if (l->items[i].tag == MOO_STRING) {
                    result = moo_string_concat(result, moo_string_new("\""));
                    result = moo_string_concat(result, item_str);
                    result = moo_string_concat(result, moo_string_new("\""));
                } else {
                    result = moo_string_concat(result, item_str);
                }
            }
            return moo_string_concat(result, moo_string_new("]"));
        }
        case MOO_DICT: {
            MooDict* d = v.data.dict;
            MooValue result = moo_string_new("{");
            bool first = true;
            for (int32_t i = 0; i < d->capacity; i++) {
                if (!d->entries[i].occupied) continue;
                if (!first) result = moo_string_concat(result, moo_string_new(", "));
                first = false;
                result = moo_string_concat(result, moo_string_new("\""));
                MooValue key; key.tag = MOO_STRING; key.data.string = d->entries[i].key;
                result = moo_string_concat(result, key);
                result = moo_string_concat(result, moo_string_new("\": "));
                result = moo_string_concat(result, moo_to_string(d->entries[i].value));
            }
            return moo_string_concat(result, moo_string_new("}"));
        }
        case MOO_OBJECT: {
            snprintf(buf, sizeof(buf), "<Objekt %s>", v.data.object->class_name);
            return moo_string_new(buf);
        }
        case MOO_ERROR:
            return moo_string_new(v.data.error_msg);
        default:
            return moo_string_new("<unbekannt>");
    }
}

void moo_print(MooValue v) {
    MooValue s = moo_to_string(v);
    printf("%s\n", s.data.string->chars);
}
