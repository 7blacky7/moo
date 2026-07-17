/**
 * test_gradient_setzen_asan.c — KIP-X1b Phase A: moo_tensor_gradient_setzen
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (HARNESSES), Quell-Satz wie test_autograd_asan.c
 * plus moo_autograd.c; Test-throw-Modell.
 *
 * PRUEFT (Design §8.6):
 *   POSITIV:
 *   1. Grad aus flacher Zahlenliste exakter Laenge -> t->grad == Liste
 *   2. Grad aus Quell-Tensor gleicher Form -> t->grad == quelle->data
 *   3. Puffer-on-demand: frischer Tensor (grad==NULL) bekommt Puffer
 *   4. grad_valid == MOO_V_DATA nach dem Setzen
 *   5. Ueberschreiben (kein Akkumulieren): zweites Setzen gewinnt
 *   6. Selbstzuweisung gradient_setzen(t,t): grad == data, kein Crash
 *   NEGATIV (wirft, KEIN Teilzustand):
 *   7. Ziel ohne requires_grad
 *   8. Liste falscher Laenge (grad unveraendert)
 *   9. Quell-Tensor anderer Form
 *   10. Quelle weder Tensor noch Liste (Zahl)
 *   11. Liste mit Nicht-Zahl-Element (grad unveraendert -> kein Teilzustand)
 *   12. Ziel kein Tensor
 *   Lifecycle: alles released -> ASan detect_leaks=1 ist das Gate.
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
void moo_kamera_free(void* p)       { (void)p; }
void moo_mikro_free(void* p)        { (void)p; }
void moo_surface_free(void* p)      { (void)p; }

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

/* Tensor mit requires_grad — mit_gradient retained zurueck, sofort freigeben. */
static MooValue t2_grad(int r, int c, const float* vals) {
    MooValue v = t2(r, c, vals);
    moo_release(moo_tensor_mit_gradient(v));
    return v;
}

static MooValue liste4(double a, double b, double c, double d) {
    MooValue L = moo_list_new(4);
    moo_list_append(L, moo_number(a));   /* Transfer-Semantik: kein release */
    moo_list_append(L, moo_number(b));
    moo_list_append(L, moo_number(c));
    moo_list_append(L, moo_number(d));
    return L;
}

