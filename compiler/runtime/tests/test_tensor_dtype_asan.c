/**
 * test_tensor_dtype_asan.c — KIP-D1: bf16-Storage-DType (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Quell-Satz wie
 * test_nn_asan.c (moo_nn.c/moo_nn_easy.c/moo_json.c + Tensor-Kern); Test-
 * throw-Modell (kein moo_error.c), ASan detect_leaks=1 ist das harte Gate.
 *
 * PRUEFT (KIP-D1 GATE, D0-Audit-Plan §4):
 *   1. Roundtrip f32->bf16->f32: Normalwerte innerhalb bf16-Toleranz
 *      (half-ULP = 2^-8 relativ), NaN bleibt NaN, +/-Inf bleibt +/-Inf,
 *      Subnormal flusht zu ~0 (kein Garbage/Crash).
 *   2. Valid-Masken-Vertrag (D0 §2): dtype/valid/data/store-Uebergaenge bei
 *      als_dtype und beim lesenden Zugriff (Eintrittspunkt-Sicherung).
 *   3. Stale-Store-Regression (D0 §4.3): als_dtype(bf16) -> f32-Mutation
 *      (invalidiert store) -> als_dtype(bf16) -> Wert aktuell.
 *   4. bf16-Op-Matrix (D0 §4.3): jeder der 26+ Registry-Ops mit bf16-Input ==
 *      f32-Referenz auf denselben rundungs-gerundeten Werten. Da f32_sichern
 *      deterministisch bf16->f32 rematerialisiert, ist das BIT-EXAKT.
 *   5. kreuzentropie/mse/vorwaerts mit bf16-Input == rundungs-f32-Referenz.
 *   6. Alloc/Free-Zyklen (Konvertieren hin/her + Op) — ASan-Leak-Gate.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <stdarg.h>
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
#define REL(v) moo_release(v)

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

/* 2D-/1D-Tensor aus flachen f32-Werten (Rueckgabe +1 owning). */
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
/* moo-Liste aus ints (fuer Shape-/Index-Argumente). */
static MooValue liste_i(int n, ...) {
    va_list ap; MooValue l = moo_list_new(n);
    va_start(ap, n);
    for (int i = 0; i < n; i++) moo_list_append(l, moo_number(va_arg(ap, int)));
    va_end(ap);
    return l;
}

/* als_dtype in-place: Rueckgabe ist derselbe Tensor +1 owning -> Extra-Ref
 * wieder freigeben; dtype-String ist BORROWED -> hier freigeben. */
static void to_dtype(MooValue t, const char* dt) {
    MooValue s = moo_string_new(dt);
    MooValue r = moo_tensor_als_dtype(t, s);
    REL(s);
    if (r.tag == MOO_TENSOR) REL(r);
}

/* Bit-exakter Vergleich zweier f32-Ergebnis-Tensoren. */
static int teq(MooValue a, MooValue b) {
    if (a.tag != MOO_TENSOR || b.tag != MOO_TENSOR) return 0;
    MooTensor* x = T(a); MooTensor* y = T(b);
    if (x->ndim != y->ndim || x->size != y->size) return 0;
    for (int d = 0; d < x->ndim; d++) if (x->shape[d] != y->shape[d]) return 0;
    for (int64_t i = 0; i < x->size; i++) if (x->data[i] != y->data[i]) return 0;
    return 1;
}

/* Op-Matrix-Helfer: op(bf16-Input) muss bit-exakt op(rundungs-f32-Ref) sein. */
typedef MooValue (*UOp)(MooValue);
typedef MooValue (*SOp)(MooValue, MooValue);
static int chk_u(const char* nm, UOp fn, MooValue ab, MooValue ar) {
    MooValue g = fn(ab), r = fn(ar);
    int ok = teq(g, r); REL(g); REL(r);
    if (!ok) { fprintf(stderr, "FAIL op-matrix unary %s\n", nm); return 0; }
    checks++; return 1;
}
static int chk_s(const char* nm, SOp fn, MooValue ab, MooValue ar, MooValue z) {
    MooValue g = fn(ab, z), r = fn(ar, z);
    int ok = teq(g, r); REL(g); REL(r);
    if (!ok) { fprintf(stderr, "FAIL op-matrix scalar/reduce %s\n", nm); return 0; }
    checks++; return 1;
}
static int chk_b(const char* nm, SOp fn, MooValue xb, MooValue yb, MooValue xr, MooValue yr) {
    MooValue g = fn(xb, yb), r = fn(xr, yr);
    int ok = teq(g, r); REL(g); REL(r);
    if (!ok) { fprintf(stderr, "FAIL op-matrix binary %s\n", nm); return 0; }
    checks++; return 1;
}
#define OK(x) do { if (!(x)) return 1; } while (0)

