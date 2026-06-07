/**
 * test_voxel_palette_asan.c - Standalone-ASan-Harness fuer die Palette-/
 * Bitpacking-Kompression (Plan-005 Phase 1c, Agent p005-rt3).
 *
 * Strategie wie die anderen Voxel-Harnesses: moo_voxel.c haengt nur an
 * Deklarationen aus moo_runtime.h + der 3D-Chunk-API. Wir stubben den moo-Heap
 * auf malloc/free (damit ASan Leaks sieht) und die 3D-API linkbar. Dict/String
 * werden minimal funktional gestubbt, weil voxel_ram_statistik ein echtes Dict
 * mit lesbaren Keys liefern muss.
 *
 * Geprueft:
 *   1. Round-Trip-Korrektheit: setzen/holen ueber alle Bitbreiten-Tiers
 *      (1/2/4/8/16 Bit) inkl. lazy Upgrade beim Hinzufuegen neuer IDs.
 *   2. Negative Koordinaten (Floor-Div/Mod) bleiben korrekt mit Palette.
 *   3. Ueberschreiben einer Zelle (Palette-Index-Wechsel) + Luft setzen.
 *   4. Leere Chunks = NULL: ein nur-Luft-Setzen allokiert nichts.
 *   5. ram_statistik-Keys STABIL + Werte plausibel (bytes_blocks gepackt,
 *      bytes_palette > 0, empty_chunks zaehlt Luft-Slots).
 *   6. RAM-BENCHMARK gegen das naive Layout (uint16 blocks[VOL] = 64 KB/Chunk)
 *      bei typischer Fuellung -> Ersparnis-Quote ausgegeben.
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra -I.. \
 *       test_voxel_palette_asan.c ../moo_voxel.c -lm -o /tmp/t_voxel_palette
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_palette
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

/* moo_throw via longjmp, damit Wurf-Tests den Kontrollfluss verlassen. */
#include <setjmp.h>
static jmp_buf g_throw_jmp;
static int     g_threw = 0;
void moo_throw(MooValue err) { (void)err; g_threw = 1; longjmp(g_throw_jmp, 1); }
MooValue moo_error(const char* msg) { (void)msg; return moo_none(); }

/* Minimaler String: die Keys aus voxel_ram_statistik sind String-Literale mit
 * statischer Lebensdauer -> wir halten den Pointer direkt (kein strdup, das
 * unter -std=c11 ein POSIX-Feature-Macro braeuchte). Block-Hex-Farben aus dem
 * Mesher werden hier nicht erzeugt (Backend inaktiv). */
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
    d->e[d->n].k = k;   /* statisches Literal, kein Kopieren noetig */
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

