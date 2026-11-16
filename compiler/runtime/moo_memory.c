#include "moo_runtime.h"
#include "moo_rc.h"

// === Reference Counting ===

// Prueft ob ein MooValue ein Heap-Objekt ist (hat refcount)
static inline bool is_heap_type(uint64_t tag) {
    return tag == MOO_STRING || tag == MOO_ERROR || tag == MOO_LIST || tag == MOO_DICT ||
           tag == MOO_OBJECT || tag == MOO_FUNC || tag == MOO_SOCKET ||
           tag == MOO_THREAD || tag == MOO_CHANNEL || tag == MOO_DATABASE ||
           tag == MOO_DB_STMT || tag == MOO_WINDOW || tag == MOO_WEBSERVER ||
           tag == MOO_VOXELWORLD || tag == MOO_FRAME || tag == MOO_GIF ||
           tag == MOO_VIDEO || tag == MOO_TENSOR || tag == MOO_KAMERA ||
           tag == MOO_MIKRO || tag == MOO_SURFACE || tag == MOO_BUFFER || tag == MOO_STRUCT ||
           tag == MOO_HANDLE || tag == MOO_ATOM || tag == MOO_SPERRE;
}

bool moo_ist_heap_wert(MooValue v) {
    return is_heap_type(v.tag);
}

void (*moo_sperre_freigabe_haken)(void*) = NULL;

extern void moo_socket_free(void* ptr);
extern void moo_buffer_free(void* ptr);
extern void moo_struct_box_free(void* ptr);

// Gibt den Pointer auf den refcount eines Heap-Objekts zurueck
static inline int32_t* get_refcount_ptr(MooValue v) {
    // refcount ist das ERSTE Feld in allen Heap-Structs
    void* ptr = moo_val_as_ptr(v);
    if (!ptr) return NULL;
    return (int32_t*)ptr;
}

// P2-T112: retain/release sind die heissesten Runtime-Funktionen (ein Aufruf je
// Variablenlesen). Ausrichtung auf eine Cache-Zeile haelt den schnellen Pfad in
// EINER Zeile; ohne sie entschied die zufaellige Linklage ueber bis zu 9 %
// Takte im refcount-dichten Fall (gleiche Instruktionszahl, gemessen).
#if defined(__GNUC__) || defined(__clang__)
#define MOO_HEISS __attribute__((aligned(64)))
#else
#define MOO_HEISS
#endif

MOO_HEISS void moo_retain(MooValue v) {
    if (!is_heap_type(v.tag)) return;
    int32_t* rc = get_refcount_ptr(v);
    // P2-T112: atomar nur bei geteilten Werten (moo_rc.h).
    if (MOO_RC_SELTEN(!rc)) return;
    moo_rc_inc(rc);
}

// Forward-Deklaration fuer rekursives Release
static void free_string(MooString* s);
static void free_list(MooList* l);
static void free_dict(MooDict* d);
static void free_object(MooObject* o);

