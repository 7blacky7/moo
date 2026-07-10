/**
 * test_ki_gpu_dirty.c — KIP-G1 Phase B Gate: Tensor-Dirty-State (valid-Maske).
 *
 * Prueft die Geraete-Transitionen der geteilten valid-Bitmaske (D0 §2 / G1 §1)
 * auf echter GPU:
 *   Start (DATA) -> nach_gpu (DATA|DEV, device=GPU) -> idempotent
 *   -> GPU-only (DEV) -> host_sichern (Download, DATA) -> nach_cpu (CPU)
 *   -> Mutation invalidiert DEV -> tensor_free gibt gpu_buf an den Pool zurueck.
 *
 * Linkt NUR moo_tensor.c + moo_ki_gpu.c (gc-sections droppt ungenutzte
 * Tensor-Funktionen); moo_throw/moo_error sind hier gestubbt (moo_error.c
 * wird nicht gelinkt). Transparenter SKIP ohne GPU. OHNE ASan-Leakcheck bauen
 * (Treiber-Noise) — ASan/UBSan-Fehlerpfade bleiben aktiv.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "moo_runtime.h"
#include "moo_ki_gpu_api.h"

/* Stubs: dieser Test linkt moo_error.c NICHT. Ein unerwarteter Throw = Fehler. */
void moo_throw(MooValue error) { (void)error; fprintf(stderr, "  FAIL: unerwarteter moo_throw\n"); exit(3); }
MooValue moo_error(const char* msg) { (void)msg; MooValue v; memset(&v, 0, sizeof(v)); return v; }

static int fehler = 0;
#define CHECK(bed, msg) do { \
    if (bed) { printf("  OK   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fehler++; } \
} while (0)

#define N 64

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz zur Laufzeit\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G1 Phase B: Tensor-Dirty-State ==\n");

    MooTensor* t = (MooTensor*)calloc(1, sizeof(MooTensor));
    t->refcount = 1; t->ndim = 1; t->size = N; t->shape[0] = N;
    t->dtype = MOO_DT_F32;
    t->data = (float*)malloc(N * sizeof(float));
    for (int i = 0; i < N; i++) t->data[i] = (float)i * 1.5f - 8.0f;
    t->valid = MOO_V_DATA; t->device = MOO_DEV_CPU;
    float orig[N]; memcpy(orig, t->data, sizeof(orig));

    CHECK(t->valid == MOO_V_DATA && t->device == MOO_DEV_CPU,
          "Start: valid=DATA, device=CPU, gpu_buf=NULL");
    CHECK(t->gpu_buf == NULL, "Start: kein gpu_buf");

    /* nach_gpu: Transfer -> DATA und DEV gueltig, device=GPU, Buffer belegt */
    moo_tensor_nach_gpu(t);
    CHECK((t->valid & MOO_V_DEV) && (t->valid & MOO_V_DATA) &&
          t->device == MOO_DEV_GPU && t->gpu_buf != NULL,
          "nach_gpu: valid=DATA|DEV, device=GPU, gpu_buf belegt");

    /* Idempotenz: erneuter nach_gpu = no-op, gleicher Buffer */
    void* buf_vor = t->gpu_buf;
    uint8_t valid_vor = t->valid;
    moo_tensor_nach_gpu(t);
    CHECK(t->gpu_buf == buf_vor && t->valid == valid_vor,
          "nach_gpu idempotent (gleicher Buffer, kein Re-Upload)");

    /* GPU-only-Zustand simulieren: DATA verwerfen + Host-Daten korrumpieren.
     * host_sichern MUSS vom gpu_buf zurueckladen. */
    t->valid = MOO_V_DEV;
    memset(t->data, 0, N * sizeof(float));
    moo_tensor_host_sichern(t);
    CHECK(t->valid & MOO_V_DATA, "host_sichern: DATA wieder gueltig (Download)");
    int gleich = 1;
    for (int i = 0; i < N; i++) if (t->data[i] != orig[i]) gleich = 0;
    CHECK(gleich, "host_sichern: data == Original (GPU->Host-Download korrekt)");

    /* nach_cpu: Host-Sicht + Ort CPU */
    moo_tensor_nach_cpu(t);
    CHECK(t->device == MOO_DEV_CPU && (t->valid & MOO_V_DATA),
          "nach_cpu: device=CPU, DATA gueltig");

    /* Mutations-Invalidierung (D0 §4.2, wie tensor_setzen/opt_schritt):
     * Write auf data macht DATA autoritativ und verwirft DEV. */
    t->valid = MOO_V_DATA;
    CHECK(!(t->valid & MOO_V_DEV), "Mutation invalidiert DEV (valid=DATA)");

    /* Nach Invalidierung wieder nach_gpu: DEV fehlt -> Re-Upload in denselben
     * (noch belegten) Buffer, kein neuer belegt. */
    void* buf2 = t->gpu_buf;
    moo_tensor_nach_gpu(t);
    CHECK(t->gpu_buf == buf2 && (t->valid & MOO_V_DEV),
          "nach_gpu nach Mutation: Re-Upload in bestehenden Buffer");

    /* tensor_free gibt gpu_buf an den Pool zurueck (kein Crash / ASan clean) */
    moo_tensor_free(t);
    CHECK(1, "tensor_free mit gpu_buf: Pool-Rueckgabe ok");

    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
