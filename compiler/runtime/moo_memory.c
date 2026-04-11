#include "moo_runtime.h"

// === Reference Counting ===

// Prueft ob ein MooValue ein Heap-Objekt ist (hat refcount)
static inline bool is_heap_type(uint64_t tag) {
    return tag == MOO_STRING || tag == MOO_LIST || tag == MOO_DICT ||
           tag == MOO_OBJECT || tag == MOO_FUNC || tag == MOO_SOCKET;
}

extern void moo_socket_free(void* ptr);

// Gibt den Pointer auf den refcount eines Heap-Objekts zurueck
static inline int32_t* get_refcount_ptr(MooValue v) {
    // refcount ist das ERSTE Feld in allen Heap-Structs
    void* ptr = moo_val_as_ptr(v);
    if (!ptr) return NULL;
    return (int32_t*)ptr;
}

void moo_retain(MooValue v) {
    if (!is_heap_type(v.tag)) return;
    int32_t* rc = get_refcount_ptr(v);
    if (rc && *rc > 0) {
        (*rc)++;
    }
}

// Forward-Deklaration fuer rekursives Release
static void free_string(MooString* s);
static void free_list(MooList* l);
static void free_dict(MooDict* d);
static void free_object(MooObject* o);

void moo_release(MooValue v) {
    if (!is_heap_type(v.tag)) return;
    int32_t* rc = get_refcount_ptr(v);
    if (!rc || *rc <= 0) return;

    (*rc)--;
    if (*rc > 0) return;

    // refcount == 0 → freigeben
    switch (v.tag) {
        case MOO_STRING:
            free_string(MV_STR(v));
            break;
        case MOO_LIST:
            free_list(MV_LIST(v));
            break;
        case MOO_DICT:
            free_dict(MV_DICT(v));
            break;
        case MOO_OBJECT:
            free_object(MV_OBJ(v));
            break;
        case MOO_FUNC: {
            MooFunc* f = MV_FUNC(v);
            if (f->name) free(f->name);
            free(f);
            break;
        }
        case MOO_SOCKET:
            moo_socket_free(moo_val_as_ptr(v));
            break;
        default:
            break;
    }
}

static void free_string(MooString* s) {
    if (s->chars) free(s->chars);
    free(s);
}

static void free_list(MooList* l) {
    // Alle Elemente releasen
    for (int32_t i = 0; i < l->length; i++) {
        moo_release(l->items[i]);
    }
    if (l->items) free(l->items);
    free(l);
}

static void free_dict(MooDict* d) {
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].occupied) {
            // Key-String freigeben
            if (d->entries[i].key) {
                if (d->entries[i].key->refcount > 0) {
                    d->entries[i].key->refcount--;
                    if (d->entries[i].key->refcount == 0) {
                        free_string(d->entries[i].key);
                    }
                }
            }
            moo_release(d->entries[i].value);
        }
    }
    if (d->entries) free(d->entries);
    free(d);
}

static void free_object(MooObject* o) {
    for (int32_t i = 0; i < o->prop_count; i++) {
        moo_release(o->properties[i].value);
    }
    if (o->properties) free(o->properties);
    if (o->class_name) free(o->class_name);
    free(o);
}

// === Allokation ===

void* moo_alloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "moo: Speicher voll!\n");
        exit(1);
    }
    return ptr;
}

void* moo_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "moo: Speicher voll!\n");
        exit(1);
    }
    return new_ptr;
}

void moo_free(void* ptr) {
    free(ptr);
}
