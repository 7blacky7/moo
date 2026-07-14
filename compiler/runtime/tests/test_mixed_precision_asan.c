/**
 * test_mixed_precision_asan.c — KIP-D2: Mixed-Precision-Training bf16 (ASan+UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Quell-Satz wie test_nn_asan.c
 * (moo_nn.c + Autograd/Tensor-Kern + Core-Runtime), Test-throw-Modell.
 *
 * VERTRAG (D2): Parameter-Master bleiben f32, Optimizer rechnet auf f32-`data`
 * (opt_schritt mutiert data, valid=MOO_V_DATA). Aktivierungen = Op-Outputs mit
 * requires_grad; bei aktivem Mixed-Precision (moo_ag_bf16_setzen(true) ODER
 * MOO_KI_BF16=1) rundet moo_ag_record jeden Op-Output IN-PLACE auf bf16-
 * Praezision (moo_tensor_bf16_runden). Gradienten bleiben f32.
 *
 * PRUEFT:
 *   1. moo_tensor_bf16_runden: idempotent, bf16-exakte Werte unveraendert,
 *      und BIT-IDENTISCH zum Storage-Pfad als_dtype("bf16")+f32_sichern
 *      (beweist: Rundungs-Numerik == D1-bf16-Storage-Numerik).
 *   2. Toggle: Default AUS; setzen(true/false) + Getter.
 *   3. Default-AUS-Regression: XOR-Training mit MP-Flag AUS == reines f32
 *      (bit-identische Endverluste vor/nach Toggle-Nutzung) -> Basisgate-Schutz.
 *   4. XOR bf16 vs f32: beide konvergieren, |bf16-f32| in Toleranz,
 *      bf16-Lauf DETERMINISTISCH (zwei Laeufe bit-identisch).
 *   5. CE-Klassifikation bf16 vs f32 (logsoftmax/kreuzentropie/relu/matmul):
 *      beide konvergieren, in Toleranz, bf16 deterministisch.
 * ASan detect_leaks=1: kein Trainings-Loop-Leak.
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

static MooValue t2(int r, int c, const float* vals) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    for (int i = 0; i < r * c; i++) t->data[i] = vals[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}
static MooValue t1(int n, const float* vals) {
    int32_t shape[1] = { n };
    MooTensor* t = moo_tensor_raw(1, shape);
    for (int i = 0; i < n; i++) t->data[i] = vals[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}
static MooValue mk_dicht(int ein, int aus, const char* akt, double seed) {
    MooValue a = akt ? moo_string_new(akt) : moo_none();
    MooValue s = (seed >= 0.0) ? moo_number(seed) : moo_none();
    MooValue d = moo_nn_schicht_dicht(moo_number(ein), moo_number(aus), a, s);
    moo_release(a);
    return d;
}

/* XOR-Training, gibt Endverlust (mse) zurueck. mp = Mixed-Precision an/aus. */
static double train_xor(bool mp, int iters) {
    moo_ag_bf16_setzen(mp);
    MooValue netz = moo_list_new(2);
    moo_list_append(netz, mk_dicht(2, 8, "tanh", 7));
    moo_list_append(netz, mk_dicht(8, 1, "sigmoid", 8));
    MooValue params = moo_nn_parameter(netz);
    MooValue opt = moo_nn_opt_adam(params, moo_number(0.05));
    float xv[8] = { 0,0, 0,1, 1,0, 1,1 };
    float zv[4] = { 0, 1, 1, 0 };
    MooValue x = t2(4, 2, xv);
    MooValue z = t2(4, 1, zv);
    double last = -1.0;
    for (int it = 0; it < iters; it++) {
        MooValue y = moo_nn_vorwaerts(netz, x);
        MooValue loss = moo_nn_mse(y, z);
        last = (double)T(loss)->data[0];
        moo_release(moo_tensor_rueckwaerts(loss));
        moo_release(moo_nn_opt_schritt(opt));
        moo_release(loss); moo_release(y);
    }
    moo_release(x); moo_release(z); moo_release(opt);
    moo_release(params); moo_release(netz);
    moo_ag_reset();
    moo_ag_bf16_setzen(false);
    return last;
}

/* CE-Klassifikation (relu + logsoftmax + kreuzentropie), gibt Endverlust. */
static double train_cls(bool mp, int iters) {
    moo_ag_bf16_setzen(mp);
    MooValue netz = moo_list_new(2);
    moo_list_append(netz, mk_dicht(3, 8, "relu", 11));
    moo_list_append(netz, mk_dicht(8, 3, "keine", 12));
    MooValue params = moo_nn_parameter(netz);
    MooValue opt = moo_nn_opt_adam(params, moo_number(0.03));
    float xv[9] = { 1,0,0,  0,1,0,  0,0,1 };
    float zv[3] = { 0, 1, 2 };
    MooValue x = t2(3, 3, xv);
    MooValue z = t2(3, 1, zv);
    double last = -1.0;
    for (int it = 0; it < iters; it++) {
        MooValue y = moo_nn_vorwaerts(netz, x);
        MooValue loss = moo_nn_kreuzentropie(y, z);
        last = (double)T(loss)->data[0];
        moo_release(moo_tensor_rueckwaerts(loss));
        moo_release(moo_nn_opt_schritt(opt));
        moo_release(loss); moo_release(y);
    }
    moo_release(x); moo_release(z); moo_release(opt);
    moo_release(params); moo_release(netz);
    moo_ag_reset();
    moo_ag_bf16_setzen(false);
    return last;
}

