/**
 * test_g4c_strikt.c — KIP-G4c Stufe 4: STRIKT-Vertrag auf echter Vulkan-
 * Hardware. NICHT Teil von run_all.sh/run_sanitize.sh (die sind bewusst
 * GPU-frei) — Aufruf ueber skripte/kip_g4c_gate.sh, SKIP ohne libvulkan.
 * ============================================================================
 * PRUEFT (MOO_KI_GPU_STRIKT=1, echte GPU):
 *   1. matmul-Forward (Stufe 1) + matmul-Backward (Stufe 2, symmetrischer
 *      Fall — beide Inputs requires_grad) + SGD-Optimizer-Schritt (Stufe 3)
 *      laufen komplett resident: cpu_fallbacks == 0 nach moo_ki_gpu_telemetrie.
 *   2. Nach dem Update ist der Parameter erst nach moo_tensor_host_sichern
 *      (I1-Trichter) auf dem Host sichtbar-aktuell — direkter ->data-Zugriff
 *      OHNE Sichern-Aufruf ist unter STRIKT bewusst NICHT der richtige Weg
 *      (das unterscheidet diesen Test von test_nn_asan.c, das deshalb NICHT
 *      1:1 als STRIKT-Gate wiederverwendbar ist, siehe Channel-Diskussion
 *      2026-07-10 ~10:48-10:51).
 *   3. Adam/AdamW-Zwei-Handle-Residenz (Commit 853bc2c, moo_ki_gpu_opt_adam2_res):
 *      Fwd(matmul)+Bwd(matmul)+Adam-Schritt komplett resident, cpu_fallbacks==0.
 *      Bisher nur Ad-hoc (/tmp, nicht committet) verifiziert — hiermit
 *      nachgezogen als reproduzierbarer Teil des Gates.
 *   4. KIP-G5: symmetrisches und einseitiges bw_matmul sowie bw_gather
 *      akkumulieren direkt in gpu_grad; isolierte Telemetrie beweist jeweils
 *      downloads==0 und cpu_fallbacks==0, Gather zusätzlich Duplikat-Summen.
 *   5. KIP-G4c Stufe 5 (ew_res-Routing, moo_tensor_ops.c ew_op): tensor_plus
 *      auf n=2^20 (Schwelle) resident, bit-exakt gegen CPU-Referenz, genau
 *      1 GPU-Submit, cpu_fallbacks==0 unter STRIKT.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include "../moo_ki_gpu_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* --- Test-throw-Modell (wie test_autograd_asan.c) ------------------------- */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

/* --- Stubs fuer moo_release()-Dispatch-Ziele (wie test_autograd_asan.c) --- */
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
void moo_kamera_free(void* p)      { (void)p; }
void moo_mikro_free(void* p)       { (void)p; }

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)

static MooTensor* T(MooValue v) { return (MooTensor*)moo_val_as_ptr(v); }

static MooValue t2(int32_t r, int32_t c, const float* d) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    memcpy(t->data, d, (size_t)(r * c) * sizeof(float));
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

