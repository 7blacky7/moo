/**
 * test_voxel_section_asan.c - Standalone-ASan-Harness fuer das 8^3-Section-
 * Layout (Plan-006 Phase R1, implementiert von p006-r1; Tests von p006-r1t).
 *
 * Strategie identisch zu den anderen Voxel-Harnesses (z.B.
 * test_voxel_palette_asan.c / test_voxel_mesher_asan.c): moo_voxel.c haengt nur
 * an Deklarationen aus moo_runtime.h + der 3D-Chunk-API. Wir stubben den
 * moo-Heap auf malloc/free (damit ASan Leaks sieht), die 3D-API linkbar und
 * moo_noise_fbm (Lazy-Worldgen wird durch occupied-Chunks vermieden). Es wird
 * KEIN echtes moo_noise.c gelinkt - reiner Stub wie im mesher-Harness.
 *
 * Geprueft (verhaltensbasiert ueber die public API, Section-Interna opak):
 *   1. EMPTY-Section: einzelnes setzen(id!=0) -> nur DIESES Voxel traegt die
 *      ID, der Rest der Section bleibt 0 (NICHT faelschlich SOLID). RISIKO 3.
 *   2. SOLID-Upgrade: Section uniform fuellen, EIN Voxel aendern -> beide
 *      Werte korrekt lesbar; PALETTE{solid_id,new_id}.
 *   3. Section-Grenzen 7/8/15/16/23/24/31 + negative Welt-Koordinaten
 *      (floor-div Chunk + Section-Index): jeder Wert exakt, kein Overspill.
 *   4. Leere Chunks bleiben 0 Bytes Blockdaten (nur-Luft-Set in fernen Chunk).
 *   5. K2-Contract: alle 6 alten ram_statistik-Keys vorhanden;
 *      bytes_sections (falls vorhanden) >= 0; bytes_total >= Summe der Teile.
 *   6. Refcount/free: voller Chunk + viele Sections -> moo_voxel_free clean
 *      (ASan: detect_leaks=1).
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra -I.. \
 *       test_voxel_section_asan.c ../moo_voxel.c -lm -o /tmp/t_voxel_section
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_section
 */

#include "moo_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <setjmp.h>

/* ===================== moo-Runtime-Stubs ===================== */

void* moo_alloc(size_t size) { return malloc(size); }
void  moo_free(void* ptr)    { free(ptr); }

MooValue moo_number(double n) { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, n); return v; }
MooValue moo_bool(bool b)     { MooValue v; v.tag = MOO_BOOL;   v.data = (uint64_t)b; return v; }
MooValue moo_none(void)       { MooValue v; v.tag = MOO_NONE;   v.data = 0; return v; }
double   moo_as_number(MooValue v) { return moo_val_as_double(v); }

static jmp_buf g_throw_jmp;
static int     g_threw = 0;
void moo_throw(MooValue err) { (void)err; g_threw = 1; longjmp(g_throw_jmp, 1); }
MooValue moo_error(const char* msg) { (void)msg; return moo_none(); }

/* Statische String-Literale: Pointer direkt halten (kein strdup noetig). */
MooValue moo_string_new(const char* s) {
    MooValue v; v.tag = MOO_STRING;
    moo_val_set_ptr(&v, (void*)s);
    return v;
}

/* Minimales Dict: lineare Liste von (key,value)-Paaren. */
typedef struct { const char* k; double v; } DictEntry;
typedef struct { DictEntry* e; int n, cap; } Dict;
MooValue moo_dict_new(void) {
    Dict* d = (Dict*)calloc(1, sizeof(Dict));
    MooValue v; v.tag = MOO_DICT; moo_val_set_ptr(&v, d);
    return v;
}
void moo_dict_set(MooValue dict, MooValue key, MooValue val) {
    Dict* d = (Dict*)moo_val_as_ptr(dict);
    const char* k = (const char*)moo_val_as_ptr(key);
    if (d->n >= d->cap) {
        d->cap = d->cap ? d->cap * 2 : 8;
        d->e = (DictEntry*)realloc(d->e, (size_t)d->cap * sizeof(DictEntry));
    }
    d->e[d->n].k = k;
    d->e[d->n].v = moo_val_as_double(val);
    d->n++;
}
static int dict_has(MooValue dict, const char* key) {
    Dict* d = (Dict*)moo_val_as_ptr(dict);
    for (int i = 0; i < d->n; i++) if (strcmp(d->e[i].k, key) == 0) return 1;
    return 0;
}
static double dict_get(MooValue dict, const char* key) {
    Dict* d = (Dict*)moo_val_as_ptr(dict);
    for (int i = 0; i < d->n; i++) if (strcmp(d->e[i].k, key) == 0) return d->e[i].v;
    return -1.0;
}
static void dict_free(MooValue dict) {
    Dict* d = (Dict*)moo_val_as_ptr(dict);
    free(d->e); free(d);
}

/* Noise-Stub wie mesher-Harness: nur zum Linken, Lazy-Worldgen wird durch
 * occupied-Chunks (alle Lesepunkte zuvor gesetzt) vermieden. */
