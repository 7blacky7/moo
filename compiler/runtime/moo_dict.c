#include "moo_runtime.h"

#define DICT_INITIAL_CAPACITY 16
#define DICT_LOAD_FACTOR 0.75

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    while (*str) hash = ((hash << 5) + hash) + (unsigned char)*str++;
    return hash;
}

MooValue moo_dict_new(void) {
    MooValue v;
    v.tag = MOO_DICT;
    MooDict* d = moo_alloc(sizeof(MooDict));
    d->refcount = 1;
    d->count = 0;
    d->capacity = DICT_INITIAL_CAPACITY;
    d->entries = moo_alloc(sizeof(MooDictEntry) * d->capacity);
    memset(d->entries, 0, sizeof(MooDictEntry) * d->capacity);
    moo_val_set_ptr(&v, d);
    return v;
}

static int32_t dict_find_slot(MooDict* d, const char* key) {
    uint32_t hash = hash_string(key);
    int32_t idx = hash % d->capacity;
    while (d->entries[idx].occupied) {
        if (strcmp(d->entries[idx].key->chars, key) == 0) return idx;
        idx = (idx + 1) % d->capacity;
    }
    return idx;
}

static void dict_grow(MooDict* d) {
    int32_t old_cap = d->capacity;
    MooDictEntry* old = d->entries;
    d->capacity *= 2;
    d->entries = moo_alloc(sizeof(MooDictEntry) * d->capacity);
    memset(d->entries, 0, sizeof(MooDictEntry) * d->capacity);
    d->count = 0;
    for (int32_t i = 0; i < old_cap; i++) {
        if (old[i].occupied) {
            int32_t slot = dict_find_slot(d, old[i].key->chars);
            d->entries[slot] = old[i];
            d->count++;
        }
    }
    moo_free(old);
}

void moo_dict_set(MooValue dict, MooValue key, MooValue value) {
    MooDict* d = MV_DICT(dict);
    MooValue key_str = moo_to_string(key);
    if ((double)d->count / d->capacity > DICT_LOAD_FACTOR) dict_grow(d);
    int32_t slot = dict_find_slot(d, MV_STR(key_str)->chars);
    if (!d->entries[slot].occupied) {
        d->entries[slot].key = MV_STR(key_str);
        d->entries[slot].occupied = true;
        d->count++;
    }
    d->entries[slot].value = value;
}

MooValue moo_dict_get(MooValue dict, MooValue key) {
    MooDict* d = MV_DICT(dict);
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, MV_STR(key_str)->chars);
    if (d->entries[slot].occupied) return d->entries[slot].value;
    return moo_none();
}

MooValue moo_dict_has(MooValue dict, MooValue key) {
    MooDict* d = MV_DICT(dict);
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, MV_STR(key_str)->chars);
    return moo_bool(d->entries[slot].occupied);
}

MooValue moo_dict_keys(MooValue dict) {
    MooDict* d = MV_DICT(dict);
    MooValue list = moo_list_new(d->count);
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].occupied) {
            MooValue key;
            key.tag = MOO_STRING;
            moo_val_set_ptr(&key, d->entries[i].key);
            moo_list_append(list, key);
        }
    }
    return list;
}

MooValue moo_dict_values(MooValue dict) {
    MooDict* d = MV_DICT(dict);
    MooValue list = moo_list_new(d->count);
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].occupied)
            moo_list_append(list, d->entries[i].value);
    }
    return list;
}

MooValue moo_dict_length(MooValue dict) {
    if (dict.tag != MOO_DICT) return moo_number(0);
    return moo_number((double)MV_DICT(dict)->count);
}

void moo_dict_remove(MooValue dict, MooValue key) {
    MooDict* d = MV_DICT(dict);
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, MV_STR(key_str)->chars);
    if (d->entries[slot].occupied) {
        d->entries[slot].occupied = false;
        d->count--;
    }
}
