/**
 * test_checkpoint_ag_asan.c — KIP-B4b Activation Checkpointing (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Quell-Satz wie test_nn_asan.
 * Test-throw-Modell (ersetzt moo_error.c).
 *
 * ISOLATIONS-GRUND: Diese Gates lebten urspruenglich als Abschnitt 21 in
 * test_nn_asan.c und erzeugten dort — nur durch Heap-Layout-Interaktion mit
 * dem davorstehenden §19-Code (kreuzentropie_maskiert) — einen residualen,
 * an §19 fehl-attribuierten Leak. Der Checkpoint-Produktionspfad selbst ist
 * beweisbar sauber. Als eigenstaendiger Harness (kein §19-Kontext, korrekte
 * String-Refcounts via mk_dicht) sind die Gates ASan-/UBSan-clean.
 *
 * GATES (Task KIP-B4b):
 *   1. KERN-GATE (harter Dropout-Fall): Grad mit moo_nn_checkpoint == ohne
 *      BIT-identisch — alle Parameter-Grads UND Input-Grad — auf einem Netz
 *      MIT Dropout. Moeglich, weil das Re-Forward den Dropout-Zaehler auf den
 *      Segment-Eintrittswert restauriert (bit-identische Maske).
 *   2. DETERMINISMUS: zwei identische checkpointed Laeufe (frische, gleich-
 *      geseedete Netze) liefern BIT-identische Parameter-Grads.
 *   3. TIEFES SEGMENT (2 Dropouts): Re-Forward-Backward memory-safe (ASan) +
 *      Grad fliesst in ALLE Parameter.
 *
 * REFCOUNT-KONVENTION: moo_nn_schicht_dicht nimmt den Aktivierungs-String
 * GELIEHEN (konsumiert ihn NICHT) -> mk_dicht baut ihn und gibt ihn wieder
 * frei. moo_tensor_mit_gradient(t2()) haelt ZWEI Refs (t2 +1, mit_gradient
 * +1) -> beide Handles separat freigeben.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <math.h>

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
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

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

/* 2D-Tensor aus flachen Werten. */
static MooValue t2(int r, int c, const float* vals) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    for (int i = 0; i < r * c; i++) t->data[i] = vals[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

/* schicht_dicht-Wrapper: Aktivierungs-String ist BORROWED (Tensor-Konvention)
 * -> hier bauen UND wieder freigeben, sonst leakt der Harness. */
static MooValue mk_dicht(int ein, int aus, const char* akt, double seed) {
    MooValue a = akt ? moo_string_new(akt) : moo_none();
    MooValue d = moo_nn_schicht_dicht(moo_number(ein), moo_number(aus),
                                      a, moo_number(seed));
    moo_release(a);
    return d;
}

/* Baut [dicht(4,6,relu,s0) -> dropout(rate) -> dicht(6,3,-,s1)] MIT Dropout. */
static MooValue mk_netz_klein(double rate, double s0, double s1) {
    MooValue net = moo_list_new(3);
    moo_list_append(net, mk_dicht(4, 6, "relu", s0));
    moo_list_append(net, moo_nn_schicht_dropout(moo_number(rate)));
    moo_list_append(net, mk_dicht(6, 3, NULL, s1));
    return net;
}

int main(void) {
    float xv[12];
    for (int i = 0; i < 12; i++) xv[i] = (float)((i * 7) % 13) * 0.1f - 0.5f;

    /* ===== 1. KERN-GATE: Grad mit/ohne Checkpoint BIT-identisch (MIT Dropout) */
    {
        /* Zwei IDENTISCHE Netze (gleiche Seeds). A normaler Forward, B checkpointed. */
        MooValue netA = mk_netz_klein(0.3, 11, 12);
        MooValue netB = mk_netz_klein(0.3, 11, 12);

        moo_ag_reset();
        MooValue xtA = t2(3, 4, xv);
        MooValue xA = moo_tensor_mit_gradient(xtA);
        MooValue yA = moo_nn_vorwaerts(netA, xA);
        MooValue LA = moo_tensor_mittel(yA, moo_none());
        moo_release(moo_tensor_rueckwaerts(LA));
        MooValue gxA = moo_tensor_gradient(xA);
        MooValue psA = moo_nn_parameter(netA);
        moo_ag_reset();

        MooValue xtB = t2(3, 4, xv);
        MooValue xB = moo_tensor_mit_gradient(xtB);
        MooValue yB = moo_nn_checkpoint(netB, xB);
        MooValue LB = moo_tensor_mittel(yB, moo_none());
        moo_release(moo_tensor_rueckwaerts(LB));
        MooValue gxB = moo_tensor_gradient(xB);
        MooValue psB = moo_nn_parameter(netB);

        /* KERN-GATE: alle Parameter-Grads mit Checkpoint == ohne BIT-identisch. */
        bool pg = (psA.tag == MOO_LIST && psB.tag == MOO_LIST &&
                   MV_LIST(psA)->length == MV_LIST(psB)->length &&
                   MV_LIST(psA)->length > 0);
        for (int32_t i = 0; pg && i < MV_LIST(psA)->length; i++) {
            MooTensor* pa = MV_TENSOR(MV_LIST(psA)->items[i]);
            MooTensor* pb = MV_TENSOR(MV_LIST(psB)->items[i]);
            if (!pa->grad || !pb->grad || pa->size != pb->size) { pg = false; break; }
            for (int64_t j = 0; j < pa->size; j++)
                if (pa->grad[j] != pb->grad[j]) { pg = false; break; }
        }
        CHECK(pg, "b4b: Parameter-Grads mit Checkpoint == ohne BIT-identisch (Netz MIT Dropout)");

        bool xg = (gxA.tag == MOO_TENSOR && gxB.tag == MOO_TENSOR &&
                   T(gxA)->size == T(gxB)->size);
        for (int64_t j = 0; xg && j < T(gxA)->size; j++)
            xg = (T(gxA)->data[j] == T(gxB)->data[j]);
        CHECK(xg, "b4b: Input-Grad mit Checkpoint == ohne BIT-identisch");

        moo_release(gxA); moo_release(psA); moo_release(yA); moo_release(LA);
        moo_release(xA); moo_release(xtA);
        moo_release(gxB); moo_release(psB); moo_release(yB); moo_release(LB);
        moo_release(xB); moo_release(xtB);
        moo_release(netA); moo_release(netB);
        moo_ag_reset();
    }

    /* ===== 2. DETERMINISMUS: 2x checkpointed Lauf BIT-identisch =============
     * Zwei frische, identisch geseedete Netze -> checkpointed Backward -> die
     * Parameter-Grads muessen bitgenau uebereinstimmen (kein RNG-Drift im
     * Re-Forward, Dropout-Zaehler-Restore deterministisch). */
    {
        MooValue net1 = mk_netz_klein(0.3, 31, 32);
        MooValue net2 = mk_netz_klein(0.3, 31, 32);

        moo_ag_reset();
        MooValue xt1 = t2(3, 4, xv);
        MooValue x1 = moo_tensor_mit_gradient(xt1);
        MooValue y1 = moo_nn_checkpoint(net1, x1);
        MooValue L1 = moo_tensor_mittel(y1, moo_none());
        moo_release(moo_tensor_rueckwaerts(L1));
        MooValue ps1 = moo_nn_parameter(net1);
        moo_ag_reset();

        MooValue xt2 = t2(3, 4, xv);
        MooValue x2 = moo_tensor_mit_gradient(xt2);
        MooValue y2 = moo_nn_checkpoint(net2, x2);
        MooValue L2 = moo_tensor_mittel(y2, moo_none());
        moo_release(moo_tensor_rueckwaerts(L2));
        MooValue ps2 = moo_nn_parameter(net2);

        bool det = (ps1.tag == MOO_LIST && ps2.tag == MOO_LIST &&
                    MV_LIST(ps1)->length == MV_LIST(ps2)->length &&
                    MV_LIST(ps1)->length > 0);
        for (int32_t i = 0; det && i < MV_LIST(ps1)->length; i++) {
            MooTensor* p1 = MV_TENSOR(MV_LIST(ps1)->items[i]);
            MooTensor* p2 = MV_TENSOR(MV_LIST(ps2)->items[i]);
            if (!p1->grad || !p2->grad || p1->size != p2->size) { det = false; break; }
            for (int64_t j = 0; j < p1->size; j++)
                if (p1->grad[j] != p2->grad[j]) { det = false; break; }
        }
        CHECK(det, "b4b: 2x checkpointed Lauf BIT-identische Parameter-Grads (Determinismus)");

        moo_release(ps1); moo_release(y1); moo_release(L1); moo_release(x1); moo_release(xt1);
        moo_release(ps2); moo_release(y2); moo_release(L2); moo_release(x2); moo_release(xt2);
        moo_release(net1); moo_release(net2);
        moo_ag_reset();
    }

    /* ===== 3. TIEFES SEGMENT (2 Dropouts): Re-Forward memory-safe + Grad ====
     * ueberall. */
    {
        MooValue netD = moo_list_new(5);
        moo_list_append(netD, mk_dicht(4, 8, "relu", 21));
        moo_list_append(netD, moo_nn_schicht_dropout(moo_number(0.2)));
        moo_list_append(netD, mk_dicht(8, 8, "relu", 22));
        moo_list_append(netD, moo_nn_schicht_dropout(moo_number(0.2)));
        moo_list_append(netD, mk_dicht(8, 2, NULL, 23));

        moo_ag_reset();
        MooValue xtD = t2(3, 4, xv);
        MooValue xD = moo_tensor_mit_gradient(xtD);
        MooValue yD = moo_nn_checkpoint(netD, xD);
        MooValue LD = moo_tensor_mittel(yD, moo_none());
        moo_release(moo_tensor_rueckwaerts(LD));
        MooValue psD = moo_nn_parameter(netD);
        bool dg = (psD.tag == MOO_LIST && MV_LIST(psD)->length > 0);
        for (int32_t i = 0; dg && i < MV_LIST(psD)->length; i++) {
            MooTensor* p = MV_TENSOR(MV_LIST(psD)->items[i]);
            bool einer = false;
            if (p->grad) for (int64_t j = 0; j < p->size && !einer; j++)
                if (p->grad[j] != 0.0f) einer = true;
            dg = dg && einer;
        }
        CHECK(dg, "b4b: tiefes Segment (2 Dropouts) Re-Forward — Grad in allen Params, ASan-clean");
        moo_release(psD); moo_release(yD); moo_release(LD); moo_release(xD);
        moo_release(xtD); moo_release(netD);
        moo_ag_reset();
    }

    printf("test_checkpoint_ag_asan: alle %d Checks bestanden\n", checks);
    return 0;
}