/* 3D-API-Stubs (Palette/CPU-Pfad fasst sie nie an, aber moo_voxel.c linkt sie). */
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

    /* ---- 1. Round-Trip ueber Bitbreiten-Tiers (lazy upgrade) ---- */
    printf("test_palette_roundtrip_tiers\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Innerhalb EINES Chunks (0..31) verschiedene IDs setzen. Phase-1-IDs
         * gehen nur bis 5 -> wir bleiben im 4-Bit-Tier; der Tier-Upgrade-Pfad
         * 1->2->4 wird durch das schrittweise Hinzufuegen distinkter IDs
         * durchlaufen. */
        setzen(w, 0,0,0, 1);                 /* 1. ID -> 1 Bit */
        CHECK(holen(w,0,0,0) == 1, "id1 nach erstem set");
        setzen(w, 1,0,0, 2);                 /* 2. ID -> noch 1 Bit (2 IDs: luft+1) ... */
        setzen(w, 2,0,0, 3);                 /* mehr distinkte IDs -> 2 Bit */
        setzen(w, 3,0,0, 4);                 /* -> 4 Bit (>4 Eintraege inkl. Luft) */
        setzen(w, 4,0,0, 5);
        CHECK(holen(w,0,0,0) == 1, "id1 nach upgrades stabil");
        CHECK(holen(w,1,0,0) == 2, "id2 stabil");
        CHECK(holen(w,2,0,0) == 3, "id3 stabil");
        CHECK(holen(w,3,0,0) == 4, "id4 stabil");
        CHECK(holen(w,4,0,0) == 5, "id5 stabil");
        CHECK(holen(w,5,0,0) == 0, "nicht gesetzt = luft");
        /* Ueberschreiben: Zelle auf andere ID, dann auf Luft. */
        setzen(w, 0,0,0, 3);
        CHECK(holen(w,0,0,0) == 3, "ueberschreiben id3");
        setzen(w, 0,0,0, 0);
        CHECK(holen(w,0,0,0) == 0, "ueberschreiben auf luft");
        CHECK(holen(w,4,0,0) == 5, "nachbar nach ueberschreiben intakt");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 2. Volle 8-/16-Bit-Tiers erzwingen ist mit Phase-1-IDs nicht
     * moeglich (max 6 IDs). Wir testen die Bit-Pack-Mechanik stattdessen
     * indirekt ueber Voll-Belegung eines Chunks mit allen 5 Block-IDs im
     * Schachbrettmuster und Verifikation jeder Zelle. ---- */
    printf("test_palette_full_chunk_pattern\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        for (int z = 0; z < 32; z++)
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    int id = ((x + y + z) % 5) + 1; /* 1..5, nie Luft */
                    setzen(w, x, y, z, id);
                }
        int ok = 1;
        for (int z = 0; z < 32 && ok; z++)
            for (int y = 0; y < 32 && ok; y++)
                for (int x = 0; x < 32 && ok; x++) {
                    int id = ((x + y + z) % 5) + 1;
                    if ((int)holen(w, x, y, z) != id) ok = 0;
                }
        CHECK(ok, "voller Chunk Schachbrett round-trip");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 3. Negative Koordinaten mit Palette ---- */
    printf("test_palette_negative_coords\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        int xs[] = { -1, -32, -33, 0, 31, 32 };
        for (int i = 0; i < 6; i++) setzen(w, xs[i], xs[i], xs[i], (i % 5) + 1);
        int ok = 1;
        for (int i = 0; i < 6; i++)
            if ((int)holen(w, xs[i], xs[i], xs[i]) != (i % 5) + 1) ok = 0;
        CHECK(ok, "negative Koordinaten round-trip mit Palette");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 4. Leere Chunks = NULL (nur Luft setzen allokiert nichts) ---- */
    printf("test_palette_empty_chunk_null\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        setzen(w, 100, 100, 100, 0); /* Luft in nie-beruehrten Chunk */
        MooValue st = moo_voxel_ram_statistik(w);
        CHECK(dict_get(st, "chunks") == 1, "luft-set legt Slot an (occupied)");
        CHECK(dict_get(st, "empty_chunks") == 1, "aber Chunk bleibt leer (NULL)");
        CHECK(dict_get(st, "bytes_blocks") == 0, "leerer Chunk = 0 Block-Bytes");
        CHECK(dict_get(st, "bytes_palette") == 0, "leerer Chunk = 0 Palette-Bytes");
        dict_free(st);
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 5. ram_statistik Keys STABIL ---- */
    printf("test_palette_ram_keys_stable\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        setzen(w, 0,0,0, 1);
        MooValue st = moo_voxel_ram_statistik(w);
        CHECK(dict_has(st, "chunks"),        "key chunks");
        CHECK(dict_has(st, "bytes_blocks"),  "key bytes_blocks");
        CHECK(dict_has(st, "bytes_palette"), "key bytes_palette");
        CHECK(dict_has(st, "bytes_mesh"),    "key bytes_mesh");
        CHECK(dict_has(st, "bytes_total"),   "key bytes_total");
        CHECK(dict_has(st, "empty_chunks"),  "key empty_chunks");
        CHECK(dict_get(st, "bytes_blocks")  > 0, "ein Festblock -> bytes_blocks>0");
        CHECK(dict_get(st, "bytes_palette") > 0, "ein Festblock -> bytes_palette>0");
        /* bytes_blocks eines 1-Bit-Chunks: VOL=32768 bits / 8 = 4096 Bytes. */
        CHECK(dict_get(st, "bytes_blocks") == 4096.0, "1-Bit Index-Array = 4096 Bytes");
        dict_free(st);
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 6. RAM-Benchmark gegen naives Layout ---- */
    printf("test_palette_ram_benchmark\n");
    {
        /* Synthetische "typische" Welt: ein Boden-Layer aus Gras/Erde/Stein +
         * sparse Streublocks. Wir fuellen ein 8x8 Chunk-Feld (x,z) je mit den
         * untersten 8 y-Lagen solide -> wenige distinkte IDs pro Chunk
         * (idealer Palette-Fall, realistisch fuer Terrain). */
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        int n_chunks = 0;
        for (int cz = 0; cz < 8; cz++) {
            for (int cx = 0; cx < 8; cx++) {
                n_chunks++;
                int bx = cx * 32, bz = cz * 32;
                for (int z = 0; z < 32; z++)
                    for (int x = 0; x < 32; x++)
                        for (int y = 0; y < 8; y++) {
                            int id = (y == 7) ? 1 : (y >= 4 ? 2 : 3); /* gras/erde/stein */
                            setzen(w, bx + x, y, bz + z, id);
                        }
            }
        }
        MooValue st = moo_voxel_ram_statistik(w);
        double palette_blocks  = dict_get(st, "bytes_blocks");
        double palette_pal     = dict_get(st, "bytes_palette");
        double palette_data    = palette_blocks + palette_pal;
        /* Naiv: jeder belegte (nicht-leere) Chunk haelt VOL*2 Bytes. */
        double naive_data = (double)n_chunks * (double)(32*32*32) * 2.0;
        double saving = 100.0 * (1.0 - palette_data / naive_data);
        printf("  BENCHMARK: %d Chunks (4 distinkte IDs: luft+gras+erde+stein)\n", n_chunks);
        printf("  naiv  (uint16 blocks): %.0f Bytes (%.1f KB)\n",
               naive_data, naive_data / 1024.0);
        printf("  palette (idx %.0f + pal %.0f): %.0f Bytes (%.1f KB)\n",
               palette_blocks, palette_pal, palette_data, palette_data / 1024.0);
        printf("  ERSPARNIS: %.2f%%\n", saving);
        CHECK(saving > 80.0, "RAM-Ersparnis > 80% (Plan-005 1c-Ziel)");
        dict_free(st);
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    printf("\n==== %d Checks, %d Fails ====\n", g_checks, g_fails);
    if (g_fails == 0) { printf("ALLE TESTS BESTANDEN\n"); return 0; }
    return 1;
}
