/**
 * test_voxel_raycast_asan.c - Standalone-ASan-Harness fuer DDA-Raycast +
 * AABB-Overlap (Plan-005 Phase 1d, Agent p005-rt4).
 *
 * Strategie wie test_voxel_mesher_asan.c: moo_voxel.c haengt nur an
 * Deklarationen aus moo_runtime.h und an der 3D-Chunk-API. Raycast/AABB selbst
 * fassen die 3D-API NICHT an (reine CPU-Lookups), aber moo_voxel.c referenziert
 * die Symbole, also stubben wir sie linkbar. Im Gegensatz zum Mesher-Harness
 * brauchen wir hier ein FUNKTIONIERENDES Dict, um die Rueckgabefelder
 * (hit/x/y/z/nx/ny/nz/id/dist bzw. hit/count/x/y/z) zu verifizieren.
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra -I.. \
 *       test_voxel_raycast_asan.c ../moo_voxel.c -lm -o /tmp/t_voxel_raycast
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_raycast
 *
 * Getestet: Treffer (Achse + diagonal), Miss (leerer Raum / ueber Reichweite),
 * negative Koordinaten, Face-Normalen aus allen 6 Richtungen, Start-im-Block,
 * Null-Richtung wirft, invalider Handle wirft, AABB Treffer/Miss/Count/negativ.
 */

#include "moo_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ===================== moo-Runtime-Stubs ===================== */

void* moo_alloc(size_t size) { return malloc(size); }
void  moo_free(void* ptr)    { free(ptr); }

MooValue moo_number(double n) { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, n); return v; }
MooValue moo_bool(bool b)     { MooValue v; v.tag = MOO_BOOL;   v.data = (uint64_t)b; return v; }
MooValue moo_none(void)       { MooValue v; v.tag = MOO_NONE;   v.data = 0; return v; }
double   moo_as_number(MooValue v) { return moo_val_as_double(v); }

/* String-Arena: alle erzeugten Strings tracken und am Ende freigeben
 * (ASan-clean). moo_voxel.c erzeugt Strings nur als Dict-Keys + Fehlertexte. */
#define MAX_TRACKED_STRINGS 100000
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

/* moo_throw: Flag-basiert. Tests, die einen Wurf erwarten, setzen
 * g_expect_throw; ein unerwarteter Wurf bricht hart ab. WICHTIG: das echte
 * moo_throw kehrt nie zurueck (longjmp). Unsere getesteten Funktionen geben
 * nach moo_throw moo_none() zurueck, also ist Weiterlaufen hier sicher. */
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

/* ===================== Funktionierendes Dict ===================== */
/* Minimal-Dict: Key-String -> double (Bool wird als 0/1 gespeichert). Genau
 * was Raycast/AABB emittieren. Alle erzeugten Dicts werden getrackt und am
 * Testende freigegeben. */
typedef struct {
    char*  keys[16];
    double vals[16];
    int    count;
} TestDict;

#define MAX_TRACKED_DICTS 100000
static TestDict* g_dicts[MAX_TRACKED_DICTS];
static int       g_dict_count = 0;

MooValue moo_dict_new(void) {
    TestDict* d = (TestDict*)calloc(1, sizeof(TestDict));
    if (g_dict_count < MAX_TRACKED_DICTS) g_dicts[g_dict_count++] = d;
    MooValue v; v.tag = MOO_DICT; moo_val_set_ptr(&v, d);
    return v;
}
void moo_dict_set(MooValue dict, MooValue key, MooValue value) {
    TestDict* d = (TestDict*)moo_val_as_ptr(dict);
    assert(d != NULL);
    assert(d->count < 16);
    MooString* k = (MooString*)moo_val_as_ptr(key);
    size_t klen = strlen(k->chars) + 1;
    char* kc = (char*)malloc(klen);
    memcpy(kc, k->chars, klen);
    d->keys[d->count] = kc;
    /* Bool -> 0/1, Number -> Wert. */
    if (value.tag == MOO_BOOL) d->vals[d->count] = moo_val_as_bool(value) ? 1.0 : 0.0;
    else                       d->vals[d->count] = moo_val_as_double(value);
    d->count++;
}
static double dict_get(MooValue dict, const char* key) {
    TestDict* d = (TestDict*)moo_val_as_ptr(dict);
    for (int i = 0; i < d->count; i++) {
        if (strcmp(d->keys[i], key) == 0) return d->vals[i];
    }
    fprintf(stderr, "TEST-BUG: Dict-Key '%s' fehlt!\n", key);
    abort();
}
static void free_tracked_dicts(void) {
    for (int i = 0; i < g_dict_count; i++) {
        for (int j = 0; j < g_dicts[i]->count; j++) free(g_dicts[i]->keys[j]);
        free(g_dicts[i]);
    }
    g_dict_count = 0;
}

