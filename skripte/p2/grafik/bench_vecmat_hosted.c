/* P2-T16 (Plan 3f03704a D4, Review #38135): C-Basis zum Hosted-Benchmark
 * vecmat_hosted_bench.moos - identische ganzzahlige Eingabe, dieselbe Formel
 * wie c_viele in test_vecmat_nativ.c (-O2 -ffp-contract=off), Median aus 7.
 * Ausgabe: "C_MS <median>" und "PRUEFSUMME <summe>". */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double jetzt(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static int kleiner(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
static void c_viele(float *erg, const float *m, const float *v, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i)
        for (int row = 0; row < 4; ++row)
            erg[i*4 + row] = m[row]*v[i*4] + m[4+row]*v[i*4+1] + m[8+row]*v[i*4+2] + m[12+row]*v[i*4+3];
}

int main(void) {
    const uint64_t n = 1000000;
    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = (float)(i + 1); /* Spalten (1,2,3,4) ... */
    float *v = malloc(n * 4 * sizeof *v), *e = malloc(n * 4 * sizeof *e);
    if (!v || !e) { fprintf(stderr, "kein Speicher\n"); return 2; }
    for (uint64_t i = 0; i < n; ++i) {
        v[i*4] = (float)(i % 7); v[i*4+1] = (float)(i % 5); v[i*4+2] = (float)(i % 3); v[i*4+3] = 1.0f;
    }
    double z[7];
    for (int l = 0; l < 7; ++l) {
        double t0 = jetzt(); c_viele(e, m, v, n); z[l] = (jetzt() - t0) * 1e3;
    }
    qsort(z, 7, sizeof *z, kleiner);
    double s = 0;
    for (uint64_t i = 0; i < n; ++i)
        s = s + (double)e[i*4] + (double)e[i*4+1] + (double)e[i*4+2] + (double)e[i*4+3];
    printf("C_MS %.6f\nPRUEFSUMME %.1f\n", z[3], s);
    free(v); free(e);
    return 0;
}
