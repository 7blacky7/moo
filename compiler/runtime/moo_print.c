#include "moo_handle.h" /* P2-T23/T53: Ausgabe von Griffen */
#include "moo_runtime.h"
#include "moo_struct_dyn.h" /* P2-T49: Ausgabe von Strukturwerten */

/* P2-T44: Faellt eine Teilkonversion (moo_string_concat, geschachteltes
 * moo_to_string) mit moo_throw aus, kehrt der Wurf im try-Kontext zurueck und
 * concat liefert "". Die Formatierung bricht dann sofort ab: keine weiteren
 * concat-Aufrufe (ein zweiter Wurf wuerde den Erstfehler ueberschreiben),
 * Zwischenwerte sind an dieser Stelle schon genau einmal freigegeben, das
 * leere Ergebnis (+1 owning) geht an den Aufrufer. Ein beim Eintritt bereits
 * gesetztes Flag zaehlt nicht als neuer Fehler: moo_throw ohne Handler druckt
 * mit MOO_STRENG=1 die Meldung selbst ueber moo_print (moo_error.c). */
#define MOO_PRINT_ABBRUCH(vorher) (moo_error_flag && !(vorher))

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
            int vorher = moo_error_flag;
            MooValue result = moo_string_new("[");
            for (int32_t i = 0; i < l->length; i++) {
                if (i > 0) {
                    MooValue sep = moo_string_new(", ");
                    MooValue t = moo_string_concat(result, sep);
                    moo_release(result); moo_release(sep);
                    result = t;
                    if (MOO_PRINT_ABBRUCH(vorher)) return result;
                }
                if (l->items[i].tag == MOO_STRING) {
                    MooValue q1 = moo_string_new("\"");
                    MooValue t1 = moo_string_concat(result, q1);
                    moo_release(result); moo_release(q1);
                    result = t1;
                    if (MOO_PRINT_ABBRUCH(vorher)) return result;
                    // Item selbst ist geliehen → concat fasst es nicht an.
                    MooValue t2 = moo_string_concat(result, l->items[i]);
                    moo_release(result);
                    result = t2;
                    if (MOO_PRINT_ABBRUCH(vorher)) return result;
                    MooValue q2 = moo_string_new("\"");
                    MooValue t3 = moo_string_concat(result, q2);
                    moo_release(result); moo_release(q2);
                    result = t3;
                    if (MOO_PRINT_ABBRUCH(vorher)) return result;
                } else {
                    // Ein Fehler im geschachtelten moo_to_string liefert "" mit
                    // gesetztem Flag; concat gibt dann ebenfalls "" zurueck.
                    MooValue item_str = moo_to_string(l->items[i]);
                    MooValue t = moo_string_concat(result, item_str);
                    moo_release(result); moo_release(item_str);
                    result = t;
                    if (MOO_PRINT_ABBRUCH(vorher)) return result;
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
            int vorher = moo_error_flag;
            MooValue result = moo_string_new("{");
            bool first = true;
            for (int32_t i = 0; i < d->capacity; i++) {
                if (!d->entries[i].occupied) continue;
                if (!first) {
                    MooValue sep = moo_string_new(", ");
                    MooValue t = moo_string_concat(result, sep);
                    moo_release(result); moo_release(sep);
                    result = t;
                    if (MOO_PRINT_ABBRUCH(vorher)) return result;
                }
                first = false;
                MooValue q1 = moo_string_new("\"");
                MooValue t1 = moo_string_concat(result, q1);
                moo_release(result); moo_release(q1);
                result = t1;
                if (MOO_PRINT_ABBRUCH(vorher)) return result;

                MooValue key;
                key.tag = MOO_STRING;
                moo_val_set_ptr(&key, d->entries[i].key);  // geliehen
                MooValue t2 = moo_string_concat(result, key);
                moo_release(result);
                result = t2;
                if (MOO_PRINT_ABBRUCH(vorher)) return result;

                MooValue qc = moo_string_new("\": ");
                MooValue t3 = moo_string_concat(result, qc);
                moo_release(result); moo_release(qc);
                result = t3;
                if (MOO_PRINT_ABBRUCH(vorher)) return result;

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
                if (MOO_PRINT_ABBRUCH(vorher)) return result;
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
        case MOO_TENSOR:
            // Delegiert an moo_tensor.c (liefert +1 owning String).
            return moo_tensor_to_string(v);
        case MOO_SURFACE:
            return moo_string_new("<Oberflaeche>");
        case MOO_BUFFER:
            return moo_string_new("<Puffer>");
        case MOO_STRUCT:
            return moo_struct_text(v);
        case MOO_HANDLE:
            return moo_string_new(moo_handle_text(v));
        case MOO_ATOM:
            return moo_string_new("<Atom>");
        case MOO_SPERRE:
            return moo_string_new("<Sperre>");
        default:
            return moo_string_new("<unbekannt>");
    }
}

void moo_print(MooValue v) {
    // P2-T44: nach einem neuen Konvertierungsfehler keine Ausgabe; der Fehler
    // liegt im try-Zustand und wird vom Compiler nach dem Statement geprueft.
    // Mit beim Eintritt gesetztem Flag (MOO_STRENG-Meldung) wird gedruckt.
    int vorher = moo_error_flag;
    MooValue s = moo_to_string(v);
    if (!MOO_PRINT_ABBRUCH(vorher)) printf("%s\n", MV_STR(s)->chars);
    if (v.tag != MOO_STRING) moo_release(s);
}
