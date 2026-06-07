/**
 * test_voxel_dirty_rendercache_asan.c - P005-T2 ASan-Harness fuer Phase 1b.
 *
 * Fokus (Task [P005-T2]):
 *   1. test_voxel_dirty_boundary VOLLSTAENDIG: Eine Aenderung auf einer
 *      Chunk-Randflaeche (lokale Koordinate 0 oder DIM-1) markiert den eigenen
 *      Chunk + GENAU den angrenzenden Nachbarn in dieser EINEN Richtung dirty
 *      (Risiko 6: 6 direkte Nachbarn fuer Faces, nicht 26). Geprueft fuer ALLE
 *      6 Richtungen (-x,+x,-y,+y,-z,+z) einzeln, plus:
 *        - Aenderung in Chunk-Mitte propagiert NICHT (nur eigener Chunk).
 *        - Rand-Aenderung OHNE existierenden Nachbarn allokiert den Nachbarn
 *          NICHT (mark_dirty ist reiner Lookup) -> nur eigener Chunk remesht.
 *   2. test_voxel_render_cache_lifetime: World-Free nach Backend-Schluss
 *      crasht nicht, kein chunk_delete ohne Backend, kein Double-Delete.
 *        - Mesh mit Backend -> Backend zu -> aktualisieren() -> free: kein Crash.
 *        - Render-ID-Recycling: entladen + free loescht jede ID GENAU 1x.
 *   3. P006-R4 AO-Dirty (Dirty-Flag-Splitting, ADDITIV zu 1.): eine Aenderung an
 *      einer Chunk-KANTE/-ECKE markiert die DIAGONALEN Nachbarn (dirty_ao) fuer
 *      einen AO-Remesh, weil deren Boundary-Vertex-AO sonst stale waere. Geprueft:
 *      Kante mit/ohne diagonalen Nachbar (4 bzw. 3 remesht), Ecke mit allen 7
 *      Diagonalen+Faces (8 remesht), negative Kante, FLAECHE markiert KEINE
 *      Diagonale (Overhead-Schutz), Innen-Edit mit vorhandener Diagonale = 1.
 *      Der 6-Face-Dirty-Contract aus 1. bleibt unveraendert (Face-Tests gruen).
 *
 * Die RT2-Harness (test_voxel_mesher_asan.c) deckt Face-Culling-Counts und den
 * +x-Sonderfall ab; diese Harness deckt die uebrigen 5 Boundary-Richtungen,
 * den "kein Nachbar"-Fall und zusaetzliche Lifetime-Pfade ab. Beide stubben die
 * 3D-API headless -> deterministisch, ASan sieht alle CPU-Allokationen.
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address -g -std=c11 -I.. \
 *       test_voxel_dirty_rendercache_asan.c ../moo_voxel.c -lm \
 *       -o /tmp/t_voxel_dirty
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_dirty
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

/* String-Arena (siehe RT2-Harness): pro Voxel-Farbe ein String, mehrfach an
 * triangle gereicht; Stub-triangle gibt NICHT frei. Wir tracken + raeumen am
 * Ende ab -> ASan-clean. */
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

MooValue moo_dict_new(void) { MooValue v; v.tag = MOO_DICT; v.data = 0; return v; }
void moo_dict_set(MooValue d, MooValue k, MooValue val) { (void)d; (void)k; (void)val; }

/* ===================== 3D-API-Stubs (Render-Cache-Spion) ===================== */

static int g_backend_active = 1;
static int g_next_render_id = 0;
#define MAX_RENDER_IDS 100000
static int g_rid_live[MAX_RENDER_IDS];
static int g_rid_delete_count[MAX_RENDER_IDS];
static int g_active_chunk = -1;
static long g_triangles_in_active = 0;
static long g_last_chunk_triangles = 0;

int moo_3d_backend_active(void) { return g_backend_active; }

/* Noise-Stub: moo_voxel.c referenziert seit RT5 moo_noise_fbm (Lazy-Worldgen
 * in voxel_holen). Dieser Harness ruft voxel_holen nicht auf -> Stub dient nur
 * dem Linken (moo_noise.c wird hier nicht mitkompiliert). */
float moo_noise_fbm(int seed, float x, float y, int octaves, float freq, float amp) {
    (void)seed; (void)x; (void)y; (void)octaves; (void)freq; (void)amp;
    return 0.0f;
}

