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
    d->frozen = false;
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

// Transfer-Semantik fuer Keys: Caller uebergibt Key mit +1 (Producer/
// load_var). Die Lookup-Funktionen (get/has/remove) benutzen den Key nur,
// speichern ihn nicht — also am Ende freigeben. Fuer non-string Keys ist
// key_str ein frischer MooString aus moo_to_string (refcount=1); fuer
// MOO_STRING ist key_str == orig Caller-Ref. In beiden Faellen liegt die
// zu releasende Owning-Ref beim Caller-Key (der Wert vor der Konvertierung).
static inline void release_key_after_lookup(MooValue key_str, MooValue orig_key) {
    if (orig_key.tag == MOO_STRING) {
        moo_release(orig_key);
    } else if (key_str.tag == MOO_STRING) {
        moo_release(key_str);
    }
}

// Kompatibilitaets-Alias fuer set (occupied): hier duerfen wir genauso
// den Caller-Key freigeben.
#define release_key_str_if_fresh release_key_after_lookup

void moo_dict_set(MooValue dict, MooValue key, MooValue value) {
    MooDict* d = MV_DICT(dict);
    if (d->frozen) { moo_throw(moo_string_new("Wörterbuch ist eingefroren!")); return; }
    MooValue key_str = moo_to_string(key);
    if ((double)d->count / d->capacity > DICT_LOAD_FACTOR) dict_grow(d);
    int32_t slot = dict_find_slot(d, MV_STR(key_str)->chars);
    // Ownership-Transfer: caller uebergibt value mit refcount=1 (Producer /
    // load_var-retain). Dict uebernimmt die Referenz, kein retain.
    if (d->entries[slot].occupied) {
        moo_release(d->entries[slot].value);
        // Key bleibt der bestehende. Caller hat key mit +1 uebergeben
        // (transfer-Semantik), also freigeben.
        if (key_str.tag == MOO_STRING) moo_release(key_str);
    } else {
        // Erster Insert: Dict uebernimmt die Caller-Referenz des keys
        // direkt. Kein retain, kein release — reiner Transfer.
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
    MooValue result;
    if (d->entries[slot].occupied) {
        result = d->entries[slot].value;
        // Owning-Konvention: Caller bekommt eigene Referenz.
        moo_retain(result);
    } else {
        result = moo_none();
    }
    release_key_str_if_fresh(key_str, key);
    return result;
}

MooValue moo_dict_has(MooValue dict, MooValue key) {
    MooDict* d = MV_DICT(dict);
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, MV_STR(key_str)->chars);
    bool has = d->entries[slot].occupied;
    release_key_str_if_fresh(key_str, key);
    return moo_bool(has);
}

MooValue moo_dict_keys(MooValue dict) {
    MooDict* d = MV_DICT(dict);
    MooValue list = moo_list_new(d->count);
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].occupied) {
            MooValue key;
            key.tag = MOO_STRING;
            moo_val_set_ptr(&key, d->entries[i].key);
            // list_append transferiert — eigene Ref retainen damit das
            // Dict seine Original-Ref behaelt.
            moo_retain(key);
            moo_list_append(list, key);
        }
    }
    return list;
}

MooValue moo_dict_values(MooValue dict) {
    MooDict* d = MV_DICT(dict);
    MooValue list = moo_list_new(d->count);
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].occupied) {
            MooValue v = d->entries[i].value;
            moo_retain(v);
            moo_list_append(list, v);
        }
    }
    return list;
}

MooValue moo_dict_length(MooValue dict) {
    if (dict.tag != MOO_DICT) return moo_number(0);
    return moo_number((double)MV_DICT(dict)->count);
}

void moo_dict_remove(MooValue dict, MooValue key) {
    MooDict* d = MV_DICT(dict);
    if (d->frozen) { moo_throw(moo_string_new("Wörterbuch ist eingefroren!")); return; }
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, MV_STR(key_str)->chars);
    if (d->entries[slot].occupied) {
        // Vorher: Key+Value blieben referenziert und wurden nur durch
        // free_dict am Ende freigegeben — bei langlebigen Dicts Leak.
        MooValue k;
        k.tag = MOO_STRING;
        moo_val_set_ptr(&k, d->entries[slot].key);
        moo_release(k);
        moo_release(d->entries[slot].value);
        d->entries[slot].occupied = false;
        d->entries[slot].key = NULL;
        d->count--;
    }
    release_key_str_if_fresh(key_str, key);
}
