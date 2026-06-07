/**
 * test_voxel_mesher_asan.c - Standalone-ASan-Harness fuer den Voxel-Mesher
 * (Plan-005 Phase 1b, Agent p005-rt2).
 *
 * Strategie: moo_voxel.c haengt nur an Deklarationen aus moo_runtime.h und an
 * der 3D-Chunk-API (moo_3d_chunk_create/begin/end/delete, moo_3d_triangle,
 * moo_3d_backend_active). Letztere brauchen im echten Build ein GL/Vulkan-
 * Backend. Fuer einen deterministischen, headless ASan-Test STUBBEN wir sie
 * hier und protokollieren die Aufrufe. Das erlaubt es, GENAU die RT2-Logik zu
 * pruefen, ohne einen GPU-Kontext:
 *   - Face-Culling (Anzahl emittierter Faces) fuer bekannte Konfigurationen
 *   - Nachbar-Culling ueber Chunk-Grenzen
 *   - Dirty-Propagation (eigener Chunk + 6 direkte Nachbarn)
 *   - Render-ID-Lifetime: GENAU EIN chunk_delete pro Chunk, kein Double-Delete
 *   - GPU-Cache-Lifetime: World-Free nach "Backend zu" crasht nicht & ruft kein
 *     chunk_delete (backend_active==0 -> render_id wurde nie angelegt)
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address -g -std=c11 -I.. \
 *       test_voxel_mesher_asan.c ../moo_voxel.c -lm -o /tmp/t_voxel_mesher
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_mesher
 *
 * Die Stubs ersetzen den moo-Heap durch malloc/free, damit ASan Leaks in
 * moo_voxel.c (Chunk-Blocks, Hashmap, World) zuverlaessig sieht.
 */

#include "moo_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ===================== moo-Runtime-Stubs ===================== */

void* moo_alloc(size_t size) { return malloc(size); }
void  moo_free(void* ptr)    { free(ptr); }

MooValue moo_number(double n) { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, n); return v; }
MooValue moo_bool(bool b)     { MooValue v; v.tag = MOO_BOOL;   v.data = (uint64_t)b; return v; }
MooValue moo_none(void)       { MooValue v; v.tag = MOO_NONE;   v.data = 0; return v; }
double   moo_as_number(MooValue v) { return moo_val_as_double(v); }

/* String-Arena: moo_string_new gibt im echten Runtime ein refcount-Objekt
 * zurueck, dessen Lebenszyklus der Aufrufer/Konsument steuert. Im Mesher wird
 * pro Voxel EINE Farbe erzeugt und an mehrere triangle-Calls weitergereicht;
 * der Stub-triangle gibt sie NICHT frei (sonst Double-Free). Wir tracken alle
 * erzeugten Strings und raeumen sie am Testende ab -> ASan-clean. */
#define MAX_TRACKED_STRINGS 200000
static MooString* g_strings[MAX_TRACKED_STRINGS];
static int        g_string_count = 0;

MooValue moo_string_new(const char* s) {
    MooString* str = (MooString*)malloc(sizeof(MooString));
    str->refcount = 1;
    str->length = (int32_t)strlen(s);
    str->capacity = str->length + 1;
    str->chars = (char*)malloc((size_t)str->capacity);
    memcpy(str->chars, s, (size_t)str->capacity);
    if (g_string_count < MAX_TRACKED_STRINGS) g_strings[g_string_count++] = str;
    MooValue v; v.tag = MOO_STRING; moo_val_set_ptr(&v, str);
    return v;
}
static void free_tracked_strings(void) {
    for (int i = 0; i < g_string_count; i++) {
        free(g_strings[i]->chars);
        free(g_strings[i]);
    }
    g_string_count = 0;
}

MooValue moo_error(const char* msg) { return moo_string_new(msg); }

/* moo_throw: im Test als Longjmp-Ersatz ueber ein Flag + abort-bei-unerwartet.
 * Tests, die einen Wurf erwarten, setzen g_expect_throw. */
static int g_threw = 0;
static int g_expect_throw = 0;
void moo_throw(MooValue v) {
    (void)v;
    g_threw = 1;
    if (!g_expect_throw) {
        fprintf(stderr, "UNERWARTETER moo_throw im Test!\n");
        abort();
    }
}