int main(void) {
    /* ===== 1. moo_tensor_bf16_runden ===== */
    {
        /* bf16-exakte Werte (0, 1, 1.5, 2, -0.5) bleiben unveraendert. */
        float ex[5] = { 0.0f, 1.0f, 1.5f, 2.0f, -0.5f };
        MooValue a = t1(5, ex);
        moo_tensor_bf16_runden(T(a));
        bool exakt = true;
        for (int i = 0; i < 5; i++) if (T(a)->data[i] != ex[i]) exakt = false;
        CHECK(exakt, "bf16_runden: bf16-exakte Werte unveraendert");
        CHECK(T(a)->valid & MOO_V_DATA, "bf16_runden: data bleibt autoritativ");
        moo_release(a);

        /* Idempotenz + Identitaet zum Storage-Pfad als_dtype(bf16)+f32_sichern. */
        float vals[6] = { 1.1f, -3.14159f, 0.001234f, 123.456f, -0.0009f, 65504.0f };
        MooValue r1 = t1(6, vals);
        MooValue r2 = t1(6, vals);
        moo_tensor_bf16_runden(T(r1));
        /* Storage-Pfad: b -> bf16-store (data frei) -> zurueck nach f32. */
        MooValue s1 = moo_string_new("bf16");
        MooValue rb = moo_tensor_als_dtype(r2, s1);   /* r2 nun bf16-store, data==NULL */
        moo_release(s1);
        moo_release(rb);                               /* +1 aus Rueckgabe abbauen */
        moo_tensor_f32_sichern(T(r2));                 /* store -> data (gerundetes f32) */
        bool identisch = true;
        for (int i = 0; i < 6; i++)
            if (T(r1)->data[i] != T(r2)->data[i]) identisch = false;
        CHECK(identisch, "bf16_runden == als_dtype(bf16)+f32_sichern (bit-identisch)");
        /* Idempotenz: nochmal runden aendert nichts. */
        float snap[6];
        for (int i = 0; i < 6; i++) snap[i] = T(r1)->data[i];
        moo_tensor_bf16_runden(T(r1));
        bool idem = true;
        for (int i = 0; i < 6; i++) if (T(r1)->data[i] != snap[i]) idem = false;
        CHECK(idem, "bf16_runden idempotent");
        moo_release(r1); moo_release(r2);
    }

    /* ===== 2. Toggle-Zustand ===== */
    {
        moo_ag_bf16_setzen(false);
        CHECK(!moo_ag_bf16_an(), "MP default/gesetzt AUS");
        moo_ag_bf16_setzen(true);
        CHECK(moo_ag_bf16_an(), "MP nach setzen(true) AN");
        moo_ag_bf16_setzen(false);
        CHECK(!moo_ag_bf16_an(), "MP nach setzen(false) AUS");
    }

    /* ===== 3. Default-AUS-Regression: MP-Flag AUS == reines f32 ===== */
    double xor_f32_a = train_xor(false, 400);
    double xor_f32_b = train_xor(false, 400);
    CHECK(xor_f32_a == xor_f32_b, "f32 XOR reproduzierbar (bit-identisch)");
    CHECK(xor_f32_a < 0.01, "f32 XOR konvergiert (< 0.01)");

    /* ===== 4. XOR bf16 vs f32 ===== */
    double xor_bf16_a = train_xor(true, 400);
    double xor_bf16_b = train_xor(true, 400);
    fprintf(stderr, "XOR   f32=%.6f  bf16=%.6f (Lauf2 %.6f)\n",
            xor_f32_a, xor_bf16_a, xor_bf16_b);
    CHECK(xor_bf16_a == xor_bf16_b, "bf16 XOR DETERMINISTISCH (zwei Laeufe bit-identisch)");
    CHECK(xor_bf16_a < 0.05, "bf16 XOR konvergiert (< 0.05)");
    CHECK(fabs(xor_bf16_a - xor_f32_a) < 0.05, "bf16 XOR nahe f32 (Toleranz 0.05)");
    /* Nach den bf16-Laeufen wieder AUS -> f32 unveraendert (globaler Flag-Leak?). */
    double xor_f32_c = train_xor(false, 400);
    CHECK(xor_f32_c == xor_f32_a, "f32 nach bf16-Laeufen unveraendert (kein Flag-Leak)");

    /* ===== 5. CE-Klassifikation bf16 vs f32 ===== */
    double cls_f32_a  = train_cls(false, 300);
    double cls_f32_b  = train_cls(false, 300);
    double cls_bf16_a = train_cls(true, 300);
    double cls_bf16_b = train_cls(true, 300);
    fprintf(stderr, "CE    f32=%.6f  bf16=%.6f (Lauf2 %.6f)\n",
            cls_f32_a, cls_bf16_a, cls_bf16_b);
    CHECK(cls_f32_a == cls_f32_b, "f32 CE reproduzierbar");
    CHECK(cls_bf16_a == cls_bf16_b, "bf16 CE DETERMINISTISCH");
    CHECK(cls_bf16_a < cls_f32_a + 0.10, "bf16 CE konvergiert nahe f32 (Toleranz 0.10)");
    CHECK(cls_bf16_a < 0.5, "bf16 CE Endverlust klein (< 0.5)");

    fprintf(stderr, "test_mixed_precision_asan: %d Checks bestanden.\n", checks);
    printf("OK: %d checks\n", checks);
    return 0;
}
