/**
 * test_ki_gpu_gather.c — KIP-G3d-d Gate: GPU gather (Embedding-Lookup) +
 * DETERMINISTISCHE scatter-add (gather-Backward) vs CPU-Referenz.
 *
 * gather = reine Kopie -> BIT-EXAKT. scatter-add akkumuliert je Segment in
 * aufsteigender i-Reihenfolge (identisch zu moo_autograd.c bw_gather) -> auch
 * BIT-EXAKT vs CPU, inkl. Duplikat-Indizes. Zusaetzlich: Determinismus-Gate
 * (G0 §2) — zwei scatter-Laeufe liefern bit-identische Ergebnisse (KEIN
 * atomicAdd). SKIP ohne Vulkan; ohne ASan.
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

#define ROWS  20
#define DIM   8
#define VOCAB 6
#define OUTN  (ROWS * DIM)     /* 160 */
#define WN    (VOCAB * DIM)    /* 48 */
#define BYMAX ((int64_t)OUTN * 4)

static int abw_exakt(const float* x, const float* y, int n) {
    int c = 0; for (int i = 0; i < n; i++) if (x[i] != y[i]) c++; return c;
}
static void cpu_gather(const float* w, const float* idx, float* o, int rows, int dim) {
    for (int i = 0; i < rows; i++) { int v = (int)idx[i];
        for (int d = 0; d < dim; d++) o[i * dim + d] = w[v * dim + d]; }
}
static void cpu_scatter(const float* g, const float* idx, float* gw, int rows, int dim, int vocab) {
    for (int k = 0; k < vocab * dim; k++) gw[k] = 0.0f;
    for (int i = 0; i < rows; i++) { int v = (int)idx[i];
        for (int d = 0; d < dim; d++) gw[v * dim + d] += g[i * dim + d]; }
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3d-d GPU gather + deterministische scatter-add ==\n");

    void* hW   = moo_ki_gpu_buf_belegen(BYMAX);
    void* hIdx = moo_ki_gpu_buf_belegen(BYMAX);
    void* hG   = moo_ki_gpu_buf_belegen(BYMAX);
    void* hO   = moo_ki_gpu_buf_belegen(BYMAX);
    void* hGw  = moo_ki_gpu_buf_belegen(BYMAX);
    void* hGw2 = moo_ki_gpu_buf_belegen(BYMAX);
    CHECK(hW && hIdx && hG && hO && hGw && hGw2, "6 residente Buffers belegt");
    moo_ki_gpu_telemetrie_reset();

    float w[WN], idx[ROWS], g[OUTN], gpu[OUTN], gpu2[OUTN], ref[OUTN];
    for (int i = 0; i < WN; i++) w[i] = 0.3f * (float)i - 5.0f;
    for (int i = 0; i < OUTN; i++) g[i] = 0.5f * sinf(0.13f * (float)i) + 0.2f;
    /* Indizes MIT Duplikaten: vocab 3 kommt mehrfach, vocab 5 gar nicht. */
    int idxv[ROWS] = { 0,1,2,3,3,1,0,2,4,3, 1,0,3,2,4,3,1,0,2,3 };
    for (int i = 0; i < ROWS; i++) idx[i] = (float)idxv[i];

    bool up = moo_ki_gpu_upload(hW, w, (int64_t)WN * 4)
           && moo_ki_gpu_upload(hIdx, idx, (int64_t)ROWS * 4)
           && moo_ki_gpu_upload(hG, g, (int64_t)OUTN * 4);
    CHECK(up, "Upload W + idx + grad");

    /* ---------------- gather Forward ---------------- */
    cpu_gather(w, idx, ref, ROWS, DIM);
    bool gf = moo_ki_gpu_gather_res(hW, hIdx, hO, ROWS, DIM, VOCAB)
           && moo_ki_gpu_download(hO, gpu, (int64_t)OUTN * 4);
    CHECK(gf && abw_exakt(gpu, ref, OUTN) == 0, "gather out[i,d]=W[idx[i],d] == CPU (bit-exakt)");

    /* ---------------- scatter-add Backward (inkl. Duplikate) ------------- */
    cpu_scatter(g, idx, ref, ROWS, DIM, VOCAB);
    bool sb = moo_ki_gpu_scatter_add_res(hG, hIdx, hGw, ROWS, DIM, VOCAB)
           && moo_ki_gpu_download(hGw, gpu, (int64_t)WN * 4);
    CHECK(sb && abw_exakt(gpu, ref, WN) == 0, "scatter-add (Duplikat-Indizes) == CPU (bit-exakt)");

    /* ---------------- Determinismus: 2. Lauf bit-identisch (G0 §2) ------- */
    bool sb2 = moo_ki_gpu_scatter_add_res(hG, hIdx, hGw2, ROWS, DIM, VOCAB)
            && moo_ki_gpu_download(hGw2, gpu2, (int64_t)WN * 4);
    CHECK(sb2 && abw_exakt(gpu, gpu2, WN) == 0, "scatter-add 2. Lauf BIT-IDENTISCH (deterministisch)");

    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");
    /* gather 1 + scatter 2 = 3 Compute-Submits */
    CHECK(tel.submits == 3, "genau 3 Compute-Submits (gather 1 + scatter 2)");

    moo_ki_gpu_buf_freigeben(hW);  moo_ki_gpu_buf_freigeben(hIdx);
    moo_ki_gpu_buf_freigeben(hG);  moo_ki_gpu_buf_freigeben(hO);
    moo_ki_gpu_buf_freigeben(hGw); moo_ki_gpu_buf_freigeben(hGw2);
    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
