/* test_ki_gpu.c — GPU2-Smoke (Plan-014): Korrektheit + Speedup der
 * Vulkan-Ops gegen CPU-Referenzen. Bewusst OHNE ASan (NVIDIA-Treiber-
 * Allokationen melden Leak-Noise). Linkt NUR moo_ki_gpu.c + libvulkan.
 * Exit 0 + "SKIP" wenn zur Laufzeit keine GPU da ist — das Smoke-Script
 * behandelt SKIP transparent (Skip ist KEIN Beweis!). */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>

bool moo_ki_gpu_matmul(const float* a, const float* b, float* o,
                       int32_t m, int32_t k, int32_t n);
bool moo_ki_gpu_ew(int32_t op, const float* a, const float* b, float* o,
                   int64_t n);
bool moo_ki_gpu_reduce_sum(const float* a, int64_t n, double* out_summe);

static int checks = 0;
#define CHECK(bed, name) do { \
    if (bed) { checks++; } else { \
        fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); \
        return 1; } } while (0)

static double jetzt(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void cpu_matmul(const float* a, const float* b, float* o,
                       int64_t m, int64_t k, int64_t n) {
    memset(o, 0, (size_t)(m * n) * sizeof(float));
    for (int64_t i = 0; i < m; i++) {
        const float* ai = a + i * k;
        float* oi = o + i * n;
        for (int64_t kk = 0; kk < k; kk++) {
            float av = ai[kk];
            const float* bk = b + kk * n;
            for (int64_t j = 0; j < n; j++) oi[j] += av * bk[j];
        }
    }
}

static float zufall(uint64_t* s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return (float)((*s >> 33) % 2000) / 1000.0f - 1.0f;
}

int main(void) {
    setenv("MOO_KI_GPU_ERZWINGEN", "1", 1);
    uint64_t seed = 42;

    /* Laufzeit-Probe: winzige matmul — false => keine GPU => SKIP */
    {
        float a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 }, o[4] = { 0 };
        if (!moo_ki_gpu_matmul(a, b, o, 2, 2, 2)) {
            printf("SKIP: kein Vulkan-Compute zur Laufzeit verfuegbar\n");
            return 0;
        }
        CHECK(o[0] == 19.0f && o[1] == 22.0f && o[2] == 43.0f && o[3] == 50.0f,
              "matmul 2x2 handgerechnet ([19,22;43,50])");
    }

    /* 1. elementwise: 4 Ops, n=100000, gegen CPU-Loop (rel 1e-6) */
    {
        int64_t n = 100000;
        float* a = malloc((size_t)n * 4);
        float* b = malloc((size_t)n * 4);
        float* g = malloc((size_t)n * 4);
        for (int64_t i = 0; i < n; i++) {
            a[i] = zufall(&seed);
            b[i] = zufall(&seed);
            if (fabsf(b[i]) < 0.01f) b[i] = 0.5f;   /* div-sicher */
        }
        for (int op = 0; op < 4; op++) {
            CHECK(moo_ki_gpu_ew(op, a, b, g, n), "ew: GPU-Lauf ok");
            for (int64_t i = 0; i < n; i++) {
                float c = (op == 0) ? a[i] + b[i] : (op == 1) ? a[i] - b[i]
                        : (op == 2) ? a[i] * b[i] : a[i] / b[i];
                double rel = fabs((double)g[i] - (double)c) /
                             (fabs((double)c) + 1e-9);
                if (rel > 1e-6) {
                    fprintf(stderr, "FAIL: ew op=%d i=%lld gpu=%f cpu=%f\n",
                            op, (long long)i, g[i], c);
                    return 1;
                }
            }
            checks++;
        }
        free(a); free(b); free(g);
    }

    /* 2. reduce sum/mean: n=2^21, gegen double-CPU (rel 1e-6) */
    {
        int64_t n = (int64_t)1 << 21;
        float* a = malloc((size_t)n * 4);
        double ref = 0.0;
        for (int64_t i = 0; i < n; i++) { a[i] = zufall(&seed); ref += (double)a[i]; }
        double s = 0.0;
        CHECK(moo_ki_gpu_reduce_sum(a, n, &s), "reduce: GPU-Lauf ok");
        double rel = fabs(s - ref) / (fabs(ref) + 1e-9);
        CHECK(rel < 1e-6, "reduce sum == double-CPU (rel 1e-6)");
        free(a);
    }

    /* 3. matmul 1024^3: Korrektheit (rel 1e-4, fp32-Reihenfolge) + Speedup */
    {
        int64_t m = 1024, k = 1024, n = 1024;
        float* a = malloc((size_t)(m * k) * 4);
        float* b = malloc((size_t)(k * n) * 4);
        float* g = malloc((size_t)(m * n) * 4);
        float* c = malloc((size_t)(m * n) * 4);
        for (int64_t i = 0; i < m * k; i++) a[i] = zufall(&seed);
        for (int64_t i = 0; i < k * n; i++) b[i] = zufall(&seed);

        double t0 = jetzt();
        CHECK(moo_ki_gpu_matmul(a, b, g, (int32_t)m, (int32_t)k, (int32_t)n),
              "matmul 1024: GPU-Lauf ok");
        double t_gpu = jetzt() - t0;
        t0 = jetzt();
        cpu_matmul(a, b, c, m, k, n);
        double t_cpu = jetzt() - t0;

        int64_t schlecht = 0;
        for (int64_t i = 0; i < m * n; i++) {
            double d = fabs((double)g[i] - (double)c[i]);
            double rel = d / (fabs((double)c[i]) + 1e-9);
            /* Auslöschung: bei ~1024 Termen ±1 liegen einzelne Ergebnisse
             * nahe 0 — dort explodiert rel trotz absdiff ~1e-5 (FMA-
             * Rundung). Fehler nur wenn abs UND rel signifikant. */
            if (d > 1e-3 && rel > 1e-4) schlecht++;
        }
        CHECK(schlecht == 0, "matmul 1024 == CPU (rel 1e-4, alle Elemente)");
        printf("matmul 1024x1024x1024: CPU %.3fs, GPU %.3fs (inkl. Transfer)"
               " -> Speedup %.1fx\n", t_cpu, t_gpu, t_cpu / t_gpu);
        free(a); free(b); free(g); free(c);
    }

    /* 4. GPU3-C Wiederhol-Op-Mikrobenchmark: Per-Op-Overhead sichtbar
     * machen (DescSet/CmdBuf/Fence-Cache + Buffer-Pool greifen erst bei
     * wiederholten Ops). Kein Speedup-CHECK (maschinenabhaengig) — nur
     * Korrektheit des letzten Laufs + Zeitausgabe fuer die Doku. */
    {
        int64_t n = (int64_t)1 << 20;
        int iter = 200;
        float* a = malloc((size_t)n * 4);
        float* b = malloc((size_t)n * 4);
        float* g = malloc((size_t)n * 4);
        for (int64_t i = 0; i < n; i++) { a[i] = zufall(&seed); b[i] = zufall(&seed); }
        double t0 = jetzt();
        for (int it = 0; it < iter; it++) {
            if (!moo_ki_gpu_ew(0, a, b, g, n)) {
                fprintf(stderr, "FAIL: wiederhol-ew Iteration %d\n", it);
                return 1;
            }
        }
        double t = jetzt() - t0;
        int64_t schlecht = 0;
        for (int64_t i = 0; i < n; i++) {
            double c = (double)a[i] + (double)b[i];
            if (fabs((double)g[i] - c) / (fabs(c) + 1e-9) > 1e-6) schlecht++;
        }
        CHECK(schlecht == 0, "wiederhol-ew: letzter Lauf korrekt");
        printf("wiederhol-ew add n=2^20 x%d: gesamt %.3fs -> %.2f ms/Op\n",
               iter, t, t * 1000.0 / iter);
        free(a); free(b); free(g);
    }
    {
        int64_t m = 512, k = 512, n = 512;
        int iter = 30;
        float* a = malloc((size_t)(m * k) * 4);
        float* b = malloc((size_t)(k * n) * 4);
        float* g = malloc((size_t)(m * n) * 4);
        float* c = malloc((size_t)(m * n) * 4);
        for (int64_t i = 0; i < m * k; i++) a[i] = zufall(&seed);
        for (int64_t i = 0; i < k * n; i++) b[i] = zufall(&seed);
        double t0 = jetzt();
        for (int it = 0; it < iter; it++) {
            if (!moo_ki_gpu_matmul(a, b, g, (int32_t)m, (int32_t)k, (int32_t)n)) {
                fprintf(stderr, "FAIL: wiederhol-matmul Iteration %d\n", it);
                return 1;
            }
        }
        double t = jetzt() - t0;
        cpu_matmul(a, b, c, m, k, n);
        int64_t schlecht = 0;
        for (int64_t i = 0; i < m * n; i++) {
            double d = fabs((double)g[i] - (double)c[i]);
            double rel = d / (fabs((double)c[i]) + 1e-9);
            if (d > 1e-3 && rel > 1e-4) schlecht++;
        }
        CHECK(schlecht == 0, "wiederhol-matmul: letzter Lauf korrekt");
        printf("wiederhol-matmul 512^3 x%d: gesamt %.3fs -> %.2f ms/Op\n",
               iter, t, t * 1000.0 / iter);
        free(a); free(b); free(g); free(c);
    }


    printf("test_ki_gpu: alle %d Checks bestanden\n", checks);
    return 0;
}
