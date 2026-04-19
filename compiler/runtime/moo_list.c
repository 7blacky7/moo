#include "moo_runtime.h"

MooValue moo_list_new(int32_t initial_capacity) {
    if (initial_capacity < 4) initial_capacity = 4;
    MooValue v;
    v.tag = MOO_LIST;
    MooList* l = moo_alloc(sizeof(MooList));
    l->refcount = 1;
    l->frozen = false;
    l->length = 0;
    l->capacity = initial_capacity;
    l->items = moo_alloc(sizeof(MooValue) * initial_capacity);
    moo_val_set_ptr(&v, l);
    return v;
}

MooValue moo_list_from(MooValue* items, int32_t count) {
    MooValue v = moo_list_new(count);
    for (int32_t i = 0; i < count; i++)
        moo_list_append(v, items[i]);
    return v;
}

static void list_grow(MooList* l) {
    l->capacity *= 2;
    l->items = moo_realloc(l->items, sizeof(MooValue) * l->capacity);
}

void moo_list_append(MooValue list, MooValue item) {
    MooList* l = MV_LIST(list);
    if (l->frozen) { moo_throw(moo_string_new("Liste ist eingefroren!")); return; }
    if (l->length >= l->capacity) list_grow(l);
    // Transfer-Semantik: Caller uebergibt item mit refcount=1, Liste
    // uebernimmt die Referenz. Kein retain hier.
    l->items[l->length++] = item;
}

MooValue moo_list_get(MooValue list, MooValue index) {
    int32_t i = (int32_t)moo_as_number(index);
    MooList* l = MV_LIST(list);
    if (i < 0) i += l->length;
    if (i < 0 || i >= l->length) return moo_none();
    MooValue v = l->items[i];
    // Owning-Konvention: Caller bekommt eigene Referenz.
    moo_retain(v);
    return v;
}

void moo_list_set(MooValue list, MooValue index, MooValue value) {
    MooList* l = MV_LIST(list);
    if (l->frozen) { moo_throw(moo_string_new("Liste ist eingefroren!")); return; }
    int32_t i = (int32_t)moo_as_number(index);
    if (i < 0) i += l->length;
    if (i >= 0 && i < l->length) {
        // Transfer: Caller-Ref uebernehmen, alten Slot-Wert freigeben.
        moo_release(l->items[i]);
        l->items[i] = value;
    } else {
        // Out-of-bounds: value wird nicht aufgenommen, also die vom
        // Caller uebergebene Referenz wieder freigeben.
        moo_release(value);
    }
}

MooValue moo_list_length(MooValue list) {
    if (list.tag != MOO_LIST) return moo_number(0);
    return moo_number((double)MV_LIST(list)->length);
}

MooValue moo_list_pop(MooValue list) {
    MooList* l = MV_LIST(list);
    if (l->frozen) { moo_throw(moo_string_new("Liste ist eingefroren!")); return moo_none(); }
    if (l->length == 0) return moo_none();
    // Transfer: der Slot-Refcount geht direkt an den Caller, kein retain.
    return l->items[--l->length];
}

MooValue moo_list_contains(MooValue list, MooValue item) {
    MooList* l = MV_LIST(list);
    for (int32_t i = 0; i < l->length; i++) {
        MooValue eq = moo_eq(l->items[i], item);
        if (MV_BOOL(eq)) return moo_bool(true);
    }
    return moo_bool(false);
}

MooValue moo_list_reverse(MooValue list) {
    MooList* l = MV_LIST(list);
    for (int32_t i = 0; i < l->length / 2; i++) {
        MooValue tmp = l->items[i];
        l->items[i] = l->items[l->length - 1 - i];
        l->items[l->length - 1 - i] = tmp;
    }
    return list;
}

static int moo_sort_compare(const void* a, const void* b) {
    MooValue va = *(const MooValue*)a;
    MooValue vb = *(const MooValue*)b;
    if (va.tag == MOO_NUMBER && vb.tag == MOO_NUMBER) {
        double da = MV_NUM(va), db = MV_NUM(vb);
        return (da > db) - (da < db);
    }
    if (va.tag == MOO_STRING && vb.tag == MOO_STRING) {
        return strcmp(MV_STR(va)->chars, MV_STR(vb)->chars);
    }
    return 0;
}

MooValue moo_list_sort(MooValue list) {
    MooList* l = MV_LIST(list);
    qsort(l->items, l->length, sizeof(MooValue), moo_sort_compare);
    return list;
}

int32_t moo_list_iter_len(MooValue list) {
    if (list.tag != MOO_LIST) return 0;
    return MV_LIST(list)->length;
}

MooValue moo_list_iter_get(MooValue list, int32_t index) {
    MooValue v = MV_LIST(list)->items[index];
    // Owning-Konvention: jeder iter-Step liefert eigene Referenz. Codegen
    // ruft store_var mit transfer-Semantik auf; ohne retain wuerde das
    // ein Alias anlegen, das beim naechsten store_var-release die Liste
    // beschaedigt.
    moo_retain(v);
    return v;
}

MooValue moo_list_join(MooValue list, MooValue delim) {
    MooList* l = MV_LIST(list);
    if (l->length == 0) return moo_string_new("");

    MooValue dstr = moo_to_string(delim);
    int32_t dlen = MV_STR(dstr)->length;
    const char* dchars = MV_STR(dstr)->chars;

    MooValue* parts = moo_alloc(sizeof(MooValue) * l->length);
    int64_t total = 0;
    for (int32_t i = 0; i < l->length; i++) {
        parts[i] = moo_to_string(l->items[i]);
        total += MV_STR(parts[i])->length;
    }
    total += (int64_t)dlen * (l->length - 1);

    MooValue v;
    v.tag = MOO_STRING;
    MooString* s = moo_alloc(sizeof(MooString));
    s->refcount = 1;
    s->length = (int32_t)total;
    s->capacity = (int32_t)total + 1;
    s->chars = moo_alloc(s->capacity);

    int64_t pos = 0;
    for (int32_t i = 0; i < l->length; i++) {
        if (i > 0 && dlen > 0) {
            memcpy(s->chars + pos, dchars, dlen);
            pos += dlen;
        }
        int32_t plen = MV_STR(parts[i])->length;
        memcpy(s->chars + pos, MV_STR(parts[i])->chars, plen);
        pos += plen;
    }
    s->chars[total] = '\0';
    moo_val_set_ptr(&v, s);

    moo_free(parts);
    return v;
}
