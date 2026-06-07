/**
 * bench_voxel_downgrade.c - Plan-006 P006-R3: RAM-Wirkung des Section-Downgrade
 * (moo_voxel_welt_optimieren) auf die setzen-gebauten Kategorien deep + mutation
 * (Agent p006-r3).
 *
 * HINTERGRUND (R2-Befund, Thought e42beb58): der Einzel-Write-Pfad
 * (voxel_setzen) erzeugt fuer uniform gefuellte Sections PALETTE{0,id} mit
 * 1-bit Indizes (nie direkt SOLID) -> die deep-Kategorie blieb bei 92.97%
 * statt >95%. R3 holt das per LAZY Main-Thread-Downgrade (PALETTE->SOLID/EMPTY)
 * bzw. ueber das explizite welt_optimieren zurueck. Dieser Bench misst beide
 * setzen-getriebenen Kategorien VORHER (wie R2) und NACHHER (nach optimieren).
 *
 * Reuse statt Duplikat: Host-Stubs aus bench_voxel_harness.h (PERF1-Infra),
 * Welt-Builder identisch zu bench_voxel_ram.c (R0/R2) damit die Zahlen direkt
 * vergleichbar sind. Dieser Bench liegt im R3-Revier und fasst weder
 * moo_voxel.c noch bench_voxel_ram.c (R0/R2-Revier) an.
 *
 * Build (NUR moo_voxel.c + moo_noise.c, headless):
 *   gcc -O2 -g -std=c11 -I.. bench_voxel_downgrade.c ../moo_voxel.c ../moo_noise.c \
 *       -lm -lpthread -o /tmp/bench_voxel_downgrade
 *   /tmp/bench_voxel_downgrade
 * ASan/UBSan:
 *   gcc -O1 -g -std=c11 -fsanitize=address,undefined -I.. bench_voxel_downgrade.c \
 *       ../moo_voxel.c ../moo_noise.c -lm -lpthread -o /tmp/bench_voxel_downgrade_asan
 */
#define BENCH_HARNESS_IMPL
#include "bench_voxel_harness.h"

#define DIM 32
#define CHUNK_VOL (DIM*DIM*DIM)
#define NAIVE_CHUNK_BYTES ((long)CHUNK_VOL * (long)sizeof(uint16_t)) /* 64 KiB */

extern MooValue moo_voxel_welt_optimieren(MooValue welt);

typedef struct {
    long nonempty;
    long naive_blocks;
    long palette_blocks;   /* bytes_blocks + bytes_palette */
    long bytes_total;
    double saving_blocks;
    double saving_total;
} RamResult;

