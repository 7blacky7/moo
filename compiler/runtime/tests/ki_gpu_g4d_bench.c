/**
 * ki_gpu_g4d_bench.c — KIP-G4d-EVAL Benchmark-/Entscheidungsnachweis (Task 291fb2da).
 * Vergleicht die dedizierten Norm-Kern-Tape-Ops (Commit d0ed5f5, WEG 2) gegen die
 * fruehere Op-Komposition (WEG-1-Aequivalent: mul/mittel/adds/sqrt/div) fuer
 * RMSNorm (affine-frei), Forward+Backward, auf echter Vulkan-Hardware.
 *
 * Misst: Wall-Time pro Iteration + GPU-Submits (Telemetrie) + Grad-Aequivalenz
 * (max. Abweichung dx fused vs. komponiert — Korrektheits-Kreuzprobe).
 *
 * KEIN Teil von run_all/run_sanitize (GPU-frei-Regel). Aufruf ad-hoc:
 *   gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I compiler/runtime \
 *       -o /tmp/g4d_bench compiler/runtime/tests/ki_gpu_g4d_bench.c \
 *       compiler/runtime/{moo_nn,moo_nn_easy,moo_json,moo_tensor,moo_tensor_ops,moo_ki_gpu,moo_autograd,moo_memory,moo_value,moo_print,moo_string,moo_dict,moo_list,moo_ops}.c \
 *       -lvulkan -lm && /tmp/g4d_bench
 * Ohne libvulkan: laeuft CPU-only (Vergleich dann wenig aussagekraeftig, kein Fehler).
 */
#include "../moo_runtime.h"
#include "../moo_ki_gpu_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

/* --- Test-throw-Modell (wie test_g4c_strikt.c) --------------------------- */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
    moo_error_flag = 1;
}

/* --- Stubs fuer moo_release()-Dispatch-Ziele ------------------------------ */
void moo_socket_free(void* p)       { (void)p; }
void moo_thread_free(void* p)       { (void)p; }
void moo_channel_free(void* p)      { (void)p; }
void moo_db_free(void* p)           { (void)p; }
void moo_db_stmt_free(void* p)      { (void)p; }
void moo_window_free(void* p)       { (void)p; }
void moo_web_free(void* p)          { (void)p; }
void moo_voxel_free(void* p)        { (void)p; }
void moo_frame_free(void* p)        { (void)p; }
void moo_gif_handle_free(void* p)   { (void)p; }
void moo_video_handle_free(void* p) { (void)p; }

