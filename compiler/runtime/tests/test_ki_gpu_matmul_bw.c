/**
 * test_ki_gpu_matmul_bw.c — KIP-G3d-e Gate: GPU matmul-Backward (Komposition
 * transpose + tiled matmul) vs CPU-Referenz + grad_accum-Fan-out.
 *
 * Fuer C = A@B (A[m,k], B[k,n]): dA = g @ B^T [m,k], dB = A^T @ g [k,n]
 * (CPU-Ref moo_autograd.c bw_matmul). GPU-Matmul akkumuliert float ->
 * Toleranz ATOL 1e-3/RTOL 1e-4. Der grad_accum-Schritt (G3c) addiert dA in
 * einen bestehenden Gradient-Buffer (Fan-out, +=) und wird BIT-EXAKT geprueft
 * (reine Addition). Dims keine Vielfachen von 16 (Rand-Guards der tiled
 * Matmul). SKIP ohne Vulkan; ohne ASan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "moo_ki_gpu_api.h"

static int fehler = 0;
#define CHECK(bed, msg) do { \
    if (bed) { printf("  OK   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fehler++; } \
} while (0)

#define M 13
#define K 20
#define N 17
#define AKN (M * K)
#define BKN (K * N)
#define CMN (M * N)
#define MAXN (BKN)               /* 340 = groesster Buffer */
#define BYMAX ((int64_t)MAXN * 4)
#define ATOL 1e-3f
#define RTOL 1e-4f

static bool nahe(float a, float b) { return fabsf(a - b) <= ATOL + RTOL * fabsf(b); }
static int abw_tol(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (!nahe(x[i], y[i])) c++; return c;
}
static int abw_exakt(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (x[i] != y[i]) c++; return c;
}

static void cpu_matmul(const float* a, const float* b, float* c, int m, int k, int n) {
    for (int i = 0; i < m; i++) for (int j = 0; j < n; j++) {
        double s = 0.0; for (int p = 0; p < k; p++) s += (double)a[i * k + p] * b[p * n + j];
        c[i * n + j] = (float)s;
    }
}
static void cpu_matmul_bw(const float* a, const float* b, const float* g,
                          float* da, float* db, int m, int k, int n) {
    /* da[i,p] = sum_j g[i,j] * b[p,j]  (= g @ B^T) */
    for (int i = 0; i < m; i++) for (int p = 0; p < k; p++) {
        double s = 0.0; for (int j = 0; j < n; j++) s += (double)g[i * n + j] * b[p * n + j];
        da[i * k + p] = (float)s;
    }
    /* db[p,j] = sum_i a[i,p] * g[i,j]  (= A^T @ g) */
    for (int p = 0; p < k; p++) for (int j = 0; j < n; j++) {
        double s = 0.0; for (int i = 0; i < m; i++) s += (double)a[i * k + p] * g[i * n + j];
        db[p * n + j] = (float)s;
    }
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3d-e GPU matmul-Backward (Komposition) + grad_accum ==\n");

    void* hA  = moo_ki_gpu_buf_belegen(BYMAX);
    void* hB  = moo_ki_gpu_buf_belegen(BYMAX);
    void* hG  = moo_ki_gpu_buf_belegen(BYMAX);
    void* hC  = moo_ki_gpu_buf_belegen(BYMAX);
    void* hDa = moo_ki_gpu_buf_belegen(BYMAX);
    void* hDb = moo_ki_gpu_buf_belegen(BYMAX);
    void* hAcc = moo_ki_gpu_buf_belegen(BYMAX);
    CHECK(hA && hB && hG && hC && hDa && hDb && hAcc, "7 residente Buffers belegt");
    moo_ki_gpu_telemetrie_reset();

    float a[AKN], b[BKN], g[CMN], gpu[MAXN], ref[MAXN];
    for (int i = 0; i < AKN; i++) a[i] = 0.4f * sinf(0.07f * (float)i) - 0.2f;
    for (int i = 0; i < BKN; i++) b[i] = 0.3f * cosf(0.05f * (float)i) + 0.1f;
    for (int i = 0; i < CMN; i++) g[i] = 0.25f * sinf(0.09f * (float)i + 0.5f);
    bool up = moo_ki_gpu_upload(hA, a, (int64_t)AKN * 4)
           && moo_ki_gpu_upload(hB, b, (int64_t)BKN * 4)
           && moo_ki_gpu_upload(hG, g, (int64_t)CMN * 4);
    CHECK(up, "Upload A + B + grad_out");

    /* ---------------- matmul Forward (Referenz-Sanity) ------------------- */
    cpu_matmul(a, b, ref, M, K, N);
    bool cf = moo_ki_gpu_matmul_res(hA, hB, hC, M, K, N)
           && moo_ki_gpu_download(hC, gpu, (int64_t)CMN * 4);
    CHECK(cf && abw_tol(gpu, ref, CMN) == 0, "matmul FWD C=A@B == CPU (Tol)");

    /* ---------------- matmul Backward dA / dB ---------------------------- */
    float da_ref[AKN], db_ref[BKN], gpu_da[AKN];
    cpu_matmul_bw(a, b, g, da_ref, db_ref, M, K, N);
    bool bw = moo_ki_gpu_matmul_bw_res(hA, hB, hG, hDa, hDb, M, K, N);
    bool dda = bw && moo_ki_gpu_download(hDa, gpu, (int64_t)AKN * 4);
    memcpy(gpu_da, gpu, sizeof(float) * AKN);
    CHECK(dda && abw_tol(gpu, da_ref, AKN) == 0, "matmul GRAD dA = g@B^T == CPU (Tol)");
    bool ddb = bw && moo_ki_gpu_download(hDb, gpu, (int64_t)BKN * 4);
    CHECK(ddb && abw_tol(gpu, db_ref, BKN) == 0, "matmul GRAD dB = A^T@g == CPU (Tol)");

    /* ---------------- grad_accum Fan-out (+=) ---------------------------- */
    float acc0[AKN];
    for (int i = 0; i < AKN; i++) acc0[i] = 0.5f * cosf(0.03f * (float)i) - 0.1f;
    bool ga = moo_ki_gpu_upload(hAcc, acc0, (int64_t)AKN * 4)
           && moo_ki_gpu_grad_accum_res(hAcc, hDa, AKN)
           && moo_ki_gpu_download(hAcc, gpu, (int64_t)AKN * 4);
    for (int i = 0; i < AKN; i++) ref[i] = acc0[i] + gpu_da[i];   /* erwartet: acc += dA */
    CHECK(ga && abw_exakt(gpu, ref, AKN) == 0, "grad_accum acc += dA (Fan-out) == CPU (bit-exakt)");

    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.cpu_fallbacks == 0, "keine CPU-Fallbacks im matmul-Bwd-Pfad");
    /* matmul fwd 1 + matmul_bw 4 (2 transpose + 2 matmul) + grad_accum 1 = 6 */
    CHECK(tel.submits == 6, "genau 6 Compute-Submits (fwd 1 + bw 4 + grad_accum 1)");

    moo_ki_gpu_buf_freigeben(hA);  moo_ki_gpu_buf_freigeben(hB);
    moo_ki_gpu_buf_freigeben(hG);  moo_ki_gpu_buf_freigeben(hC);
    moo_ki_gpu_buf_freigeben(hDa); moo_ki_gpu_buf_freigeben(hDb);
    moo_ki_gpu_buf_freigeben(hAcc);
    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
