/* P2-T15/T50 C4 (Plan bb3878ef): C-Referenz zu native_puffer_bench.moos.
 * 10^7 f32 fuellen (i % 1024) und in f64 summieren; Speicher ausserhalb des
 * Messintervalls angelegt. Ausgabe: Sekunden, Pruefsumme (Soll 5114877120). */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
__attribute__((noinline)) static double fuellen_summe(float *a, long n) {
    for (long i = 0; i < n; ++i) a[i] = (float)(i % 1024);
    double s = 0.0;
    for (long i = 0; i < n; ++i) s += (double)a[i];
    return s;
}
static double now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) exit(2);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *end = NULL;
    long n = strtol(argv[1], &end, 10);
    if (!end || *end || n != 10000000L) return 2;
    float *a = calloc((size_t)n, sizeof *a);
    if (!a) return 3;
    double start = now();
    double s = fuellen_summe(a, n);
    double stop = now();
    free(a);
    printf("%.9f\n%.0f\n", stop - start, s);
    return s == 5114877120.0 ? 0 : 3;
}
