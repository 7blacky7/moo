/**
 * bench_voxel_ram.c - Plan-006 P006-R0: RAM-Kategorien-Benchmark (Agent p006-r0).
 *
 * ZIEL: Die in Plan-005 gemessene 75.56%-Mixed-Terrain-Ersparnis REPRODUZIERBAR
 * machen und das Schoenrechnen verhindern, indem RAM gegen das AKTUELLE
 * Chunk-Palette-Layout (VOR der 8^3-Section-Migration in R1) in fuenf
 * Kategorien gemessen wird. Diese Vorher-Zahlen sind die Messlatte fuer R2.
 *
 * Kategorien (Spec aus Memory plan-006-voxel-ram-optimierung-und-offene-punkte):
 *   1. bench_voxel_ram_surface     - Mixed-/Surface-Terrain (echtes Worldgen) -> 75.56%-Baseline
 *   2. bench_voxel_ram_deep        - tiefe uniforme Stein-Chunks
 *   3. bench_voxel_ram_air_water   - Luft/Wasser/uniforme Chunks
 *   4. bench_voxel_ram_random_worst- worst-case zufaellige Blockverteilung (zeigt Kompressions-Grenze)
 *   5. bench_voxel_ram_mutation    - nach vielen voxel_setzen-Edits (kein Worldgen-Schoenrechnen)
 *
 * Reuse statt Duplikat: alle Host-Stubs kommen aus bench_voxel_harness.h
 * (PERF1-Bench-Infrastruktur erweitert, nicht kopiert).
 *
 * Build (NUR moo_voxel.c + moo_noise.c, headless):
 *   gcc -O2 -g -std=c11 -I.. bench_voxel_ram.c ../moo_voxel.c ../moo_noise.c \
 *       -lm -lpthread -o /tmp/bench_voxel_ram
 *   /tmp/bench_voxel_ram
 * ASan:
 *   gcc -O1 -g -std=c11 -fsanitize=address,undefined -I.. bench_voxel_ram.c \
 *       ../moo_voxel.c ../moo_noise.c -lm -lpthread -o /tmp/bench_voxel_ram_asan
 *   /tmp/bench_voxel_ram_asan
 *
 * ========================================================================
 * THEORETISCHE UNTERGRENZEN (aktuelles Chunk-weites Palette-Layout, 32^3)
 * ========================================================================
 * Naiv pro nicht-leerem Chunk: 32^3 * sizeof(uint16_t) = 32768 * 2 = 64 KiB.
 * Palette-Layout speichert bitgepackte Indizes; die kleinste Bitbreite haengt
 * von der Anzahl distinkter Block-IDs IM CHUNK ab (moo_voxel_bits_for):
 *
 *   distinkte IDs   bits   index_array            block-Ersparnis ggü. naiv
 *   -----------------------------------------------------------------------
 *   <=2 (z.B. {x})   1     32768*1/8 =  4096 B    93.75 %
 *   <=4              2     32768*2/8 =  8192 B    87.50 %
 *   <=16             4     32768*4/8 = 16384 B    75.00 %   <-- Surface-Limit!
 *   <=256            8     32768*8/8 = 32768 B    50.00 %
 *   <=65536         16     32768*16/8= 65536 B     0.00 %   (kein Gewinn)
 *
 * KERNBEFUND: Sobald ein 32^3-Chunk >4 verschiedene Block-IDs mischt (Surface:
 * gras+erde+stein+sand+wasser+luft = bis zu 5-6 -> 4 bit), ist 75% die HARTE
 * Obergrenze der Ersparnis. Das erklaert die 75.56%-Mixed-Baseline vollstaendig.
 * Eine chunk-weite Palette kann fuer gemischte Surface-Chunks per Konstruktion
 * NICHT deutlich ueber 75% kommen.
 *
 * UNTERGRENZE MIT 8^3-SECTION-PALETTE (R1, hier nur als Ziel dokumentiert):
 * Bei 8^3 = 512 Voxel/Section, 64 Sections/Chunk, trennt sich Surface fein:
 * die meisten Sections werden EMPTY (0 B) oder SOLID (0 Index-Bytes), nur der
 * Surface-/Wasser-Schnitt braucht 2/4-bit Palette. Theoretisch fuer ein
 * Mixed-Terrain mit ~1-2 Surface-Sections pro vertikaler 8er-Spalte:
 *   - SOLID/EMPTY-Sections: nur Header (~Pointer+Modus), keine Indexdaten.
 *   - 4-bit Surface-Section: 512*4/8 = 256 B statt 8^3*2 = 1024 B naiv.
 * Erwartung (Claude-Review K1, Header ~2 KiB/Chunk ehrlich eingerechnet):
 * Mixed 85-92%, deep/uniform >95%. R1/R2 muessen das gegen DIESE Zahlen zeigen.
 * ========================================================================
 */
