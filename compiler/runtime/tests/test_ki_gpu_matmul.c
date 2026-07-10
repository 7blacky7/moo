/**
 * test_ki_gpu_matmul.c — KIP-G2 Gate: Tiled Matmul-Shader.
 *
 *  (1) DIFFERENTIAL ueber eine SHAPE-MATRIX (inkl. nicht durch 16 teilbarer
 *      Dims) — tiled GPU-matmul == CPU-Referenz (bit-Toleranz).
 *  (2) MIKROBENCHMARK alt (naiv) vs neu (tiled) auf residenten Buffers bei
 *      mehreren Groessen: ms/Op + GFLOP/s + Speedup (GPU3-C-Muster).
 *
 * Transparenter SKIP ohne GPU. OHNE ASan bauen (Treiber-Leak-Noise).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include "moo_ki_gpu_api.h"

static int fehler = 0;
#define CHECK(bed, msg) do { \
    if (bed) { printf("  OK   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fehler++; } \
} while (0)

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void cpu_mm(const float* a, const float* b, float* o, int M, int K, int N) {
    for (int r = 0; r < M; r++)
        for (int c = 0; c < N; c++) {
            float s = 0.0f;
            for (int i = 0; i < K; i++) s += a[r * K + i] * b[i * N + c];
            o[r * N + c] = s;
        }
}

/* Ein Shape durch die residente tiled-matmul jagen + gegen CPU pruefen. */
static bool eine_shape(int M, int K, int N) {
    size_t na = (size_t)M * K, nb = (size_t)K * N, no = (size_t)M * N;
    float* A = malloc(na * 4); float* B = malloc(nb * 4);
    float* Oc = malloc(no * 4); float* Og = malloc(no * 4);
    if (!A || !B || !Oc || !Og) { free(A); free(B); free(Oc); free(Og); return false; }
    for (size_t i = 0; i < na; i++) A[i] = (float)((int)(i % 13) - 6) / 6.0f;
    for (size_t i = 0; i < nb; i++) B[i] = (float)((int)(i % 11) - 5) / 5.0f;
    cpu_mm(A, B, Oc, M, K, N);

    void* hA = moo_ki_gpu_buf_belegen((int64_t)na * 4);
    void* hB = moo_ki_gpu_buf_belegen((int64_t)nb * 4);
    void* hO = moo_ki_gpu_buf_belegen((int64_t)no * 4);
    bool ok = hA && hB && hO
           && moo_ki_gpu_upload(hA, A, (int64_t)na * 4)
           && moo_ki_gpu_upload(hB, B, (int64_t)nb * 4)
           && moo_ki_gpu_matmul_res(hA, hB, hO, M, K, N)
           && moo_ki_gpu_download(hO, Og, (int64_t)no * 4);
    if (ok) {
        for (size_t i = 0; i < no; i++)
            if (fabsf(Og[i] - Oc[i]) > 1e-3f + 1e-4f * fabsf(Oc[i])) { ok = false; break; }
    }
    moo_ki_gpu_buf_freigeben(hA); moo_ki_gpu_buf_freigeben(hB); moo_ki_gpu_buf_freigeben(hO);
    free(A); free(B); free(Oc); free(Og);
    return ok;
}

static void bench(int D, int reps) {
    size_t n = (size_t)D * D;
    float* A = malloc(n * 4); float* B = malloc(n * 4);
    for (size_t i = 0; i < n; i++) { A[i] = (float)((int)(i % 13) - 6) / 6.0f;
                                     B[i] = (float)((int)(i % 11) - 5) / 5.0f; }
    void* hA = moo_ki_gpu_buf_belegen((int64_t)n * 4);
    void* hB = moo_ki_gpu_buf_belegen((int64_t)n * 4);
    void* hO = moo_ki_gpu_buf_belegen((int64_t)n * 4);
    if (!hA || !hB || !hO ||
        !moo_ki_gpu_upload(hA, A, (int64_t)n * 4) ||
        !moo_ki_gpu_upload(hB, B, (int64_t)n * 4)) {
        printf("  bench %d: SKIP (Alloc/Upload)\n", D);
        goto weg;
    }
    /* warmup */
    moo_ki_gpu_matmul_naiv_res(hA, hB, hO, D, D, D);
    moo_ki_gpu_matmul_res(hA, hB, hO, D, D, D);

    double t0 = now_s();
    for (int r = 0; r < reps; r++) moo_ki_gpu_matmul_naiv_res(hA, hB, hO, D, D, D);
    double t_naiv = (now_s() - t0) / reps;

    t0 = now_s();
    for (int r = 0; r < reps; r++) moo_ki_gpu_matmul_res(hA, hB, hO, D, D, D);
    double t_tiled = (now_s() - t0) / reps;

    double flops = 2.0 * D * D * D;
    printf("  bench %4dx%4dx%4d: naiv %7.3f ms (%6.1f GFLOP/s) | tiled %7.3f ms (%6.1f GFLOP/s) | Speedup %.2fx\n",
           D, D, D, t_naiv * 1e3, flops / t_naiv / 1e9,
           t_tiled * 1e3, flops / t_tiled / 1e9, t_naiv / t_tiled);
weg:
    moo_ki_gpu_buf_freigeben(hA); moo_ki_gpu_buf_freigeben(hB); moo_ki_gpu_buf_freigeben(hO);
    free(A); free(B);
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz zur Laufzeit\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G2 Tiled Matmul ==\n");

    /* Shape-Matrix: exakte Tiles, nicht-teilbare Dims, K=1, Vektoren, entartete Faelle */
    int shapes[][3] = {
        {16,16,16}, {32,32,32}, {17,33,15}, {31,31,31}, {1,1,1}, {1,64,1},
        {64,1,64}, {48,16,32}, {100,7,3}, {3,100,7}, {7,3,100}, {33,17,49},
        {129,65,17}, {16,1,16}, {15,15,15},
    };
    int ns = (int)(sizeof(shapes) / sizeof(shapes[0]));
    int diff_fehler = 0;
    for (int i = 0; i < ns; i++) {
        int M = shapes[i][0], K = shapes[i][1], N = shapes[i][2];
        bool ok = eine_shape(M, K, N);
        char msg[64];
        snprintf(msg, sizeof(msg), "matmul %dx%dx%d == CPU", M, K, N);
        CHECK(ok, msg);
        if (!ok) diff_fehler++;
    }
    CHECK(diff_fehler == 0, "Shape-Matrix vollstaendig bit-Toleranz-gleich");

    printf("\n-- Mikrobenchmark alt(naiv) vs neu(tiled), resident --\n");
    bench(256, 50);
    bench(512, 30);
    bench(1024, 10);

    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
