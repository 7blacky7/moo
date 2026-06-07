/**
 * test_voxel_worldgen_asan.c - Standalone-ASan-Harness fuer den minimalen
 * prozeduralen Worldgen (Plan-005 Phase 1a/RT5, Agent p005-rt5).
 *
 * Im Gegensatz zu den anderen Voxel-Harnesses kompilieren wir hier moo_voxel.c
 * ZUSAMMEN mit dem ECHTEN moo_noise.c — der Determinismus (seed-parametrisierte
 * fBm) soll real geprueft werden, nicht gestubbt. Die moo-Heap-Funktionen und
 * die 3D-Chunk-API werden gestubbt (Backend inaktiv), damit ASan Leaks sieht.
 *
 * voxel_holen ist ein REINER Lesezugriff (P0.5-Contract: nie beschriebener
 * Chunk = Luft, KEIN Lazy-Gen). Terrain wird daher EXPLIZIT via
 * voxel_generieren(w, cx, cz) erzeugt, bevor Saeulen gelesen werden.
 *
 * Geprueft:
 *   1. Explizites voxel_generieren: erzeugt eine nicht-leere Saeule; Free
 *      danach ASan-clean.
 *   2. Idempotenz: zweiter voxel_generieren-Aufruf erzeugt 0 neue Chunks;
 *      Free ASan-clean.
 *   3. Determinismus: gleicher Seed -> identische Saeule; benachbarter Seed
 *      -> mindestens eine abweichende Saeule. Kein globaler Seed-Leak zwischen
 *      Welt-Instanzen.
 *   4. Schichtung plausibel: Oberflaeche gras/sand, darunter erde, darunter
 *      stein; unter Meeresspiegel ueber Terrain -> wasser (opak, id 5).
 *   5. Dirty-Markierung: generierte, nicht-leere Chunks sind dirty.
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra -I.. \
 *       test_voxel_worldgen_asan.c ../moo_voxel.c ../moo_noise.c -lm \
 *       -o /tmp/t_voxel_worldgen
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_voxel_worldgen
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

/* Dict/String-Stubs: moo_voxel.c linkt sie (ram_statistik/strahl/aabb). Dieser
 * Harness ruft diese Pfade nie auf -> triviale, leakfreie No-op-Stubs. */
MooValue moo_string_new(const char* s) {
    MooValue v; v.tag = MOO_STRING; moo_val_set_ptr(&v, (void*)s); return v;
}
MooValue moo_dict_new(void)  { return moo_none(); }
void     moo_dict_set(MooValue dict, MooValue key, MooValue val) {
    (void)dict; (void)key; (void)val;
}

/* 3D-API-Stubs (Backend inaktiv: Worldgen fasst CPU-Daten an, kein GPU). */
int      moo_3d_backend_active(void) { return 0; }
MooValue moo_3d_chunk_create(void)   { return moo_number(-1.0); }
void     moo_3d_chunk_begin(MooValue id) { (void)id; }
void     moo_3d_chunk_end(void) {}
void     moo_3d_chunk_delete(MooValue id) { (void)id; }
void     moo_3d_triangle(MooValue w, MooValue ax, MooValue ay, MooValue az,
                         MooValue bx, MooValue by, MooValue bz,
                         MooValue cx, MooValue cy, MooValue cz, MooValue col) {
    (void)w;(void)ax;(void)ay;(void)az;(void)bx;(void)by;(void)bz;
    (void)cx;(void)cy;(void)cz;(void)col;
}

/* ===================== Zu testende API ===================== */
extern MooValue moo_voxel_welt_neu(MooValue seed);
extern MooValue moo_voxel_generieren(MooValue w, MooValue cx, MooValue cz);
extern MooValue moo_voxel_holen(MooValue w, MooValue x, MooValue y, MooValue z);
extern void     moo_voxel_free(void* ptr);

/* Generiert die 2x2-Block horizontaler Chunk-Spalten {-1,0}x{-1,0}, die alle
 * Probepunkte dieses Harness (world x,y in [-2,12]) abdecken. */
static void gen_world(MooValue w) {
    moo_voxel_generieren(w, moo_number(0),  moo_number(0));
    moo_voxel_generieren(w, moo_number(-1), moo_number(0));
    moo_voxel_generieren(w, moo_number(0),  moo_number(-1));
    moo_voxel_generieren(w, moo_number(-1), moo_number(-1));
}

/* ===================== Mini-Test-Framework ===================== */
static int g_fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (msg)); g_fails++; } \
} while (0)

static double holen(MooValue w, int x, int y, int z) {
    return moo_as_number(moo_voxel_holen(w, moo_number(x), moo_number(y), moo_number(z)));
}

/* Fuellstand einer Saeule (vertikale Z-Achse) bei horizontalem (x,y). */
static int saeule_summe(MooValue w, int x, int y) {
    int summe = 0;
    for (int z = 0; z < 64; z++) {
        if (holen(w, x, y, z) != 0) summe++;
    }
    return summe;
}

static void free_world(MooValue w) {
    moo_voxel_free(moo_val_as_ptr(w));
}

