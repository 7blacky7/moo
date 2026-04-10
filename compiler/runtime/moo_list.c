#include "moo_runtime.h"

MooValue moo_list_new(int32_t initial_capacity) {
    if (initial_capacity < 4) initial_capacity = 4;
    MooValue v;
    v.tag = MOO_LIST;
    MooList* l = moo_alloc(sizeof(MooList));
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
    if (l->length >= l->capacity) list_grow(l);
    l->items[l->length++] = item;
}

MooValue moo_list_get(MooValue list, MooValue index) {
    int32_t i = (int32_t)moo_as_number(index);
    MooList* l = MV_LIST(list);
    if (i < 0) i += l->length;
    if (i < 0 || i >= l->length) return moo_none();
    return l->items[i];
}

void moo_list_set(MooValue list, MooValue index, MooValue value) {
    int32_t i = (int32_t)moo_as_number(index);
    MooList* l = MV_LIST(list);
    if (i < 0) i += l->length;
    if (i >= 0 && i < l->length) l->items[i] = value;
}

MooValue moo_list_length(MooValue list) {
    if (list.tag != MOO_LIST) return moo_number(0);
    return moo_number((double)MV_LIST(list)->length);
}

MooValue moo_list_pop(MooValue list) {
    MooList* l = MV_LIST(list);
    if (l->length == 0) return moo_none();
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
    return MV_LIST(list)->items[index];
}

MooValue moo_list_join(MooValue list, MooValue delim) {
    MooList* l = MV_LIST(list);
    if (l->length == 0) return moo_string_new("");
    MooValue result = moo_to_string(l->items[0]);
    for (int32_t i = 1; i < l->length; i++) {
        result = moo_string_concat(result, delim);
        result = moo_string_concat(result, moo_to_string(l->items[i]));
    }
    return result;
}
