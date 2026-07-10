/**
 * test_ki_gpu_reduce.c — KIP-G3d-b Gate: GPU Achsen-Reduktion (sum/mean/max) +
 * Broadcast (Reduktions-Backward) + Max-Subgradient vs CPU-Referenz.
 *
 * CPU-Referenz spiegelt moo_tensor_ops.c reduce_op und moo_autograd.c
 * bw_sum/bw_mean/bw_max. sum/mean nutzen double-Akkumulation -> Toleranz
 * ATOL 1e-5/RTOL 1e-4; max + broadcast + max-Subgradient sind reine
 * Vergleiche/Kopien -> BIT-EXAKT. Dims keine Vielfachen von 256. SKIP ohne
 * Vulkan; ohne ASan.
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
static int abw_tol(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (!nahe(x[i], y[i])) c++; return c;
}
static int abw_exakt(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (x[i] != y[i]) c++; return c;
}

static void cpu_reduce(int op, int axis, const float* a, float* o, int r, int c) {
    if (axis == 0) {
        for (int j = 0; j < c; j++) {
            if (op == MOO_KI_RED_MAX) { float m = a[j];
                for (int i = 1; i < r; i++) { float v = a[i * c + j]; if (v > m) m = v; } o[j] = m; }
            else { double acc = 0.0; for (int i = 0; i < r; i++) acc += a[i * c + j];
                o[j] = (float)(op == MOO_KI_RED_MEAN ? acc / r : acc); }
        }
    } else {
        for (int i = 0; i < r; i++) { const float* row = a + i * c;
            if (op == MOO_KI_RED_MAX) { float m = row[0];
                for (int j = 1; j < c; j++) if (row[j] > m) m = row[j]; o[i] = m; }
            else { double acc = 0.0; for (int j = 0; j < c; j++) acc += row[j];
                o[i] = (float)(op == MOO_KI_RED_MEAN ? acc / c : acc); }
        }
    }
}
static void cpu_broadcast(int axis, const float* src, float* o, int r, int c, float scale) {
    for (int i = 0; i < r; i++)
        for (int j = 0; j < c; j++) o[i * c + j] = (axis == 0 ? src[j] : src[i]) * scale;
}
static void cpu_max_bw(int axis, const float* a, const float* g, float* gin, int r, int c) {
    memset(gin, 0, sizeof(float) * r * c);
    if (axis == 0) {
        for (int j = 0; j < c; j++) { int am = 0; float m = a[j];
            for (int i = 1; i < r; i++) { float v = a[i * c + j]; if (v > m) { m = v; am = i; } }
            gin[am * c + j] = g[j]; }
    } else {
        for (int i = 0; i < r; i++) { const float* row = a + i * c; int am = 0; float m = row[0];
            for (int j = 1; j < c; j++) if (row[j] > m) { m = row[j]; am = j; }
            gin[i * c + am] = g[i]; }
    }
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3d-b GPU Achsen-Reduktion + Broadcast + Max-Subgradient ==\n");

    void* hA   = moo_ki_gpu_buf_belegen(BY);
    void* hRed = moo_ki_gpu_buf_belegen(BY);
    void* hO   = moo_ki_gpu_buf_belegen(BY);
    void* hGin = moo_ki_gpu_buf_belegen(BY);
    void* hGkd = moo_ki_gpu_buf_belegen(BY);
    CHECK(hA && hRed && hO && hGin && hGkd, "5 residente Buffers belegt");
    moo_ki_gpu_telemetrie_reset();

    float a[NN], gpu[NN], ref[NN];
    for (int i = 0; i < NN; i++) a[i] = 1.7f * sinf(0.11f * (float)i + 0.3f) - 0.4f * (float)(i % 6);
    CHECK(moo_ki_gpu_upload(hA, a, BY), "Upload a");

    /* ---------------- Achsen-Reduktion sum/mean/max, axis 0/1 ------------- */
    const int arten[3] = { MOO_KI_RED_SUM, MOO_KI_RED_MEAN, MOO_KI_RED_MAX };
    const char* an[3] = { "sum", "mean", "max" };
    for (int axis = 0; axis <= 1; axis++) {
        int outn = axis == 0 ? COLS : ROWS;
        for (int k = 0; k < 3; k++) {
            cpu_reduce(arten[k], axis, a, ref, ROWS, COLS);
            bool ok = moo_ki_gpu_reduce_axis_res(arten[k], axis, hA, hRed, ROWS, COLS)
                   && moo_ki_gpu_download(hRed, gpu, (int64_t)outn * 4);
            int d = (arten[k] == MOO_KI_RED_MAX) ? abw_exakt(gpu, ref, outn) : abw_tol(gpu, ref, outn);
            char msg[80]; snprintf(msg, sizeof msg, "reduce %s axis %d == CPU%s", an[k], axis,
                                   arten[k] == MOO_KI_RED_MAX ? " (bit-exakt)" : " (Tol)");
            CHECK(ok && d == 0, msg);
        }
    }

    /* ---------------- Broadcast (sum-bw scale 1 + mean-bw scale 1/n) ------ */
    for (int axis = 0; axis <= 1; axis++) {
        int srcn = axis == 0 ? COLS : ROWS;
        float src[COLS > ROWS ? COLS : ROWS];
        for (int i = 0; i < srcn; i++) src[i] = 0.5f * (float)i - 2.0f;
        float scales[2]; scales[0] = 1.0f; scales[1] = 1.0f / (float)(axis == 0 ? ROWS : COLS);
        const char* sn[2] = { "scale 1 (sum-bw)", "scale 1/n (mean-bw)" };
        for (int s = 0; s < 2; s++) {
            moo_ki_gpu_upload(hRed, src, (int64_t)srcn * 4);
            cpu_broadcast(axis, src, ref, ROWS, COLS, scales[s]);
            bool ok = moo_ki_gpu_broadcast_res(axis, hRed, hO, ROWS, COLS, scales[s])
                   && moo_ki_gpu_download(hO, gpu, BY);
            char msg[96]; snprintf(msg, sizeof msg, "broadcast axis %d %s == CPU (bit-exakt)", axis, sn[s]);
            CHECK(ok && abw_exakt(gpu, ref, NN) == 0, msg);
        }
    }

    /* ---------------- Max-Reduktions-Subgradient, axis 0/1 --------------- */
    for (int axis = 0; axis <= 1; axis++) {
        int gn = axis == 0 ? COLS : ROWS;
        float gkd[COLS > ROWS ? COLS : ROWS];
        for (int i = 0; i < gn; i++) gkd[i] = 0.7f * (float)i + 0.2f;
        moo_ki_gpu_upload(hGkd, gkd, (int64_t)gn * 4);
        cpu_max_bw(axis, a, gkd, ref, ROWS, COLS);
        bool ok = moo_ki_gpu_reduce_max_bw_res(axis, hA, hGkd, hGin, ROWS, COLS)
               && moo_ki_gpu_download(hGin, gpu, BY);
        char msg[80]; snprintf(msg, sizeof msg, "max-Subgradient axis %d (argmax) == CPU (bit-exakt)", axis);
        CHECK(ok && abw_exakt(gpu, ref, NN) == 0, msg);
    }

    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");
    /* 6 reduce + 4 broadcast + 2 max-bw = 12 Compute-Submits */
    CHECK(tel.submits == 12, "genau 12 Compute-Submits (reduce 6 + broadcast 4 + max-bw 2)");

    moo_ki_gpu_buf_freigeben(hA);   moo_ki_gpu_buf_freigeben(hRed);
    moo_ki_gpu_buf_freigeben(hO);   moo_ki_gpu_buf_freigeben(hGin);
    moo_ki_gpu_buf_freigeben(hGkd);
    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
