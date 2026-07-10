/**
 * test_ki_gpu_norm.c — KIP-G3b Gate: GPU LayerNorm/RMSNorm-KERN (Forward +
 * Backward, ohne Affine) auf residenten Buffers vs CPU-Referenz.
 *
 * CPU-Referenz spiegelt moo_nn.c fw_layernorm/fw_rmsnorm (Normalisierungs-Kern
 * ueber die letzte Achse). Zusaetzlich prueft ein Finite-Differenzen-Gradcheck
 * die analytische Backward-Formel (dx) unabhaengig — das beweist die Formel,
 * nicht nur GPU==CPU. GPU rechnet float -> Toleranz ATOL 1e-5 / RTOL 1e-4;
 * FD-Check gegen die CPU-Analytik mit ATOL 5e-3 (FD ist grob). Dims keine
 * Vielfachen von 256. Ohne echte Vulkan-Hardware SKIP; ohne ASan.
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
#define EPS  1e-5f
#define ATOL 1e-5f
#define RTOL 1e-4f

static bool nahe(float a, float b) { return fabsf(a - b) <= ATOL + RTOL * fabsf(b); }
static int zaehle_abw(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (!nahe(x[i], y[i])) c++; return c;
}

static void cpu_norm(int op, const float* x, float* o, int rows, int cols, float eps) {
    for (int i = 0; i < rows; i++) {
        const float* xi = x + i * cols; float* oi = o + i * cols;
        if (op == MOO_KI_NORM_RMS) {
            double ms = 0.0; for (int j = 0; j < cols; j++) ms += (double)xi[j] * xi[j];
            ms /= cols; float s = (float)sqrt(ms + eps);
            for (int j = 0; j < cols; j++) oi[j] = xi[j] / s;
        } else {
            double mean = 0.0; for (int j = 0; j < cols; j++) mean += xi[j]; mean /= cols;
            double var = 0.0; for (int j = 0; j < cols; j++) { double d = xi[j] - mean; var += d * d; }
            var /= cols; float s = (float)sqrt(var + eps);
            for (int j = 0; j < cols; j++) oi[j] = (float)((xi[j] - mean) / s);
        }
    }
}
static void cpu_norm_bw(int op, const float* x, const float* g, float* dx,
                        int rows, int cols, float eps) {
    for (int i = 0; i < rows; i++) {
        const float* xi = x + i * cols; const float* gi = g + i * cols;
        float* di = dx + i * cols;
        if (op == MOO_KI_NORM_RMS) {
            double ms = 0.0; for (int j = 0; j < cols; j++) ms += (double)xi[j] * xi[j];
            ms /= cols; float s = (float)sqrt(ms + eps);
            double gn = 0.0; for (int j = 0; j < cols; j++) gn += (double)gi[j] * (xi[j] / s);
            gn /= cols;
            for (int j = 0; j < cols; j++) { float n = xi[j] / s; di[j] = (float)((gi[j] - n * gn) / s); }
        } else {
            double mean = 0.0; for (int j = 0; j < cols; j++) mean += xi[j]; mean /= cols;
            double var = 0.0; for (int j = 0; j < cols; j++) { double d = xi[j] - mean; var += d * d; }
            var /= cols; float s = (float)sqrt(var + eps);
            double gmean = 0.0, gn = 0.0;
            for (int j = 0; j < cols; j++) { double n = (xi[j] - mean) / s; gmean += gi[j]; gn += (double)gi[j] * n; }
            gmean /= cols; gn /= cols;
            for (int j = 0; j < cols; j++) { double n = (xi[j] - mean) / s; di[j] = (float)((gi[j] - gmean - n * gn) / s); }
        }
    }
}

/* Skalar-Verlust EINER Zeile: sum_j g_j * norm(x_row)_j (fuer FD-Gradcheck). */
static double zeilen_loss(int op, const double* xr, const float* gr, int cols, float eps) {
    if (op == MOO_KI_NORM_RMS) {
        double ms = 0.0; for (int j = 0; j < cols; j++) ms += xr[j] * xr[j]; ms /= cols;
        double s = sqrt(ms + eps), L = 0.0;
        for (int j = 0; j < cols; j++) L += (double)gr[j] * (xr[j] / s);
        return L;
    } else {
        double mean = 0.0; for (int j = 0; j < cols; j++) mean += xr[j]; mean /= cols;
        double var = 0.0; for (int j = 0; j < cols; j++) { double d = xr[j] - mean; var += d * d; }
        var /= cols; double s = sqrt(var + eps), L = 0.0;
        for (int j = 0; j < cols; j++) L += (double)gr[j] * ((xr[j] - mean) / s);
        return L;
    }
}

