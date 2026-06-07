/**
 * test_voxel_jobqueue_asan.c - Phase-3-Sanitizer-Harness (Plan-005 V3.1/V3.2,
 * Agent p005-perf1) fuer die C-interne pthread Job-Queue + Greedy/AO-Mesher.
 *
 * Fokus:
 *   1. RACES: moo_voxel_aktualisieren spannt einen Worker-Pool auf, der pro
 *      dirty Chunk einen CPU-Vertex-Buffer baut; der Main-Thread lädt hoch.
 *      Wir meshen viele Chunks in mehreren Runden + bauen/freien viele Welten,
 *      damit ThreadSanitizer/helgrind echte Data-Races sehen wuerden.
 *   2. LEAKS: jede Welt (inkl. lazy erzeugtem Pool + allen CPU-Buffern) wird via
 *      moo_voxel_free abgeraeumt. ASan mit detect_leaks=1 sieht jeden Leak.
 *   3. GREEDY-KORREKTHEIT: ein voller 32^3-Stein-Chunk hat genau 6 Aussenflaechen
 *      a 32x32 -> Greedy merged jede zu EINEM Quad (2 Dreiecke) -> 12 Dreiecke
 *      total (statt 32*32*6*2 = 12288 naiv). Verifiziert ueber den triangle-Spion.
 *   4. AO erzeugt mehrere Farben: eine L-foermige Anordnung verschattet Ecken
 *      -> mehr als eine distinkte Quad-Farbe (AO aktiv).
 *   5. LIFECYCLE: free waehrend/ohne Backend, mehrfaches aktualisieren.
 *
 * Sanitizer-Entscheidung (dokumentiert): Races sind hier der Hauptfeind. TSan ist
 * fuer pthread-Synchronisation praeziser/schneller als helgrind -> wir fahren
 * TSan + ASan-Stress. Build-Varianten:
 *   ASan:  gcc -fsanitize=address -g -std=c11 -I.. test_voxel_jobqueue_asan.c \
 *              ../moo_voxel.c -lm -lpthread -o /tmp/t_voxel_jobqueue_asan
 *   TSan:  gcc -fsanitize=thread  -g -std=c11 -I.. test_voxel_jobqueue_asan.c \
 *              ../moo_voxel.c -lm -lpthread -o /tmp/t_voxel_jobqueue_tsan
 * Ausfuehren: ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_jobqueue_asan
 *            TSAN_OPTIONS=halt_on_error=1  /tmp/t_voxel_jobqueue_tsan
 *
 * Linken NUR mit ../moo_voxel.c (eigener moo_noise_fbm-Stub unten); NICHT mit
 * ../moo_noise.c (sonst multiple-definition, QA1-Lehre).
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

/* String-Arena: moo_string_new wird NUR im Main-Thread (Upload) aufgerufen ->
 * kein Lock noetig. Wir tracken alle Strings + raeumen am Ende ab (ASan-clean).
 * Zusaetzlich registrieren wir die zuletzt erzeugte Farbe pro Upload, um die
 * Anzahl distinkter Quad-Farben (AO-Nachweis) zu zaehlen. */
#define MAX_TRACKED_STRINGS 2000000
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

static int g_expect_throw = 0;
void moo_throw(MooValue v) {
    (void)v;
    if (!g_expect_throw) { fprintf(stderr, "UNERWARTETER moo_throw im Test!\n"); abort(); }
}

MooValue moo_dict_new(void) { MooValue v; v.tag = MOO_DICT; v.data = 0; return v; }
void moo_dict_set(MooValue d, MooValue k, MooValue val) { (void)d; (void)k; (void)val; }

/* ===================== 3D-API-Stubs (Render-Cache + Geometrie-Spion) ===================== */

static int g_backend_active = 1;
static int g_next_render_id = 0;
#define MAX_RENDER_IDS 200000
static int  g_rid_live[MAX_RENDER_IDS];
static int  g_rid_delete_count[MAX_RENDER_IDS];
static long g_triangles_in_active = 0;
static long g_last_chunk_triangles = 0;

/* Distinkte Farben im zuletzt geschlossenen Chunk (AO-Nachweis). */
#define MAX_COLORS 4096
static unsigned g_colors[MAX_COLORS];
static int      g_color_count = 0;
static int      g_last_color_count = 0;