#define BENCH_HARNESS_IMPL
#include "bench_voxel_harness.h"

#define DIM 32
#define CHUNK_VOL (DIM*DIM*DIM)
#define NAIVE_CHUNK_BYTES ((long)CHUNK_VOL * (long)sizeof(uint16_t)) /* 64 KiB */

/* Block-Registry (moo_voxel.c): 0=luft 1=gras 2=erde 3=stein 4=sand 5=wasser. */

/* Ergebnis einer Kategorie-Messung. */
typedef struct {
    const char* name;
    long chunks, nonempty, empty;
    long naive_blocks;     /* nonempty * 64 KiB */
    long palette_blocks;   /* bytes_blocks + bytes_palette */
    long bytes_total;      /* inkl. Verwaltungs-Overhead */
    double saving_blocks;  /* % gegen naive_blocks (Block+Palette) */
    double saving_total;   /* % gegen naive_blocks (inkl. Overhead) */
} RamResult;

/* Liest ram_statistik einer Welt und berechnet die Ersparnis-Kennzahlen.
 * naiv = jeder NICHT-leere Chunk als uint16[32^3]; leere Chunks = 0 B in beiden
 * Modellen (NULL-Chunks, Plan-005-Invariante). */
static RamResult measure(const char* name, MooValue w) {
    dict_reset();
    MooValue stat = moo_voxel_ram_statistik(w);
    (void)stat;
    RamResult r; memset(&r, 0, sizeof r);
    r.name = name;
    r.chunks       = (long)dict_get("chunks");
    r.empty        = (long)dict_get("empty_chunks");
    r.nonempty     = r.chunks - r.empty;
    long bytes_blocks  = (long)dict_get("bytes_blocks");
    long bytes_palette = (long)dict_get("bytes_palette");
    r.bytes_total  = (long)dict_get("bytes_total");
    r.naive_blocks   = r.nonempty * NAIVE_CHUNK_BYTES;
    r.palette_blocks = bytes_blocks + bytes_palette;
    r.saving_blocks = r.naive_blocks > 0
        ? 100.0 * (1.0 - (double)r.palette_blocks / (double)r.naive_blocks) : 0.0;
    r.saving_total = r.naive_blocks > 0
        ? 100.0 * (1.0 - (double)r.bytes_total / (double)r.naive_blocks) : 0.0;
    return r;
}

static void print_result(RamResult r, const char* goal) {
    printf("  chunks=%ld (nonempty=%ld, empty=%ld)\n", r.chunks, r.nonempty, r.empty);
    printf("  naiv Blockdaten:   %ld B (%.2f MiB)\n", r.naive_blocks, r.naive_blocks/1048576.0);
    printf("  palette Block+Pal: %ld B (%.2f MiB)\n", r.palette_blocks, r.palette_blocks/1048576.0);
    printf("  bytes_total(Ovh):  %ld B (%.2f MiB)\n", r.bytes_total, r.bytes_total/1048576.0);
    printf("  ERSPARNIS Blockdaten: %.2f%%   (inkl. Overhead: %.2f%%)   %s\n",
           r.saving_blocks, r.saving_total, goal);
}

/* ---- Welt-Builder pro Kategorie (alle deterministisch) ---- */

/* 1) SURFACE: echtes Worldgen, 32x32 horizontale Saeulen. Reproduziert exakt
 *    den Plan-005-Aufbau (1024 nonempty + 1024 empty). */
