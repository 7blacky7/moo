#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Separate shared library: C ABI cost, not a dynamic/native Moo transition. */
extern double moo_g5_ffi_step(double);
static double moo_g5_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { perror("clock_gettime"); exit(2); }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    const size_t n = 10000000;
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "memory")) {
        double start = moo_g5_now();
        double *v = malloc(n * sizeof(*v));
        if (!v) return 3;
        for (size_t i = 0; i < n; ++i) v[i] = (double)(i % 100);
        double mid = moo_g5_now(), sum = 0;
        for (size_t i = 0; i < n; ++i) sum += v[i];
        double end = moo_g5_now();
        printf("%.9f\n%.9f\n%.0f\n", mid-start, end-mid, sum);
        free(v);
        return sum == 495000000.0 ? 0 : 4;
    }
    if (!strcmp(argv[1], "ffi_c")) {
        double sum = 0, start = moo_g5_now();
        for (size_t i = 0; i < n; ++i) sum += moo_g5_ffi_step((double)(i % 100));
        double end = moo_g5_now();
        printf("%.9f\n%.0f\n", end-start, sum);
        return sum == 505000000.0 ? 0 : 4;
    }
    return 2;
}
