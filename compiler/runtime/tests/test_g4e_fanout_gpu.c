/**
 * test_g4e_fanout_gpu.c — KIP-G4e Schritt 3: Fan-out-Cross-Residenz-Gate auf
 * echter Vulkan-Hardware (docs/kip/G4e-gpu-grad-residency-vertrag.md §5.3,
 * §7 -- Test-Vorschlag kip-ops Msg 12867, hier von kip-gpu selbst gebaut da
 * es das direkte Korrektheits-Gate fuer den eigenen Schritt-1/2-Umbau ist).
 * NICHT Teil von run_all.sh/run_sanitize.sh (bewusst GPU-frei) -- eigenes
 * Gate, Aufruf manuell/ueber ein kip_g4e_gate.sh-Skript. Ohne libvulkan
 * transparent SKIP (kein Beweis).
 * ============================================================================
 * PRUEFT (MOO_KI_GPU_STRIKT=1, echte GPU):
 *   x [2] ist Fan-out-Ziel in ZWEI bw_mul-Zweigen unterschiedlicher
 *   Residenz-Klasse:
 *     y = mul(x, c)   -- c gleiche Form wie x -> x-Kontribution nimmt den
 *                        NEUEN residenten Pfad (gpu_ew_kontribution2 r==2,
 *                        akkumuliert DIREKT in x->gpu_grad, I8).
 *     z = mul(w, x)   -- x wird gegen w [3,2] gebroadcastet (ndim/shape
 *                        weichen ab) -> x-Kontribution nimmt den unveraen-
 *                        derten VOLLEN CPU-Pfad (materialize_bcast +
 *                        accum_bcast_mul -> accum_bcast -> grad_sicherstellen).
 *   loss = summe(y) + summe(z); rueckwaerts(loss).
 *   Das ist GENAU der in Doc §2 beschriebene Seitenwechsel-Fall (Knoten
 *   GPU-resident UND Knoten CPU-resident schreiben abwechselnd in dieselbe
 *   Grad-Zelle) -- der Trichter (grad_materialisieren/grad_materialisieren_gpu)
 *   MUSS in jeder Traversierungsreihenfolge den jeweils anderen Beitrag
 *   korrekt mitnehmen (I8/I9/I12), kein Beitrag darf verloren gehen.
 *
 *   Erwartung (Hand-Referenz): dx = c + spaltensumme(w).
 *   x=[1.5,-2.0], c=[0.5,3.0], w=[[1,2],[3,4],[5,6]] (spaltensumme=[9,12])
 *   -> erwartetes x.grad = [9.5, 15.0] (unabhaengig vom x-Wert selbst, mul
 *      ist linear in x fuer die Ableitung).
 * ============================================================================
 */
#include "../moo_runtime.h"
#include "../moo_ki_gpu_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* --- Test-throw-Modell (wie test_g4c_strikt.c) ---------------------------- */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

/* --- Stubs fuer moo_release()-Dispatch-Ziele (wie test_g4c_strikt.c) ------ */
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

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)

static MooTensor* T(MooValue v) { return (MooTensor*)moo_val_as_ptr(v); }

static MooValue t1(int32_t n, const float* d) {
    int32_t shape[1] = { n };
    MooTensor* t = moo_tensor_raw(1, shape);
    memcpy(t->data, d, (size_t)n * sizeof(float));
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}
static MooValue t2(int32_t r, int32_t c, const float* d) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    memcpy(t->data, d, (size_t)(r * c) * sizeof(float));
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}
#define NAHE(a, b) (fabsf((a) - (b)) < 1e-4f)

