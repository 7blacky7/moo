#include "moo_runtime.h"
#include "moo_rc.h"
#include "moo_struct_box_priv.h"
#include "moo_buffer_priv.h"

// === Reference Counting ===

// Prueft ob ein MooValue ein Heap-Objekt ist (hat refcount)
static inline bool is_heap_type(uint64_t tag) {
    /* P2-T114: Atom/Sperre liegen oberhalb der reservierten 30/31. Den
       seltenen Bereich getrennt pruefen: sonst wird aus der 32-Bit-Maske
       der haeufigen Typen auf x86 ein movabs im retain/release-Pfad. */
    if (MOO_RC_SELTEN(tag > MOO_HANDLE))
        return tag == MOO_ATOM || tag == MOO_SPERRE;
    return tag == MOO_STRING || tag == MOO_ERROR || tag == MOO_LIST || tag == MOO_DICT ||
           tag == MOO_OBJECT || tag == MOO_FUNC || tag == MOO_SOCKET ||
           tag == MOO_THREAD || tag == MOO_CHANNEL || tag == MOO_DATABASE ||
           tag == MOO_DB_STMT || tag == MOO_WINDOW || tag == MOO_WEBSERVER ||
           tag == MOO_VOXELWORLD || tag == MOO_FRAME || tag == MOO_GIF ||
           tag == MOO_VIDEO || tag == MOO_TENSOR || tag == MOO_KAMERA ||
           tag == MOO_MIKRO || tag == MOO_SURFACE || tag == MOO_BUFFER || tag == MOO_STRUCT ||
           tag == MOO_HANDLE;
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

/* Den seltenen Destruktor-Dispatch vom haeufigen Dekrement trennen.
 * Sonst liegen selbst Rueckspruenge fuer Zahlen/noch lebende Werte erst
 * hinter dem Switch-Prolog (P2-T114, x86-Disassembly). */
#if defined(__GNUC__) || defined(__clang__)
static void moo_letzte_referenz_freigeben(MooValue v) __attribute__((noinline));
#elif defined(_MSC_VER)
static __declspec(noinline) void moo_letzte_referenz_freigeben(MooValue v);
#else
static void moo_letzte_referenz_freigeben(MooValue v);
#endif

MOO_HEISS void moo_release(MooValue v) {
    if (!is_heap_type(v.tag)) return;
    int32_t* rc = get_refcount_ptr(v);
    if (MOO_RC_SELTEN(!rc)) return;
    if (MOO_RC_SELTEN(moo_rc_dec(rc))) moo_letzte_referenz_freigeben(v);
}

static void moo_letzte_referenz_freigeben(MooValue v) {
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
    if (v.tag == MOO_BUFFER) return moo_buffer_geteilt(moo_val_as_ptr(v));
    int32_t* rc = get_refcount_ptr(v);
    return rc && moo_rc_ist_geteilt(rc);
}

/* Owning-MooValue-Kanten genau einmal beschreiben: Teilen und die
 * Sperrenpruefung sehen denselben Graphen. Unveraenderliche Dict-Schluessel
 * erhalten direkt die threadfeste RC-Markierung. Der Besucher leiht Werte. */
typedef void (*MooKindBesucher)(MooValue wert, void* kontext);
static void moo_wert_kinder(MooValue v, MooKindBesucher besuch, void* kontext) {
    switch (v.tag) {
        case MOO_LIST: {
            MooList* l = MV_LIST(v);
            for (int32_t i = 0; i < l->length; i++) besuch(l->items[i], kontext);
            break;
        }
        case MOO_DICT: {
            MooDict* d = MV_DICT(v);
            for (int32_t i = 0; i < d->capacity; i++) {
                if (!d->entries[i].occupied) continue;
                MooString* k = d->entries[i].key;
                if (k && moo_rc_lesen(&k->refcount) > 0)
                    moo_rc_bit_setzen(&k->refcount);
                besuch(d->entries[i].value, kontext);
            }
            break;
        }
        case MOO_OBJECT: {
            MooObject* o = MV_OBJ(v);
            for (int32_t i = 0; i < o->prop_count; i++) besuch(o->properties[i].value, kontext);
            if (o->parent) {
                MooValue p;
                p.tag = MOO_OBJECT;
                moo_val_set_ptr(&p, o->parent);
                besuch(p, kontext);
            }
            break;
        }
        case MOO_FUNC: {
            MooFunc* f = MV_FUNC(v);
            for (int32_t i = 0; f->captured && i < f->n_captured; i++) besuch(f->captured[i], kontext);
            break;
        }
        case MOO_THREAD: {
            MooThread* t = moo_val_as_ptr(v);
            besuch(t->result, kontext);
            besuch(t->halt_func, kontext);
            besuch(t->halt_arg, kontext);
            break;
        }
        case MOO_STRUCT: {
            MooStructBox* box = moo_val_as_ptr(v);
            if (box && box->besitzer) {
                MooValue b;
                b.tag = MOO_STRUCT;
                moo_val_set_ptr(&b, box->besitzer);
                besuch(b, kontext);
            }
            break;
        }
        case MOO_SPERRE:
            /* Inhalt ausschliesslich unter dem eigenen Mutex erreichbar. */
            break;
        default:
            /* Weitere Heap-Typen enthalten keine MooValues; Kanalinhalte
             * werden schon vor dem Senden geteilt. */
            break;
    }
}

typedef struct {
    MooValue** stapel;
    MooValue* lokal;
    int32_t* n;
    int32_t* cap;
} MooTeilBesuch;

static void teilen_kind(MooValue wert, void* kontext) {
    MooTeilBesuch* b = kontext;
    teilen_push(b->stapel, b->lokal, b->n, b->cap, wert);
}

void moo_teilen(MooValue wurzel) {
    if (!is_heap_type(wurzel.tag)) return;
    MooValue lokal[64];
    MooValue* stapel = lokal;
    int32_t n = 0, cap = 64;
    MooTeilBesuch besuch = { &stapel, lokal, &n, &cap };
    stapel[n++] = wurzel;
    while (n > 0) {
        MooValue v = stapel[--n];
        int32_t* rc = get_refcount_ptr(v);
        if (!rc || moo_rc_lesen(rc) <= 0) continue;
        if (v.tag == MOO_BUFFER) {
            MooBuffer* puffer = moo_val_as_ptr(v);
            moo_rc_schreiben(&puffer->storage->eingefroren, 1);
        }
        moo_rc_bit_setzen(rc);
        moo_wert_kinder(v, teilen_kind, &besuch);
    }
    if (stapel != lokal) free(stapel);
}



/* Von der Sperre erreichbare owning Referenzen von aeusseren Aliasen
 * unterscheiden. Ein Knoten wird einmal besucht, jede Kante gezaehlt. */
typedef struct {
    MooValue wert;                 /* geliehen; bei Speicher unbenutzt */
    MooBufferStorage* speicher;
    size_t innen;
} MooSperrKnoten;

typedef struct {
    MooValue wurzel;
    MooSperrKnoten lokal[8];
    size_t index_lokal[16];
    MooSperrKnoten* knoten;
    size_t* index;
    size_t n, cap, index_cap;
} MooSperrGraph;

static void sperrgraph_voll(void) {
    fprintf(stderr, "moo: Sperren-Graph zu gross oder Speicher voll!\n");
    exit(1);
}

static void* sperrgraph_kennung(const MooSperrKnoten* k) {
    return k->speicher ? (void*)k->speicher : moo_val_as_ptr(k->wert);
}

static size_t sperrgraph_platz(const MooSperrGraph* g, void* kennung) {
    /* Der Hash darf absichtlich unsigned umbrechen. Keine Pointerarithmetik. */
    uint64_t h = (uint64_t)(uintptr_t)kennung;
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    size_t p = (size_t)h & (g->index_cap - 1);
    while (g->index[p]
           && sperrgraph_kennung(&g->knoten[g->index[p] - 1]) != kennung)
        p = (p + 1) & (g->index_cap - 1);
    return p;
}

static void sperrgraph_index_wachsen(MooSperrGraph* g) {
    if (g->index_cap > SIZE_MAX / 2
        || g->index_cap * 2 > SIZE_MAX / sizeof(size_t)) sperrgraph_voll();
    size_t cap = g->index_cap * 2;
    size_t* index = calloc(cap, sizeof(size_t));
    if (!index) sperrgraph_voll();
    if (g->index != g->index_lokal) free(g->index);
    g->index = index;
    g->index_cap = cap;
    for (size_t i = 0; i < g->n; i++) {
        size_t p = sperrgraph_platz(g, sperrgraph_kennung(&g->knoten[i]));
        g->index[p] = i + 1;
    }
}

static void sperrgraph_eintragen(MooSperrGraph* g, MooValue wert,
                                MooBufferStorage* speicher) {
    void* kennung = speicher ? (void*)speicher : moo_val_as_ptr(wert);
    size_t p = sperrgraph_platz(g, kennung);
    if (g->index[p]) {
        MooSperrKnoten* k = &g->knoten[g->index[p] - 1];
        if (k->innen == SIZE_MAX) sperrgraph_voll();
        k->innen++;
        return;
    }
    if (g->n == g->cap) {
        if (g->cap > SIZE_MAX / 2
            || g->cap * 2 > SIZE_MAX / sizeof(MooSperrKnoten)) sperrgraph_voll();
        size_t cap = g->cap * 2;
        MooSperrKnoten* neu = malloc(cap * sizeof(MooSperrKnoten));
        if (!neu) sperrgraph_voll();
        memcpy(neu, g->knoten, g->n * sizeof(MooSperrKnoten));
        if (g->knoten != g->lokal) free(g->knoten);
        g->knoten = neu;
        g->cap = cap;
    }
    if (g->n >= g->index_cap / 2) {
        sperrgraph_index_wachsen(g);
        p = sperrgraph_platz(g, kennung);
    }
    g->knoten[g->n].wert = wert;
    g->knoten[g->n].speicher = speicher;
    g->knoten[g->n].innen = 1;
    g->index[p] = ++g->n;
}

static void sperrgraph_start(MooSperrGraph* g) {
    /* Erst ein Heapkind braucht Aliaszaehlung. Ungenutzte Knoten werden
     * nicht gelesen; nur der Hashindex braucht leere Slots. */
    memset(g->index_lokal, 0, sizeof(g->index_lokal));
    g->knoten = g->lokal;
    g->index = g->index_lokal;
    g->cap = 8;
    g->index_cap = 16;
    sperrgraph_eintragen(g, g->wurzel, NULL);
}

static void sperrgraph_kind(MooValue wert, void* kontext) {
    if (!is_heap_type(wert.tag)) return;
    int32_t* rc = get_refcount_ptr(wert);
    if (!rc || moo_rc_lesen(rc) <= 0) return;
    MooSperrGraph* g = kontext;
    if (!g->n) sperrgraph_start(g);
    sperrgraph_eintragen(g, wert, NULL);
}

void moo_sperre_unterelemente_sichern(MooValue wurzel) {
    if (!is_heap_type(wurzel.tag)) return;
    int32_t* rc = get_refcount_ptr(wurzel);
    if (!rc || moo_rc_lesen(rc) <= 0) return;
    MooSperrGraph g;
    g.n = 0;
    g.wurzel = wurzel;
    size_t anfang = 1;
    if (wurzel.tag == MOO_BUFFER) {
        /* Auch ohne MooValue-Kinder koennen weitere Speicheransichten
         * existieren. Daher Buffer immer samt Speicher zaehlen. */
        sperrgraph_start(&g);
        anfang = 0;
    } else {
        moo_wert_kinder(wurzel, sperrgraph_kind, &g);
        if (!g.n) {
            /* Keine ungeteilten Heapkinder: nur die eigene Referenz der
             * Sperre liegt innen. Schluessel sind bereits threadfest. */
            if (moo_rc_lesen(rc) > 1) moo_teilen(wurzel);
            return;
        }
    }
    /* Die Wurzelkinder wurden bereits besucht; jede Kante nur einmal. */
    for (size_t i = anfang; i < g.n; i++) {
        /* Der Besucher darf den Knotenvektor vergroessern. */
        MooSperrKnoten k = g.knoten[i];
        if (k.speicher) continue;
        if (k.wert.tag == MOO_BUFFER) {
            MooBuffer* b = moo_val_as_ptr(k.wert);
            if (!moo_rc_lesen(&b->storage->eingefroren)) {
                MooValue leer = {0};
                sperrgraph_eintragen(&g, leer, b->storage);
            }
        }
        moo_wert_kinder(k.wert, sperrgraph_kind, &g);
    }
    for (size_t i = 0; i < g.n; i++) {
        MooSperrKnoten* k = &g.knoten[i];
        if (k->speicher) {
            /* Jede im Graphen gehaltene Ansicht zaehlt eine Speicherref.
             * Weitere Ansichten/Pins duerfen den Speicher nicht beschreiben. */
            if (moo_rc_size_lesen(&k->speicher->refs) > k->innen)
                moo_rc_schreiben(&k->speicher->eingefroren, 1);
        } else {
            int32_t r = moo_rc_lesen(get_refcount_ptr(k->wert));
            if (r > 0 && (size_t)r > k->innen) moo_teilen(k->wert);
        }
    }
    if (g.knoten != g.lokal) free(g.knoten);
    if (g.index != g.index_lokal) free(g.index);
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