/* Dict-Stubs (nur fuer ram_statistik; hier nicht im Fokus, aber linkbar). */
MooValue moo_dict_new(void) { MooValue v; v.tag = MOO_DICT; v.data = 0; return v; }
void moo_dict_set(MooValue d, MooValue k, MooValue val) { (void)d; (void)k; (void)val; }

/* ===================== 3D-API-Stubs (Render-Cache-Spion) ===================== */

static int g_backend_active = 1;       /* 1 = Backend "an" */
static int g_next_render_id = 0;       /* incrementelle Render-IDs */
#define MAX_RENDER_IDS 100000
static int g_rid_live[MAX_RENDER_IDS]; /* 1 = derzeit angelegt */
static int g_rid_delete_count[MAX_RENDER_IDS];
static int g_active_chunk = -1;        /* aktuell offener begin-Kontext */
static long g_triangles_in_active = 0; /* Dreiecke im offenen Chunk */
static long g_last_chunk_triangles = 0;/* Dreiecke des zuletzt geschlossenen Chunks */

int moo_3d_backend_active(void) { return g_backend_active; }

/* Noise-Stub: moo_voxel.c referenziert seit RT5 moo_noise_fbm (Lazy-Worldgen
 * in voxel_holen). Dieser Harness ruft voxel_holen nicht auf -> Stub dient nur
 * dem Linken (moo_noise.c wird hier nicht mitkompiliert). */
float moo_noise_fbm(int seed, float x, float y, int octaves, float freq, float amp) {
    (void)seed; (void)x; (void)y; (void)octaves; (void)freq; (void)amp;
    return 0.0f;
}

MooValue moo_3d_chunk_create(void) {
    if (!g_backend_active) { /* echtes moo_3d_chunk_create wuerde werfen */
        moo_throw(moo_error("Kein 3D-Backend aktiv"));
        return moo_number(-1.0);
    }
    int id = g_next_render_id++;
    assert(id < MAX_RENDER_IDS);
    g_rid_live[id] = 1;
    return moo_number((double)id);
}

void moo_3d_chunk_begin(MooValue id) {
    if (!g_backend_active) return; /* safe no-op */
    g_active_chunk = (int)moo_as_number(id);
    g_triangles_in_active = 0;
}

void moo_3d_chunk_end(void) {
    if (!g_backend_active) return; /* safe no-op */
    g_last_chunk_triangles = g_triangles_in_active;
    g_active_chunk = -1;
}

void moo_3d_chunk_delete(MooValue id) {
    if (!g_backend_active) return; /* safe no-op (wie echtes moo_3d_chunk_delete) */
    int rid = (int)moo_as_number(id);
    assert(rid >= 0 && rid < MAX_RENDER_IDS);
    g_rid_delete_count[rid]++;
    /* Double-Delete-Waechter: */
    assert(g_rid_delete_count[rid] == 1 && "Render-ID doppelt geloescht!");
    g_rid_live[rid] = 0;
}

/* moo_3d_triangle: zaehlt nur. Signatur muss EXAKT der in moo_runtime.h
 * entsprechen (10 MooValue-Parameter). */
void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color) {
    (void)win; (void)x1; (void)y1; (void)z1; (void)x2; (void)y2; (void)z2;
    (void)x3; (void)y3; (void)z3; (void)color;
    if (!g_backend_active) return;
    g_triangles_in_active++;
}

/* ===================== Test-Helfer ===================== */

extern MooValue moo_voxel_welt_neu(MooValue seed);
extern MooValue moo_voxel_setzen(MooValue welt, MooValue x, MooValue y, MooValue z, MooValue id);
extern MooValue moo_voxel_holen(MooValue welt, MooValue x, MooValue y, MooValue z);
extern MooValue moo_voxel_chunk_entladen(MooValue welt, MooValue x, MooValue y, MooValue z);
extern MooValue moo_voxel_mesh_bauen(MooValue welt, MooValue x, MooValue y, MooValue z);
extern MooValue moo_voxel_aktualisieren(MooValue welt);
extern void     moo_voxel_free(void* ptr);

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  PASS: %s\n", name); } \
    else      { g_fail++; printf("  FAIL: %s\n", name); } \
} while (0)

static MooValue N(double d) { return moo_number(d); }

/* int-Wrapper fuer mesh_bauen (Chunk-Koordinaten als ints im Test). */
static MooValue mb(MooValue w, int a, int b, int c) {
    return moo_voxel_mesh_bauen(w, N(a), N(b), N(c));
}