/* ===================== 3D-API-Stubs (linkbar, ungenutzt) ===================== */
/* Raycast/AABB beruehren die 3D-API nicht; die Stubs existieren nur fuers
 * Linken von moo_voxel.c. */
int      moo_3d_backend_active(void) { return 0; }
MooValue moo_3d_chunk_create(void)   { return moo_number(-1.0); }
void     moo_3d_chunk_begin(MooValue id) { (void)id; }
void     moo_3d_chunk_end(void)      { }
void     moo_3d_chunk_delete(MooValue id) { (void)id; }
void moo_3d_triangle(MooValue win,
                     MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color) {
    (void)win; (void)x1; (void)y1; (void)z1; (void)x2; (void)y2; (void)z2;
    (void)x3; (void)y3; (void)z3; (void)color;
}

/* ===================== Voxel-API (zu testen) ===================== */
extern MooValue moo_voxel_welt_neu(MooValue seed);
extern MooValue moo_voxel_setzen(MooValue welt, MooValue x, MooValue y, MooValue z, MooValue id);
extern MooValue moo_voxel_strahl(MooValue welt,
                                 MooValue ox, MooValue oy, MooValue oz,
                                 MooValue dx, MooValue dy, MooValue dz,
                                 MooValue max_dist);
extern MooValue moo_voxel_aabb(MooValue welt,
                               MooValue minx, MooValue miny, MooValue minz,
                               MooValue maxx, MooValue maxy, MooValue maxz);
extern void moo_voxel_free(void* ptr);

/* ===================== Test-Helfer ===================== */

static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fails++; fprintf(stderr, "  FAIL: %s (Zeile %d)\n", (msg), __LINE__); } \
} while (0)

static MooValue N(double n) { return moo_number(n); }

static void set_block(MooValue w, int x, int y, int z, int id) {
    g_expect_throw = 0; g_threw = 0;
    moo_voxel_setzen(w, N(x), N(y), N(z), N(id));
    assert(!g_threw);
}

static MooValue ray(MooValue w, double ox, double oy, double oz,
                    double dx, double dy, double dz, double maxd) {
    g_expect_throw = 0; g_threw = 0;
    return moo_voxel_strahl(w, N(ox), N(oy), N(oz), N(dx), N(dy), N(dz), N(maxd));
}

/* ===================== Tests ===================== */