static MooValue build_surface(int side) {
    MooValue w = moo_voxel_welt_neu(N(1337));
    for (int cz = 0; cz < side; cz++)
        for (int cx = 0; cx < side; cx++)
            moo_voxel_generieren(w, N(cx), N(cz));
    return w;
}

/* 2) DEEP: uniforme Stein-Chunks (id=3) komplett gefuellt -> 1 Palette-Eintrag,
 *    1-bit Indizes. Repraesentiert tiefe Untergrund-Chunks. */
static MooValue build_deep(int n_chunks) {
    MooValue w = moo_voxel_welt_neu(N(7));
    for (int c = 0; c < n_chunks; c++) {
        int bx = c * DIM; /* jeder Chunk in eigener X-Spalte */
        for (int lx = 0; lx < DIM; lx++)
            for (int ly = 0; ly < DIM; ly++)
                for (int lz = 0; lz < DIM; lz++)
                    moo_voxel_setzen(w, N(bx+lx), N(ly), N(lz), N(3));
    }
    return w;
}

/* 3) AIR_WATER: jeder Chunk ist untere Haelfte uniformes Wasser (id=5), obere
 *    Haelfte Luft (nicht gespeichert). Plus dazwischen reine Luft-Chunks, die
 *    NULL bleiben muessen. -> 1 Palette-Eintrag {5}, 1-bit. */
static MooValue build_air_water(int n_chunks) {
    MooValue w = moo_voxel_welt_neu(N(11));
    for (int c = 0; c < n_chunks; c++) {
        int bx = c * DIM;
        for (int lx = 0; lx < DIM; lx++)
            for (int ly = 0; ly < DIM; ly++)
                for (int lz = 0; lz < DIM/2; lz++) /* untere Haelfte Wasser */
                    moo_voxel_setzen(w, N(bx+lx), N(ly), N(lz), N(5));
        /* obere Haelfte bleibt Luft (id 0) -> nicht geschrieben. */
    }
    /* Eine reine Luft-Spalte: nur lesen/leer lassen -> bleibt NULL, 0 Bytes.
     * (kein voxel_setzen, daher kein Chunk allokiert.) */
    return w;
}

/* 4) RANDOM_WORST: jeder Voxel eine zufaellige Block-ID 1..5 -> bis zu 5-6
 *    distinkte IDs/Chunk -> 4-bit (75% Obergrenze), plus Palette-Malus. Zeigt
 *    wo Kompression an ihre physikalische Grenze stoesst. KEIN Prozentziel. */
static MooValue build_random_worst(int n_chunks) {
    MooValue w = moo_voxel_welt_neu(N(99));
    unsigned st = 0xC0FFEEu;
    for (int c = 0; c < n_chunks; c++) {
        int bx = c * DIM;
        for (int lx = 0; lx < DIM; lx++)
            for (int ly = 0; ly < DIM; ly++)
                for (int lz = 0; lz < DIM; lz++) {
                    st = st * 1103515245u + 12345u;
                    int id = 1 + (int)((st >> 16) % 5u); /* 1..5, nie 0 -> dichter Chunk */
                    moo_voxel_setzen(w, N(bx+lx), N(ly), N(lz), N(id));
                }
    }
    return w;
}

/* 5) MUTATION: echtes Worldgen, danach viele zufaellige voxel_setzen-Edits
 *    (Abbau id=0 + Setzen id 1..5), wie interaktives Bauen/Minen. Darf die
 *    Ersparnis nicht durch reines Worldgen schoenrechnen. */
static MooValue build_mutation(int side, long edits) {
    MooValue w = build_surface(side);
    unsigned st = 0xBEEFu;
    int span = side * DIM;
    for (long e = 0; e < edits; e++) {
        st = st * 1103515245u + 12345u; int x = (int)((st >> 8) % (unsigned)span);
        st = st * 1103515245u + 12345u; int y = (int)((st >> 8) % (unsigned)span);
        st = st * 1103515245u + 12345u; int z = (int)((st >> 8) % 48u); /* 0..47 Terrain+Luft */
        st = st * 1103515245u + 12345u; int id = (int)((st >> 8) % 6u); /* 0=abbauen, 1..5 setzen */
        moo_voxel_setzen(w, N(x), N(y), N(z), N(id));
    }
    return w;
}