MooValue moo_3d_chunk_create(void) {
    if (!g_backend_active) {
        moo_throw(moo_error("Kein 3D-Backend aktiv"));
        return moo_number(-1.0);
    }
    int id = g_next_render_id++;
    assert(id < MAX_RENDER_IDS);
    g_rid_live[id] = 1;
    return moo_number((double)id);
}

void moo_3d_chunk_begin(MooValue id) {
    if (!g_backend_active) return;
    g_active_chunk = (int)moo_as_number(id);
    g_triangles_in_active = 0;
}

void moo_3d_chunk_end(void) {
    if (!g_backend_active) return;
    g_last_chunk_triangles = g_triangles_in_active;
    g_active_chunk = -1;
}

void moo_3d_chunk_delete(MooValue id) {
    if (!g_backend_active) return; /* safe no-op (wie echtes moo_3d_chunk_delete) */
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
    (void)x3; (void)y3; (void)z3; (void)color;
    if (!g_backend_active) return;
    g_triangles_in_active++;
}

/* ===================== oeffentliche Voxel-API ===================== */

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
static void set_block(MooValue w, int x, int y, int z, int id) {
    moo_voxel_setzen(w, N(x), N(y), N(z), N(id));
}
static long aktualisieren(MooValue w) {
    return (long)moo_as_number(moo_voxel_aktualisieren(w));
}
static void free_world(MooValue w) {
    moo_voxel_free((void*)(uintptr_t)w.data);
}

/* Baut zwei benachbarte, NICHT-leere Chunks auf, mesht beide clean und prueft
 * dann: Aenderung am Block (wx,wy,wz) [Rand des eigenen Chunks] markiert
 * eigenen Chunk + den Nachbar-Chunk -> aktualisieren() remesht GENAU 2.
 * own_* / nb_* sind ein nicht-Rand-Block in jedem Chunk, damit beide
 * existieren und Geometrie haben. */
static void boundary_dir_test(const char* name,
                              int own_x, int own_y, int own_z,
                              int nb_x,  int nb_y,  int nb_z,
                              int edge_x, int edge_y, int edge_z) {
    g_backend_active = 1;
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, own_x, own_y, own_z, 1); /* eigener Chunk existiert + dirty */
    set_block(w, nb_x,  nb_y,  nb_z,  1); /* Nachbar-Chunk existiert + dirty */
    long n0 = aktualisieren(w);
    CHECK(n0 == 2, name); /* sanity: beide initial dirty (im name kodiert) */
    long clean = aktualisieren(w);
    /* Randblock im eigenen Chunk setzen -> eigener + 1 Nachbar dirty. */
    set_block(w, edge_x, edge_y, edge_z, 3);
    long n_after = aktualisieren(w);
    CHECK(clean == 0 && n_after == 2, name);
    free_world(w);
}

