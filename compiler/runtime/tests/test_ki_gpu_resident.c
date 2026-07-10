/**
 * test_ki_gpu_resident.c — KIP-G1 Gate: GPU-residente Buffers.
 *
 * Beweist drei Dinge auf echter Vulkan-Hardware (sonst transparenter SKIP):
 *  (1) DIFFERENTIAL: eine Op-Kette (matmul -> add/sub/mul/div -> sum/mean)
 *      auf RESIDENTEN Buffers liefert dasselbe wie die CPU-Referenz.
 *  (2) SUBMIT-ZAEHLER: die Kette macht GENAU 1 Compute-Submit pro Op und
 *      ZWISCHEN den Ops KEINEN Host<->Device-Transfer (uploads nur am
 *      Anfang, downloads nur am Ende) — Residenz ist gemessen, nicht behauptet.
 *  (3) SLOT-REUSE: belegen/freigeben/erneut-belegen liefert unabhaengige
 *      Buffers ohne Alias/Stale-Daten (G1 §2 Slot-Reuse-Sicherheit).
 *
 * Baut ohne Autograd/Tensor-Schicht — nur gegen moo_ki_gpu_api.h.
 * OHNE ASan bauen (NVIDIA-Treiber erzeugt Leak-Noise), wie ki-gpu-smoke.sh.
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

#define N   16                 /* Matrix-Kante */
#define SZ  (N * N)            /* Elemente pro Tensor */
#define BY  ((int64_t)SZ * 4)  /* Bytes */

static bool nahe(float x, float y) {
    return fabsf(x - y) <= 1e-2f + 1e-3f * fabsf(y);
}

/* CPU-Referenzen (identische Semantik zu den Shadern) */
static void cpu_matmul(const float* a, const float* b, float* o) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            float s = 0.0f;
            for (int i = 0; i < N; i++) s += a[r * N + i] * b[i * N + c];
            o[r * N + c] = s;
        }
}
static void cpu_ew(int op, const float* a, const float* b, float* o) {
    for (int i = 0; i < SZ; i++) {
        float x = a[i], y = b[i];
        o[i] = (op == 0) ? x + y : (op == 1) ? x - y
             : (op == 2) ? x * y : x / y;
    }
}

