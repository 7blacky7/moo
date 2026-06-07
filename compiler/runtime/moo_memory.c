#include "moo_runtime.h"

// === Reference Counting ===

// Prueft ob ein MooValue ein Heap-Objekt ist (hat refcount)
static inline bool is_heap_type(uint64_t tag) {
    return tag == MOO_STRING || tag == MOO_LIST || tag == MOO_DICT ||
           tag == MOO_OBJECT || tag == MOO_FUNC || tag == MOO_SOCKET ||
           tag == MOO_THREAD || tag == MOO_CHANNEL || tag == MOO_DATABASE ||
           tag == MOO_DB_STMT || tag == MOO_WINDOW || tag == MOO_WEBSERVER ||
           tag == MOO_VOXELWORLD || tag == MOO_FRAME;
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
            // Captures releasen (Closure-Environment)
            if (f->captured && f->n_captured > 0) {
                for (int32_t i = 0; i < f->n_captured; i++) {
                    moo_release(f->captured[i]);
                }
                free(f->captured);
            }
            if (f->name) free(f->name);
            free(f);
            break;
        }
        case MOO_SOCKET:
            moo_socket_free(moo_val_as_ptr(v));
            break;
        case MOO_THREAD:
            moo_thread_free(moo_val_as_ptr(v));
            break;
        case MOO_CHANNEL:
            moo_channel_free(moo_val_as_ptr(v));
            break;
        case MOO_DATABASE:
            moo_db_free(moo_val_as_ptr(v));
            break;
        case MOO_DB_STMT:
            moo_db_stmt_free(moo_val_as_ptr(v));
            break;
        case MOO_WINDOW:
            moo_window_free(moo_val_as_ptr(v));
            break;
        case MOO_WEBSERVER:
            moo_web_free(moo_val_as_ptr(v));
            break;
        case MOO_VOXELWORLD:
            moo_voxel_free(moo_val_as_ptr(v));
            break;
        case MOO_FRAME:
            moo_frame_free(moo_val_as_ptr(v));
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

// === Checked-Arithmetik fuer Allokationsgroessen (Plan-007 P007-U3) ===
// Beide Helfer nehmen ihre Operanden bereits als int64_t entgegen (der
// Aufrufer promotet seine int32_t-Laengen verlustfrei), rechnen im int64_t-
// Bereich (kein Overflow moeglich, da int32*int32 bzw. int32+int32 sicher in
// int64 passt) und pruefen das Ergebnis gegen [0, MOO_MAX_ALLOC_SIZE].
//
// Bei Verletzung: moo_throw mit klarer deutscher Meldung. ACHTUNG: moo_throw
// kehrt INNERHALB eines try/fange-Blocks ZURUECK (es setzt dann nur das
// Error-Flag, siehe moo_error.c) — es ist also KEIN longjmp/noreturn. Damit ein
// Aufrufer, der nach dem Wurf weiterlaeuft (try-Kontext), NICHT mit einer
// riesigen Groesse allokiert, geben wir im Fehlerfall 0 zurueck (harmlose,
// kleine Allokation). Aufrufer MUESSEN zusaetzlich nach dem Aufruf das
// Error-Flag pruefen (moo_error_flag) und vor dem Schreiben in den Puffer
// abbrechen — siehe moo_string.c/moo_crypto.c/moo_web.c.
int64_t moo_checked_add_i32(int64_t a, int64_t b, const char* ctx) {
    int64_t r = a + b;  // a,b stammen aus int32-Laengen -> Summe sicher in int64
    if (r < 0 || r > MOO_MAX_ALLOC_SIZE) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s: Groessen-Ueberlauf (%lld + %lld ueberschreitet das erlaubte Limit von %d Bytes)",
                 ctx ? ctx : "Allokation", (long long)a, (long long)b, (int)MOO_MAX_ALLOC_SIZE);
        moo_throw(moo_error(msg));
        return 0;
    }
    return r;
}

int64_t moo_checked_mul_i32(int64_t a, int64_t b, const char* ctx) {
    int64_t r = a * b;  // a,b stammen aus int32-Werten -> Produkt sicher in int64
    if (r < 0 || r > MOO_MAX_ALLOC_SIZE) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s: Groessen-Ueberlauf (%lld * %lld ueberschreitet das erlaubte Limit von %d Bytes)",
                 ctx ? ctx : "Allokation", (long long)a, (long long)b, (int)MOO_MAX_ALLOC_SIZE);
        moo_throw(moo_error(msg));
        return 0;
    }
    return r;
}