/* Finite-Differenzen-Gradcheck der analytischen dx (Zeile 0). */
static bool fd_gradcheck(int op, const float* x, const float* g, const float* dx,
                         int cols, float eps) {
    double xr[COLS]; for (int j = 0; j < cols; j++) xr[j] = x[j];
    const double h = 1e-3;
    for (int k = 0; k < cols; k++) {
        double save = xr[k];
        xr[k] = save + h; double lp = zeilen_loss(op, xr, g, cols, eps);
        xr[k] = save - h; double lm = zeilen_loss(op, xr, g, cols, eps);
        xr[k] = save;
        double fd = (lp - lm) / (2.0 * h);
        if (fabs(fd - dx[k]) > 5e-3 + 5e-3 * fabs(dx[k])) return false;
    }
    return true;
}

static bool run_variant(int op, const char* name, void* hX, void* hG, void* hO,
                        void* hDx, const float* x, const float* gout) {
    float gpu[NN], ref[NN];
    /* Forward */
    cpu_norm(op, x, ref, ROWS, COLS, EPS);
    bool f = moo_ki_gpu_norm_res(op, hX, hO, ROWS, COLS, EPS)
          && moo_ki_gpu_download(hO, gpu, BY);
    char m1[80]; snprintf(m1, sizeof m1, "%s FWD == CPU (Tol)", name);
    CHECK(f && zaehle_abw(gpu, ref, NN) == 0, m1);
    /* Backward vs CPU */
    cpu_norm_bw(op, x, gout, ref, ROWS, COLS, EPS);
    bool b = moo_ki_gpu_norm_bw_res(op, hX, hG, hDx, ROWS, COLS, EPS)
          && moo_ki_gpu_download(hDx, gpu, BY);
    char m2[80]; snprintf(m2, sizeof m2, "%s GRAD dx == CPU (Tol)", name);
    CHECK(b && zaehle_abw(gpu, ref, NN) == 0, m2);
    /* FD-Gradcheck der analytischen Formel (Zeile 0) */
    char m3[80]; snprintf(m3, sizeof m3, "%s GRAD == Finite-Differenzen (Formel-Beweis)", name);
    CHECK(fd_gradcheck(op, x, gout, ref, COLS, EPS), m3);
    return f && b;
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3b GPU LayerNorm/RMSNorm-Kern (Fwd+Bwd, Differential + FD) ==\n");

    void* hX = moo_ki_gpu_buf_belegen(BY);
    void* hG = moo_ki_gpu_buf_belegen(BY);
    void* hO = moo_ki_gpu_buf_belegen(BY);
    void* hDx = moo_ki_gpu_buf_belegen(BY);
    CHECK(hX && hG && hO && hDx, "4 residente Buffers belegt");
    moo_ki_gpu_telemetrie_reset();

    float x[NN], gout[NN];
    for (int i = 0; i < NN; i++) x[i] = 1.2f * sinf(0.09f * (float)i) - 0.5f * (float)(i % 4) + 0.3f;
    for (int i = 0; i < NN; i++) gout[i] = 0.4f * cosf(0.06f * (float)i) + 0.05f;
    CHECK(moo_ki_gpu_upload(hX, x, BY) && moo_ki_gpu_upload(hG, gout, BY), "Upload x + grad_out");

    run_variant(MOO_KI_NORM_LAYER, "LayerNorm", hX, hG, hO, hDx, x, gout);
    run_variant(MOO_KI_NORM_RMS,   "RMSNorm",   hX, hG, hO, hDx, x, gout);

    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");
    /* LN f/b + RMS f/b = 4 Compute-Submits */
    CHECK(tel.submits == 4, "genau 4 Compute-Submits (LayerNorm 2 + RMSNorm 2)");

    moo_ki_gpu_buf_freigeben(hX); moo_ki_gpu_buf_freigeben(hG);
    moo_ki_gpu_buf_freigeben(hO); moo_ki_gpu_buf_freigeben(hDx);
    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
