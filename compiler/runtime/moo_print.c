#include "moo_runtime.h"

MooValue moo_to_string(MooValue v) {
    char buf[64];
    switch (v.tag) {
        case MOO_NUMBER: {
            double n = MV_NUM(v);
            if (n == (int64_t)n)
                snprintf(buf, sizeof(buf), "%lld", (long long)n);
            else
                snprintf(buf, sizeof(buf), "%g", n);
            return moo_string_new(buf);
        }
        case MOO_STRING: return v;
        case MOO_BOOL:
            return moo_string_new(MV_BOOL(v) ? "wahr" : "falsch");
        case MOO_NONE:
            return moo_string_new("nichts");
        case MOO_LIST: {
            // RB6 (v2): concat-Kette released alte result-Zwischenwerte.
            // Items sind GELIEHEN (l->items[i]); item_str nur releasen wenn
            // moo_to_string einen frischen (+1 owning) Wert erzeugt hat
            // (d.h. item.tag != MOO_STRING). Bei String-Items ist item_str
            // das Item selbst (to_string Pass-Through) → NICHT releasen.
            MooList* l = MV_LIST(v);
            MooValue result = moo_string_new("[");
            for (int32_t i = 0; i < l->length; i++) {
                if (i > 0) {
                    MooValue sep = moo_string_new(", ");
                    MooValue t = moo_string_concat(result, sep);
                    moo_release(result); moo_release(sep);
                    result = t;
                }
                if (l->items[i].tag == MOO_STRING) {
                    MooValue q1 = moo_string_new("\"");
                    MooValue t1 = moo_string_concat(result, q1);
                    moo_release(result); moo_release(q1);
                    result = t1;
                    // Item selbst ist geliehen → concat fasst es nicht an.
                    MooValue t2 = moo_string_concat(result, l->items[i]);
                    moo_release(result);
                    result = t2;
                    MooValue q2 = moo_string_new("\"");
                    MooValue t3 = moo_string_concat(result, q2);
                    moo_release(result); moo_release(q2);
                    result = t3;
                } else {
                    MooValue item_str = moo_to_string(l->items[i]);
                    MooValue t = moo_string_concat(result, item_str);
                    moo_release(result); moo_release(item_str);
                    result = t;
                }
            }
            MooValue close = moo_string_new("]");
            MooValue final_r = moo_string_concat(result, close);
            moo_release(result); moo_release(close);
            return final_r;
        }
        case MOO_DICT: {
            // RB6 (v2): wie MOO_LIST. Keys sind MooString* im Dict-Slot
            // (geliehen; wir wrappen sie nur in eine MooValue). Values sind
            // ebenfalls geliehen (dict-owned). moo_to_string fuer String-Value
            // liefert das Value selbst zurueck → NICHT releasen.
            MooDict* d = MV_DICT(v);
            MooValue result = moo_string_new("{");
            bool first = true;
            for (int32_t i = 0; i < d->capacity; i++) {
                if (!d->entries[i].occupied) continue;
                if (!first) {
                    MooValue sep = moo_string_new(", ");
                    MooValue t = moo_string_concat(result, sep);
                    moo_release(result); moo_release(sep);
                    result = t;
                }
                first = false;
                MooValue q1 = moo_string_new("\"");
                MooValue t1 = moo_string_concat(result, q1);
                moo_release(result); moo_release(q1);
                result = t1;

                MooValue key;
                key.tag = MOO_STRING;
                moo_val_set_ptr(&key, d->entries[i].key);  // geliehen
                MooValue t2 = moo_string_concat(result, key);
                moo_release(result);
                result = t2;

                MooValue qc = moo_string_new("\": ");
                MooValue t3 = moo_string_concat(result, qc);
                moo_release(result); moo_release(qc);
                result = t3;

                if (d->entries[i].value.tag == MOO_STRING) {
                    MooValue t4 = moo_string_concat(result, d->entries[i].value);
                    moo_release(result);
                    result = t4;
                } else {
                    MooValue vs = moo_to_string(d->entries[i].value);
                    MooValue t4 = moo_string_concat(result, vs);
                    moo_release(result); moo_release(vs);
                    result = t4;
                }
            }
            MooValue close = moo_string_new("}");
            MooValue final_r = moo_string_concat(result, close);
            moo_release(result); moo_release(close);
            return final_r;
        }
        case MOO_OBJECT: {
            snprintf(buf, sizeof(buf), "<Objekt %s>", MV_OBJ(v)->class_name);
            return moo_string_new(buf);
        }
        case MOO_ERROR:
            return moo_string_new(MV_ERR(v));
        case MOO_THREAD:
            return moo_string_new("<Thread>");
        case MOO_CHANNEL:
            return moo_string_new("<Kanal>");
        case MOO_DATABASE:
            return moo_string_new("<Datenbank>");
        case MOO_WINDOW:
            return moo_string_new("<Fenster>");
        case MOO_WINDOW3D:
            return moo_string_new("<3D-Fenster>");
        case MOO_SOCKET:
            return moo_string_new("<Socket>");
        case MOO_WEBSERVER:
            return moo_string_new("<WebServer>");
        default:
            return moo_string_new("<unbekannt>");
    }
}

void moo_print(MooValue v) {
    MooValue s = moo_to_string(v);
    printf("%s\n", MV_STR(s)->chars);
}