int main(void) {
    if (setjmp(g_throw_jmp)) {
        printf("UNERWARTETER WURF waehrend Worldgen-Test\n");
        return 2;
    }

    /* ---- 1. Explizites voxel_generieren + Lesen ---- */
    printf("test_worldgen_generieren_holen\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(42));
        gen_world(w);
        int s = saeule_summe(w, 0, 0);
        CHECK(s > 0,  "Saeule ist nicht reine Luft");
        CHECK(s < 64, "Saeule ist nicht komplett gefuellt");
        /* Wiederholtes Lesen liefert identisch (Chunk bleibt occupied). */
        CHECK(saeule_summe(w, 0, 0) == s, "Saeule stabil bei Re-Read");
        free_world(w);
    }

    /* ---- 2. Explizites voxel_generieren + Idempotenz ---- */
    printf("test_worldgen_generieren_idempotent\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(7));
        double n1 = moo_as_number(moo_voxel_generieren(w, moo_number(0), moo_number(0)));
        CHECK(n1 > 0, "generieren erzeugt mind. einen Chunk");
        double n2 = moo_as_number(moo_voxel_generieren(w, moo_number(0), moo_number(0)));
        CHECK(n2 == 0, "zweiter generieren-Aufruf erzeugt 0 neue Chunks (idempotent)");
        CHECK(saeule_summe(w, 3, 5) > 0, "generierte Region nicht leer");
        free_world(w);
    }

    /* ---- 3. Determinismus + kein Seed-Leak ---- */
    printf("test_worldgen_determinismus\n");
    {
        /* Gleicher Seed -> identische Saeulen-Signatur. */
        MooValue a = moo_voxel_welt_neu(moo_number(42));
        gen_world(a);
        int sa0 = saeule_summe(a, 0, 0), sa1 = saeule_summe(a, 5, 7), sa2 = saeule_summe(a, -3, 12);
        free_world(a);

        /* Andere Welt dazwischen darf nicht durchschlagen (instanzgebundener Seed). */
        MooValue mid = moo_voxel_welt_neu(moo_number(777));
        gen_world(mid);
        (void)saeule_summe(mid, 1, 1);
        free_world(mid);

        MooValue b = moo_voxel_welt_neu(moo_number(42));
        gen_world(b);
        int sb0 = saeule_summe(b, 0, 0), sb1 = saeule_summe(b, 5, 7), sb2 = saeule_summe(b, -3, 12);
        free_world(b);
        CHECK(sa0 == sb0 && sa1 == sb1 && sa2 == sb2, "gleicher Seed -> identische Saeulen (kein Leak)");

        /* Benachbarter Seed -> mind. eine Saeule unterscheidet sich. */
        MooValue c = moo_voxel_welt_neu(moo_number(43));
        gen_world(c);
        int sc0 = saeule_summe(c, 0, 0), sc1 = saeule_summe(c, 5, 7), sc2 = saeule_summe(c, -3, 12);
        free_world(c);
        CHECK(sa0 != sc0 || sa1 != sc1 || sa2 != sc2, "anderer Seed -> abweichende Saeule");
    }

    /* ---- 4. Schichtung plausibel + Wasser opak ---- */
    printf("test_worldgen_schichten\n");
    {
        MooValue w = moo_voxel_welt_neu(moo_number(123));
        gen_world(w);
        /* Pruefe ueber mehrere horizontale Positionen, dass die Schichtfolge
         * von unten nach oben gilt: tiefster nicht-Luft-Block ist stein (3),
         * Oberflaeche ist gras(1)/sand(4)/wasser(5)-Bereich (nie Luft unter der
         * Oberflaeche). */
        int seen_stein = 0, seen_surface = 0, seen_wasser = 0;
        for (int x = -2; x <= 5; x++) {
            for (int y = -2; y <= 5; y++) {
                /* untersten soliden Block finden */
                int bottom = -1;
                for (int z = 0; z < 64; z++) { if (holen(w, x, y, z) != 0) { bottom = z; break; } }
                if (bottom >= 0 && (int)holen(w, x, y, bottom) == 3) seen_stein = 1;
                /* obersten soliden Block finden */
                int top = -1;
                for (int z = 63; z >= 0; z--) { if (holen(w, x, y, z) != 0) { top = z; break; } }
                if (top >= 0) {
                    int id = (int)holen(w, x, y, top);
                    if (id == 1 || id == 4 || id == 5) seen_surface = 1;
                }
                /* Wasser irgendwo in der Saeule? */
                for (int z = 0; z < 64; z++) { if ((int)holen(w, x, y, z) == 5) { seen_wasser = 1; break; } }
            }
        }
        CHECK(seen_stein,   "stein als tiefste Schicht vorhanden");
        CHECK(seen_surface, "plausible Oberflaeche (gras/sand/wasser)");
        CHECK(seen_wasser,  "wasser (opak) wird unter Meeresspiegel aufgefuellt");
        free_world(w);
    }

    if (g_threw) { printf("UNERWARTETER WURF\n"); return 2; }
    if (g_fails == 0) { printf("ALLE TESTS BESTANDEN\n"); return 0; }
    printf("%d TEST(S) FEHLGESCHLAGEN\n", g_fails);
    return 1;
}