float moo_noise_fbm(int seed, float x, float y, int octaves, float freq, float amp) {
    (void)seed; (void)x; (void)y; (void)octaves; (void)freq; (void)amp;
    return 0.0f;
}

/* 3D-API-Stubs (CPU-Pfad fasst sie nie an, moo_voxel.c linkt sie). */
MooValue moo_3d_chunk_create(void) { return moo_number(-1.0); }
void     moo_3d_chunk_begin(MooValue id) { (void)id; }
void     moo_3d_chunk_end(void) {}
void     moo_3d_chunk_delete(MooValue id) { (void)id; }
void     moo_3d_triangle(MooValue w, MooValue ax, MooValue ay, MooValue az,
                         MooValue bx, MooValue by, MooValue bz,
                         MooValue cx, MooValue cy, MooValue cz, MooValue col) {
    (void)w;(void)ax;(void)ay;(void)az;(void)bx;(void)by;(void)bz;
    (void)cx;(void)cy;(void)cz;(void)col;
}
int moo_3d_backend_active(void) { return 0; }

/* ===================== Voxel-API-Prototypen ===================== */
extern MooValue moo_voxel_welt_neu(MooValue seed);
extern MooValue moo_voxel_setzen(MooValue w, MooValue x, MooValue y, MooValue z, MooValue id);
extern MooValue moo_voxel_holen(MooValue w, MooValue x, MooValue y, MooValue z);
extern MooValue moo_voxel_ram_statistik(MooValue w);
extern void     moo_voxel_free(void* ptr);

/* ===================== Test-Helfer ===================== */
static int g_checks = 0, g_fails = 0;
#define CHECK(cond, msg) do { g_checks++; if (!(cond)) { \
    g_fails++; printf("  FAIL: %s\n", (msg)); } } while (0)

static double holen(MooValue w, int x, int y, int z) {
    return moo_as_number(moo_voxel_holen(w, moo_number(x), moo_number(y), moo_number(z)));
}
static void setzen(MooValue w, int x, int y, int z, int id) {
    moo_voxel_setzen(w, moo_number(x), moo_number(y), moo_number(z), moo_number(id));
}

