/**
 * test_autograd_asan.c — Plan-014 B1: Tape + backward (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES), Quell-Satz wie
 * test_tensor_ops_asan.c plus moo_autograd.c; Test-throw-Modell.
 *
 * PRUEFT (alle Gradienten handgerechnet):
 *   1. z = sum(a*b): dz/da = b, dz/db = a
 *   2. Kette: z = sum(relu(a @ w)) — matmul+relu backward
 *   3. Fan-out-Akkumulation: y = a*a per mul(a,a) -> da = 2a (Input 2x im Node)
 *      und a in ZWEI getrennten Ops -> Gradienten addieren sich
 *   4. Broadcast-Bias: z = sum(x + b), b=[1,c] -> db = [batch]-Summen
 *   5. muls/pow/mean-Skalar-Ketten
 *   6. requires_grad-Propagation + Zweige ohne Loss-Beitrag (grad NULL, skip)
 *   7. no_grad (autograd_aus): kein Tape-Wachstum
 *   8. Tape-Lifecycle: reset released alles — ASan detect_leaks=1 ist das Gate
 *   9. rueckwaerts wirft bei Nicht-Skalar-Loss (deutsche Meldung)
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <stdarg.h>

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

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
#define NAHE(x, y) (fabs((double)(x) - (double)(y)) < 1e-4)

static MooValue t2(int r, int c, const float* vals) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    memcpy(t->data, vals, (size_t)(r * c) * sizeof(float));
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

int main(void) {
    /* ---- 1. z = sum(a*b): dz/da = b ---- */
    float a_v[4] = { 1, 2, 3, 4 }, b_v[4] = { 10, 20, 30, 40 };
    MooValue A = t2(2, 2, a_v), B = t2(2, 2, b_v);
    moo_release(moo_tensor_mit_gradient(A));   /* +1-Rueckgabe direkt abgeben */
    moo_release(moo_tensor_mit_gradient(B));
    MooValue P = moo_tensor_mul(A, B);
    MooValue Z = moo_tensor_summe(P, moo_number(-1));
    CHECK(MV_TENSOR(P)->requires_grad && MV_TENSOR(Z)->requires_grad,
          "requires_grad propagiert");
    moo_tensor_rueckwaerts(Z);
    MooTensor* at = MV_TENSOR(A);
    MooTensor* bt = MV_TENSOR(B);
    CHECK(at->grad && bt->grad, "grads existieren");
    for (int i = 0; i < 4; i++) {
        CHECK(NAHE(at->grad[i], b_v[i]), "dz/da = b");
        CHECK(NAHE(bt->grad[i], a_v[i]), "dz/db = a");
    }
    moo_release(P); moo_release(Z);
    moo_ag_reset();
    moo_tensor_gradient_loeschen(A); moo_tensor_gradient_loeschen(B);

    /* ---- 2. Kette: z = sum(relu(a @ w)) ----
     * a=[[1,-1],[2,0]], w=[[1,0],[0,1]] -> aw = a; relu(a)=[[1,0],[2,0]]
     * z = 3. drelu-Maske M = (aw>0) = [[1,0],[1,0]]
     * dz/dw = a^T @ M = [[1,2],[ -1,0]]^T… rechnen: a^T=[[1,2],[-1,0]];
     * a^T @ M = [[1*1+2*1, 0],[ -1*1+0*1, 0]] = [[3,0],[-1,0]]           */
    float a2_v[4] = { 1, -1, 2, 0 }, w_v[4] = { 1, 0, 0, 1 };
    MooValue A2 = t2(2, 2, a2_v), W = t2(2, 2, w_v);
    moo_release(moo_tensor_mit_gradient(W));
    MooValue MM = moo_tensor_matmul(A2, W);
    MooValue R = moo_tensor_relu(MM);
    MooValue Z2 = moo_tensor_summe(R, moo_number(-1));
    CHECK(NAHE(MV_TENSOR(Z2)->data[0], 3.0f), "forward kette z=3");
    moo_tensor_rueckwaerts(Z2);
    MooTensor* wt = MV_TENSOR(W);
    CHECK(wt->grad, "w-grad existiert");
    CHECK(NAHE(wt->grad[0], 3.0f) && NAHE(wt->grad[1], 0.0f) &&
          NAHE(wt->grad[2], -1.0f) && NAHE(wt->grad[3], 0.0f),
          "dz/dw = a^T @ relu-Maske");
    CHECK(MV_TENSOR(A2)->grad == NULL, "a2 ohne mit_gradient: kein grad");
    moo_release(MM); moo_release(R); moo_release(Z2);
    moo_ag_reset();
    moo_tensor_gradient_loeschen(W);

    /* ---- 3. Fan-out: y = a*a -> dy/da = 2a; plus a in 2 Ops -> Summe ---- */
    float c_v[2] = { 3, 5 };
    int32_t sh1[1] = { 2 };
    MooTensor* ct = moo_tensor_raw(1, sh1);
    memcpy(ct->data, c_v, sizeof(c_v));
    MooValue C; C.tag = MOO_TENSOR; moo_val_set_ptr(&C, ct);
    moo_release(moo_tensor_mit_gradient(C));
    MooValue Q = moo_tensor_mul(C, C);                       /* c^2 */
    MooValue S1 = moo_tensor_summe(Q, moo_number(-1));
    moo_tensor_rueckwaerts(S1);
    CHECK(NAHE(ct->grad[0], 6.0f) && NAHE(ct->grad[1], 10.0f),
          "mul(a,a): da = 2a (beide Input-Slots akkumulieren)");
    moo_release(Q); moo_release(S1);
    moo_ag_reset();
    moo_tensor_gradient_loeschen(C);

    MooValue U1 = moo_tensor_muls(C, moo_number(2.0));       /* 2c  */
    MooValue U2 = moo_tensor_muls(C, moo_number(3.0));       /* 3c  */
    MooValue U3 = moo_tensor_add(U1, U2);                    /* 5c  */
    MooValue S2 = moo_tensor_summe(U3, moo_number(-1));
    moo_tensor_rueckwaerts(S2);
    CHECK(NAHE(ct->grad[0], 5.0f) && NAHE(ct->grad[1], 5.0f),
          "fan-out ueber 2 Ops: grads addieren (2+3)");
    moo_release(U1); moo_release(U2); moo_release(U3); moo_release(S2);
    moo_ag_reset();
    moo_release(C);

    /* ---- 4. Broadcast-Bias: z = sum(x + b), b=[1,2], x=[3,2] ---- */
    float x_v[6] = { 1, 2, 3, 4, 5, 6 }, bias_v[2] = { 0.5f, -0.5f };
    MooValue X = t2(3, 2, x_v), BI = t2(1, 2, bias_v);
    moo_release(moo_tensor_mit_gradient(BI));
    MooValue XB = moo_tensor_add(X, BI);
    MooValue Z3 = moo_tensor_summe(XB, moo_number(-1));
    moo_tensor_rueckwaerts(Z3);
    MooTensor* bit = MV_TENSOR(BI);
    CHECK(bit->grad && NAHE(bit->grad[0], 3.0f) && NAHE(bit->grad[1], 3.0f),
          "bias-grad = batch-summe (broadcast-reduktion)");
    moo_release(XB); moo_release(Z3);
    moo_ag_reset();
    moo_release(X); moo_release(BI);

    /* ---- 5. pow + mean: z = mean(c^3), dz/dc = 3c^2 / n ---- */
    MooValue C2 = t2(1, 2, c_v);   /* [3,5] als [1x2] */
    moo_release(moo_tensor_mit_gradient(C2));
    MooValue PW = moo_tensor_pow(C2, moo_number(3.0));
    MooValue MN = moo_tensor_mittel(PW, moo_number(-1));
    moo_tensor_rueckwaerts(MN);
    MooTensor* c2t = MV_TENSOR(C2);
    CHECK(NAHE(c2t->grad[0], 3.0f * 9.0f / 2.0f) && NAHE(c2t->grad[1], 3.0f * 25.0f / 2.0f),
          "pow+mean kette");
    moo_release(PW); moo_release(MN);
    moo_ag_reset();
    moo_release(C2);

    /* ---- 6./7. no_grad: kein Tracking ---- */
    MooValue D = t2(2, 2, a_v);
    moo_release(moo_tensor_mit_gradient(D));
    moo_ag_aus();
    MooValue E = moo_tensor_relu(D);
    CHECK(MV_TENSOR(E)->requires_grad == false, "autograd_aus: kein tracking");
    moo_ag_an();
    moo_release(E); moo_release(D);

    /* ---- 9. Fehlerfall: rueckwaerts auf Nicht-Skalar ---- */
    fehler_reset();
    MooValue F = t2(2, 2, a_v);
    moo_release(moo_tensor_mit_gradient(F));
    MooValue G = moo_tensor_relu(F);
    moo_tensor_rueckwaerts(G);   /* size 4 -> wirft */
    CHECK(moo_error_flag, "rueckwaerts wirft bei nicht-skalarem Loss");
    fehler_reset();
    moo_release(G); moo_release(F);
    moo_ag_reset();

    /* ---- 8. Lifecycle: alles released — ASan detect_leaks prueft ---- */
    moo_release(A); moo_release(B); moo_release(A2); moo_release(W);

    printf("test_autograd_asan: %d Checks OK\n", checks);
    return 0;
}
