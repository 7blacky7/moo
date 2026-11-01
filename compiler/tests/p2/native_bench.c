#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
__attribute__((noinline)) static int64_t summe(int64_t n) {
    int64_t s = 0;
    for (int64_t i=0; i<n; ++i) s += i;
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
    double start = now();
    int64_t s = summe((int64_t)n);
    double stop = now();
    printf("%.9f\n%lld\n",stop-start,(long long)s);
    return s == 49999995000000LL ? 0 : 3;
}
