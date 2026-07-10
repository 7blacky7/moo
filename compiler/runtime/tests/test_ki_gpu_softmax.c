/**
 * test_ki_gpu_softmax.c — KIP-G3a Gate: GPU Softmax/LogSoftmax + fused Cross-
 * Entropy (Forward + Backward) auf residenten Buffers vs CPU-Referenz.
 *
 * CPU-Referenz spiegelt moo_tensor_ops.c softmax_impl (max-shift, double-Summe),
 * moo_autograd.c bw_softmax/bw_logsoftmax und moo_nn.c kreuzentropie (fused,
 * loss = mean_i(-sum_j t*logsoftmax)). GPU rechnet float (exp/log/rsqrt) ->
 * Toleranz ATOL 1e-5 / RTOL 1e-4 (CE-Skalar ATOL 1e-4). Dims bewusst keine
 * Vielfachen von 256. Ohne echte Vulkan-Hardware transparenter SKIP; ohne ASan.
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

#define ROWS 17
#define COLS 13
#define NN   (ROWS * COLS)
#define BY   ((int64_t)NN * 4)
#define ATOL 1e-5f
#define RTOL 1e-4f

static bool nahe(float a, float b) { return fabsf(a - b) <= ATOL + RTOL * fabsf(b); }
static int zaehle_abw(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (!nahe(x[i], y[i])) c++; return c;
}

static void cpu_softmax(const float* x, float* o, int rows, int cols, int logv) {
    for (int i = 0; i < rows; i++) {
        const float* xi = x + i * cols; float* oi = o + i * cols;
        float m = xi[0]; for (int j = 1; j < cols; j++) if (xi[j] > m) m = xi[j];
        double sum = 0.0; for (int j = 0; j < cols; j++) sum += exp((double)(xi[j] - m));
        if (logv) { double lse = log(sum);
            for (int j = 0; j < cols; j++) oi[j] = (float)((double)(xi[j] - m) - lse); }
        else { for (int j = 0; j < cols; j++) oi[j] = (float)(exp((double)(xi[j] - m)) / sum); }
    }
}
static void cpu_softmax_bw(const float* y, const float* g, float* gin,
                           int rows, int cols, int logv) {
    for (int i = 0; i < rows; i++) {
        const float* yi = y + i * cols; const float* gi = g + i * cols;
        float* zi = gin + i * cols;
        if (logv) { double gs = 0.0; for (int j = 0; j < cols; j++) gs += gi[j];
            for (int j = 0; j < cols; j++) zi[j] = gi[j] - expf(yi[j]) * (float)gs; }
        else { double dot = 0.0; for (int j = 0; j < cols; j++) dot += (double)gi[j] * yi[j];
            for (int j = 0; j < cols; j++) zi[j] = yi[j] * (gi[j] - (float)dot); }
    }
}
static double cpu_ce(const float* x, const float* t, int rows, int cols) {
    double total = 0.0;
    for (int i = 0; i < rows; i++) {
        const float* xi = x + i * cols; const float* ti = t + i * cols;
        float m = xi[0]; for (int j = 1; j < cols; j++) if (xi[j] > m) m = xi[j];
        double sum = 0.0; for (int j = 0; j < cols; j++) sum += exp((double)(xi[j] - m));
        double lse = log(sum), loss = 0.0;
        for (int j = 0; j < cols; j++) loss += (double)ti[j] * ((double)(xi[j] - m) - lse);
        total += -loss;
    }
    return total / rows;
}
static void cpu_ce_bw(const float* x, const float* t, float* grad,
                      int rows, int cols, float scale) {
    for (int i = 0; i < rows; i++) {
        const float* xi = x + i * cols; const float* ti = t + i * cols;
        float* gi = grad + i * cols;
        float m = xi[0]; for (int j = 1; j < cols; j++) if (xi[j] > m) m = xi[j];
        double sum = 0.0; for (int j = 0; j < cols; j++) sum += exp((double)(xi[j] - m));
        for (int j = 0; j < cols; j++) {
            float sm = (float)(exp((double)(xi[j] - m)) / sum);
            gi[j] = (sm - ti[j]) * scale;
        }
    }
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3a GPU Softmax/LogSoftmax + fused CE (Fwd+Bwd, Differential) ==\n");

    void* hX = moo_ki_gpu_buf_belegen(BY);
    void* hT = moo_ki_gpu_buf_belegen(BY);
    void* hO = moo_ki_gpu_buf_belegen(BY);
    void* hG = moo_ki_gpu_buf_belegen(BY);
    void* hGin = moo_ki_gpu_buf_belegen(BY);
    CHECK(hX && hT && hO && hG && hGin, "5 residente Buffers belegt");
    moo_ki_gpu_telemetrie_reset();

    float x[NN], gout[NN], gpu[NN], ref[NN];
    for (int i = 0; i < NN; i++) x[i] = 1.5f * sinf(0.07f * (float)i) - 0.4f * (float)(i % 5);
    for (int i = 0; i < NN; i++) gout[i] = 0.3f * cosf(0.05f * (float)i) + 0.1f;
    bool up = moo_ki_gpu_upload(hX, x, BY) && moo_ki_gpu_upload(hG, gout, BY);
    CHECK(up, "Upload logits + grad_out");

    /* ---------------- softmax Fwd + Bwd ---------------- */
    cpu_softmax(x, ref, ROWS, COLS, 0);
    bool sf = moo_ki_gpu_softmax_res(MOO_KI_SM_SOFTMAX, hX, hO, ROWS, COLS)
           && moo_ki_gpu_download(hO, gpu, BY);
    CHECK(sf && zaehle_abw(gpu, ref, NN) == 0, "softmax FWD == CPU (Tol)");

    float yref[NN]; memcpy(yref, ref, sizeof ref);
    cpu_softmax_bw(yref, gout, ref, ROWS, COLS, 0);
    bool sb = moo_ki_gpu_softmax_bw_res(MOO_KI_SM_SOFTMAX, hO, hG, hGin, ROWS, COLS)
           && moo_ki_gpu_download(hGin, gpu, BY);
    CHECK(sb && zaehle_abw(gpu, ref, NN) == 0, "softmax GRAD da=y*(g-sum(g*y)) == CPU (Tol)");

    /* ---------------- logsoftmax Fwd + Bwd ---------------- */
    cpu_softmax(x, ref, ROWS, COLS, 1);
    bool lf = moo_ki_gpu_softmax_res(MOO_KI_SM_LOGSOFTMAX, hX, hO, ROWS, COLS)
           && moo_ki_gpu_download(hO, gpu, BY);
    CHECK(lf && zaehle_abw(gpu, ref, NN) == 0, "logsoftmax FWD == CPU (Tol)");

    memcpy(yref, ref, sizeof ref);
    cpu_softmax_bw(yref, gout, ref, ROWS, COLS, 1);
    bool lb = moo_ki_gpu_softmax_bw_res(MOO_KI_SM_LOGSOFTMAX, hO, hG, hGin, ROWS, COLS)
           && moo_ki_gpu_download(hGin, gpu, BY);
    CHECK(lb && zaehle_abw(gpu, ref, NN) == 0, "logsoftmax GRAD da=g-exp(y)*sum(g) == CPU (Tol)");

    /* ---------------- fused CE Fwd + Bwd ---------------- */
    float t[NN]; memset(t, 0, sizeof t);
    for (int i = 0; i < ROWS; i++) t[i * COLS + (i % COLS)] = 1.0f;   /* one-hot */
    bool tu = moo_ki_gpu_upload(hT, t, BY);
    double ref_loss = cpu_ce(x, t, ROWS, COLS);
    double gpu_loss = 0.0;
    bool cf = tu && moo_ki_gpu_ce_fwd_res(hX, hT, ROWS, COLS, &gpu_loss);
    CHECK(cf && fabs(gpu_loss - ref_loss) <= 1e-4 + 1e-4 * fabs(ref_loss),
          "fused CE FWD loss == CPU (Tol)");

    float scale = 1.0f / (float)ROWS;
    cpu_ce_bw(x, t, ref, ROWS, COLS, scale);
    bool cb = moo_ki_gpu_ce_bw_res(hX, hT, hGin, ROWS, COLS, scale)
           && moo_ki_gpu_download(hGin, gpu, BY);
    CHECK(cb && zaehle_abw(gpu, ref, NN) == 0, "fused CE GRAD (softmax-target)/batch == CPU (Tol)");

    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");
    /* softmax f/b + logsoftmax f/b + ce f/b = 6 Compute-Submits */
    CHECK(tel.submits == 6, "genau 6 Compute-Submits (softmax 2 + logsoftmax 2 + ce 2)");

    moo_ki_gpu_buf_freigeben(hX); moo_ki_gpu_buf_freigeben(hT);
    moo_ki_gpu_buf_freigeben(hO); moo_ki_gpu_buf_freigeben(hG);
    moo_ki_gpu_buf_freigeben(hGin);
    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
