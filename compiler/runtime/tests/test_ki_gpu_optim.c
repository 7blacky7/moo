/**
 * test_ki_gpu_optim.c — KIP-G3c Gate: GPU-Gradient-Akkumulation + Optimizer-
 * Schritt (SGD-Momentum / Adam / AdamW) auf residenten Buffers vs CPU-Referenz.
 *
 * Die CPU-Referenz spiegelt moo_nn.c moo_nn_opt_schritt() BIT-fuer-BIT in der
 * Reihenfolge (decoupled-Decay zuerst, dann m/v, dann p; bc = 1-beta^t float).
 * Der Optimizer laeuft MEHRERE Schritte residenten (m/v/mv bleiben zwischen
 * den Schritten auf der GPU — kein Download dazwischen), erst am Ende werden
 * p und der Zustand heruntergeladen und verglichen (E2b-Download-Schnittstelle).
 *
 * grad_accum ist reine Addition -> BIT-EXAKT. Der Optimizer-Schritt nutzt
 * mul/div/sqrt: GPU-FMA/rsqrt weicht minimal von CPU ab -> Toleranz
 * ATOL 1e-5 / RTOL 1e-4 (dokumentiert, wie unary_res). Ohne echte Vulkan-
 * Hardware transparenter SKIP. Ohne ASan (NVIDIA-Leak-Noise), wie ki-gpu-smoke.
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

#define N     500                 /* bewusst kein Vielfaches von 256 -> Rand-Guard */
#define BY    ((int64_t)N * 4)
#define BY2   ((int64_t)N * 2 * 4) /* mv-Buffer fuer Adam (m|v) */
#define STEPS 5
#define ATOL  1e-5f
#define RTOL  1e-4f

static bool nahe(float a, float b) {
    float d = fabsf(a - b);
    return d <= ATOL + RTOL * fabsf(b);
}
static int zaehle_abw(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (!nahe(x[i], y[i])) c++; return c;
}

/* Deterministischer, beschraenkter Gradient je Schritt/Index. */
static float grad_at(int i, int step) {
    return 0.5f * sinf(0.05f * (float)i + 0.3f * (float)step) - 0.1f;
}

