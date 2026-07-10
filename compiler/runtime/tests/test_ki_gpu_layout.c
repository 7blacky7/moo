/**
 * test_ki_gpu_layout.c — KIP-G3d-c Gate: GPU Layout-Ops transpose/reshape/concat.
 *
 * Beweist je Op Forward UND Backward auf residenten Buffers vs CPU-Referenz
 * (moo_tensor_ops.c transponieren/umformen/verbinden + moo_autograd.c
 * bw_transpose/bw_reshape/bw_concat). Reine Daten-Bewegungen (Kopie/
 * Permutation, KEINE Arithmetik) -> Vergleich ist BIT-EXAKT (gpu[i]==cpu[i]).
 * Ohne echte Vulkan-Hardware transparenter SKIP.
 *
 * Kernels: transpose.comp (2D; Fwd + Bwd via vertauschte Dims) und copy.comp
 * (o[dst_off+i]=a[src_off+i]; reshape=Offsets 0, concat=Block-Offsets).
 * Dims bewusst keine Vielfachen von 16/256 -> Rand-Guards mitgetestet.
 * Ohne ASan (NVIDIA-Leak-Noise), wie ki-gpu-smoke.sh.
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

#define TR   30                 /* transpose: Zeilen */
#define TC   20                 /* transpose: Spalten */
#define TSZ  (TR * TC)          /* 600 */
#define CA   12                 /* concat: Zeilen a */
#define CB   7                  /* concat: Zeilen b */
#define CC   20                 /* concat: Spalten (gleich) */
#define ASZ  (CA * CC)          /* 240 */
#define BSZ  (CB * CC)          /* 140 */
#define OSZ  ((CA + CB) * CC)   /* 380 */
#define MAXN 600
#define BY   ((int64_t)MAXN * 4)

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) {
        printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n");
        return 0;
    }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3d-c Layout-Ops transpose/reshape/concat (Fwd+Bwd) ==\n");

    void* hA = moo_ki_gpu_buf_belegen(BY);
    void* hB = moo_ki_gpu_buf_belegen(BY);
    void* hC = moo_ki_gpu_buf_belegen(BY);
    void* hD = moo_ki_gpu_buf_belegen(BY);
    void* hE = moo_ki_gpu_buf_belegen(BY);
    CHECK(hA && hB && hC && hD && hE, "5 residente Buffers belegt");
    moo_ki_gpu_telemetrie_reset();

    float gpu[MAXN], ref[MAXN];

    /* ---------------- transpose ---------------- */
    float a[TSZ];
    for (int i = 0; i < TSZ; i++) a[i] = (float)i * 0.5f - 3.0f;
    for (int i = 0; i < TR; i++)
        for (int j = 0; j < TC; j++) ref[j * TR + i] = a[i * TC + j];
    bool tf = moo_ki_gpu_upload(hA, a, (int64_t)TSZ * 4)
           && moo_ki_gpu_transpose_res(hA, hB, TR, TC)
           && moo_ki_gpu_download(hB, gpu, (int64_t)TSZ * 4);
    int dtf = 0; for (int i = 0; i < TSZ; i++) if (gpu[i] != ref[i]) dtf++;
    CHECK(tf && dtf == 0, "transpose FWD == CPU (bit-exakt)");

    float gout[TSZ];
    for (int i = 0; i < TSZ; i++) gout[i] = (float)(TSZ - i) * 0.25f;
    /* bw: gin[R,C], gin[i*C+j] = gout[j*R+i]  ==  transpose(gout[C,R]) */
    for (int i = 0; i < TR; i++)
        for (int j = 0; j < TC; j++) ref[i * TC + j] = gout[j * TR + i];
    bool tb = moo_ki_gpu_upload(hC, gout, (int64_t)TSZ * 4)
           && moo_ki_gpu_transpose_res(hC, hD, TC, TR)
           && moo_ki_gpu_download(hD, gpu, (int64_t)TSZ * 4);
    int dtb = 0; for (int i = 0; i < TSZ; i++) if (gpu[i] != ref[i]) dtb++;
    CHECK(tb && dtb == 0, "transpose GRAD == CPU (bit-exakt)");

    /* ---------------- reshape (copy, Offsets 0) ---------------- */
    float rin[MAXN];
    for (int i = 0; i < MAXN; i++) rin[i] = (float)(i % 97) - 40.0f;
    bool rf = moo_ki_gpu_upload(hA, rin, BY)
           && moo_ki_gpu_copy_res(hA, hB, MAXN, 0, 0)
           && moo_ki_gpu_download(hB, gpu, BY);
    int drf = 0; for (int i = 0; i < MAXN; i++) if (gpu[i] != rin[i]) drf++;
    CHECK(rf && drf == 0, "reshape FWD == CPU (copy, bit-exakt)");
    bool rb = moo_ki_gpu_copy_res(hA, hC, MAXN, 0, 0)
           && moo_ki_gpu_download(hC, gpu, BY);
    int drb = 0; for (int i = 0; i < MAXN; i++) if (gpu[i] != rin[i]) drb++;
    CHECK(rb && drb == 0, "reshape GRAD == CPU (copy, bit-exakt)");

    /* ---------------- concat (Achse 0) ---------------- */
    float ca[ASZ], cb[BSZ], cat[OSZ];
    for (int i = 0; i < ASZ; i++) ca[i] = (float)i + 0.1f;
    for (int i = 0; i < BSZ; i++) cb[i] = -(float)i - 0.2f;
    for (int i = 0; i < ASZ; i++) cat[i] = ca[i];
    for (int i = 0; i < BSZ; i++) cat[ASZ + i] = cb[i];
    bool cf = moo_ki_gpu_upload(hA, ca, (int64_t)ASZ * 4)
           && moo_ki_gpu_upload(hB, cb, (int64_t)BSZ * 4)
           && moo_ki_gpu_copy_res(hA, hC, ASZ, 0, 0)
           && moo_ki_gpu_copy_res(hB, hC, BSZ, 0, ASZ)
           && moo_ki_gpu_download(hC, gpu, (int64_t)OSZ * 4);
    int dcf = 0; for (int i = 0; i < OSZ; i++) if (gpu[i] != cat[i]) dcf++;
    CHECK(cf && dcf == 0, "concat FWD == CPU (bit-exakt)");

    float cg[OSZ], gpu_b[BSZ];
    for (int i = 0; i < OSZ; i++) cg[i] = (float)(i * 2 - 100) * 0.5f;
    /* bw: gin_a = gout[0..ASZ), gin_b = gout[ASZ..OSZ) */
    bool cbw = moo_ki_gpu_upload(hC, cg, (int64_t)OSZ * 4)
            && moo_ki_gpu_copy_res(hC, hD, ASZ, 0, 0)
            && moo_ki_gpu_copy_res(hC, hE, BSZ, ASZ, 0)
            && moo_ki_gpu_download(hD, gpu, (int64_t)ASZ * 4)
            && moo_ki_gpu_download(hE, gpu_b, (int64_t)BSZ * 4);
    int dca = 0; for (int i = 0; i < ASZ; i++) if (gpu[i] != cg[i]) dca++;
    int dcb = 0; for (int i = 0; i < BSZ; i++) if (gpu_b[i] != cg[ASZ + i]) dcb++;
    CHECK(cbw && dca == 0 && dcb == 0, "concat GRAD (Split a|b) == CPU (bit-exakt)");

    MooKiGpuTelemetrie t;
    moo_ki_gpu_telemetrie(&t);
    CHECK(t.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");
    /* 2 transpose + 2 reshape-copy + 4 concat-copy = 8 Compute-Submits */
    CHECK(t.submits == 8, "genau 8 Compute-Submits (transpose 2 + reshape 2 + concat 4)");

    moo_ki_gpu_buf_freigeben(hA); moo_ki_gpu_buf_freigeben(hB);
    moo_ki_gpu_buf_freigeben(hC); moo_ki_gpu_buf_freigeben(hD);
    moo_ki_gpu_buf_freigeben(hE);
    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