/* schicht_dicht-Wrapper: Aktivierungs-String BORROWED -> hier freigeben. */
static MooValue mk_dicht(int ein, int aus, const char* akt, double seed) {
    MooValue a = akt ? moo_string_new(akt) : moo_none();
    MooValue s = (seed >= 0.0) ? moo_number(seed) : moo_none();
    MooValue d = moo_nn_schicht_dicht(moo_number(ein), moo_number(aus), a, s);
    REL(a);
    return d;
}

int main(void) {
    MooValue off = moo_ag_aus(); (void)off;   /* Autograd AUS: kein Tape in dieser Harness */

    /* Positive Werte, die in bf16 NICHT exakt sind (echtes Runden). */
    const float A[6] = { 0.1f, 1.7f, 2.3f, 0.333f, 1.111f, 2.718f };
    const float B[6] = { 1.3f, 0.7f, 2.1f, 0.9f,   1.4f,   3.3f   };
    const float C[6] = { 0.6f, 1.2f, 0.8f, 1.9f,   2.2f,   0.4f   };  /* 3x2 fuer matmul */

    /* ===== 1. Roundtrip + Spezialwerte ===== */
    {
        /* Normalwerte-Toleranz (half-ULP bf16 = 2^-8 relativ; Marge 2^-7). */
        MooValue ar = t2(2, 3, A); to_dtype(ar, "bf16"); to_dtype(ar, "f32");
        for (int i = 0; i < 6; i++) {
            double rel = fabs((double)T(ar)->data[i] - (double)A[i]) / fabs((double)A[i]);
            CHECK(rel <= (1.0 / 128.0), "roundtrip Normalwert innerhalb bf16-Toleranz");
        }
        REL(ar);

        float sv[8] = { 1.0f, -2.5f, 0.0f, nanf(""), INFINITY, -INFINITY, 1e-40f, 12345.0f };
        MooValue s = t1(8, sv); to_dtype(s, "bf16"); to_dtype(s, "f32");
        float* d = T(s)->data;
        CHECK(d[0] == 1.0f,  "roundtrip 1.0 exakt");
        CHECK(d[1] == -2.5f, "roundtrip -2.5 exakt");
        CHECK(d[2] == 0.0f,  "roundtrip 0.0 exakt");
        CHECK(isnan(d[3]),                 "roundtrip NaN bleibt NaN");
        CHECK(isinf(d[4]) && d[4] > 0.0f,  "roundtrip +Inf bleibt +Inf");
        CHECK(isinf(d[5]) && d[5] < 0.0f,  "roundtrip -Inf bleibt -Inf");
        CHECK(fabs((double)d[6]) < 1e-30,  "roundtrip Subnormal flusht zu ~0");
        CHECK(fabs(((double)d[7] - 12345.0) / 12345.0) < 0.01, "roundtrip 12345 rel<1%");
        REL(s);
    }

    /* ===== 2. Valid-Masken-Vertrag (D0 §2) ===== */
    {
        MooValue t = t2(2, 3, A);
        CHECK(T(t)->dtype == MOO_DT_F32 && T(t)->valid == MOO_V_DATA, "init: F32 + VALID=DATA");
        CHECK(T(t)->store == NULL, "init: kein store");
        to_dtype(t, "bf16");
        CHECK(T(t)->dtype == MOO_DT_BF16, "als_dtype bf16: dtype=BF16");
        CHECK(T(t)->valid == MOO_V_STORE, "bf16: VALID=STORE only");
        CHECK(T(t)->data == NULL && T(t)->store != NULL, "bf16: data frei, store da");
        /* lesender Zugriff (holen) sichert data via Eintrittspunkt */
        MooValue idx = liste_i(2, 0, 0);
        MooValue got = moo_tensor_holen(t, idx); (void)got; REL(idx);
        CHECK(T(t)->valid == (MOO_V_STORE | MOO_V_DATA), "nach Lesen: VALID=STORE|DATA");
        to_dtype(t, "f32");
        CHECK(T(t)->dtype == MOO_DT_F32 && T(t)->store == NULL && T(t)->valid == MOO_V_DATA,
              "zurueck zu F32: store frei, VALID=DATA");
        REL(t);
    }

    /* ===== 3. Stale-Store-Regression (D0 §4.3) ===== */
    {
        float v[2] = { 1.111f, 2.222f };
        MooValue t = t1(2, v);
        to_dtype(t, "bf16");                       /* store = bf16(1.111, 2.222) */
        MooValue idx = liste_i(1, 0);
        moo_tensor_setzen(t, idx, moo_number(9.0)); /* Eintritt sichert data, schreibt, invalidiert store */
        REL(idx);
        CHECK(T(t)->valid == MOO_V_DATA, "nach setzen: VALID=DATA (store stale-invalidiert)");
        to_dtype(t, "bf16");                       /* store aus aktuellem data neu bauen */
        to_dtype(t, "f32");
        MooValue idx2 = liste_i(1, 0);
        MooValue r = moo_tensor_holen(t, idx2); REL(idx2);
        CHECK(fabs(MV_NUM(r) - 9.0) < 1e-3, "stale-store behoben: Wert aktuell (9.0)");
        REL(t);
    }

    /* ===== 4. bf16-Op-Matrix (26+ Registry-Ops, bit-exakt) ===== */
    {
        /* bf16- und rundungs-f32-Varianten aller Operanden. */
        MooValue Ab = t2(2, 3, A); to_dtype(Ab, "bf16");
        MooValue Ar = t2(2, 3, A); to_dtype(Ar, "bf16"); to_dtype(Ar, "f32");
        MooValue Bb = t2(2, 3, B); to_dtype(Bb, "bf16");
        MooValue Br = t2(2, 3, B); to_dtype(Br, "bf16"); to_dtype(Br, "f32");
        MooValue Cb = t2(3, 2, C); to_dtype(Cb, "bf16");
        MooValue Cr = t2(3, 2, C); to_dtype(Cr, "bf16"); to_dtype(Cr, "f32");
        MooValue Db = t2(2, 3, B); to_dtype(Db, "bf16");   /* fuer verbinden (2x3) */
        MooValue Dr = t2(2, 3, B); to_dtype(Dr, "bf16"); to_dtype(Dr, "f32");

        /* Binaer elementwise + matmul + verbinden */
        OK(chk_b("add", moo_tensor_add, Ab, Bb, Ar, Br));
        OK(chk_b("sub", moo_tensor_sub, Ab, Bb, Ar, Br));
        OK(chk_b("mul", moo_tensor_mul, Ab, Bb, Ar, Br));
        OK(chk_b("div", moo_tensor_div, Ab, Bb, Ar, Br));
        OK(chk_b("matmul", moo_tensor_matmul, Ab, Cb, Ar, Cr));
        OK(chk_b("verbinden", moo_tensor_verbinden, Ab, Db, Ar, Dr));

        /* Skalar-Ops + pow */
        OK(chk_s("adds", moo_tensor_adds, Ab, Ar, moo_number(1.5)));
        OK(chk_s("subs", moo_tensor_subs, Ab, Ar, moo_number(1.5)));
        OK(chk_s("muls", moo_tensor_muls, Ab, Ar, moo_number(1.5)));
        OK(chk_s("divs", moo_tensor_divs, Ab, Ar, moo_number(1.5)));
        OK(chk_s("pow",  moo_tensor_pow,  Ab, Ar, moo_number(2.0)));

        /* Reduktionen (achse = -1: gesamt) */
        OK(chk_s("summe",   moo_tensor_summe,   Ab, Ar, moo_number(-1.0)));
        OK(chk_s("mittel",  moo_tensor_mittel,  Ab, Ar, moo_number(-1.0)));
        OK(chk_s("maximum", moo_tensor_maximum, Ab, Ar, moo_number(-1.0)));

        /* Unaere Ops (positive Inputs -> log/sqrt sicher) */
        OK(chk_u("exp",     moo_tensor_exp,     Ab, Ar));
        OK(chk_u("log",     moo_tensor_log,     Ab, Ar));
        OK(chk_u("sqrt",    moo_tensor_sqrt,    Ab, Ar));
        OK(chk_u("neg",     moo_tensor_neg,     Ab, Ar));
        OK(chk_u("relu",    moo_tensor_relu,    Ab, Ar));
        OK(chk_u("sigmoid", moo_tensor_sigmoid, Ab, Ar));
        OK(chk_u("tanh",    moo_tensor_tanh,    Ab, Ar));
        OK(chk_u("gelu",    moo_tensor_gelu,    Ab, Ar));
        OK(chk_u("softmax",    moo_tensor_softmax,    Ab, Ar));
        OK(chk_u("logsoftmax", moo_tensor_logsoftmax, Ab, Ar));
        OK(chk_u("transponieren", moo_tensor_transponieren, Ab, Ar));

        /* Layout-Ops mit Extra-Argumenten */
        {
            MooValue sh = liste_i(2, 3, 2);
            MooValue g = moo_tensor_umformen(Ab, sh), r = moo_tensor_umformen(Ar, sh);
            CHECK(teq(g, r), "op-matrix umformen bf16==ref"); REL(g); REL(r); REL(sh);
        }
        {
            MooValue g = moo_tensor_zeilen(Ab, moo_number(0), moo_number(1));
            MooValue r = moo_tensor_zeilen(Ar, moo_number(0), moo_number(1));
            CHECK(teq(g, r), "op-matrix zeilen bf16==ref"); REL(g); REL(r);
        }

        REL(Ab); REL(Ar); REL(Bb); REL(Br);
        REL(Cb); REL(Cr); REL(Db); REL(Dr);
    }

    /* ===== 5. kreuzentropie / mse / vorwaerts mit bf16-Input ===== */
    {
        /* mse */
        MooValue Pb = t2(2, 3, A); to_dtype(Pb, "bf16");
        MooValue Pr = t2(2, 3, A); to_dtype(Pr, "bf16"); to_dtype(Pr, "f32");
        MooValue Qb = t2(2, 3, B); to_dtype(Qb, "bf16");
        MooValue Qr = t2(2, 3, B); to_dtype(Qr, "bf16"); to_dtype(Qr, "f32");
        MooValue mg = moo_nn_mse(Pb, Qb), mr = moo_nn_mse(Pr, Qr);
        CHECK(teq(mg, mr), "mse bf16==ref"); REL(mg); REL(mr);
        REL(Pb); REL(Pr); REL(Qb); REL(Qr);

        /* kreuzentropie: Logits 2x3, Ziele Klassen-Indizes [2] */
        float lg[6] = { 1.1f, -0.7f, 2.3f, 0.4f, 1.9f, -1.2f };
        float zi[2] = { 0.0f, 2.0f };
        MooValue Lb = t2(2, 3, lg); to_dtype(Lb, "bf16");
        MooValue Lr = t2(2, 3, lg); to_dtype(Lr, "bf16"); to_dtype(Lr, "f32");
        MooValue Zb = t1(2, zi); to_dtype(Zb, "bf16");
        MooValue Zr = t1(2, zi); to_dtype(Zr, "bf16"); to_dtype(Zr, "f32");
        MooValue cg = moo_nn_kreuzentropie(Lb, Zb), cr = moo_nn_kreuzentropie(Lr, Zr);
        CHECK(teq(cg, cr), "kreuzentropie bf16==ref"); REL(cg); REL(cr);
        REL(Lb); REL(Lr); REL(Zb); REL(Zr);

        /* vorwaerts: dichte Schicht 3->2, bf16-Input */
        float xv[6] = { 0.2f, 1.3f, 2.1f, 0.9f, 1.6f, 0.5f };
        MooValue L = mk_dicht(3, 2, "keine", 7);
        MooValue Xb = t2(2, 3, xv); to_dtype(Xb, "bf16");
        MooValue Xr = t2(2, 3, xv); to_dtype(Xr, "bf16"); to_dtype(Xr, "f32");
        MooValue vg = moo_nn_vorwaerts(L, Xb), vr = moo_nn_vorwaerts(L, Xr);
        CHECK(teq(vg, vr), "vorwaerts bf16==ref"); REL(vg); REL(vr);
        REL(Xb); REL(Xr); REL(L);
    }

    /* ===== 6. als_dtype Fehlerpfad + Alloc/Free-Leak-Gate ===== */
    {
        MooValue t = t2(2, 3, A);
        MooValue bad = moo_string_new("int8");
        fehler_reset();
        MooValue r = moo_tensor_als_dtype(t, bad);  /* wirft, gibt none zurueck */
        REL(bad);
        CHECK(moo_error_flag && r.tag != MOO_TENSOR, "als_dtype unbekannter Typ wirft, kein Leak");
        fehler_reset();
        REL(t);
    }
    for (int i = 0; i < 2000; i++) {
        float v[16];
        for (int j = 0; j < 16; j++) v[j] = (float)(i % 7) + 0.3f * (float)j + 0.1f;
        MooValue t = t2(4, 4, v);
        to_dtype(t, "bf16"); to_dtype(t, "f32"); to_dtype(t, "bf16");
        MooValue r = moo_tensor_relu(t);   /* Op auf bf16-Input -> f32_sichern */
        REL(r); REL(t);
    }

    printf("test_tensor_dtype_asan: %d Checks bestanden.\n", checks);
    return 0;
}