int main(void) {
    /* Verfuegbarkeit pruefen: keine GPU-Residenz => SKIP (kein Beweis, aber
     * kein Fehler; run_all bleibt GPU-frei, dieses Script ist das Gate). */
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) {
        printf("SKIP: keine GPU-Residenz zur Laufzeit (kein Vulkan/keine GPU)\n");
        return 0;
    }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G1 residente Buffers ==\n");

    /* Deterministische Eingaben. C strikt > 0 (Division sicher). */
    float A[SZ], B[SZ], C[SZ];
    for (int i = 0; i < SZ; i++) {
        A[i] = (float)((i % 7) - 3) * 0.5f;         /* [-1.5, 1.5] */
        B[i] = (float)((i % 5) - 2) * 0.4f;         /* [-0.8, 0.8] */
        C[i] = (float)((i % 4) + 1) * 0.25f;        /* [0.25, 1.0] */
    }

    /* CPU-Referenzkette: O1=A@B, O2=O1+C, O3=O2-C, O4=O3*C, O5=O4/C */
    float O1[SZ], O2[SZ], O3[SZ], O4[SZ], O5[SZ];
    cpu_matmul(A, B, O1);
    cpu_ew(0, O1, C, O2);
    cpu_ew(1, O2, C, O3);
    cpu_ew(2, O3, C, O4);
    cpu_ew(3, O4, C, O5);
    double cpu_summe = 0.0;
    for (int i = 0; i < SZ; i++) cpu_summe += (double)O5[i];
    double cpu_mittel = cpu_summe / (double)SZ;

    /* --- GPU: residente Kette --- */
    void* hA = moo_ki_gpu_buf_belegen(BY);
    void* hB = moo_ki_gpu_buf_belegen(BY);
    void* hC = moo_ki_gpu_buf_belegen(BY);
    void* h1 = moo_ki_gpu_buf_belegen(BY);
    void* h2 = moo_ki_gpu_buf_belegen(BY);
    void* h3 = moo_ki_gpu_buf_belegen(BY);
    void* h4 = moo_ki_gpu_buf_belegen(BY);
    void* h5 = moo_ki_gpu_buf_belegen(BY);
    CHECK(hA && hB && hC && h1 && h2 && h3 && h4 && h5, "8 residente Buffers belegt");

    moo_ki_gpu_telemetrie_reset();
    bool up = moo_ki_gpu_upload(hA, A, BY)
           && moo_ki_gpu_upload(hB, B, BY)
           && moo_ki_gpu_upload(hC, C, BY);
    CHECK(up, "Eingaben A/B/C hochgeladen");

    MooKiGpuTelemetrie t0;
    moo_ki_gpu_telemetrie(&t0);
    CHECK(t0.uploads == 3 && t0.submits == 0 && t0.downloads == 0,
          "nach Upload: uploads=3, submits=0, downloads=0");

    /* Op-Kette — KEINE Transfers dazwischen */
    bool ops = moo_ki_gpu_matmul_res(hA, hB, h1, N, N, N)
            && moo_ki_gpu_ew_res(0, h1, hC, h2, SZ)
            && moo_ki_gpu_ew_res(1, h2, hC, h3, SZ)
            && moo_ki_gpu_ew_res(2, h3, hC, h4, SZ)
            && moo_ki_gpu_ew_res(3, h4, hC, h5, SZ);
    CHECK(ops, "residente Op-Kette (matmul + add/sub/mul/div) ausgefuehrt");

    MooKiGpuTelemetrie t1;
    moo_ki_gpu_telemetrie(&t1);
    /* DER RESIDENZ-BEWEIS: 5 Compute-Submits, uploads unveraendert (3),
     * KEINE downloads zwischen den Ops. */
    CHECK(t1.submits == 5, "genau 5 Compute-Submits (1 pro Op)");
    CHECK(t1.uploads == 3, "KEINE zusaetzlichen Uploads waehrend der Kette");
    CHECK(t1.downloads == 0, "KEINE Downloads zwischen den Ops (resident)");

    float gpu_O5[SZ];
    bool dn = moo_ki_gpu_download(h5, gpu_O5, BY);
    CHECK(dn, "Endergebnis heruntergeladen");

    int diffs = 0;
    for (int i = 0; i < SZ; i++) if (!nahe(gpu_O5[i], O5[i])) diffs++;
    CHECK(diffs == 0, "GPU-Kette == CPU-Referenz (elementweise)");

    /* Reduktion auf residentem Ergebnis: sum + mean */
    double gpu_summe = 0.0;
    bool rd = moo_ki_gpu_reduce_sum_res(h5, SZ, &gpu_summe);
    CHECK(rd, "residente Voll-Reduktion ausgefuehrt");
    double gpu_mittel = gpu_summe / (double)SZ;
    CHECK(fabs(gpu_summe - cpu_summe) <= 1e-2 + 1e-3 * fabs(cpu_summe),
          "GPU-sum == CPU-sum");
    CHECK(fabs(gpu_mittel - cpu_mittel) <= 1e-3 + 1e-3 * fabs(cpu_mittel),
          "GPU-mean == CPU-mean");

    MooKiGpuTelemetrie t2;
    moo_ki_gpu_telemetrie(&t2);
    /* 6 Submits (5 + reduce), Downloads: 1 (Endergebnis) + 1 (Reduce-Partials) */
    CHECK(t2.submits == 6, "6 Compute-Submits gesamt (Kette + Reduktion)");
    CHECK(t2.downloads == 2, "genau 2 Downloads (Ergebnis + Reduce-Partials)");
    CHECK(t2.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");

    moo_ki_gpu_buf_freigeben(hA); moo_ki_gpu_buf_freigeben(hB);
    moo_ki_gpu_buf_freigeben(hC); moo_ki_gpu_buf_freigeben(h1);
    moo_ki_gpu_buf_freigeben(h2); moo_ki_gpu_buf_freigeben(h3);
    moo_ki_gpu_buf_freigeben(h4); moo_ki_gpu_buf_freigeben(h5);

    /* --- Slot-Reuse-Regression --- */
    float patA[SZ], patB[SZ], out[SZ];
    for (int i = 0; i < SZ; i++) { patA[i] = (float)i; patB[i] = -1.0f - (float)i; }

    void* x = moo_ki_gpu_buf_belegen(BY);
    CHECK(x != NULL, "Slot-Reuse: erster Buffer belegt");
    memset(out, 0, sizeof(out));
    bool r1 = moo_ki_gpu_upload(x, patA, BY) && moo_ki_gpu_download(x, out, BY);
    int okA = 1; for (int i = 0; i < SZ; i++) if (!nahe(out[i], patA[i])) okA = 0;
    CHECK(r1 && okA, "Slot-Reuse: patA korrekt round-trip");
    moo_ki_gpu_buf_freigeben(x);                 /* Slot geht zurueck in den Pool */

    void* y = moo_ki_gpu_buf_belegen(BY);        /* re-belegen: reuse wahrscheinlich */
    CHECK(y != NULL, "Slot-Reuse: zweiter Buffer belegt (Pool-Slot recycelt)");
    memset(out, 0, sizeof(out));
    bool r2 = moo_ki_gpu_upload(y, patB, BY) && moo_ki_gpu_download(y, out, BY);
    int okB = 1; for (int i = 0; i < SZ; i++) if (!nahe(out[i], patB[i])) okB = 0;
    CHECK(r2 && okB, "Slot-Reuse: patB unabhaengig, KEINE Stale-Daten von patA");

    /* zweiter gleichzeitiger Buffer -> muss verschieden von y sein */
    void* z = moo_ki_gpu_buf_belegen(BY);
    CHECK(z != NULL && z != y, "Slot-Reuse: paralleler Buffer != recycelter");
    moo_ki_gpu_buf_freigeben(y);
    moo_ki_gpu_buf_freigeben(z);

    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