int main(void) {
    float av[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float bv[4] = { 5.0f, 6.0f, 7.0f, 8.0f };

    /* ---- 1. Grad aus flacher Zahlenliste ---- */
    MooValue T1 = t2_grad(2, 2, av);
    MooValue L = liste4(10.0, 20.0, 30.0, 40.0);
    moo_tensor_gradient_setzen(T1, L);
    CHECK(!moo_error_flag, "liste: kein fehler");
    MooTensor* t1 = MV_TENSOR(T1);
    CHECK(t1->grad != NULL, "liste: grad-puffer angelegt");
    CHECK(NAHE(t1->grad[0], 10) && NAHE(t1->grad[1], 20) &&
          NAHE(t1->grad[2], 30) && NAHE(t1->grad[3], 40),
          "liste: werte exakt in grad");
    /* 4. grad_valid == MOO_V_DATA */
    CHECK(t1->grad_valid == MOO_V_DATA, "grad_valid == MOO_V_DATA");
    moo_release(L);

    /* ---- 5. Ueberschreiben (kein Akkumulieren) ---- */
    MooValue L2 = liste4(1.0, 1.0, 1.0, 1.0);
    moo_tensor_gradient_setzen(T1, L2);
    CHECK(NAHE(t1->grad[0], 1) && NAHE(t1->grad[3], 1),
          "ueberschreiben: zweites setzen gewinnt");
    moo_release(L2);

    /* ---- 2. Grad aus Quell-Tensor gleicher Form ---- */
    MooValue T2 = t2_grad(2, 2, av);
    MooValue S = t2(2, 2, bv);   /* Quelle ohne requires_grad ist zulaessig */
    moo_tensor_gradient_setzen(T2, S);
    CHECK(!moo_error_flag, "tensorquelle: kein fehler");
    MooTensor* t2p = MV_TENSOR(T2);
    CHECK(NAHE(t2p->grad[0], 5) && NAHE(t2p->grad[3], 8),
          "tensorquelle: data->grad kopiert");

    /* ---- 6. Selbstzuweisung: grad = data von T2 = av ---- */
    moo_tensor_gradient_setzen(T2, T2);
    CHECK(!moo_error_flag, "selbst: kein fehler");
    CHECK(NAHE(t2p->grad[0], 1) && NAHE(t2p->grad[3], 4), "selbst: grad == data");
    moo_release(S);

    /* ---- 3. Puffer-on-demand: frischer Tensor ---- */
    MooValue T3 = t2_grad(2, 2, av);
    CHECK(MV_TENSOR(T3)->grad == NULL, "frisch: grad zunaechst NULL");
    MooValue S3 = t2(2, 2, bv);
    moo_tensor_gradient_setzen(T3, S3);
    CHECK(MV_TENSOR(T3)->grad != NULL && NAHE(MV_TENSOR(T3)->grad[2], 7),
          "frisch: puffer on-demand");
    moo_release(S3);

    /* ===================== NEGATIV ===================== */
    /* 7. Ziel ohne requires_grad */
    fehler_reset();
    MooValue N1 = t2(2, 2, av);   /* KEIN requires_grad */
    MooValue LN = liste4(1.0, 2.0, 3.0, 4.0);
    moo_tensor_gradient_setzen(N1, LN);
    CHECK(moo_error_flag, "negativ: ziel ohne requires_grad wirft");
    CHECK(MV_TENSOR(N1)->grad == NULL, "negativ: kein teilzustand (grad NULL)");
    fehler_reset();

    /* 8. Liste falscher Laenge (T1-grad == [1,1,1,1] aus L2) */
    MooValue LB = moo_list_new(3);
    moo_list_append(LB, moo_number(9.0));
    moo_list_append(LB, moo_number(9.0));
    moo_list_append(LB, moo_number(9.0));
    moo_tensor_gradient_setzen(T1, LB);
    CHECK(moo_error_flag, "negativ: falsche laenge wirft");
    CHECK(NAHE(t1->grad[0], 1) && NAHE(t1->grad[3], 1),
          "negativ: grad unveraendert (aus L2)");
    fehler_reset();
    moo_release(LB);

    /* 9. Quell-Tensor anderer Form */
    float cv[6] = { 1, 2, 3, 4, 5, 6 };
    int32_t shp[2] = { 2, 3 };
    MooTensor* s9 = moo_tensor_raw(2, shp);
    memcpy(s9->data, cv, 6 * sizeof(float));
    MooValue S9; S9.tag = MOO_TENSOR; moo_val_set_ptr(&S9, s9);
    moo_tensor_gradient_setzen(T1, S9);
    CHECK(moo_error_flag, "negativ: andere form wirft");
    CHECK(NAHE(t1->grad[0], 1), "negativ: form-fehler kein teilzustand");
    fehler_reset();
    moo_release(S9);

    /* 10. Quelle weder Tensor noch Liste */
    moo_tensor_gradient_setzen(T1, moo_number(42.0));
    CHECK(moo_error_flag, "negativ: zahl-quelle wirft");
    fehler_reset();

    /* 11. Liste mit Nicht-Zahl-Element -> kein Teilzustand */
    MooValue LX = moo_list_new(4);
    moo_list_append(LX, moo_number(7.0));
    moo_list_append(LX, moo_number(7.0));
    moo_list_append(LX, moo_string_new("boese"));   /* Transfer: Liste besitzt */
    moo_list_append(LX, moo_number(7.0));
    moo_tensor_gradient_setzen(T1, LX);
    CHECK(moo_error_flag, "negativ: nicht-zahl-element wirft");
    CHECK(NAHE(t1->grad[0], 1) && NAHE(t1->grad[1], 1),
          "negativ: kein teilzustand (grad aus L2)");
    fehler_reset();
    moo_release(LX);

    /* 12. Ziel kein Tensor */
    moo_tensor_gradient_setzen(moo_number(1.0), LN);
    CHECK(moo_error_flag, "negativ: nicht-tensor-ziel wirft");
    fehler_reset();
    moo_release(LN);

    /* ---- Lifecycle: alles released — ASan detect_leaks prueft ---- */
    moo_release(N1);
    moo_release(T1);
    moo_release(T2);
    moo_release(T3);
    moo_ag_reset();

    printf("test_gradient_setzen_asan: %d Checks OK\n", checks);
    return 0;
}