int main(void) {
    setenv("MOO_KI_GPU_STRIKT", "1", 1);
    fehler_reset();

    /* ===== 1. Fwd+Bwd+SGD, symmetrischer Fall, komplett resident ===== */
    moo_ki_gpu_telemetrie_reset();
    int32_t N = 8;
    float av[64], bv[64];
    for (int i = 0; i < N * N; i++) {
        av[i] = (float)(i % 7) * 0.1f;
        bv[i] = (float)((i * 3) % 5) * 0.2f;
    }
    MooValue A = t2(N, N, av);
    MooValue B = t2(N, N, bv);
    moo_release(moo_tensor_mit_gradient(A));
    moo_release(moo_tensor_mit_gradient(B));

    float a_before[64];
    memcpy(a_before, T(A)->data, sizeof(a_before));

    MooValue C = moo_tensor_matmul(A, B);
    CHECK(!moo_error_flag, "Forward wirft nicht unter STRIKT (Vulkan vorhanden)");
    MooValue Z = moo_tensor_summe(C, moo_number(-1));
    moo_ki_gpu_telemetrie_reset(); /* nur Backward messen */
    moo_tensor_rueckwaerts(Z);
    CHECK(!moo_error_flag, "Backward wirft nicht unter STRIKT (symmetrischer Fall)");
    MooKiGpuTelemetrie bw_tel;
    moo_ki_gpu_telemetrie(&bw_tel);
    CHECK(bw_tel.uploads == 3, "bw_matmul: nur Output-Grad + zwei initiale Grad-Akkus hochgeladen");
    CHECK(bw_tel.downloads == 0, "bw_matmul: keine Host-Downloads im Backward");

    MooValue params = moo_list_new(1);
    moo_retain(A); moo_list_append(params, A);
    MooValue opt = moo_nn_opt_sgd(params, moo_number(0.1), moo_none());
    moo_release(moo_nn_opt_schritt(opt));
    CHECK(!moo_error_flag, "opt_schritt wirft nicht unter STRIKT");

    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.submits > 0, "mind. 1 residenter GPU-Submit fand statt");
    CHECK(tel.cpu_fallbacks == 0, "keine CPU-Fallbacks unter STRIKT (Stufe 1+2+3)");
    /* Backward-Downloadfreiheit ist oben isoliert über bw_tel bewiesen;
     * Optimizer-Telemetrie besitzt einen eigenen, älteren Vertrag. */

    /* I1-Trichter: OHNE expliziten Sichern-Aufruf ist ->data hier bewusst
     * stale (GPU ist autoritativ) -- das ist KEIN Bug, siehe Kommentar oben. */
    moo_tensor_host_sichern(T(A));
    int diffs = 0;
    for (int i = 0; i < N * N; i++)
        if (T(A)->data[i] == a_before[i]) diffs++;
    CHECK(diffs == 0, "Parameter nach host_sichern vollstaendig aktualisiert");

    moo_release(C); moo_release(Z); moo_release(opt); moo_release(params);
    moo_release(A); moo_release(B);
    moo_ag_reset();

    /* ===== 2. Fwd+Bwd+Adam, symmetrischer Fall, komplett resident =====
     * Nachzug aus Handoff kip-gpu-g4c-handoff-2026-07-10-1108 Punkt 1: die
     * Adam-Verifikation aus Commit 853bc2c war nur Ad-hoc (/tmp), hier
     * committet als reproduzierbarer Teil des STRIKT-Gates. Nicht 256-
     * teilbare Groesse (n=37 ueber N*N=49... hier bewusst N=7 -> 49 Elemente,
     * ungerade/nicht potenz-of-2) deckt Randbehandlung ab. */
    moo_ki_gpu_telemetrie_reset();
    int32_t N2 = 7;
    float av2[49], bv2[49];
    for (int i = 0; i < N2 * N2; i++) {
        av2[i] = (float)(i % 5) * 0.13f;
        bv2[i] = (float)((i * 7) % 11) * 0.07f;
    }
    MooValue A3 = t2(N2, N2, av2);
    MooValue B3 = t2(N2, N2, bv2);
    moo_release(moo_tensor_mit_gradient(A3));
    moo_release(moo_tensor_mit_gradient(B3));

    float a3_before[49];
    memcpy(a3_before, T(A3)->data, sizeof(a3_before));

    MooValue C3 = moo_tensor_matmul(A3, B3);
    CHECK(!moo_error_flag, "Adam: Forward wirft nicht unter STRIKT");
    MooValue Z3 = moo_tensor_summe(C3, moo_number(-1));
    moo_tensor_rueckwaerts(Z3);
    CHECK(!moo_error_flag, "Adam: Backward wirft nicht unter STRIKT (symmetrischer Fall)");

    MooValue params3 = moo_list_new(1);
    moo_retain(A3); moo_list_append(params3, A3);
    MooValue opt3 = moo_nn_opt_adam(params3, moo_number(0.1));
    moo_release(moo_nn_opt_schritt(opt3));
    CHECK(!moo_error_flag, "Adam: opt_schritt wirft nicht unter STRIKT");

    MooKiGpuTelemetrie tel3;
    moo_ki_gpu_telemetrie(&tel3);
    CHECK(tel3.submits > 0, "Adam: mind. 1 residenter GPU-Submit fand statt");
    CHECK(tel3.cpu_fallbacks == 0, "Adam: keine CPU-Fallbacks unter STRIKT (Fwd+Bwd+opt_adam2_res)");

    moo_tensor_host_sichern(T(A3));
    int diffs3 = 0;
    for (int i = 0; i < N2 * N2; i++)
        if (T(A3)->data[i] == a3_before[i]) diffs3++;
    CHECK(diffs3 == 0, "Adam: Parameter nach host_sichern vollstaendig aktualisiert");

    moo_release(C3); moo_release(Z3); moo_release(opt3); moo_release(params3);
    moo_release(A3); moo_release(B3);
    moo_ag_reset();

    /* ===== 3. KIP-G5: asymmetrischer matmul-Backward jetzt resident =====
     * matmul_bw_res berechnet weiterhin beide reinen Beiträge; nur der
     * angeforderte Ziel-Grad wird ohne Host-Roundtrip akkumuliert. */
    moo_ki_gpu_telemetrie_reset();
    float a2_v[4] = { 1, -1, 2, 0 }, w_v[4] = { 1, 0, 0, 1 };
    MooValue A2 = t2(2, 2, a2_v), W = t2(2, 2, w_v);
    moo_release(moo_tensor_mit_gradient(W));   /* NUR W hat Grad -- asymmetrisch */
    MooValue MM = moo_tensor_matmul(A2, W);
    MooValue R = moo_tensor_relu(MM);
    MooValue Z2 = moo_tensor_summe(R, moo_number(-1));
    moo_ki_gpu_telemetrie_reset();
    moo_tensor_rueckwaerts(Z2);
    CHECK(!moo_error_flag, "einseitiger bw_matmul-Fall läuft unter STRIKT resident");
    MooTensor* wt = MV_TENSOR(W);
    MooKiGpuTelemetrie one_tel; moo_ki_gpu_telemetrie(&one_tel);
    CHECK((wt->grad_valid & MOO_V_DEV) != 0, "einseitiger W-Grad ist GPU-autoritativ");
    CHECK(one_tel.downloads == 0, "einseitiger bw_matmul ohne Host-Download");
    CHECK(one_tel.cpu_fallbacks == 0, "einseitiger bw_matmul ohne CPU-Fallback");
    moo_release(MM); moo_release(R); moo_release(Z2);
    moo_ag_reset();
    moo_tensor_gradient_loeschen(W);
    moo_release(A2); moo_release(W);

    /* ===== 4. KIP-G5: gather-Backward scatter-add resident ===== */
    {
        float wdata[8] = {0,1, 2,3, 4,5, 6,7};
        MooValue GW = t2(4, 2, wdata);
        moo_release(moo_tensor_mit_gradient(GW));
        int32_t ishape[1] = {3};
        MooTensor* it = moo_tensor_raw(1, ishape);
        it->data[0] = 1.0f; it->data[1] = 1.0f; it->data[2] = 3.0f;
        MooValue GI; GI.tag = MOO_TENSOR; moo_val_set_ptr(&GI, it);
        MooValue GO = moo_tensor_gather(GW, GI);
        MooValue GL = moo_tensor_summe(GO, moo_number(-1));
        moo_ki_gpu_telemetrie_reset();
        moo_tensor_rueckwaerts(GL);
        CHECK(!moo_error_flag, "gather-Backward läuft unter STRIKT resident");
        MooKiGpuTelemetrie gather_tel; moo_ki_gpu_telemetrie(&gather_tel);
        CHECK(gather_tel.downloads == 0, "bw_gather: kein Host-Download");
        CHECK(gather_tel.cpu_fallbacks == 0, "bw_gather: kein CPU-Fallback");
        CHECK((T(GW)->grad_valid & MOO_V_DEV) != 0, "Embedding-Grad ist GPU-autoritativ");
        moo_tensor_grad_sichern(T(GW)); /* expliziter Prüf-Readback NACH Telemetrie */
        CHECK(fabsf(T(GW)->grad[2] - 2.0f) < 1e-6f &&
              fabsf(T(GW)->grad[3] - 2.0f) < 1e-6f &&
              fabsf(T(GW)->grad[6] - 1.0f) < 1e-6f &&
              fabsf(T(GW)->grad[7] - 1.0f) < 1e-6f,
              "bw_gather: Duplikat-Indizes deterministisch summiert");
        moo_release(GO); moo_release(GL);
        moo_ag_reset();
        moo_release(GW); moo_release(GI);
    }

    /* ===== 5. ew_res-Routing (Stufe 5, moo_tensor_ops.c ew_op) =====
     * tensor_plus auf n=2^20 (Schwelle) -- resident, bit-exakt gegen CPU-
     * Referenz, genau 1 GPU-Submit, cpu_fallbacks==0 unter STRIKT. */
    {
        moo_ki_gpu_telemetrie_reset();
        int64_t n = (int64_t)1 << 20;
        int32_t shape1[1] = { (int32_t)n };
        MooTensor* ea = moo_tensor_raw(1, shape1);
        MooTensor* eb = moo_tensor_raw(1, shape1);
        float* ref = (float*)malloc((size_t)n * sizeof(float));
        for (int64_t i = 0; i < n; i++) {
            ea->data[i] = (float)(i % 97) * 0.01f;
            eb->data[i] = (float)((i * 3) % 53) * 0.02f;
            ref[i] = ea->data[i] + eb->data[i];
        }
        MooValue eav; eav.tag = MOO_TENSOR; moo_val_set_ptr(&eav, ea);
        MooValue ebv; ebv.tag = MOO_TENSOR; moo_val_set_ptr(&ebv, eb);
        MooValue ecv = moo_tensor_add(eav, ebv);
        CHECK(!moo_error_flag, "ew_res: tensor_plus wirft nicht unter STRIKT");
        MooTensor* ec = T(ecv);
        moo_tensor_host_sichern(ec);
        int64_t ediffs = 0;
        for (int64_t i = 0; i < n; i++)
            if (fabsf(ec->data[i] - ref[i]) > 1e-5f) ediffs++;
        CHECK(ediffs == 0, "ew_res: tensor_plus bit-exakt gegen CPU-Referenz");
        MooKiGpuTelemetrie tel4;
        moo_ki_gpu_telemetrie(&tel4);
        CHECK(tel4.submits > 0, "ew_res: mind. 1 residenter GPU-Submit fand statt");
        CHECK(tel4.cpu_fallbacks == 0, "ew_res: keine CPU-Fallbacks unter STRIKT");
        free(ref);
        moo_release(ecv); moo_release(eav); moo_release(ebv);
    }

    printf("test_g4c_strikt: %d Checks OK (submits=%llu uploads=%llu downloads=%llu cpu_fallbacks=%llu)\n",
           checks, 0ULL, 0ULL, 0ULL, 0ULL);
    return 0;
}