/* CPU-Referenz: SGD-Momentum (moo_nn.c art==sgd). */
static void cpu_sgd(float* p, float* m, const float* g, int n, float lr, float mu) {
    for (int j = 0; j < n; j++) { m[j] = mu * m[j] + g[j]; p[j] -= lr * m[j]; }
}
/* CPU-Referenz: Adam/AdamW (moo_nn.c art==adam/adamw), Reihenfolge identisch. */
static void cpu_adam(float* p, float* m, float* v, const float* g, int n,
                     float lr, float b1, float b2, float eps, float wd,
                     int adamw, int t) {
    float bc1 = (float)(1.0 - pow((double)b1, (double)t));
    float bc2 = (float)(1.0 - pow((double)b2, (double)t));
    for (int j = 0; j < n; j++) {
        float gj = g[j];
        if (adamw) p[j] -= lr * wd * p[j];
        m[j] = b1 * m[j] + (1.0f - b1) * gj;
        v[j] = b2 * v[j] + (1.0f - b2) * gj * gj;
        float mhat = m[j] / bc1;
        float vhat = v[j] / bc2;
        p[j] -= lr * mhat / (sqrtf(vhat) + eps);
    }
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) {
        printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n");
        return 0;
    }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3c GPU grad_accum + Optimizer-Schritt (Differential vs CPU) ==\n");

    void* hP  = moo_ki_gpu_buf_belegen(BY);
    void* hG  = moo_ki_gpu_buf_belegen(BY);
    void* hM  = moo_ki_gpu_buf_belegen(BY);   /* SGD-Momentum / grad_accum-Ziel */
    void* hMV = moo_ki_gpu_buf_belegen(BY2);  /* Adam m|v */
    CHECK(hP && hG && hM && hMV, "4 residente Buffers belegt");
    moo_ki_gpu_telemetrie_reset();

    float p0[N], gpu_p[N], gpu_m[N], gpu_v[N];
    float cpu_p[N], cpu_m[N], cpu_v[N];
    for (int i = 0; i < N; i++) p0[i] = 0.3f * cosf(0.02f * (float)i) + 0.05f;

    /* ---------------- grad_accum: acc += g ueber 3 Beitraege (bit-exakt) ---- */
    {
        float acc0[N]; for (int i = 0; i < N; i++) acc0[i] = 0.0f;
        for (int i = 0; i < N; i++) cpu_m[i] = 0.0f;   /* CPU-acc */
        bool ok = moo_ki_gpu_upload(hM, acc0, BY);
        for (int s = 0; s < 3; s++) {
            float g[N]; for (int i = 0; i < N; i++) { g[i] = grad_at(i, s); cpu_m[i] += g[i]; }
            ok = ok && moo_ki_gpu_upload(hG, g, BY)
                    && moo_ki_gpu_grad_accum_res(hM, hG, N);
        }
        ok = ok && moo_ki_gpu_download(hM, gpu_m, BY);
        int d = 0; for (int i = 0; i < N; i++) if (gpu_m[i] != cpu_m[i]) d++;
        CHECK(ok && d == 0, "grad_accum acc+=g (3x) == CPU (bit-exakt)");
    }

    /* ---------------- SGD-Momentum: STEPS Schritte resident ---------------- */
    {
        float lr = 0.05f, mu = 0.9f;
        memcpy(cpu_p, p0, sizeof p0);
        for (int i = 0; i < N; i++) cpu_m[i] = 0.0f;
        float m0[N]; for (int i = 0; i < N; i++) m0[i] = 0.0f;
        bool ok = moo_ki_gpu_upload(hP, p0, BY) && moo_ki_gpu_upload(hM, m0, BY);
        for (int s = 0; s < STEPS; s++) {
            float g[N]; for (int i = 0; i < N; i++) g[i] = grad_at(i, s);
            cpu_sgd(cpu_p, cpu_m, g, N, lr, mu);
            ok = ok && moo_ki_gpu_upload(hG, g, BY)
                    && moo_ki_gpu_opt_sgd_res(hP, hM, hG, N, lr, mu);
        }
        ok = ok && moo_ki_gpu_download(hP, gpu_p, BY)
                && moo_ki_gpu_download(hM, gpu_m, BY);
        CHECK(ok, "SGD: 5 residente Schritte ausgefuehrt");
        CHECK(zaehle_abw(gpu_p, cpu_p, N) == 0, "SGD p == CPU nach 5 Schritten (Tol)");
        CHECK(zaehle_abw(gpu_m, cpu_m, N) == 0, "SGD Momentum m == CPU nach 5 Schritten (Tol)");
    }

    /* ---------------- Adam: STEPS Schritte resident (mv-Buffer) ------------ */
    {
        float lr = 0.01f, b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
        memcpy(cpu_p, p0, sizeof p0);
        for (int i = 0; i < N; i++) { cpu_m[i] = 0.0f; cpu_v[i] = 0.0f; }
        float mv0[2 * N]; for (int i = 0; i < 2 * N; i++) mv0[i] = 0.0f;
        bool ok = moo_ki_gpu_upload(hP, p0, BY) && moo_ki_gpu_upload(hMV, mv0, BY2);
        for (int s = 0; s < STEPS; s++) {
            float g[N]; for (int i = 0; i < N; i++) g[i] = grad_at(i, s);
            cpu_adam(cpu_p, cpu_m, cpu_v, g, N, lr, b1, b2, eps, 0.0f, 0, s + 1);
            ok = ok && moo_ki_gpu_upload(hG, g, BY)
                    && moo_ki_gpu_opt_adam_res(hP, hG, hMV, N, lr, b1, b2, eps,
                                               0.0f, /*adamw*/ 0, /*t*/ s + 1);
        }
        float mv[2 * N];
        ok = ok && moo_ki_gpu_download(hP, gpu_p, BY)
                && moo_ki_gpu_download(hMV, mv, BY2);
        for (int i = 0; i < N; i++) { gpu_m[i] = mv[i]; gpu_v[i] = mv[N + i]; }
        CHECK(ok, "Adam: 5 residente Schritte ausgefuehrt");
        CHECK(zaehle_abw(gpu_p, cpu_p, N) == 0, "Adam p == CPU nach 5 Schritten (Tol)");
        CHECK(zaehle_abw(gpu_m, cpu_m, N) == 0, "Adam m == CPU nach 5 Schritten (Tol)");
        CHECK(zaehle_abw(gpu_v, cpu_v, N) == 0, "Adam v == CPU nach 5 Schritten (Tol)");
    }

    /* ---------------- AdamW: decoupled weight decay ------------------------ */
    {
        float lr = 0.01f, b1 = 0.9f, b2 = 0.999f, eps = 1e-8f, wd = 0.01f;
        memcpy(cpu_p, p0, sizeof p0);
        for (int i = 0; i < N; i++) { cpu_m[i] = 0.0f; cpu_v[i] = 0.0f; }
        float mv0[2 * N]; for (int i = 0; i < 2 * N; i++) mv0[i] = 0.0f;
        bool ok = moo_ki_gpu_upload(hP, p0, BY) && moo_ki_gpu_upload(hMV, mv0, BY2);
        for (int s = 0; s < STEPS; s++) {
            float g[N]; for (int i = 0; i < N; i++) g[i] = grad_at(i, s);
            cpu_adam(cpu_p, cpu_m, cpu_v, g, N, lr, b1, b2, eps, wd, 1, s + 1);
            ok = ok && moo_ki_gpu_upload(hG, g, BY)
                    && moo_ki_gpu_opt_adam_res(hP, hG, hMV, N, lr, b1, b2, eps,
                                               wd, /*adamw*/ 1, /*t*/ s + 1);
        }
        float mv[2 * N];
        ok = ok && moo_ki_gpu_download(hP, gpu_p, BY)
                && moo_ki_gpu_download(hMV, mv, BY2);
        for (int i = 0; i < N; i++) { gpu_m[i] = mv[i]; gpu_v[i] = mv[N + i]; }
        CHECK(ok, "AdamW: 5 residente Schritte ausgefuehrt");
        CHECK(zaehle_abw(gpu_p, cpu_p, N) == 0, "AdamW p (decoupled decay) == CPU (Tol)");
        CHECK(zaehle_abw(gpu_m, cpu_m, N) == 0, "AdamW m == CPU (Tol)");
        CHECK(zaehle_abw(gpu_v, cpu_v, N) == 0, "AdamW v == CPU (Tol)");
    }

    MooKiGpuTelemetrie t;
    moo_ki_gpu_telemetrie(&t);
    CHECK(t.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");
    /* grad_accum 3 + SGD 5 + Adam 5 + AdamW 5 = 18 Compute-Submits */
    CHECK(t.submits == 18, "genau 18 Compute-Submits (grad_accum 3 + sgd 5 + adam 5 + adamw 5)");

    moo_ki_gpu_buf_freigeben(hP); moo_ki_gpu_buf_freigeben(hG);
    moo_ki_gpu_buf_freigeben(hM); moo_ki_gpu_buf_freigeben(hMV);
    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