/* Setzt einen Block ohne dass ein unerwarteter Wurf den Test killt. */
static void set_block(MooValue w, int x, int y, int z, int id) {
    moo_voxel_setzen(w, N(x), N(y), N(z), N(id));
}

/* Zugriff auf die Welt-Interna fuer Assertions ueber die Render-ID/Dirty.
 * Wir spiegeln die (privaten) Structs aus moo_voxel.c hier NICHT — stattdessen
 * pruefen wir Verhalten ueber die oeffentliche API + die 3D-Spione. */

int main(void) {
    printf("== Voxel-Mesher ASan-Harness (Plan-005 1b) ==\n");

    /* ---------- Test 1: Einzelner Block -> 6 Faces (12 Dreiecke) ---------- */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 3); /* stein, mitten im Chunk 0 */
        MooValue rid = mb(w, 0, 0, 0);
        CHECK((int)moo_as_number(rid) >= 0, "mesh_bauen liefert gueltige Render-ID");
        CHECK(g_last_chunk_triangles == 12,
              "Einzel-Block: 6 sichtbare Faces = 12 Dreiecke");
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* ---------- Test 2: zwei benachbarte Bloecke -> 10 Faces (20 Dreiecke) ---------- */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);
        set_block(w, 6, 5, 5, 1); /* teilen je 1 Face -> 12-2 = 10 Faces */
        mb(w, 0, 0, 0);
        CHECK(g_last_chunk_triangles == 20,
              "Zwei benachbarte Bloecke: 10 Faces = 20 Dreiecke (innere Faces gecullt)");
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* ---------- Test 3: Nachbar-Culling ueber Chunk-Grenze ----------
     * Block bei x=31 (Rand Chunk 0) + Block bei x=32 (Rand Chunk 1).
     * Sie sind im Welt-Gitter direkt benachbart -> die zugewandten Faces
     * MUESSEN ueber die Chunk-Grenze gecullt werden. */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 31, 5, 5, 2); /* Chunk (0,0,0), lokal x=31 */
        set_block(w, 32, 5, 5, 2); /* Chunk (1,0,0), lokal x=0  */
        mb(w, 0, 0, 0);
        long c0 = g_last_chunk_triangles;
        mb(w, 1, 0, 0); /* cx=1 */
        long c1 = g_last_chunk_triangles;
        CHECK(c0 == 10 && c1 == 10,
              "Cross-Chunk-Culling: je 5 Faces pro Block (zugewandte Face gecullt)");
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* ---------- Test 4: Dirty-Propagation an x=31 markiert Chunk cx+1 ----------
     * Wir bauen beide Chunks (clean), aendern dann x=31 und pruefen via
     * aktualisieren(), dass GENAU 2 Chunks remesht werden (eigener + Nachbar).
     * Erst Chunk 1 ein Block geben, damit der Nachbar existiert. */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 0, 0, 0, 1);   /* Chunk 0 existiert + dirty */
        set_block(w, 32, 0, 0, 1);  /* Chunk 1 existiert + dirty */
        long n_first = (long)moo_as_number(moo_voxel_aktualisieren(w));
        CHECK(n_first == 2, "aktualisieren: anfangs 2 dirty Chunks gemesht");
        long n_clean = (long)moo_as_number(moo_voxel_aktualisieren(w));
        CHECK(n_clean == 0, "aktualisieren: nach Mesh keine dirty Chunks mehr");
        /* Aenderung an Chunk-0-Rand x=31 -> Chunk 0 dirty + Nachbar Chunk 1 dirty */
        set_block(w, 31, 0, 0, 3);
        long n_after = (long)moo_as_number(moo_voxel_aktualisieren(w));
        CHECK(n_after == 2,
              "Dirty-Boundary: Aenderung an x=31 markiert eigenen Chunk + cx+1 (2 remesht)");
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* ---------- Test 5: Aenderung in Chunk-Mitte propagiert NICHT ---------- */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 0, 0, 0, 1);
        set_block(w, 32, 0, 0, 1);
        moo_voxel_aktualisieren(w);
        set_block(w, 15, 15, 15, 3); /* mittig in Chunk 0, kein Rand */
        long n_after = (long)moo_as_number(moo_voxel_aktualisieren(w));
        CHECK(n_after == 1, "Innen-Aenderung: nur eigener Chunk dirty (1 remesht)");
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* ---------- Test 6: Render-ID-Lifetime: entladen loescht GENAU 1x ---------- */
    {
        int del_before = 0;
        for (int i = 0; i < g_next_render_id; i++) del_before += g_rid_delete_count[i];
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 1, 1, 1, 3);
        MooValue rid = mb(w, 0, 0, 0);
        int rv = (int)moo_as_number(rid);
        CHECK(rv >= 0 && g_rid_live[rv] == 1, "mesh_bauen legt Render-ID an (live)");
        moo_voxel_chunk_entladen(w, N(0), N(0), N(0));
        CHECK(g_rid_live[rv] == 0 && g_rid_delete_count[rv] == 1,
              "chunk_entladen loescht Render-ID GENAU einmal");
        /* zweites entladen desselben (nun leeren) Chunks -> kein weiteres delete */
        moo_voxel_chunk_entladen(w, N(0), N(0), N(0));
        CHECK(g_rid_delete_count[rv] == 1, "doppeltes entladen -> kein Double-Delete");
        moo_voxel_free((void*)(uintptr_t)w.data); /* darf rv NICHT erneut loeschen */
        CHECK(g_rid_delete_count[rv] == 1, "free nach entladen -> kein Double-Delete");
    }

    /* ---------- Test 7: free gibt alle aktiven Render-IDs frei (1x) ---------- */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 1, 1, 1, 3);
        set_block(w, 40, 1, 1, 3); /* zweiter Chunk */
        int r0 = (int)moo_as_number(mb(w, 0, 0, 0));
        int r1 = (int)moo_as_number(mb(w, 1, 0, 0));
        CHECK(g_rid_live[r0] == 1 && g_rid_live[r1] == 1, "zwei Render-IDs live");
        moo_voxel_free((void*)(uintptr_t)w.data);
        CHECK(g_rid_live[r0] == 0 && g_rid_live[r1] == 0 &&
              g_rid_delete_count[r0] == 1 && g_rid_delete_count[r1] == 1,
              "free loescht beide Render-IDs GENAU einmal");
    }

    /* ---------- Test 8: GPU-Cache-Lifetime ohne Backend (Risiko 10) ----------
     * Welt mit Blocks aufbauen WAEHREND Backend aus ist -> mesh_bauen legt
     * KEINE Render-ID an (gibt -1). free darf nicht crashen, kein chunk_delete. */
    {
        g_backend_active = 0;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 2, 2, 2, 4);
        MooValue rid = mb(w, 0, 0, 0);
        CHECK((int)moo_as_number(rid) == -1,
              "mesh_bauen ohne Backend: keine Render-ID (-1)");
        long n = (long)moo_as_number(moo_voxel_aktualisieren(w));
        CHECK(n == 0, "aktualisieren ohne Backend: 0 (kein GPU-Cache)");
        moo_voxel_free((void*)(uintptr_t)w.data); /* darf nicht crashen */
        CHECK(1, "free ohne Backend crasht nicht (CPU immer frei)");
        g_backend_active = 1;
    }

    /* ---------- Test 9: World ueberlebt Backend-Schluss ----------
     * Mesh MIT Backend bauen, dann Backend "schliessen", dann free.
     * moo_3d_chunk_delete ist no-op ohne Backend -> render_id wird NICHT erneut
     * real geloescht, kein Crash, kein Double-Delete-Assert. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 3, 3, 3, 5);
        int r = (int)moo_as_number(mb(w, 0, 0, 0));
        CHECK(g_rid_live[r] == 1, "Render-ID live bei aktivem Backend");
        g_backend_active = 0; /* Fenster/Backend zu */
        moo_voxel_free((void*)(uintptr_t)w.data); /* CPU frei, GPU no-op */
        CHECK(g_rid_delete_count[r] == 0,
              "free nach Backend-Schluss: chunk_delete ist no-op (kein Crash)");
        g_backend_active = 1;
    }

    /* ---------- Test 10: leerer Chunk (nur Luft gesetzt) -> kein Mesh ---------- */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        /* Setzen von Luft in nie-allokierten Chunk allokiert nicht -> Chunk
         * existiert gar nicht -> mesh_bauen liefert -1. */
        set_block(w, 7, 7, 7, 0);
        MooValue rid = mb(w, 0, 0, 0);
        CHECK((int)moo_as_number(rid) == -1,
              "mesh_bauen auf nie-allokiertem Chunk: -1 (keine Geometrie)");
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    free_tracked_strings();

    printf("\n== Ergebnis: %d PASS, %d FAIL ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