MOO_HEISS void moo_release(MooValue v) {
    if (!is_heap_type(v.tag)) return;
    int32_t* rc = get_refcount_ptr(v);
    if (MOO_RC_SELTEN(!rc) || !moo_rc_dec(rc)) return;

    // refcount == 0 → freigeben
    switch (v.tag) {
        case MOO_STRING:
            free_string(MV_STR(v));
            break;
        case MOO_ERROR: {
            MooError* e = MV_ERROR(v);
            free(e->chars);
            free(e);
            break;
        }
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
        // MOO_WINDOW/MOO_VOXELWORLD existieren nur in 3D-Builds (build.rs
        // definiert dort MOO_HAS_3D). In UI-/Core-only-Builds gibt es keinen
        // Erzeugungspfad fuer diese Tags — die Symbole moo_window_free/
        // moo_voxel_free sind dann nicht gelinkt und duerfen hier nicht
        // referenziert werden (Linkerfehler, CI-Run 27437564015).
        case MOO_WINDOW:
#ifdef MOO_HAS_3D
            moo_window_free(moo_val_as_ptr(v));
#endif
            break;
        case MOO_WEBSERVER:
            moo_web_free(moo_val_as_ptr(v));
            break;
        case MOO_VOXELWORLD:
#ifdef MOO_HAS_3D
            moo_voxel_free(moo_val_as_ptr(v));
#endif
            break;
        case MOO_FRAME:
            moo_frame_free(moo_val_as_ptr(v));
            break;
        case MOO_GIF:
            moo_gif_handle_free(moo_val_as_ptr(v));
            break;
        case MOO_VIDEO:
            moo_video_handle_free(moo_val_as_ptr(v));
            break;
        case MOO_TENSOR:
            // Immer gebaut (moo_tensor.c in der Kernliste) — kein Gating noetig.
            moo_tensor_free(moo_val_as_ptr(v));
            break;
        case MOO_KAMERA:
            moo_kamera_free(moo_val_as_ptr(v));
            break;
        case MOO_MIKRO:
            moo_mikro_free(moo_val_as_ptr(v));
            break;
        case MOO_BUFFER:
            moo_buffer_free(moo_val_as_ptr(v));
            break;
        case MOO_STRUCT:
            moo_struct_box_free(moo_val_as_ptr(v));
            break;
        case MOO_ATOM:
            /* P2-T114 B: Atom besitzt nichts weiter; direkt freigeben, damit
               Harnesses mit fester Quellliste ohne moo_thread.c linkbar bleiben. */
            free(moo_val_as_ptr(v));
            break;
        case MOO_SPERRE:
            /* Eine Sperre entsteht nur ueber moo_sperre_neu (moo_thread.c), das
               den Haken setzt. */
            if (moo_sperre_freigabe_haken) moo_sperre_freigabe_haken(moo_val_as_ptr(v));
            break;
        case MOO_HANDLE:
            /* P2-T23/T53: die Griff-Box besitzt keine fremden Ressourcen (kein
               Finalizer); direkt freigeben, damit Harnesses mit fester
               Quellliste ohne moo_handle.c linkbar bleiben. */
            free(moo_val_as_ptr(v));
            break;
        case MOO_SURFACE:
#ifdef MOO_HAS_SURFACE
            moo_surface_free(moo_val_as_ptr(v));
#endif
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
            if (d->entries[i].key && moo_rc_dec(&d->entries[i].key->refcount)) {
                free_string(d->entries[i].key);
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
        // Property-NAME gehoert dem Objekt (moo_object_set legt name_str mit
        // rc=1 an, Transfer). Ohne dieses free leakten name_str + chars pro
        // Objekt (CG1-v4: 2 Allokationen/Objekt bei frischen Receivern,
        // ASan 2M Leaks bei 1M Iterationen).
        if (o->properties[i].name) free_string(o->properties[i].name);
    }
    if (o->properties) free(o->properties);
    if (o->class_name) free(o->class_name);
    free(o);
}

// === P2-T112: geteilte Werte (Geteilt-Bit, siehe moo_rc.h) ===

// 1, sobald der erste Moo-Thread gestartet wurde. Wird nur vom startenden
// Thread VOR pthread_create/CreateThread gesetzt und nie zurueckgesetzt;
// P2-T114 Phase C: Globale gehoeren dem Hauptthread und werden nicht mehr
// pauschal geteilt (moo_globale_anmelden/moo_global_geschrieben entfallen).
uint8_t moo_rc_mt_aktiv = 0;

extern void moo_struct_box_teilen(void* box);

static void teilen_push(MooValue** stapel, MooValue* lokal, int32_t* n, int32_t* cap, MooValue v) {
    if (!is_heap_type(v.tag)) return;
    if (*n == *cap) {
        int32_t neu = *cap * 2;
        MooValue* s = (MooValue*)malloc(sizeof(MooValue) * (size_t)neu);
        if (!s) { fprintf(stderr, "moo: Speicher voll!\n"); exit(1); }
        memcpy(s, *stapel, sizeof(MooValue) * (size_t)*n);
        if (*stapel != lokal) free(*stapel);
        *stapel = s;
        *cap = neu;
    }
    (*stapel)[(*n)++] = v;
}

// Markiert v und alles von v Erreichbare als geteilt. Bereits geteilte
// Teilbaeume werden nicht erneut besucht (Invariante: geteilt => Kinder
// geteilt), jedes Objekt wird also hoechstens einmal markiert. Darf nur vom
// Thread aufgerufen werden, dem die ungeteilten Teile allein gehoeren.
bool moo_ist_geteilt(MooValue v) {
    if (!is_heap_type(v.tag)) return false;
    int32_t* rc = get_refcount_ptr(v);
    return rc && moo_rc_ist_geteilt(rc);
}

void moo_teilen(MooValue wurzel) {
    if (!is_heap_type(wurzel.tag)) return;
    MooValue lokal[64];
    MooValue* stapel = lokal;
    int32_t n = 0, cap = 64;
    stapel[n++] = wurzel;
    while (n > 0) {
        MooValue v = stapel[--n];
        int32_t* rc = get_refcount_ptr(v);
        if (!rc) continue;
        int32_t r = moo_rc_lesen(rc);
        if (r <= 0) continue;  /* unsterblich oder schon geteilt */
        moo_rc_bit_setzen(rc);
        switch (v.tag) {
            case MOO_LIST: {
                MooList* l = MV_LIST(v);
                for (int32_t i = 0; i < l->length; i++)
                    teilen_push(&stapel, lokal, &n, &cap, l->items[i]);
                break;
            }
            case MOO_DICT: {
                MooDict* d = MV_DICT(v);
                for (int32_t i = 0; i < d->capacity; i++) {
                    if (!d->entries[i].occupied) continue;
                    MooString* k = d->entries[i].key;
                    if (k && moo_rc_lesen(&k->refcount) > 0) moo_rc_bit_setzen(&k->refcount);
                    teilen_push(&stapel, lokal, &n, &cap, d->entries[i].value);
                }
                break;
            }
            case MOO_OBJECT: {
                MooObject* o = MV_OBJ(v);
                for (int32_t i = 0; i < o->prop_count; i++)
                    teilen_push(&stapel, lokal, &n, &cap, o->properties[i].value);
                if (o->parent) {
                    MooValue p;
                    p.tag = MOO_OBJECT;
                    moo_val_set_ptr(&p, o->parent);
                    teilen_push(&stapel, lokal, &n, &cap, p);
                }
                break;
            }
            case MOO_FUNC: {
                MooFunc* f = MV_FUNC(v);
                for (int32_t i = 0; f->captured && i < f->n_captured; i++)
                    teilen_push(&stapel, lokal, &n, &cap, f->captured[i]);
                break;
            }
            case MOO_THREAD: {
                MooThread* t = (MooThread*)moo_val_as_ptr(v);
                teilen_push(&stapel, lokal, &n, &cap, t->result);
                teilen_push(&stapel, lokal, &n, &cap, t->halt_func);
                teilen_push(&stapel, lokal, &n, &cap, t->halt_arg);
                break;
            }
            case MOO_STRUCT:
                // Sichten halten eine Referenz auf ihre Wurzelbox.
                moo_struct_box_teilen(moo_val_as_ptr(v));
                break;
            case MOO_SPERRE:
                // P2-T114 B: Der Inhalt einer Sperre wird nicht geteilt (und
                // damit nicht eingefroren); er ist nur unter ihrem Mutex
                // erreichbar (mit ... als).
                break;
            default:
                // Uebrige Heap-Typen enthalten keine MooValues (Kanalinhalte
                // werden beim Senden markiert).
                break;
        }
    }
    if (stapel != lokal) free(stapel);
}

// Vor dem ersten Thread-Start (moo_thread_spawn). P2-T114 Phase C: Globale
// werden dabei nicht mehr geteilt. Sie gehoeren dem Hauptthread; der Compiler
// lehnt beweisbare Zugriffe aus Thread-Code ab (typpruefung.rs, M-P001), der
// Rest prueft zur Laufzeit (moo_global_pruefen, moo_thread.c). Ausdruecklich
// `geteilt` markierte Globale teilt der Codegen bei der Initialisierung.
void moo_mt_aktivieren(void) {
    moo_rc_mt_aktiv = 1;
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