int main(void) {
    setenv("MOO_KI_GPU_STRIKT", "1", 1);
    fehler_reset();
    moo_ki_gpu_telemetrie_reset();

    float xv[2] = { 1.5f, -2.0f };
    float cv[2] = { 0.5f, 3.0f };
    float wv[6] = { 1, 2, 3, 4, 5, 6 };   /* [3,2], Spaltensumme = [9,12] */

    MooValue X = t1(2, xv);
    MooValue C = t1(2, cv);
    MooValue W = t2(3, 2, wv);

    /* ===== KIP-FINAL-FIX2 (67efae9e, koordinator2-Gegenreview-Fund) =====
     * Regressionstest: REINER same-shape bw_mul (kein Fan-out, keine CPU-
     * Kontribution) setzt x2.grad_valid=MOO_V_DEV. Die OEFFENTLICHE Export-
     * API moo_tensor_gradient(x2) MUSS trotzdem den echten Wert liefern --
     * vorher lieferte sie (ohne moo_tensor_grad_sichern-Trichter) einen
     * stalen/genullten Host-Puffer ([0,0] statt [0.5,3.0]), obwohl
     * submits>0 und cpu_fallbacks==0 (Bug sah nach Erfolg aus). */
    float x2v[2] = { 1.5f, -2.0f };
    float c2v[2] = { 0.5f, 3.0f };
    MooValue X2 = t1(2, x2v);
    MooValue C2 = t1(2, c2v);
    moo_release(moo_tensor_mit_gradient(X2));
    MooValue Y2 = moo_tensor_mul(X2, C2);
    CHECK(!moo_error_flag, "y2=mul(x2,c2) Forward wirft nicht unter STRIKT");
    MooValue Loss2 = moo_tensor_summe(Y2, moo_number(-1));
    moo_tensor_rueckwaerts(Loss2);
    CHECK(!moo_error_flag, "rueckwaerts(loss2) wirft nicht");
    CHECK((T(X2)->grad_valid & MOO_V_DEV) != 0, "x2.grad ist rein GPU-resident (Voraussetzung fuer den Repro)");

    MooValue G2 = moo_tensor_gradient(X2);   // OEFFENTLICHE API, NICHT moo_tensor_grad_sichern direkt
    CHECK(!moo_error_flag, "moo_tensor_gradient(x2) wirft nicht");
    float* g2 = T(G2)->data;
    CHECK(NAHE(g2[0], 0.5f), "moo_tensor_gradient(x2)[0] == c2[0] (0.5) -- NICHT 0 (stale Host-Puffer waere der alte Bug)");
    CHECK(NAHE(g2[1], 3.0f), "moo_tensor_gradient(x2)[1] == c2[1] (3.0) -- NICHT 0 (stale Host-Puffer waere der alte Bug)");
    moo_release(G2); moo_release(Y2); moo_release(Loss2); moo_release(X2); moo_release(C2);
    moo_ag_reset();

    moo_release(moo_tensor_mit_gradient(X));

    /* y = x*c (gleiche Form -> nimmt den NEUEN residenten gpu_grad-Pfad) */
    MooValue Y = moo_tensor_mul(X, C);
    CHECK(!moo_error_flag, "y=mul(x,c) Forward wirft nicht unter STRIKT");
    /* z = w*x (x gebroadcastet -> nimmt den bestehenden vollen CPU-Pfad) */
    MooValue Z = moo_tensor_mul(W, X);
    CHECK(!moo_error_flag, "z=mul(w,x) Forward wirft nicht unter STRIKT (dokumentierte Broadcast-CPU-Luecke)");

    MooValue Ysum = moo_tensor_summe(Y, moo_number(-1));
    MooValue Zsum = moo_tensor_summe(Z, moo_number(-1));
    MooValue Loss = moo_tensor_add(Ysum, Zsum);
    CHECK(!moo_error_flag, "loss=summe(y)+summe(z) wirft nicht");

    moo_tensor_rueckwaerts(Loss);
    CHECK(!moo_error_flag, "rueckwaerts() wirft nicht (Fan-out-Seitenwechsel sauber)");

    /* x->grad koennte nach dem residenten mul-Zweig NUR MOO_V_DEV-autoritativ
     * sein (I1/I2-Trichter) -- ohne expliziten Sichern-Aufruf waere ->data
     * bewusst stale, genau wie im G4c-STRIKT-Test. */
    moo_tensor_grad_sichern(T(X));
    float* xg = T(X)->grad;
    CHECK(xg != NULL, "x->grad existiert nach Sichern");
    CHECK(NAHE(xg[0], 9.5f), "x.grad[0] == c[0]+spaltensumme(w)[0] (9.5) -- kein Beitrag verloren");
    CHECK(NAHE(xg[1], 15.0f), "x.grad[1] == c[1]+spaltensumme(w)[1] (15.0) -- kein Beitrag verloren");

    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.submits > 0, "mind. 1 residenter GPU-Submit fand statt (der y=mul(x,c)-Zweig)");
    CHECK(tel.cpu_fallbacks == 0, "keine CPU-Fallbacks unter STRIKT (Broadcast-Zweig ist dokumentierte Luecke, kein Fallback-Versuch)");

    moo_release(Y); moo_release(Z); moo_release(Ysum); moo_release(Zsum); moo_release(Loss);
    moo_release(X); moo_release(C); moo_release(W);
    moo_ag_reset();

    printf("test_g4e_fanout_gpu: %d Checks OK (submits=%llu uploads=%llu downloads=%llu cpu_fallbacks=%llu)\n",
           checks, (unsigned long long)tel.submits, (unsigned long long)tel.uploads,
           (unsigned long long)tel.downloads, (unsigned long long)tel.cpu_fallbacks);
    return 0;
}