static void test_ray_hit_axis(void) {
    printf("test_ray_hit_axis\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 5, 0, 0, 3); /* stein bei x=5 */

    /* Strahl von (0.5,0.5,0.5) entlang +X. Trifft Block 5 an dessen -X-Face. */
    MooValue r = ray(w, 0.5, 0.5, 0.5, 1.0, 0.0, 0.0, 100.0);
    CHECK(dict_get(r, "hit") == 1.0, "Achsen-Strahl muss treffen");
    CHECK(dict_get(r, "x") == 5.0, "Treffer-x == 5");
    CHECK(dict_get(r, "y") == 0.0, "Treffer-y == 0");
    CHECK(dict_get(r, "z") == 0.0, "Treffer-z == 0");
    CHECK(dict_get(r, "id") == 3.0, "Block-ID == 3 (stein)");
    /* Einstieg von links -> Normale (-1,0,0). */
    CHECK(dict_get(r, "nx") == -1.0, "Face-Normale -X");
    CHECK(dict_get(r, "ny") == 0.0, "ny == 0");
    CHECK(dict_get(r, "nz") == 0.0, "nz == 0");
    /* Einstiegs-Face bei x=5, Start x=0.5 -> dist 4.5. */
    CHECK(fabs(dict_get(r, "dist") - 4.5) < 1e-9, "dist == 4.5");

    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_miss_empty(void) {
    printf("test_ray_miss_empty\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    /* Leere Welt -> immer Miss. */
    MooValue r = ray(w, 0.5, 0.5, 0.5, 1.0, 0.0, 0.0, 100.0);
    CHECK(dict_get(r, "hit") == 0.0, "Leerer Raum -> kein Treffer");
    CHECK(dict_get(r, "id") == 0.0, "Miss id == 0");
    CHECK(dict_get(r, "dist") == 0.0, "Miss dist == 0");
    CHECK(dict_get(r, "nx") == 0.0 && dict_get(r, "ny") == 0.0 && dict_get(r, "nz") == 0.0,
          "Miss Normale 0");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_miss_out_of_range(void) {
    printf("test_ray_miss_out_of_range\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 50, 0, 0, 1); /* weit weg */
    /* max_dist 10 < Distanz 49.5 -> Miss. */
    MooValue r = ray(w, 0.5, 0.5, 0.5, 1.0, 0.0, 0.0, 10.0);
    CHECK(dict_get(r, "hit") == 0.0, "Ausserhalb Reichweite -> Miss");
    /* Mit grosser Reichweite -> Treffer. */
    MooValue r2 = ray(w, 0.5, 0.5, 0.5, 1.0, 0.0, 0.0, 100.0);
    CHECK(dict_get(r2, "hit") == 1.0, "In Reichweite -> Treffer");
    CHECK(dict_get(r2, "x") == 50.0, "Treffer x == 50");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_negative_coords(void) {
    printf("test_ray_negative_coords\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, -5, -1, -1, 2); /* erde im negativen Bereich, eigener Chunk -1/-1/-1 */
    /* Strahl von (0.5,-0.5,-0.5) entlang -X, trifft Block bei x=-5 an dessen +X-Face. */
    MooValue r = ray(w, 0.5, -0.5, -0.5, -1.0, 0.0, 0.0, 100.0);
    CHECK(dict_get(r, "hit") == 1.0, "Negativer Strahl trifft");
    CHECK(dict_get(r, "x") == -5.0, "Treffer x == -5");
    CHECK(dict_get(r, "y") == -1.0, "Treffer y == -1");
    CHECK(dict_get(r, "z") == -1.0, "Treffer z == -1");
    CHECK(dict_get(r, "id") == 2.0, "id == 2 (erde)");
    /* Einstieg von rechts (von +X kommend) -> Normale (+1,0,0). */
    CHECK(dict_get(r, "nx") == 1.0, "Face-Normale +X");
    /* Einstiegs-Face bei x=-4 (rechte Seite von Voxel -5), Start 0.5 -> dist 4.5. */
    CHECK(fabs(dict_get(r, "dist") - 4.5) < 1e-9, "dist == 4.5");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_all_face_normals(void) {
    printf("test_ray_all_face_normals\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 0, 0, 0, 1); /* Ziel-Block am Ursprung */

    /* Von +X kommend (Start x>1, Richtung -X) -> Normale +X. */
    MooValue rxp = ray(w, 5.5, 0.5, 0.5, -1.0, 0.0, 0.0, 100.0);
    CHECK(dict_get(rxp, "hit") == 1.0 && dict_get(rxp, "nx") == 1.0, "Einstieg +X");
    /* Von -X kommend -> Normale -X. */
    MooValue rxn = ray(w, -5.5, 0.5, 0.5, 1.0, 0.0, 0.0, 100.0);
    CHECK(dict_get(rxn, "hit") == 1.0 && dict_get(rxn, "nx") == -1.0, "Einstieg -X");
    /* Von oben (+Y) -> Normale +Y. */
    MooValue ryp = ray(w, 0.5, 5.5, 0.5, 0.0, -1.0, 0.0, 100.0);
    CHECK(dict_get(ryp, "hit") == 1.0 && dict_get(ryp, "ny") == 1.0, "Einstieg +Y");
    /* Von unten (-Y) -> Normale -Y. */
    MooValue ryn = ray(w, 0.5, -5.5, 0.5, 0.0, 1.0, 0.0, 100.0);
    CHECK(dict_get(ryn, "hit") == 1.0 && dict_get(ryn, "ny") == -1.0, "Einstieg -Y");
    /* Von vorne (+Z) -> Normale +Z. */
    MooValue rzp = ray(w, 0.5, 0.5, 5.5, 0.0, 0.0, -1.0, 100.0);
    CHECK(dict_get(rzp, "hit") == 1.0 && dict_get(rzp, "nz") == 1.0, "Einstieg +Z");
    /* Von hinten (-Z) -> Normale -Z. */
    MooValue rzn = ray(w, 0.5, 0.5, -5.5, 0.0, 0.0, 1.0, 100.0);
    CHECK(dict_get(rzn, "hit") == 1.0 && dict_get(rzn, "nz") == -1.0, "Einstieg -Z");

    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_diagonal(void) {
    printf("test_ray_diagonal\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    /* Wand aus Bloecken bei x=3 (mehrere y/z), damit ein diagonaler Strahl
     * sie sicher trifft. */
    for (int y = 0; y < 4; y++)
        for (int z = 0; z < 4; z++)
            set_block(w, 3, y, z, 3);
    /* Diagonaler Strahl, der die Wand in der x=3-Ebene durchstoesst. */
    MooValue r = ray(w, 0.5, 0.5, 0.5, 1.0, 0.3, 0.3, 100.0);
    CHECK(dict_get(r, "hit") == 1.0, "Diagonaler Strahl trifft Wand");
    CHECK(dict_get(r, "x") == 3.0, "Wand-Treffer x == 3");
    CHECK(dict_get(r, "id") == 3.0, "Wand id == 3");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_start_inside_block(void) {
    printf("test_ray_start_inside_block\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 2, 2, 2, 4); /* sand */
    /* Ursprung liegt IM Block (2,2,2). Sofort-Treffer, dist 0, Normale 0. */
    MooValue r = ray(w, 2.5, 2.5, 2.5, 1.0, 0.0, 0.0, 100.0);
    CHECK(dict_get(r, "hit") == 1.0, "Start-im-Block -> Treffer");
    CHECK(dict_get(r, "x") == 2.0 && dict_get(r, "y") == 2.0 && dict_get(r, "z") == 2.0,
          "Treffer == Start-Voxel");
    CHECK(dict_get(r, "id") == 4.0, "id == 4 (sand)");
    CHECK(dict_get(r, "dist") == 0.0, "dist == 0 bei Start-im-Block");
    CHECK(dict_get(r, "nx") == 0.0 && dict_get(r, "ny") == 0.0 && dict_get(r, "nz") == 0.0,
          "Keine Einstiegsseite -> Normale 0");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_zero_direction_throws(void) {
    printf("test_ray_zero_direction_throws\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    g_expect_throw = 1; g_threw = 0;
    moo_voxel_strahl(w, N(0), N(0), N(0), N(0), N(0), N(0), N(10));
    CHECK(g_threw == 1, "Null-Richtung muss werfen");
    g_expect_throw = 0;
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_ray_invalid_handle_throws(void) {
    printf("test_ray_invalid_handle_throws\n");
    MooValue not_a_world = moo_number(42.0);
    g_expect_throw = 1; g_threw = 0;
    moo_voxel_strahl(not_a_world, N(0), N(0), N(0), N(1), N(0), N(0), N(10));
    CHECK(g_threw == 1, "Invalider Handle muss werfen");
    g_expect_throw = 0;
}

static void test_ray_max_dist_zero(void) {
    printf("test_ray_max_dist_zero\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 1, 0, 0, 1);
    MooValue r = ray(w, 0.5, 0.5, 0.5, 1.0, 0.0, 0.0, 0.0);
    CHECK(dict_get(r, "hit") == 0.0, "max_dist 0 -> Miss");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_aabb_hit_and_count(void) {
    printf("test_aabb_hit_and_count\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 0, 0, 0, 1);
    set_block(w, 1, 0, 0, 1);
    set_block(w, 0, 1, 0, 1);
    /* Box [0,2]x[0,2]x[0,1] ueberlappt Zellen (0..1, 0..1, 0). Solide: (0,0,0),
     * (1,0,0), (0,1,0) -> count 3. */
    MooValue r = moo_voxel_aabb(w, N(0.0), N(0.0), N(0.0), N(2.0), N(2.0), N(1.0));
    CHECK(dict_get(r, "hit") == 1.0, "AABB trifft solide Bloecke");
    CHECK(dict_get(r, "count") == 3.0, "AABB count == 3");
    /* Erster Treffer in Scan-Reihenfolge (z,y,x) ist (0,0,0). */
    CHECK(dict_get(r, "x") == 0.0 && dict_get(r, "y") == 0.0 && dict_get(r, "z") == 0.0,
          "Erster Treffer (0,0,0)");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_aabb_miss(void) {
    printf("test_aabb_miss\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 10, 10, 10, 1);
    MooValue r = moo_voxel_aabb(w, N(0.0), N(0.0), N(0.0), N(2.0), N(2.0), N(2.0));
    CHECK(dict_get(r, "hit") == 0.0, "AABB ueber leerem Raum -> Miss");
    CHECK(dict_get(r, "count") == 0.0, "Miss count == 0");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_aabb_negative_and_swapped(void) {
    printf("test_aabb_negative_and_swapped\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, -3, -3, -3, 2);
    /* min/max vertauscht angegeben -> Funktion normalisiert; deckt Zelle -3. */
    MooValue r = moo_voxel_aabb(w, N(-2.0), N(-2.0), N(-2.0), N(-3.5), N(-3.5), N(-3.5));
    CHECK(dict_get(r, "hit") == 1.0, "AABB negativ + vertauscht trifft");
    CHECK(dict_get(r, "count") == 1.0, "count == 1");
    CHECK(dict_get(r, "x") == -3.0 && dict_get(r, "y") == -3.0 && dict_get(r, "z") == -3.0,
          "Treffer (-3,-3,-3)");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_aabb_point_box(void) {
    printf("test_aabb_point_box\n");
    MooValue w = moo_voxel_welt_neu(N(0));
    set_block(w, 7, 7, 7, 3);
    /* Punkt-Box (min==max) im Inneren von Voxel 7. Deckt genau Zelle 7. */
    MooValue r = moo_voxel_aabb(w, N(7.5), N(7.5), N(7.5), N(7.5), N(7.5), N(7.5));
    CHECK(dict_get(r, "hit") == 1.0, "Punkt-Box im Block trifft");
    CHECK(dict_get(r, "count") == 1.0, "Punkt-Box count == 1");
    moo_voxel_free(moo_val_as_ptr(w));
}

static void test_aabb_invalid_handle_throws(void) {
    printf("test_aabb_invalid_handle_throws\n");
    MooValue not_a_world = moo_number(42.0);
    g_expect_throw = 1; g_threw = 0;
    moo_voxel_aabb(not_a_world, N(0), N(0), N(0), N(1), N(1), N(1));
    CHECK(g_threw == 1, "Invalider Handle (AABB) muss werfen");
    g_expect_throw = 0;
}

int main(void) {
    test_ray_hit_axis();
    test_ray_miss_empty();
    test_ray_miss_out_of_range();
    test_ray_negative_coords();
    test_ray_all_face_normals();
    test_ray_diagonal();
    test_ray_start_inside_block();
    test_ray_zero_direction_throws();
    test_ray_invalid_handle_throws();
    test_ray_max_dist_zero();
    test_aabb_hit_and_count();
    test_aabb_miss();
    test_aabb_negative_and_swapped();
    test_aabb_point_box();
    test_aabb_invalid_handle_throws();

    free_tracked_dicts();
    free_tracked_strings();

    printf("\n==== %d Checks, %d Fails ====\n", g_checks, g_fails);
    if (g_fails == 0) printf("ALLE TESTS BESTANDEN\n");
    return g_fails == 0 ? 0 : 1;
}