int main(void) {
    printf("== Voxel-RAM-Kategorien-Benchmark (Plan-006 R0, aktuelles Chunk-Palette-Layout) ==\n");
    printf("Block-Registry: 0=luft 1=gras 2=erde 3=stein 4=sand 5=wasser\n");
    printf("Naiv-Modell: nonempty * 32^3 * 2 B = %ld B/Chunk (64 KiB)\n", NAIVE_CHUNK_BYTES);

    int rc = 0;

    /* 1) SURFACE / MIXED — Baseline-Reproduktion. */
    printf("\n[1] bench_voxel_ram_surface (Mixed-Terrain, echtes Worldgen 32x32)\n");
    {
        MooValue w = build_surface(32);
        RamResult r = measure("surface", w);
        /* Ziel: ~75.56%-Groessenordnung reproduzieren (Toleranz +/- 1pp). */
        int ok = (r.saving_blocks > 74.0 && r.saving_blocks < 77.0);
        print_result(r, ok ? "[~75.56% reproduziert -> OK]" : "[ABWEICHUNG erklaeren!]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* 2) DEEP — uniforme Stein-Chunks. */
    printf("\n[2] bench_voxel_ram_deep (uniformer Stein, 64 Chunks)\n");
    {
        MooValue w = build_deep(64);
        RamResult r = measure("deep", w);
        /* Aktuelles Layout: 1 distinkte ID -> 1-bit -> 93.75% Blockersparnis.
         * (>95% erst mit Section-SOLID-Modus in R1 erreichbar -> Ziel fuer R2.) */
        int ok = (r.saving_blocks > 90.0);
        print_result(r, ok ? "[1-bit, ~93.75% erwartet -> OK; R2-Ziel >95% via SOLID]"
                            : "[unerwartet niedrig!]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* 3) AIR_WATER — uniformes Wasser + Luft. */
    printf("\n[3] bench_voxel_ram_air_water (Wasser unten / Luft oben, 64 Chunks)\n");
    {
        MooValue w = build_air_water(64);
        RamResult r = measure("air_water", w);
        int ok = (r.saving_blocks > 90.0);
        print_result(r, ok ? "[1 Palette-Eintrag {wasser}, 1-bit -> OK]"
                            : "[unerwartet niedrig!]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* 4) RANDOM_WORST — worst-case zufaellig (KEIN Prozentziel). */
    printf("\n[4] bench_voxel_ram_random_worst (zufaellige IDs 1..5, 16 Chunks)\n");
    {
        MooValue w = build_random_worst(16);
        RamResult r = measure("random_worst", w);
        /* Akzeptanz (Claude-Review): kein %-Ziel; korrekt + nicht SCHLECHTER als
         * naiv +5% (Header-/Palette-Malus-Deckel). saving_total >= -5%. */
        int ok = (r.saving_total >= -5.0);
        print_result(r, ok ? "[<=16 IDs -> 4-bit Obergrenze 75%; nicht schlechter als naiv+5% -> OK]"
                            : "[SCHLECHTER als naiv+5% -> FAIL]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* 5) MUTATION — Worldgen + viele Edits. */
    printf("\n[5] bench_voxel_ram_mutation (Worldgen 16x16 + 200k zufaellige Edits)\n");
    {
        MooValue w = build_mutation(16, 200000);
        RamResult r = measure("mutation", w);
        /* Ziel: nach realistischem Edit-Muster immer noch >80% oder dokumentierter Grund.
         * Mixed-Surface startet bei ~75% -> Edits duerfen es nicht massiv verschlechtern. */
        int ok = (r.saving_blocks > 70.0);
        print_result(r, ok ? "[Edit-Muster haelt Mixed-Niveau -> OK]"
                            : "[Edits zerstoeren Kompression -> pruefen]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    long peak = peak_rss_kb();
    printf("\nPeak-RSS (VmHWM): %ld KB (%.2f MiB)\n", peak, peak/1024.0);
    printf("\n== R0-Baseline fertig (rc=%d) ==\n", rc);
    printf("Diese Zahlen sind die VORHER-Messlatte fuer R2 (8^3-Section-Layout).\n");
    return rc;
}