int main(void) {
    if (setjmp(g_throw_jmp)) {
        printf("UNERWARTETER WURF im Haupttest\n");
        return 1;
    }

    /* ---- 1. EMPTY-Section: Einzel-Write erzeugt kein faelschliches SOLID ---- */
    printf("test_section_empty_single_write\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        setzen(w, 2, 2, 2, 3); /* einzelnes Voxel in sonst leerer Section */
        CHECK(holen(w, 2, 2, 2) == 3, "gesetztes Voxel = 3");
        /* Gesamte 8^3-Section (0..7) ausser (2,2,2) muss Luft sein. */
        int rest_air = 1;
        for (int z = 0; z < 8; z++)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    if (x == 2 && y == 2 && z == 2) continue;
                    if ((int)holen(w, x, y, z) != 0) rest_air = 0;
                }
        CHECK(rest_air, "EMPTY-Section: Rest bleibt Luft (kein faelschl. SOLID)");
        /* Voxel in anderer Section desselben Chunks (>=8) -> Luft. */
        CHECK(holen(w, 8, 8, 8) == 0, "andere Section = Luft");
        CHECK(holen(w, 16, 0, 0) == 0, "weitere Section = Luft");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 2. SOLID-Upgrade: uniforme Section, 1 Voxel aendern ---- */
    printf("test_section_solid_upgrade\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        for (int z = 0; z < 8; z++)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    setzen(w, x, y, z, 3); /* SOLID(3) */
        setzen(w, 4, 4, 4, 5);             /* -> PALETTE{3,5} */
        CHECK(holen(w, 4, 4, 4) == 5, "geaendertes Voxel = 5");
        int rest3 = 1;
        for (int z = 0; z < 8; z++)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    if (x == 4 && y == 4 && z == 4) continue;
                    if ((int)holen(w, x, y, z) != 3) rest3 = 0;
                }
        CHECK(rest3, "SOLID-Rest weiterhin 3 nach Upgrade");
        /* Geaendertes Voxel auf Luft -> 0, Rest bleibt 3. */
        setzen(w, 4, 4, 4, 0);
        CHECK(holen(w, 4, 4, 4) == 0, "auf Luft gesetzt = 0");
        CHECK(holen(w, 0, 0, 0) == 3, "Rest stabil nach Luft-Set");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 3. Section-Grenzen + negative Koordinaten ---- */
    printf("test_section_boundaries\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        int gb[] = { 7, 8, 15, 16, 23, 24, 31 };
        int ids[] = { 1, 2, 3, 4, 5, 1, 2 };
        for (int i = 0; i < 7; i++) setzen(w, gb[i], 0, 0, ids[i]);
        int ok = 1;
        for (int i = 0; i < 7; i++)
            if ((int)holen(w, gb[i], 0, 0) != ids[i]) ok = 0;
        CHECK(ok, "Section-Grenzen entlang x exakt");
        CHECK(holen(w, 9, 0, 0) == 0,  "Luecke 9 = Luft");
        CHECK(holen(w, 17, 0, 0) == 0, "Luecke 17 = Luft");

        /* Negative Welt-Koordinaten: floor-div + Section-Index. */
        setzen(w, -1, -1, -1, 5);
        setzen(w, -8, -8, -8, 1);
        setzen(w, -9, -9, -9, 2);
        setzen(w, -16, 0, 0, 3);
        CHECK(holen(w, -1, -1, -1) == 5,  "neg (-1) korrekt");
        CHECK(holen(w, -8, -8, -8) == 1,  "neg (-8) korrekt");
        CHECK(holen(w, -9, -9, -9) == 2,  "neg (-9) korrekt");
        CHECK(holen(w, -16, 0, 0) == 3,   "neg (-16) korrekt");
        CHECK(holen(w, -2, -1, -1) == 0,  "neg Nachbar Luft");
        CHECK(holen(w, -7, -8, -8) == 0,  "neg Nachbar Luft 2");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 4. Leerer Chunk = 0 Block-Bytes (nur-Luft-Set in fernen Chunk) ---- */
    printf("test_section_empty_chunk_zero_bytes\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        setzen(w, 5000, 0, 5000, 0); /* Luft in nie beruehrten Chunk */
        MooValue st = moo_voxel_ram_statistik(w);
        CHECK(dict_get(st, "bytes_blocks")  == 0, "leerer Chunk: 0 Block-Bytes");
        CHECK(dict_get(st, "bytes_palette") == 0, "leerer Chunk: 0 Palette-Bytes");
        CHECK(dict_get(st, "empty_chunks")  >= 1, "empty_chunks zaehlt Luft-Slot");
        if (dict_has(st, "bytes_sections"))
            CHECK(dict_get(st, "bytes_sections") >= 0, "bytes_sections>=0 (leer)");
        dict_free(st);
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 5. K2-Contract: alle 6 alten Keys + bytes_sections additiv ---- */
    printf("test_section_ram_contract\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Mix: SOLID-Section + PALETTE-Section in einem Chunk. */
        for (int z = 0; z < 8; z++)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    setzen(w, x, y, z, 3);          /* Section (0,0,0) SOLID */
        setzen(w, 12, 12, 12, 1);
        setzen(w, 13, 12, 12, 2);
        setzen(w, 14, 12, 12, 4);                   /* Section (1,1,1) PALETTE */
        MooValue st = moo_voxel_ram_statistik(w);
        CHECK(dict_has(st, "chunks"),        "key chunks");
        CHECK(dict_has(st, "bytes_blocks"),  "key bytes_blocks");
        CHECK(dict_has(st, "bytes_palette"), "key bytes_palette");
        CHECK(dict_has(st, "bytes_mesh"),    "key bytes_mesh");
        CHECK(dict_has(st, "bytes_total"),   "key bytes_total");
        CHECK(dict_has(st, "empty_chunks"),  "key empty_chunks");
        double blocks = dict_get(st, "bytes_blocks");
        double pal    = dict_get(st, "bytes_palette");
        double mesh   = dict_get(st, "bytes_mesh");
        double total  = dict_get(st, "bytes_total");
        CHECK(blocks >= 0 && pal >= 0 && mesh >= 0, "Teil-Bytes >= 0");
        CHECK(total  >= blocks + pal + mesh, "bytes_total >= Summe der Teile");
        if (dict_has(st, "bytes_sections")) {
            double secs = dict_get(st, "bytes_sections");
            CHECK(secs >= 0, "bytes_sections >= 0 (additiv)");
            CHECK(total >= blocks + pal + mesh + secs - 1e-6,
                  "bytes_total enthaelt auch Section-Header");
        }
        dict_free(st);
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 6. Refcount/free ueber viele Sections (ASan-Leak-Check) ---- */
    printf("test_section_free_many_sections\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Voller 32^3 Chunk im Schachbrettmuster -> alle 64 Sections belegt,
         * gemischte Modi. free muss alle Section-Allokationen freigeben. */
        for (int z = 0; z < 32; z++)
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++)
                    setzen(w, x, y, z, ((x + y + z) % 5) + 1);
        /* Stichprobe der Korrektheit. */
        CHECK(holen(w, 0, 0, 0) == 1, "schachbrett (0,0,0)");
        CHECK(holen(w, 31, 31, 31) == ((31*3) % 5) + 1, "schachbrett (31,31,31)");
        moo_voxel_free((void*)moo_val_as_ptr(w)); /* ASan prueft auf Leaks */
    }

    printf("\n==== %d Checks, %d Fails ====\n", g_checks, g_fails);
    if (g_threw) { printf("WARNUNG: unerwarteter Wurf irgendwo\n"); }
    if (g_fails == 0) { printf("ALLE TESTS BESTANDEN\n"); return 0; }
    return 1;
}
