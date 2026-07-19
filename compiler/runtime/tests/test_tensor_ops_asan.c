/**
 * test_tensor_ops_asan.c — Plan-014 A2: Op-Harness (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Quell-Satz wie
 * test_tensor_asan.c plus moo_tensor_ops.c; gleiches Test-throw-Modell
 * (moo_error.c NICHT gelinkt, Stub konsumiert den Fehler via moo_release).
 *
 * PRUEFT (handgerechnete Referenzen):
 *   1. Elementwise same-shape + alle Broadcast-Pfade (row [1,c], col [r,1],
 *      Skalar-Tensor [1], generischer Pfad) + Skalar-Varianten
 *   2. matmul 2x3 @ 3x2 per Hand + Fehlerfaelle (Mismatch, nicht-2D)
 *   3. transponieren, umformen (Roundtrip), zeilen (Batch-Slice), verbinden
 *   4. Reduktionen: summe/mittel/maximum — alles + Achsen mit keepdims-Shapes
 *   5. exp/log/sqrt-Roundtrip, relu/sigmoid/tanh/gelu-Werte, pow
 *   6. softmax: Zeilensumme==1, STABIL bei Logit 1000 (kein inf/nan),
 *      logsoftmax == log(softmax) elementweise
 *   7. div folgt IEEE (1/0=inf, 0/0=nan — dokumentierter Vertrag, kein throw)
 *   8. Registry: 26 Eintraege, lookup findet jeden, at()-Iteration (B2-Muster)
 * Hinweis: tensor_zeilen ist bewusst NICHT in der Registry (3-arg-Zugriffs-
 * Helfer, kein Autograd-Op).
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
#define NAHE(x, y) (fabs((double)(x) - (double)(y)) < 1e-5)

/* 2D-Tensor aus flachen Werten bauen. */
static MooValue t2(int r, int c, const float* vals) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    memcpy(t->data, vals, (size_t)(r * c) * sizeof(float));
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}
static MooValue t1(int n, const float* vals) {
    int32_t shape[1] = { n };
    MooTensor* t = moo_tensor_raw(1, shape);
    memcpy(t->data, vals, (size_t)n * sizeof(float));
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

int main(void) {
    /* ---- 1. Elementwise ---- */
    float a_v[6] = { 1, 2, 3, 4, 5, 6 };        /* [2x3] */
    float b_v[6] = { 10, 20, 30, 40, 50, 60 };  /* [2x3] */
    MooValue A = t2(2, 3, a_v), B = t2(2, 3, b_v);
    MooValue S = moo_tensor_add(A, B);
    CHECK(MV_TENSOR(S)->data[0] == 11.0f && MV_TENSOR(S)->data[5] == 66.0f, "add same");
    moo_release(S);
    S = moo_tensor_mul(A, B);
    CHECK(MV_TENSOR(S)->data[4] == 250.0f, "mul same");
    moo_release(S);

    float row_v[3] = { 100, 200, 300 };          /* [1x3] — Bias-Muster */
    MooValue R = t2(1, 3, row_v);
    S = moo_tensor_add(A, R);
    CHECK(MV_TENSOR(S)->ndim == 2 && MV_TENSOR(S)->shape[0] == 2, "row-broadcast shape");
    CHECK(MV_TENSOR(S)->data[0] == 101.0f && MV_TENSOR(S)->data[5] == 306.0f, "row-broadcast");
    moo_release(S);

    float col_v[2] = { 10, 20 };                 /* [2x1] — generischer Pfad */
    MooValue C = t2(2, 1, col_v);
    S = moo_tensor_add(A, C);
    CHECK(MV_TENSOR(S)->data[0] == 11.0f && MV_TENSOR(S)->data[3] == 24.0f &&
          MV_TENSOR(S)->data[5] == 26.0f, "col-broadcast (generic)");
    moo_release(S);

    float sk_v[1] = { 5 };                       /* [1] Skalar-Tensor */
    MooValue SK = t1(1, sk_v);
    S = moo_tensor_mul(A, SK);
    CHECK(MV_TENSOR(S)->data[5] == 30.0f, "skalar-tensor-broadcast");
    moo_release(S); moo_release(SK);

    S = moo_tensor_muls(A, moo_number(2.0));
    CHECK(MV_TENSOR(S)->data[2] == 6.0f, "muls skalar");
    moo_release(S);
    S = moo_tensor_subs(A, moo_number(1.0));
    CHECK(MV_TENSOR(S)->data[0] == 0.0f, "subs skalar");
    moo_release(S);

    /* Broadcast-Fehler */
    fehler_reset();
    float w_v[8] = {0}; MooValue W = t2(4, 2, w_v);
    S = moo_tensor_add(A, W);
    CHECK(moo_error_flag && S.tag == MOO_NONE, "broadcast-mismatch wirft");
    moo_release(W); fehler_reset();

    /* ---- 2. matmul: [[1,2,3],[4,5,6]] @ [[7,8],[9,10],[11,12]] ----
     * Hand: Zeile1 = [1*7+2*9+3*11, 1*8+2*10+3*12] = [58, 64]
     *       Zeile2 = [4*7+5*9+6*11, 4*8+5*10+6*12] = [139, 154]      */
    float m_v[6] = { 7, 8, 9, 10, 11, 12 };
    MooValue M = t2(3, 2, m_v);
    S = moo_tensor_matmul(A, M);
    CHECK(MV_TENSOR(S)->shape[0] == 2 && MV_TENSOR(S)->shape[1] == 2, "matmul shape");
    CHECK(MV_TENSOR(S)->data[0] == 58.0f && MV_TENSOR(S)->data[1] == 64.0f &&
          MV_TENSOR(S)->data[2] == 139.0f && MV_TENSOR(S)->data[3] == 154.0f, "matmul werte");
    moo_release(S);

    fehler_reset();
    S = moo_tensor_matmul(A, B);   /* 2x3 @ 2x3: inner mismatch */
    CHECK(moo_error_flag && S.tag == MOO_NONE, "matmul mismatch wirft");
    fehler_reset();

    /* ---- 3. Form-Ops ---- */
    S = moo_tensor_transponieren(A);
    CHECK(MV_TENSOR(S)->shape[0] == 3 && MV_TENSOR(S)->data[1] == 4.0f &&
          MV_TENSOR(S)->data[4] == 3.0f, "transponieren");
    moo_release(S);

    MooValue form6 = moo_list_new(1);
    moo_list_append(form6, moo_number(6));
    S = moo_tensor_umformen(A, form6);
    CHECK(MV_TENSOR(S)->ndim == 1 && MV_TENSOR(S)->size == 6 &&
          MV_TENSOR(S)->data[3] == 4.0f, "umformen 2D->1D");
    moo_release(S); moo_release(form6);

    fehler_reset();
    MooValue form9 = moo_list_new(1);
    moo_list_append(form9, moo_number(9));
    S = moo_tensor_umformen(A, form9);
    CHECK(moo_error_flag, "umformen falsche groesse wirft");
    moo_release(form9); fehler_reset();

    S = moo_tensor_zeilen(A, moo_number(1), moo_number(2));
    CHECK(MV_TENSOR(S)->shape[0] == 1 && MV_TENSOR(S)->data[0] == 4.0f &&
          MV_TENSOR(S)->data[2] == 6.0f, "zeilen [1,2)");
    moo_release(S);

    S = moo_tensor_verbinden(A, B);
    CHECK(MV_TENSOR(S)->shape[0] == 4 && MV_TENSOR(S)->data[6] == 10.0f, "verbinden achse0");
    moo_release(S);

    /* ---- 4. Reduktionen (keepdims!) ---- */
    S = moo_tensor_summe(A, moo_number(-1));
    CHECK(MV_TENSOR(S)->ndim == 1 && MV_TENSOR(S)->data[0] == 21.0f, "summe alles");
    moo_release(S);
    S = moo_tensor_summe(A, moo_number(0));
    CHECK(MV_TENSOR(S)->shape[0] == 1 && MV_TENSOR(S)->shape[1] == 3 &&
          MV_TENSOR(S)->data[0] == 5.0f && MV_TENSOR(S)->data[2] == 9.0f, "summe achse0 [1,c]");
    moo_release(S);
    S = moo_tensor_summe(A, moo_number(1));
    CHECK(MV_TENSOR(S)->shape[0] == 2 && MV_TENSOR(S)->shape[1] == 1 &&
          MV_TENSOR(S)->data[0] == 6.0f && MV_TENSOR(S)->data[1] == 15.0f, "summe achse1 [r,1]");
    moo_release(S);
    S = moo_tensor_mittel(A, moo_number(-1));
    CHECK(NAHE(MV_TENSOR(S)->data[0], 3.5f), "mittel alles");
    moo_release(S);
    S = moo_tensor_maximum(A, moo_number(0));
    CHECK(MV_TENSOR(S)->data[0] == 4.0f && MV_TENSOR(S)->data[2] == 6.0f, "maximum achse0");
    moo_release(S);

    /* ---- 5. Unaer + Aktivierungen ---- */
    float x_v[4] = { -2, -0.5f, 0.5f, 2 };
    MooValue X = t1(4, x_v);
    S = moo_tensor_relu(X);
    CHECK(MV_TENSOR(S)->data[0] == 0.0f && MV_TENSOR(S)->data[3] == 2.0f, "relu");
    moo_release(S);
    S = moo_tensor_sigmoid(X);
    CHECK(NAHE(MV_TENSOR(S)->data[3], 0.8807971f) && NAHE(MV_TENSOR(S)->data[0], 0.1192029f),
          "sigmoid");
    moo_release(S);
    S = moo_tensor_tanh(X);
    CHECK(NAHE(MV_TENSOR(S)->data[3], 0.9640276f), "tanh");
    moo_release(S);
    S = moo_tensor_gelu(X);   /* gelu(2) ~ 1.9546, gelu(-2) ~ -0.0454 (tanh-Approx) */
    CHECK(fabs((double)MV_TENSOR(S)->data[3] - 1.9546) < 1e-3 &&
          fabs((double)MV_TENSOR(S)->data[0] + 0.0454) < 1e-3, "gelu");
    moo_release(S);

    float e_v[3] = { 1, 4, 9 };
    MooValue E = t1(3, e_v);
    S = moo_tensor_sqrt(E);
    CHECK(MV_TENSOR(S)->data[2] == 3.0f, "sqrt");
    MooValue S2 = moo_tensor_log(S);        /* log(sqrt(x)) */
    MooValue S3 = moo_tensor_exp(S2);       /* roundtrip = sqrt(x) */
    CHECK(NAHE(MV_TENSOR(S3)->data[1], 2.0f), "exp(log(x)) roundtrip");
    moo_release(S); moo_release(S2); moo_release(S3);
    S = moo_tensor_pow(E, moo_number(0.5));
    CHECK(NAHE(MV_TENSOR(S)->data[1], 2.0f), "pow 0.5");
    moo_release(S);
    S = moo_tensor_neg(E);
    CHECK(MV_TENSOR(S)->data[0] == -1.0f, "neg");
    moo_release(S);
    moo_release(E);

    /* ---- 6. Softmax: Stabilitaet + Summe + logsoftmax-Konsistenz ---- */
    float big_v[6] = { 1000, 1001, 1002, 3, 2, 1 };  /* Zeile 1: riesige Logits */
    MooValue G = t2(2, 3, big_v);
    S = moo_tensor_softmax(G);
    for (int i = 0; i < 6; i++)
        CHECK(isfinite(MV_TENSOR(S)->data[i]), "softmax stabil (kein inf/nan)");
    double z0 = (double)MV_TENSOR(S)->data[0] + MV_TENSOR(S)->data[1] + MV_TENSOR(S)->data[2];
    double z1 = (double)MV_TENSOR(S)->data[3] + MV_TENSOR(S)->data[4] + MV_TENSOR(S)->data[5];
    CHECK(fabs(z0 - 1.0) < 1e-5 && fabs(z1 - 1.0) < 1e-5, "softmax zeilensumme 1");
    MooValue LS = moo_tensor_logsoftmax(G);
    for (int i = 0; i < 6; i++)
        CHECK(fabs(log((double)MV_TENSOR(S)->data[i]) - (double)MV_TENSOR(LS)->data[i]) < 1e-4,
              "logsoftmax == log(softmax)");
    moo_release(S); moo_release(LS); moo_release(G);

    /* ---- 7. div = IEEE ---- */
    float dz_v[2] = { 1, 0 }, dn_v[2] = { 0, 0 };
    MooValue DZ = t1(2, dz_v), DN = t1(2, dn_v);
    S = moo_tensor_div(DZ, DN);
    CHECK(isinf(MV_TENSOR(S)->data[0]) && isnan(MV_TENSOR(S)->data[1]),
          "div IEEE (1/0=inf, 0/0=nan, kein throw)");
    CHECK(!moo_error_flag, "div wirft nicht");
    moo_release(S); moo_release(DZ); moo_release(DN);

    /* ---- 8. Registry ---- */
    CHECK(moo_tensor_op_count() == 32, "registry: 32 ops");   /* +im2col/pooling (KI-MULTI-V2), +hadamard (KI-Q1) */
    const MooTensorOp* op = moo_tensor_op_lookup("matmul");
    CHECK(op && op->art == MOO_OP_BINARY && op->fw2 == moo_tensor_matmul, "lookup matmul");
    op = moo_tensor_op_lookup("softmax");
    CHECK(op && op->art == MOO_OP_UNARY && op->fw1 == moo_tensor_softmax, "lookup softmax");
    CHECK(moo_tensor_op_lookup("gibtsnicht") == NULL, "lookup unbekannt -> NULL");
    int mit_fw = 0;
    for (int i = 0; i < moo_tensor_op_count(); i++) {
        const MooTensorOp* o = moo_tensor_op_at(i);
        CHECK(o && o->name, "at() liefert Eintrag");
        if ((o->art == MOO_OP_UNARY && o->fw1) || (o->art != MOO_OP_UNARY && o->fw2)) mit_fw++;
        CHECK(o->bw == NULL, "bw noch NULL (Autograd = B1)");
    }
    CHECK(mit_fw == 32, "alle ops haben forward");

    moo_release(A); moo_release(B); moo_release(R); moo_release(C);
    moo_release(M); moo_release(X);

    printf("test_tensor_ops_asan: %d Checks OK\n", checks);
    return 0;
}