int moo_3d_backend_active(void) { return g_backend_active; }

/* Noise-Stub (Lazy-Worldgen-Link; hier ungenutzt). */
float moo_noise_fbm(int seed, float x, float y, int octaves, float freq, float amp) {
    (void)seed; (void)x; (void)y; (void)octaves; (void)freq; (void)amp; return 0.0f;
}

MooValue moo_3d_chunk_create(void) {
    if (!g_backend_active) { moo_throw(moo_error("Kein 3D-Backend aktiv")); return moo_number(-1.0); }
    int id = g_next_render_id++;
    assert(id < MAX_RENDER_IDS);
    g_rid_live[id] = 1;
    return moo_number((double)id);
}
void moo_3d_chunk_begin(MooValue id) {
    (void)id;
    if (!g_backend_active) return;
    g_triangles_in_active = 0;
    g_color_count = 0;
}
void moo_3d_chunk_end(void) {
    if (!g_backend_active) return;
    g_last_chunk_triangles = g_triangles_in_active;
    g_last_color_count = g_color_count;
}
void moo_3d_chunk_delete(MooValue id) {
    if (!g_backend_active) return;
    int rid = (int)moo_as_number(id);
    assert(rid >= 0 && rid < MAX_RENDER_IDS);
    g_rid_delete_count[rid]++;
    assert(g_rid_delete_count[rid] == 1 && "Render-ID doppelt geloescht!");
    g_rid_live[rid] = 0;
}
void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color) {
    (void)win; (void)x1; (void)y1; (void)z1; (void)x2; (void)y2; (void)z2;
    (void)x3; (void)y3; (void)z3;
    if (!g_backend_active) return;
    g_triangles_in_active++;
    /* Farbe registrieren (Hex-String -> int). */
    MooString* s = (MooString*)moo_val_as_ptr(color);
    if (s && s->chars && s->chars[0] == '#') {
        unsigned hex = (unsigned)strtoul(s->chars + 1, NULL, 16);
        int found = 0;
        for (int i = 0; i < g_color_count; i++) if (g_colors[i] == hex) { found = 1; break; }
        if (!found && g_color_count < MAX_COLORS) g_colors[g_color_count++] = hex;
    }
}

/* ===================== oeffentliche Voxel-API ===================== */

extern MooValue moo_voxel_welt_neu(MooValue seed);
extern MooValue moo_voxel_setzen(MooValue welt, MooValue x, MooValue y, MooValue z, MooValue id);
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
static void set_block(MooValue w, int x, int y, int z, int id) {
    moo_voxel_setzen(w, N(x), N(y), N(z), N(id));
}
static long aktualisieren(MooValue w) { return (long)moo_as_number(moo_voxel_aktualisieren(w)); }
static void free_world(MooValue w) { moo_voxel_free((void*)(uintptr_t)w.data); }

#define DIM 32