int main(void) {
    printf("== P005-T2 Voxel Dirty-Boundary + Render-Cache ASan-Harness ==\n");

    /* ---------- 6 Boundary-Richtungen einzeln ----------
     * Chunk-DIM = 32. Welt-Koordinate k -> Chunk floor(k/32), lokal k mod 32.
     * Wir legen je einen "Anker"-Block tief im eigenen Chunk (Chunk 0,0,0) und
     * einen im Ziel-Nachbarn, dann aendern wir die zugewandte Randflaeche. */

    /* -x: lokales lx=0 (wx=0) -> Nachbar cx-1 (wx=-1, Chunk -1,0,0). */
    boundary_dir_test("Dirty -x: wx=0 markiert Chunk cx-1 (2 remesht)",
                      5, 5, 5,   -5, 5, 5,   0, 5, 5);
    /* +x: lokales lx=31 (wx=31) -> Nachbar cx+1 (Chunk 1,0,0). */
    boundary_dir_test("Dirty +x: wx=31 markiert Chunk cx+1 (2 remesht)",
                      5, 5, 5,   40, 5, 5,   31, 5, 5);
    /* -y: wy=0 -> Nachbar cy-1. */
    boundary_dir_test("Dirty -y: wy=0 markiert Chunk cy-1 (2 remesht)",
                      5, 5, 5,   5, -5, 5,   5, 0, 5);
    /* +y: wy=31 -> Nachbar cy+1. */
    boundary_dir_test("Dirty +y: wy=31 markiert Chunk cy+1 (2 remesht)",
                      5, 5, 5,   5, 40, 5,   5, 31, 5);
    /* -z: wz=0 -> Nachbar cz-1. */
    boundary_dir_test("Dirty -z: wz=0 markiert Chunk cz-1 (2 remesht)",
                      5, 5, 5,   5, 5, -5,   5, 5, 0);
    /* +z: wz=31 -> Nachbar cz+1. */
    boundary_dir_test("Dirty +z: wz=31 markiert Chunk cz+1 (2 remesht)",
                      5, 5, 5,   5, 5, 40,   5, 5, 31);

    /* ---------- Mitte propagiert NICHT ---------- */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);
        set_block(w, 40, 5, 5, 1); /* Nachbar existiert, darf aber nicht dirty werden */
        aktualisieren(w);
        set_block(w, 15, 15, 15, 3); /* mittig, keine Randflaeche */
        long n = aktualisieren(w);
        CHECK(n == 1, "Innen-Aenderung: nur eigener Chunk dirty (1 remesht, kein Nachbar)");
        free_world(w);
    }

    /* ---------- Rand-Aenderung OHNE existierenden Nachbarn ----------
     * mark_dirty ist reiner Lookup -> ein nie-allokierter Luft-Nachbar wird
     * NICHT angelegt. Aenderung an wx=31 ohne Chunk 1 -> nur eigener remesht. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1); /* nur Chunk 0 existiert */
        aktualisieren(w);
        set_block(w, 31, 5, 5, 3); /* Rand, aber Nachbar Chunk 1 existiert nicht */
        long n = aktualisieren(w);
        CHECK(n == 1, "Rand ohne Nachbar: kein Nachbar allokiert (nur 1 remesht)");
        free_world(w);
    }

    /* ---------- alle 3 Achsen-Raender gleichzeitig (Ecke) ----------
     * Block bei (31,31,31) im Chunk 0 mit existierenden Nachbarn NUR in +x,+y,+z
     * (KEINE diagonalen). Face-Dirty markiert die 3 Face-Nachbarn; die AO-Dirty
     * (P006-R4) wuerde die diagonalen Kanten-/Eck-Nachbarn markieren, die hier
     * aber NICHT existieren -> mark_dirty_ao ist reiner Lookup, allokiert nichts.
     * -> eigener + 3 Face-Nachbarn = 4 dirty (Face-Contract UNVERAENDERT). */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);     /* Chunk 0 */
        set_block(w, 40, 5, 5, 1);    /* Chunk +x */
        set_block(w, 5, 40, 5, 1);    /* Chunk +y */
        set_block(w, 5, 5, 40, 1);    /* Chunk +z */
        aktualisieren(w);
        set_block(w, 31, 31, 31, 3);  /* Ecke: +x,+y,+z Raender gleichzeitig */
        long n = aktualisieren(w);
        CHECK(n == 4, "Ecke (31,31,31) ohne Diagonale: eigener + 3 Face-Nachbarn (4 remesht)");
        free_world(w);
    }

    /* =====================================================================
     * P006-R4 — AO-Dirty fuer DIAGONALE Boundary-Nachbarn (ADDITIV zum
     * 6-Face-Dirty-Contract). Diese Tests pruefen, dass eine Aenderung an
     * einer Chunk-KANTE/-ECKE die diagonalen Nachbarn fuer einen AO-Remesh
     * dirty markiert — und dass Innen-/Flaechen-Edits das NICHT tun.
     * ===================================================================== */

    /* ---------- KANTE: Voxel an einer Chunk-Kante (2 Raender) ----------
     * Voxel (31,31,5) liegt an der +x/+y-Kante von Chunk 0. Betroffen sind:
     *   - eigener Chunk 0
     *   - Face-Nachbar +x (Chunk 1,0,0)   [via dirty]
     *   - Face-Nachbar +y (Chunk 0,1,0)   [via dirty]
     *   - DIAGONALER Kanten-Nachbar +x+y (Chunk 1,1,0) [via dirty_ao, P006-R4]
     * Alle 4 existieren -> 4 remesht. OHNE P006-R4 waeren es nur 3 (Diagonale
     * bliebe stale). */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);      /* Chunk (0,0,0) */
        set_block(w, 40, 5, 5, 1);     /* Chunk (1,0,0)  +x  Face */
        set_block(w, 5, 40, 5, 1);     /* Chunk (0,1,0)  +y  Face */
        set_block(w, 40, 40, 5, 1);    /* Chunk (1,1,0)  +x+y DIAGONAL */
        aktualisieren(w);
        set_block(w, 31, 31, 5, 3);    /* +x/+y-Kante */
        long n = aktualisieren(w);
        CHECK(n == 4, "Kante (31,31,5): eigener + 2 Face + 1 diagonaler AO-Nachbar (4 remesht)");
        free_world(w);
    }

    /* ---------- KANTE ohne diagonalen Nachbar ----------
     * Wie oben, aber der diagonale +x+y-Chunk existiert NICHT. mark_dirty_ao ist
     * reiner Lookup -> kein Luft-Nachbar wird allokiert. -> nur eigener + 2 Face
     * = 3 remesht. Beweist: AO-Dirty erfindet keine Chunks. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);      /* Chunk (0,0,0) */
        set_block(w, 40, 5, 5, 1);     /* Chunk (1,0,0)  +x  Face */
        set_block(w, 5, 40, 5, 1);     /* Chunk (0,1,0)  +y  Face */
        /* KEIN (1,1,0) */
        aktualisieren(w);
        set_block(w, 31, 31, 5, 3);    /* +x/+y-Kante, Diagonale fehlt */
        long n = aktualisieren(w);
        CHECK(n == 3, "Kante ohne Diagonale: eigener + 2 Face, kein Diagonal-Allok (3 remesht)");
        free_world(w);
    }

    /* ---------- ECKE: Voxel (31,31,31), ALLE 7 Diagonalen+Faces da ----------
     * Eck-Voxel erreicht ( box {0,+1}^3 minus self): 3 Face (+x,+y,+z),
     * 3 Kanten (+x+y, +x+z, +y+z), 1 Eck (+x+y+z) = 7 Nachbarn. Mit eigenem
     * Chunk = 8 remesht (das volle 26er-Maximum, hier minimal als 7 betroffene).
     * Face via dirty, die 4 diagonalen (Kanten+Ecke) via dirty_ao (P006-R4). */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);       /* (0,0,0) */
        set_block(w, 40, 5, 5, 1);      /* (1,0,0)  +x   Face */
        set_block(w, 5, 40, 5, 1);      /* (0,1,0)  +y   Face */
        set_block(w, 5, 5, 40, 1);      /* (0,0,1)  +z   Face */
        set_block(w, 40, 40, 5, 1);     /* (1,1,0)  +x+y Kante */
        set_block(w, 40, 5, 40, 1);     /* (1,0,1)  +x+z Kante */
        set_block(w, 5, 40, 40, 1);     /* (0,1,1)  +y+z Kante */
        set_block(w, 40, 40, 40, 1);    /* (1,1,1)  +x+y+z Ecke */
        aktualisieren(w);
        set_block(w, 31, 31, 31, 3);    /* Ecke: alle 3 Achsen-Raender */
        long n = aktualisieren(w);
        CHECK(n == 8, "Ecke (31,31,31) mit allen Diagonalen: eigener + 3 Face + 3 Kanten + 1 Ecke (8 remesht)");
        free_world(w);
    }

    /* ---------- KANTE am unteren Rand (lx=0, ly=0) negative Diagonale ----------
     * Spiegelbild zum +-Fall: Voxel (0,0,5) erreicht -x, -y, -x-y. Prueft, dass
     * die Offset-Logik auch fuer die negative Richtung korrekt diagonal markiert. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);       /* (0,0,0) */
        set_block(w, -5, 5, 5, 1);      /* (-1,0,0)  -x  Face */
        set_block(w, 5, -5, 5, 1);      /* (0,-1,0)  -y  Face */
        set_block(w, -5, -5, 5, 1);     /* (-1,-1,0) -x-y DIAGONAL */
        aktualisieren(w);
        set_block(w, 0, 0, 5, 3);       /* -x/-y-Kante */
        long n = aktualisieren(w);
        CHECK(n == 4, "Kante (0,0,5) negativ: eigener + 2 Face + 1 diagonaler AO-Nachbar (4 remesht)");
        free_world(w);
    }

    /* ---------- FLAECHE markiert KEINE Diagonale (Overhead-Schutz) ----------
     * Voxel (31,5,5) liegt nur auf der +x-FLAECHE (1 Rand). Selbst wenn die
     * diagonalen +x+y / +x+z-Chunks existieren, duerfen sie NICHT dirty werden
     * (das Voxel ist nicht in deren Pad-Schale). -> nur eigener + 1 Face = 2. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);       /* (0,0,0) */
        set_block(w, 40, 5, 5, 1);      /* (1,0,0)  +x   Face */
        set_block(w, 40, 40, 5, 1);     /* (1,1,0)  +x+y diagonal — darf NICHT remeshen */
        set_block(w, 40, 5, 40, 1);     /* (1,0,1)  +x+z diagonal — darf NICHT remeshen */
        aktualisieren(w);
        set_block(w, 31, 5, 5, 3);      /* reine +x-Flaeche (ly,lz innen) */
        long n = aktualisieren(w);
        CHECK(n == 2, "Flaeche (31,5,5): nur eigener + 1 Face, keine diagonale AO-Dirty (2 remesht)");
        free_world(w);
    }

    /* ---------- Innen-Edit markiert weiterhin nichts (1 remesht) ----------
     * Doppelpruefung mit existierenden Diagonalen: ein voll innen liegendes
     * Voxel darf weder Face- noch AO-Nachbarn anfassen. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 5, 5, 5, 1);       /* (0,0,0) */
        set_block(w, 40, 40, 40, 1);    /* (1,1,1) Eck-Diagonale existiert */
        aktualisieren(w);
        set_block(w, 15, 15, 15, 3);    /* mittig */
        long n = aktualisieren(w);
        CHECK(n == 1, "Innen-Edit mit vorhandener Diagonale: nur eigener Chunk (1 remesht)");
        free_world(w);
    }

    /* ---------- Render-Cache-Lifetime: Mesh -> Backend zu -> free ----------
     * GENAU der Risiko-10-Fall: World ueberlebt den Backend-Schluss. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 3, 3, 3, 5);
        int r = (int)moo_as_number(moo_voxel_mesh_bauen(w, N(0), N(0), N(0)));
        CHECK(r >= 0 && g_rid_live[r] == 1, "Render-Cache: Mesh legt Render-ID an (Backend aktiv)");
        g_backend_active = 0;                 /* Fenster/Backend zu */
        long n = aktualisieren(w);            /* darf nicht crashen, no-op */
        CHECK(n == 0, "Render-Cache: aktualisieren ohne Backend ist no-op");
        free_world(w);                        /* CPU frei, GPU-delete no-op */
        CHECK(g_rid_delete_count[r] == 0,
              "Render-Cache: free nach Backend-Schluss ruft kein chunk_delete (no-op)");
        g_backend_active = 1;
    }

    /* ---------- Render-Cache-Lifetime: entladen + free, jede ID genau 1x ----------
     * Zwei Chunks meshen, einen entladen (1 delete), free loescht den Rest 1x. */
    {
        g_backend_active = 1;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 3, 3, 3, 5);   /* Chunk 0 */
        set_block(w, 40, 3, 3, 5);  /* Chunk 1 */
        int r0 = (int)moo_as_number(moo_voxel_mesh_bauen(w, N(0), N(0), N(0)));
        int r1 = (int)moo_as_number(moo_voxel_mesh_bauen(w, N(1), N(0), N(0)));
        CHECK(r0 >= 0 && r1 >= 0 && g_rid_live[r0] && g_rid_live[r1],
              "Render-Cache: zwei Render-IDs live");
        moo_voxel_chunk_entladen(w, N(0), N(0), N(0));
        CHECK(g_rid_delete_count[r0] == 1 && g_rid_live[r0] == 0,
              "Render-Cache: entladen loescht Chunk-0-ID genau 1x");
        free_world(w);
        CHECK(g_rid_delete_count[r0] == 1 && g_rid_delete_count[r1] == 1 &&
              g_rid_live[r1] == 0,
              "Render-Cache: free loescht restliche ID 1x, kein Double-Delete");
        g_backend_active = 1;
    }

    /* ---------- Render-Cache-Lifetime: Backend war NIE an ----------
     * Welt komplett ohne Backend aufbauen + meshen -> nie eine Render-ID,
     * free crasht nicht, kein chunk_delete. */
    {
        g_backend_active = 0;
        MooValue w = moo_voxel_welt_neu(N(0));
        set_block(w, 2, 2, 2, 4);
        int r = (int)moo_as_number(moo_voxel_mesh_bauen(w, N(0), N(0), N(0)));
        CHECK(r == -1, "Render-Cache: ohne Backend keine Render-ID (-1)");
        free_world(w);
        CHECK(1, "Render-Cache: free ohne je aktives Backend crasht nicht");
        g_backend_active = 1;
    }

    free_tracked_strings();

    printf("\n== Ergebnis: %d PASS, %d FAIL ==\n", g_pass, g_fail);
    (void)g_threw; (void)g_expect_throw; (void)g_active_chunk; (void)g_last_chunk_triangles;
    return g_fail == 0 ? 0 : 1;
}