static RamResult measure(MooValue w) {
    dict_reset();
    (void)moo_voxel_ram_statistik(w);
    RamResult r; memset(&r, 0, sizeof r);
    long chunks  = (long)dict_get("chunks");
    long empty   = (long)dict_get("empty_chunks");
    r.nonempty   = chunks - empty;
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

static void print_pair(const char* name, RamResult before, RamResult after, const char* goal) {
    printf("  %s\n", name);
    printf("    VORHER : Block+Pal %ld B  Ersparnis %.2f%% (inkl.Ovh %.2f%%)\n",
           before.palette_blocks, before.saving_blocks, before.saving_total);
    printf("    NACHHER: Block+Pal %ld B  Ersparnis %.2f%% (inkl.Ovh %.2f%%)  %s\n",
           after.palette_blocks, after.saving_blocks, after.saving_total, goal);
}

/* ---- Welt-Builder (identisch zu bench_voxel_ram.c) ---- */
static MooValue build_surface(int side) {
    MooValue w = moo_voxel_welt_neu(N(1337));
    for (int cz = 0; cz < side; cz++)
        for (int cx = 0; cx < side; cx++)
            moo_voxel_generieren(w, N(cx), N(cz));
    return w;
}
static MooValue build_deep(int n_chunks) {
    MooValue w = moo_voxel_welt_neu(N(7));
    for (int c = 0; c < n_chunks; c++) {
        int bx = c * DIM;
        for (int lx = 0; lx < DIM; lx++)
            for (int ly = 0; ly < DIM; ly++)
                for (int lz = 0; lz < DIM; lz++)
                    moo_voxel_setzen(w, N(bx+lx), N(ly), N(lz), N(3));
    }
    return w;
}
static MooValue build_air_water(int n_chunks) {
    MooValue w = moo_voxel_welt_neu(N(11));
    for (int c = 0; c < n_chunks; c++) {
        int bx = c * DIM;
        for (int lx = 0; lx < DIM; lx++)
            for (int ly = 0; ly < DIM; ly++)
                for (int lz = 0; lz < DIM/2; lz++)
                    moo_voxel_setzen(w, N(bx+lx), N(ly), N(lz), N(5));
    }
    return w;
}
static MooValue build_mutation(int side, long edits) {
    MooValue w = build_surface(side);
    unsigned st = 0xBEEFu;
    int span = side * DIM;
    for (long e = 0; e < edits; e++) {
        st = st * 1103515245u + 12345u; int x = (int)((st >> 8) % (unsigned)span);
        st = st * 1103515245u + 12345u; int y = (int)((st >> 8) % (unsigned)span);
        st = st * 1103515245u + 12345u; int z = (int)((st >> 8) % 48u);
        st = st * 1103515245u + 12345u; int id = (int)((st >> 8) % 6u);
        moo_voxel_setzen(w, N(x), N(y), N(z), N(id));
    }
    return w;
}

int main(void) {
    printf("== Voxel-RAM Downgrade-Wirkung (Plan-006 R3, welt_optimieren) ==\n");
    printf("Naiv-Modell: nonempty * 32^3 * 2 B = %ld B/Chunk (64 KiB)\n", NAIVE_CHUNK_BYTES);
    int rc = 0;

    /* 1) DEEP (setzen-gebaut) — Kernziel von R3: >95% nach Downgrade. */
    {
        MooValue w = build_deep(64);
        RamResult before = measure(w);
        double changed = moo_as_number(moo_voxel_welt_optimieren(w));
        RamResult after = measure(w);
        int ok = (after.saving_blocks > 95.0);
        printf("\n[1] deep (uniformer Stein, 64 Chunks via setzen)  geaenderte Chunks=%.0f\n", changed);
        print_pair("deep", before, after,
                   ok ? "[R3-Ziel deep >95% ERREICHT]" : "[R3-Ziel deep >95% VERFEHLT]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* 2) AIR_WATER (setzen-gebaut) — sollte ebenfalls auf SOLID -> >95%. */
    {
        MooValue w = build_air_water(64);
        RamResult before = measure(w);
        double changed = moo_as_number(moo_voxel_welt_optimieren(w));
        RamResult after = measure(w);
        int ok = (after.saving_blocks > 95.0);
        printf("\n[2] air_water (Wasser unten / Luft oben, 64 Chunks)  geaenderte Chunks=%.0f\n", changed);
        print_pair("air_water", before, after,
                   ok ? "[>95% ERREICHT (SOLID-Downgrade)]" : "[<95% -> pruefen]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    /* 3) MUTATION — darf nach Downgrade nicht UNTER 80% fallen (Pflicht), soll
     *    eher steigen (uniform gewordene Sections kollabieren auf SOLID/EMPTY). */
    {
        MooValue w = build_mutation(16, 200000);
        RamResult before = measure(w);
        double changed = moo_as_number(moo_voxel_welt_optimieren(w));
        RamResult after = measure(w);
        int ok = (after.saving_blocks >= 80.0);
        printf("\n[3] mutation (Worldgen 16x16 + 200k Edits)  geaenderte Chunks=%.0f\n", changed);
        print_pair("mutation", before, after,
                   ok ? "[R3-Pflicht mutation >=80% gehalten]" : "[mutation < 80% -> FAIL]");
        if (!ok) rc = 1;
        moo_voxel_free((void*)(uintptr_t)w.data);
    }

    long peak = peak_rss_kb();
    printf("\nPeak-RSS (VmHWM): %ld KB (%.2f MiB)\n", peak, peak/1024.0);
    printf("\n== R3 Downgrade-Messlauf fertig (rc=%d) ==\n", rc);
    return rc;
}