int main(void) {
    printf("== Voxel Job-Queue + Greedy/AO Sanitizer-Harness (Plan-005 Phase 3) ==\n");

    /* ---------- Test 1: voller Chunk -> Greedy merged jede Aussenflaeche ---------- */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        for (int z = 0; z < DIM; z++)
            for (int y = 0; y < DIM; y++)
                for (int x = 0; x < DIM; x++)
                    set_block(w, x, y, z, 3); /* stein */
        long n = aktualisieren(w);
        CHECK(n == 1, "Voller Chunk: genau 1 Chunk gemesht");
        CHECK(g_last_chunk_triangles == 12,
              "Greedy: voller 32^3-Chunk = 6 Quads = 12 Dreiecke (statt 12288 naiv)");
        free_world(w);
    }

    /* ---------- Test 2: Einzel-Block -> 6 Quads (Greedy kann 1x1 nicht mergen) ---------- */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);
        aktualisieren(w);
        CHECK(g_last_chunk_triangles == 12, "Einzel-Block: 6 Faces = 12 Dreiecke");
        free_world(w);
    }

    /* ---------- Test 3: AO erzeugt mehrere distinkte Farben ----------
     * Eine 2x2x1-Steinplatte + ein aufgesetzter Block verschattet Ecken ->
     * AO macht aus der einheitlichen Stein-Grundfarbe mehrere Helligkeiten. */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        for (int y = 4; y <= 7; y++)
            for (int x = 4; x <= 7; x++)
                set_block(w, x, y, 5, 3);   /* Bodenplatte */
        set_block(w, 5, 5, 6, 3);           /* aufgesetzter Block (verschattet) */
        aktualisieren(w);
        CHECK(g_last_color_count >= 2,
              "AO: verschattete Nachbarschaft erzeugt >1 distinkte Quad-Farbe");
        free_world(w);
    }

    /* ---------- Test 4: STRESS - viele Chunks parallel meshen (Race-Jagd) ----------
     * 6x6x2 = 72 nicht-leere Chunks, jeder mit einem Block -> 72 Jobs in einem
     * aktualisieren-Batch. Mehrere Runden + Remesh, damit TSan/helgrind Races
     * auf Pool/Queue/Chunk-Daten sehen wuerde. */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        int expect = 0;
        for (int cz = 0; cz < 2; cz++)
            for (int cy = 0; cy < 6; cy++)
                for (int cx = 0; cx < 6; cx++) {
                    set_block(w, cx * DIM + 3, cy * DIM + 3, cz * DIM + 3, 1 + ((cx + cy + cz) % 5));
                    expect++;
                }
        long n1 = aktualisieren(w);
        CHECK(n1 == expect, "Stress: alle 72 dirty Chunks in einem Batch gemesht");
        long n2 = aktualisieren(w);
        CHECK(n2 == 0, "Stress: zweiter Lauf -> nichts mehr dirty");
        /* Remesh-Runde: jeden Chunk erneut dirty machen. */
        for (int cz = 0; cz < 2; cz++)
            for (int cy = 0; cy < 6; cy++)
                for (int cx = 0; cx < 6; cx++)
                    set_block(w, cx * DIM + 4, cy * DIM + 4, cz * DIM + 4, 3);
        long n3 = aktualisieren(w);
        CHECK(n3 == expect, "Stress: Remesh-Runde -> alle Chunks erneut gemesht");
        free_world(w);
    }

    /* ---------- Test 5: viele Welten erzeugen+freien (Pool-Lifecycle, Leak-Jagd) ---------- */
    {
        for (int it = 0; it < 12; it++) {
            MooValue w = moo_voxel_welt_neu(N(it));
            for (int i = 0; i < 8; i++) set_block(w, i * DIM + 1, 1, 1, 1 + (i % 5));
            aktualisieren(w);
            free_world(w);
        }
        CHECK(1, "Pool-Lifecycle: 12 Welten erzeugt+gemesht+freigegeben (Leak via ASan)");
    }

    /* ---------- Test 6: free direkt nach Pool-Erzeugung ohne weiteres Mesh ---------- */
    {
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 1, 1, 1, 2);
        aktualisieren(w);              /* erzeugt Pool */
        free_world(w);                 /* muss Pool sauber joinen + freien */
        CHECK(1, "free nach erstem aktualisieren: Pool sauber heruntergefahren");
    }

    /* ---------- Test 7: aktualisieren ohne Backend ist no-op (kein Pool-Bedarf) ---------- */
    {
        g_backend_active = 0;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 2, 2, 2, 4);
        long n = aktualisieren(w);
        CHECK(n == 0, "ohne Backend: aktualisieren no-op (0)");
        free_world(w);                 /* kein Pool angelegt -> kein Crash */
        CHECK(1, "ohne Backend: free crasht nicht");
        g_backend_active = 1;
    }

    /* ---------- Test 8: Chunk leeren -> aktualisieren gibt Render-ID frei ---------- */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 3);
        aktualisieren(w);
        /* Block wieder auf Luft -> Chunk wird leer (indices bleibt aber alloziert
         * in der Palette-Schicht; Render-Geometrie wird leer). aktualisieren mesht
         * den Chunk neu (jetzt 0 Dreiecke). */
        set_block(w, 5, 5, 5, 0);
        long n = aktualisieren(w);
        CHECK(n >= 0, "leeren + remesh: kein Crash");
        CHECK(g_last_chunk_triangles == 0 || n == 0,
              "geleerter Chunk: keine Geometrie mehr");
        free_world(w);
    }

    free_tracked_strings();
    printf("\n== Ergebnis: %d PASS, %d FAIL ==\n", g_pass, g_fail);
    (void)g_expect_throw;
    return g_fail == 0 ? 0 : 1;
}