static MooTensor* T(MooValue v) { return (MooTensor*)moo_val_as_ptr(v); }

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static MooValue mach_x(int32_t r, int32_t c, uint32_t seed) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    uint32_t s = seed;
    for (int64_t i = 0; i < (int64_t)r * c; i++) {
        s = s * 1664525u + 1013904223u;
        t->data[i] = ((float)(s >> 8) / 16777216.0f) * 2.0f - 1.0f;
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

/* Ein Fwd+Bwd-Schritt, fused (WEG 2, d0ed5f5). Gibt Loss-Wert zurueck. */
static float schritt_fused(MooValue x) {
    MooValue n = moo_tensor_rmsnorm_kern(x);
    MooValue z = moo_tensor_summe(n, moo_number(-1));
    moo_tensor_rueckwaerts(z);
    float loss = T(z)->data[0];
    moo_release(n); moo_release(z);
    return loss;
}

/* Ein Fwd+Bwd-Schritt, Komposition (WEG-1-Aequivalent = fw_rmsnorm VOR d0ed5f5):
 * n = x / sqrt(mean(x*x, achse=letzte) + 1e-5)  -> 5 Tape-Nodes statt 1. */
static float schritt_komposition(MooValue x) {
    MooValue achse = moo_number((double)(T(x)->ndim - 1));
    MooValue x2 = moo_tensor_mul(x, x);
    MooValue m  = moo_tensor_mittel(x2, achse);
    MooValue me = moo_tensor_adds(m, moo_number(1e-5));
    MooValue s  = moo_tensor_sqrt(me);
    MooValue n  = moo_tensor_div(x, s);
    MooValue z  = moo_tensor_summe(n, moo_number(-1));
    moo_tensor_rueckwaerts(z);
    float loss = T(z)->data[0];
    moo_release(x2); moo_release(m); moo_release(me);
    moo_release(s); moo_release(n); moo_release(z);
    return loss;
}

typedef float (*schritt_fn)(MooValue);

typedef struct {
    double ms_pro_iter;
    unsigned long long submits;
    float loss;
} BenchErgebnis;

static BenchErgebnis bench(schritt_fn fn, int32_t r, int32_t c, int iters, float* dx_out) {
    BenchErgebnis e = {0};
    MooValue x = mach_x(r, c, 42);
    moo_release(moo_tensor_mit_gradient(x));
    /* Warmup (Shader-Pipelines/Buffer-Pool) */
    for (int w = 0; w < 3; w++) {
        moo_tensor_gradient_loeschen(x);
        fn(x);
        moo_ag_reset();
    }
    moo_ki_gpu_telemetrie_reset();
    double t0 = now_ms();
    for (int i = 0; i < iters; i++) {
        moo_tensor_gradient_loeschen(x);
        e.loss = fn(x);
        moo_ag_reset();
    }
    double t1 = now_ms();
    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    e.ms_pro_iter = (t1 - t0) / (double)iters;
    e.submits = (unsigned long long)tel.submits;
    if (dx_out) {
        /* letzter Grad-Stand fuer Aequivalenz-Kreuzprobe (nur 1 Schritt drin) */
        moo_tensor_host_sichern(T(x));
        moo_tensor_grad_sichern(T(x));
        memcpy(dx_out, T(x)->grad, (size_t)r * (size_t)c * sizeof(float));
    }
    moo_tensor_gradient_loeschen(x);
    moo_release(x);
    return e;
}

static void lauf(int32_t r, int32_t c, int iters) {
    int64_t n = (int64_t)r * c;
    float* dx_a = (float*)malloc((size_t)n * sizeof(float));
    float* dx_b = (float*)malloc((size_t)n * sizeof(float));
    BenchErgebnis a = bench(schritt_fused,       r, c, iters, dx_a);
    BenchErgebnis b = bench(schritt_komposition, r, c, iters, dx_b);
    float maxdiff = 0.0f;
    for (int64_t i = 0; i < n; i++) {
        float d = fabsf(dx_a[i] - dx_b[i]);
        if (d > maxdiff) maxdiff = d;
    }
    printf("[%5dx%-5d n=%8lld iters=%d]\n", r, c, (long long)n, iters);
    printf("  fused (WEG 2, d0ed5f5): %8.3f ms/iter  submits=%llu (%.1f/iter)  loss=%.6f\n",
           a.ms_pro_iter, a.submits, (double)a.submits / iters, a.loss);
    printf("  komposition (alt)     : %8.3f ms/iter  submits=%llu (%.1f/iter)  loss=%.6f\n",
           b.ms_pro_iter, b.submits, (double)b.submits / iters, b.loss);
    printf("  speedup fused: %.2fx | grad-maxdiff fused vs. komposition: %.3e\n\n",
           b.ms_pro_iter / a.ms_pro_iter, maxdiff);
    free(dx_a); free(dx_b);
}

int main(void) {
    /* Bewusst OHNE STRIKT: realistische Produktions-Routing-Entscheidung
     * (Schwelle n>=2^20 fuer Residenz, wie im echten Trainingspfad). Der
     * bw_div-db-Zweig der Komposition ist CPU-only und wuerde unter STRIKT
     * werfen — genau die Asymmetrie, die WEG 2 vermeidet. */
    printf("KIP-G4d-EVAL Benchmark: fused rmsnorm_kern vs. Op-Komposition (Fwd+Bwd)\n\n");
    lauf(   8, 1024,  200);   /* klein: unter Schwelle, beide CPU — Tape-Overhead-Vergleich */
    lauf(1024, 1024,   50);   /* n=2^20: Residenz-Schwelle erreicht */
    lauf(4096, 1024,   20);   /* n=2^22: klar resident */
    if (moo_error_flag) { fprintf(stderr, "FEHLER-FLAG gesetzt — Ergebnis ungueltig\n"); return 1; }
    printf("fertig.\n");
    return 0;
}
