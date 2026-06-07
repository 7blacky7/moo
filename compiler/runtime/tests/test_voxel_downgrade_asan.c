/**
 * test_voxel_downgrade_asan.c - Standalone-ASan-Harness fuer die Mutation/
 * Downgrade-Optimierung (Plan-006 Phase R3, Agent p006-r3).
 *
 * Strategie identisch zu den anderen Voxel-Harnesses (z.B.
 * test_voxel_section_asan.c): moo_voxel.c haengt nur an Deklarationen aus
 * moo_runtime.h + der 3D-Chunk-API. Wir stubben den moo-Heap auf malloc/free
 * (damit ASan Leaks sieht), die 3D-API linkbar und moo_noise_fbm. Es wird KEIN
 * echtes moo_noise.c gelinkt.
 *
 * Geprueft (verhaltensbasiert ueber die public API; Section-Interna opak, aber
 * ueber ram_statistik beobachtbar):
 *   1. PALETTE->EMPTY: Section komplett auf Luft editiert + optimieren -> Block-
 *      und Palette-Bytes fallen auf 0; Lese-Resultate unveraendert (alles Luft).
 *   2. PALETTE->SOLID: Section uniform via voxel_setzen gefuellt (Einzel-Write
 *      -> PALETTE{0,id}), optimieren -> SOLID (Block-Bytes der Section -> 0),
 *      alle 512 Voxel lesen weiterhin die ID.
 *   3. Palette-Kompaktierung: Section mit mehreren IDs, dann alle bis auf eine
 *      ueberschrieben -> nach optimieren weniger/keine ungenutzten IDs,
 *      Block-Bytes sinken (kleinere Bitbreite), Lese-Resultate unveraendert.
 *   4. Chunk komplett leer editiert + optimieren -> Section-Array kollabiert auf
 *      NULL (bytes_sections faellt auf 0, empty_chunks zaehlt den Slot). K1.
 *   5. Lese-Neutralitaet ueber gemischte Welt: optimieren aendert KEIN einziges
 *      Lese-Resultat (reine Repraesentations-Umstellung).
 *   6. Idempotenz: zweimal optimieren -> zweiter Lauf meldet 0 weitere Aenderung
 *      bei den bereits optimalen Chunks (bzw. Bytes bleiben stabil).
 *   7. RAM-Gewinn deep-artig: voller Stein-Chunk via setzen (PALETTE 1-bit),
 *      optimieren -> SOLID, Block-Bytes -> 0 (das ist der R3-Kern fuer >95%).
 *   8. free nach optimieren clean (ASan: detect_leaks=1), inkl. NULL-kollabierter
 *      Chunks.
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address,undefined -g -std=c11 -Wall -Wextra -I.. \
 *       test_voxel_downgrade_asan.c ../moo_voxel.c -lm -o /tmp/t_voxel_downgrade
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_downgrade
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

float moo_noise_fbm(int seed, float x, float y, int octaves, float freq, float amp) {
    (void)seed; (void)x; (void)y; (void)octaves; (void)freq; (void)amp;
    return 0.0f;
}

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
extern MooValue moo_voxel_welt_optimieren(MooValue w);
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
static double optimieren(MooValue w) {
    return moo_as_number(moo_voxel_welt_optimieren(w));
}
static double stat(MooValue w, const char* key) {
    MooValue st = moo_voxel_ram_statistik(w);
    double v = dict_get(st, key);
    dict_free(st);
    return v;
}

/* Fuellt eine ganze 8^3-Section eines Chunks (Welt-Ursprung) mit id. */
static void fill_section(MooValue w, int id) {
    for (int z = 0; z < 8; z++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                setzen(w, x, y, z, id);
}

int main(void) {
    if (setjmp(g_throw_jmp)) {
        printf("UNERWARTETER WURF im Haupttest\n");
        return 1;
    }

    /* ---- 1. PALETTE->EMPTY: Section komplett auf Luft editiert ---- */
    printf("test_downgrade_palette_to_empty\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Section mit ein paar Festbloecken -> PALETTE. */
        setzen(w, 1, 1, 1, 3);
        setzen(w, 2, 2, 2, 4);
        setzen(w, 3, 3, 3, 5);
        CHECK(stat(w, "bytes_blocks") > 0, "vor Downgrade: Block-Bytes > 0");
        /* Alles wieder auf Luft. PALETTE bleibt (kein Lazy-Downgrade beim Setzen). */
        setzen(w, 1, 1, 1, 0);
        setzen(w, 2, 2, 2, 0);
        setzen(w, 3, 3, 3, 0);
        CHECK(stat(w, "bytes_blocks") > 0, "ohne optimieren: PALETTE bleibt belegt");
        double changed = optimieren(w);
        CHECK(changed >= 1, "optimieren meldet Aenderung");
        /* Chunk ist jetzt komplett leer -> NULL-Kollaps (K1). */
        CHECK(stat(w, "bytes_blocks") == 0,   "nach optimieren: 0 Block-Bytes");
        CHECK(stat(w, "bytes_palette") == 0,  "nach optimieren: 0 Palette-Bytes");
        CHECK(stat(w, "bytes_sections") == 0, "nach optimieren: 0 Section-Header (NULL-Kollaps)");
        CHECK(holen(w, 1, 1, 1) == 0, "Lese-Resultat unveraendert (Luft)");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 2. PALETTE->SOLID: uniforme Section via Einzel-Write ---- */
    printf("test_downgrade_palette_to_solid\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        fill_section(w, 3);  /* 512x setzen -> PALETTE{0,3} (kein direktes SOLID) */
        double blocks_before = stat(w, "bytes_blocks");
        CHECK(blocks_before > 0, "vor Downgrade: PALETTE-Indexbytes > 0");
        double changed = optimieren(w);
        CHECK(changed >= 1, "optimieren meldet Aenderung (SOLID)");
        CHECK(stat(w, "bytes_blocks") == 0, "nach Downgrade SOLID: 0 Index-Bytes");
        /* Section-Header bleiben (Chunk nicht leer). */
        CHECK(stat(w, "bytes_sections") > 0, "Chunk nicht leer -> Section-Header bleiben");
        /* Alle 512 Voxel lesen weiterhin 3. */
        int ok = 1;
        for (int z = 0; z < 8; z++)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    if ((int)holen(w, x, y, z) != 3) ok = 0;
        CHECK(ok, "SOLID-Section liest weiterhin 3");
        /* Nach SOLID weiter editierbar: 1 Voxel aendern -> wieder PALETTE. */
        setzen(w, 4, 4, 4, 5);
        CHECK(holen(w, 4, 4, 4) == 5, "SOLID nach Edit: geaendertes Voxel = 5");
        CHECK(holen(w, 0, 0, 0) == 3, "SOLID nach Edit: Rest = 3");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 3. Palette-Kompaktierung: ungenutzte IDs raus, Bitbreite runter ---- */
    printf("test_downgrade_palette_compaction\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Section mit 5 distinkten IDs -> braucht 4-bit (>4 Eintraege inkl. Luft). */
        setzen(w, 0, 0, 0, 1);
        setzen(w, 1, 0, 0, 2);
        setzen(w, 2, 0, 0, 3);
        setzen(w, 3, 0, 0, 4);
        setzen(w, 4, 0, 0, 5);
        double blocks_5ids = stat(w, "bytes_blocks");
        /* Jetzt alle bis auf eine ID ueberschreiben -> nur noch {0,1}. */
        setzen(w, 1, 0, 0, 1);
        setzen(w, 2, 0, 0, 1);
        setzen(w, 3, 0, 0, 1);
        setzen(w, 4, 0, 0, 1);
        /* Ohne optimieren bleibt die 4-bit-Breite (kein Lazy-Compact). */
        CHECK(stat(w, "bytes_blocks") == blocks_5ids, "ohne optimieren: Bitbreite bleibt 4-bit");
        optimieren(w);
        /* Jetzt nur noch {Luft, 1} -> 1-bit -> weniger Index-Bytes. */
        CHECK(stat(w, "bytes_blocks") < blocks_5ids, "nach Kompaktierung: weniger Index-Bytes");
        CHECK(stat(w, "bytes_blocks") > 0, "noch nicht leer (1 Block gesetzt)");
        /* Lese-Resultate exakt. */
        CHECK(holen(w, 0, 0, 0) == 1, "compaction: (0,0,0)=1");
        CHECK(holen(w, 4, 0, 0) == 1, "compaction: (4,0,0)=1");
        CHECK(holen(w, 5, 0, 0) == 0, "compaction: (5,0,0)=Luft");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 4. Chunk komplett leer editiert -> NULL-Kollaps ---- */
    printf("test_downgrade_chunk_null_collapse\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Mehrere Sections eines Chunks fuellen, dann alles abbauen. */
        setzen(w, 1, 1, 1, 3);
        setzen(w, 9, 9, 9, 4);    /* andere Section */
        setzen(w, 20, 20, 20, 5); /* dritte Section */
        CHECK(stat(w, "bytes_sections") > 0, "vor Abbau: Section-Header da");
        setzen(w, 1, 1, 1, 0);
        setzen(w, 9, 9, 9, 0);
        setzen(w, 20, 20, 20, 0);
        optimieren(w);
        CHECK(stat(w, "bytes_sections") == 0, "Chunk leer -> Section-Array kollabiert auf NULL");
        CHECK(stat(w, "bytes_total") > 0, "World/Hashmap-Overhead bleibt (kein Crash)");
        CHECK(holen(w, 1, 1, 1) == 0, "leerer Chunk liest Luft");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 5. Lese-Neutralitaet ueber gemischte Welt ---- */
    printf("test_downgrade_read_neutral\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Deterministisches gemischtes Muster ueber mehrere Chunks. */
        unsigned st = 0x1234u;
        int coords[400][3];
        int vals[400];
        int nset = 0;
        for (int i = 0; i < 400; i++) {
            st = st * 1103515245u + 12345u; int x = (int)((st >> 8) % 80u);
            st = st * 1103515245u + 12345u; int y = (int)((st >> 8) % 80u);
            st = st * 1103515245u + 12345u; int z = (int)((st >> 8) % 40u);
            st = st * 1103515245u + 12345u; int id = (int)((st >> 8) % 6u);
            setzen(w, x, y, z, id);
            coords[nset][0] = x; coords[nset][1] = y; coords[nset][2] = z;
            vals[nset] = id; nset++;
        }
        /* Snapshot der aktuellen Lese-Resultate (letzter Schreiber gewinnt). */
        optimieren(w);
        int ok = 1;
        /* Letzten geschriebenen Wert je Koordinate rekonstruieren. */
        for (int i = 0; i < nset; i++) {
            int last = vals[i];
            for (int j = i + 1; j < nset; j++)
                if (coords[j][0] == coords[i][0] && coords[j][1] == coords[i][1] &&
                    coords[j][2] == coords[i][2]) last = vals[j];
            if ((int)holen(w, coords[i][0], coords[i][1], coords[i][2]) != last) ok = 0;
        }
        CHECK(ok, "optimieren aendert kein Lese-Resultat");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 6. Idempotenz ---- */
    printf("test_downgrade_idempotent\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        fill_section(w, 3);
        optimieren(w);
        double b1 = stat(w, "bytes_blocks");
        double s1 = stat(w, "bytes_sections");
        optimieren(w);   /* zweiter Lauf: nichts mehr zu tun */
        CHECK(stat(w, "bytes_blocks") == b1,   "idempotent: Block-Bytes stabil");
        CHECK(stat(w, "bytes_sections") == s1, "idempotent: Section-Bytes stabil");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    /* ---- 7. RAM-Gewinn deep-artig: voller Stein-Chunk via setzen ---- */
    printf("test_downgrade_deep_full_chunk\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(0));
        /* Voller 32^3 Chunk uniform Stein -> 64x PALETTE{0,3} (1-bit). */
        for (int z = 0; z < 32; z++)
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++)
                    setzen(w, x, y, z, 3);
        double blocks_before = stat(w, "bytes_blocks");
        CHECK(blocks_before > 0, "vor Downgrade: PALETTE-Indexbytes > 0 (1-bit)");
        optimieren(w);
        /* Alle 64 Sections uniform 3 -> SOLID -> 0 Index-Bytes. Das ist der
         * R3-Kern, der deep/uniform-setzen ueber >95% bringt. */
        CHECK(stat(w, "bytes_blocks") == 0,  "nach Downgrade: 0 Index-Bytes (alle SOLID)");
        CHECK(stat(w, "bytes_palette") == 0, "nach Downgrade: 0 Palette-Bytes (alle SOLID)");
        int ok = 1;
        for (int z = 0; z < 32; z += 7)
            for (int y = 0; y < 32; y += 7)
                for (int x = 0; x < 32; x += 7)
                    if ((int)holen(w, x, y, z) != 3) ok = 0;
        CHECK(ok, "deep: alle Stichproben weiterhin 3");
        moo_voxel_free((void*)moo_val_as_ptr(w));
    }

    printf("\n==== %d Checks, %d Fails ====\n", g_checks, g_fails);
    if (g_threw) { printf("WARNUNG: unerwarteter Wurf irgendwo\n"); }
    if (g_fails == 0) { printf("ALLE TESTS BESTANDEN\n"); return 0; }
    return 1;
}
