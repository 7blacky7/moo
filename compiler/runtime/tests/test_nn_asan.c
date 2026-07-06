/**
 * test_nn_asan.c — Plan-014 C1: Schichten/Loss/Optimizer (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES), Quell-Satz wie
 * test_autograd_asan.c plus moo_nn.c; Test-throw-Modell.
 *
 * PRUEFT (handgerechnete Referenzen):
 *   1. schicht_dicht: Formen, b=0, Init im Xavier-Limit, seed-deterministisch,
 *      requires_grad; Fehlerfaelle werfen
 *   2. Vorwaerts dicht: matmul+bias handgerechnet, Aktivierungen (sigmoid(0)=0.5)
 *   3. Netz-Liste: 2 Schichten sequenziell, Formen korrekt
 *   4. parameter(): 5 Tensoren aus [dicht, dropout, layernorm, embedding]
 *   5. mse handgerechnet; kreuzentropie one-hot == Index-Variante, Wert
 *      -log(0.5) bei Gleichverteilung; Index out-of-range wirft
 *   6. layernorm: Zeilen-Mittel ~0, Varianz ~1 (gamma=1, beta=0)
 *   7. dropout: Werte in {0, x/(1-rate)}, aktiv=0 -> Identitaet,
 *      seed-deterministisch
 *   8. embedding: Zeilen-Lookup via one-hot@W korrekt
 *   9. sgd/adam/adamw: EIN Schritt handgerechnet; Grads nach schritt genullt,
 *      Tape geleert
 *  10. XOR-KONVERGENZ-GATE: dicht(2,8,tanh)+dicht(8,1,sigmoid), adam(0.05),
 *      400 Iterationen -> Loss < 0.01 UND monoton unter Start-Loss.
 *      ASan detect_leaks=1 beweist: der Trainings-Loop leakt nicht.
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
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
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

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

/* 2D-Tensor aus flachen Werten. */
static MooValue t2(int r, int c, const float* vals) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    for (int i = 0; i < r * c; i++) t->data[i] = vals[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}
/* 1D-Tensor. */
static MooValue t1(int n, const float* vals) {
    int32_t shape[1] = { n };
    MooTensor* t = moo_tensor_raw(1, shape);
    for (int i = 0; i < n; i++) t->data[i] = vals[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

static MooValue dget_(MooValue d, const char* k) {
    return moo_dict_get(d, moo_string_new(k));
}

/* schicht_dicht-Wrapper: Aktivierungs-String ist BORROWED (Tensor-
 * Konvention) -> hier bauen UND wieder freigeben, sonst leakt der Harness. */
static MooValue mk_dicht(int ein, int aus, const char* akt, double seed) {
    MooValue a = akt ? moo_string_new(akt) : moo_none();
    MooValue s = (seed >= 0.0) ? moo_number(seed) : moo_none();
    MooValue d = moo_nn_schicht_dicht(moo_number(ein), moo_number(aus), a, s);
    moo_release(a);
    return d;
}

int main(void) {
    /* ===== 1. schicht_dicht: Konstruktion ===== */
    MooValue d1 = mk_dicht(2, 3, "keine", 7);
    CHECK(d1.tag == MOO_DICT, "dicht ist Dict");
    {
        MooValue w = dget_(d1, "w");
        MooValue b = dget_(d1, "b");
        CHECK(w.tag == MOO_TENSOR && T(w)->ndim == 2 &&
              T(w)->shape[0] == 2 && T(w)->shape[1] == 3, "w-Form [2,3]");
        CHECK(b.tag == MOO_TENSOR && T(b)->ndim == 2 &&
              T(b)->shape[0] == 1 && T(b)->shape[1] == 3, "b-Form [1,3]");
        CHECK(T(w)->requires_grad && T(b)->requires_grad, "w/b requires_grad");
        bool b_null = true;
        for (int i = 0; i < 3; i++) if (T(b)->data[i] != 0.0f) b_null = false;
        CHECK(b_null, "b startet bei 0");
        double limit = sqrt(6.0 / 5.0);  /* Xavier bei ein=2, aus=3 */
        bool im_limit = true;
        for (int i = 0; i < 6; i++)
            if (fabs((double)T(w)->data[i]) > limit) im_limit = false;
        CHECK(im_limit, "w im Xavier-Limit");
        /* deterministisch */
        MooValue d1b = mk_dicht(2, 3, "keine", 7);
        MooValue w2 = dget_(d1b, "w");
        bool gleich = true;
        for (int i = 0; i < 6; i++)
            if (T(w)->data[i] != T(w2)->data[i]) gleich = false;
        CHECK(gleich, "Init seed-deterministisch");
        moo_release(w); moo_release(b); moo_release(w2); moo_release(d1b);
    }
    /* Fehlerfaelle */
    fehler_reset();
    {
        MooValue xs = moo_string_new("x");
        MooValue bad = moo_nn_schicht_dicht(xs, moo_number(3), moo_none(), moo_none());
        CHECK(moo_error_flag == 1 && bad.tag == MOO_NONE, "dicht wirft bei Text-Eingabe");
        moo_release(xs);
    }
    fehler_reset();

    /* ===== 2. Vorwaerts dicht handgerechnet ===== */
    {
        MooValue w = dget_(d1, "w");
        MooValue b = dget_(d1, "b");
        float wv[6] = { 1, 0, 1,   0, 1, 1 };   /* [2,3] row-major */
        for (int i = 0; i < 6; i++) T(w)->data[i] = wv[i];
        for (int i = 0; i < 3; i++) T(b)->data[i] = 0.5f;
        float xv[2] = { 1, 2 };
        MooValue x = t2(1, 2, xv);
        MooValue y = moo_nn_vorwaerts(d1, x);
        CHECK(y.tag == MOO_TENSOR && T(y)->shape[0] == 1 && T(y)->shape[1] == 3,
              "vorwaerts Form [1,3]");
        CHECK(NAHE(T(y)->data[0], 1.5f) && NAHE(T(y)->data[1], 2.5f) &&
              NAHE(T(y)->data[2], 3.5f), "vorwaerts = x@w + b");
        moo_release(y); moo_release(x); moo_release(w); moo_release(b);
        moo_release(d1);
        moo_ag_reset();
    }

    /* Aktivierung: sigmoid(0) = 0.5 */
    {
        MooValue ds = mk_dicht(2, 2, "sigmoid", 1);
        MooValue w = dget_(ds, "w");
        for (int i = 0; i < 4; i++) T(w)->data[i] = 0.0f;
        moo_release(w);
        float xv[2] = { 3, -3 };
        MooValue x = t2(1, 2, xv);
        MooValue y = moo_nn_vorwaerts(ds, x);
        CHECK(NAHE(T(y)->data[0], 0.5f) && NAHE(T(y)->data[1], 0.5f),
              "sigmoid-Aktivierung");
        moo_release(y); moo_release(x); moo_release(ds);
        moo_ag_reset();
    }
    /* unbekannte Aktivierung wirft */
    fehler_reset();
    {
        MooValue dbad = mk_dicht(2, 2, "quark", -1.0);
        float xv[2] = { 1, 1 };
        MooValue x = t2(1, 2, xv);
        MooValue y = moo_nn_vorwaerts(dbad, x);
        CHECK(moo_error_flag == 1 && y.tag == MOO_NONE, "unbekannte Aktivierung wirft");
        moo_release(x); moo_release(dbad);
        fehler_reset(); moo_ag_reset();
    }

    /* ===== 3. Netz-Liste: 2 Schichten ===== */
    {
        MooValue l1 = mk_dicht(2, 4, "tanh", 3);
        MooValue l2 = mk_dicht(4, 1, "sigmoid", 4);
        MooValue netz = moo_list_new(2);
        moo_list_append(netz, l1);   /* transfer */
        moo_list_append(netz, l2);
        float xv[8] = { 0,0, 0,1, 1,0, 1,1 };
        MooValue x = t2(4, 2, xv);
        MooValue y = moo_nn_vorwaerts(netz, x);
        CHECK(y.tag == MOO_TENSOR && T(y)->shape[0] == 4 && T(y)->shape[1] == 1,
              "Netz-Liste Form [4,1]");
        moo_release(y); moo_release(x); moo_release(netz);
        moo_ag_reset();
    }

    /* ===== 4. parameter() ===== */
    {
        MooValue netz = moo_list_new(4);
        moo_list_append(netz, mk_dicht(2, 3, NULL, -1.0));
        moo_list_append(netz, moo_nn_schicht_dropout(moo_number(0.5)));
        moo_list_append(netz, moo_nn_schicht_layernorm(moo_number(3)));
        moo_list_append(netz, moo_nn_schicht_embedding(moo_number(5), moo_number(3),
                                                       moo_none()));
        MooValue ps = moo_nn_parameter(netz);
        CHECK(ps.tag == MOO_LIST && MV_LIST(ps)->length == 5,
              "parameter: 5 Tensoren (w,b,gamma,beta,emb-w)");
        for (int i = 0; i < 5; i++)
            CHECK(MV_LIST(ps)->items[i].tag == MOO_TENSOR &&
                  T(MV_LIST(ps)->items[i])->requires_grad, "Parameter requires_grad");
        moo_release(ps); moo_release(netz);
    }

    /* ===== 5. mse + kreuzentropie ===== */
    {
        float av[2] = { 1, 2 }, bv[2] = { 0, 0 };
        MooValue a = t2(1, 2, av);
        MooValue b = t2(1, 2, bv);
        MooValue m = moo_nn_mse(a, b);
        CHECK(m.tag == MOO_TENSOR && T(m)->size == 1 && NAHE(T(m)->data[0], 2.5f),
              "mse([1,2],[0,0]) = 2.5");
        moo_release(m); moo_release(a); moo_release(b);
        moo_ag_reset();
    }
    {
        float lv[2] = { 0, 0 };
        MooValue logits = t2(1, 2, lv);
        float ohv[2] = { 1, 0 };
        MooValue oh = t2(1, 2, ohv);
        MooValue c1 = moo_nn_kreuzentropie(logits, oh);
        CHECK(c1.tag == MOO_TENSOR && NAHE(T(c1)->data[0], 0.6931472f),
              "kreuzentropie one-hot = -log(0.5)");
        float iv[1] = { 0 };
        MooValue idx = t1(1, iv);
        MooValue c2 = moo_nn_kreuzentropie(logits, idx);
        CHECK(NAHE(T(c1)->data[0], T(c2)->data[0]), "Index-Variante == one-hot");
        moo_release(c1); moo_release(c2); moo_release(oh); moo_release(idx);
        /* Index ausserhalb wirft */
        fehler_reset();
        float bi[1] = { 5 };
        MooValue badix = t1(1, bi);
        MooValue c3 = moo_nn_kreuzentropie(logits, badix);
        CHECK(moo_error_flag == 1 && c3.tag == MOO_NONE, "CE-Index out-of-range wirft");
        fehler_reset();
        moo_release(badix); moo_release(logits);
        moo_ag_reset();
    }

    /* ===== 6. layernorm ===== */
    {
        MooValue ln = moo_nn_schicht_layernorm(moo_number(4));
        float xv[8] = { 1, 2, 3, 4,   10, 20, 30, 40 };
        MooValue x = t2(2, 4, xv);
        MooValue y = moo_nn_vorwaerts(ln, x);
        CHECK(y.tag == MOO_TENSOR, "layernorm liefert Tensor");
        for (int r = 0; r < 2; r++) {
            double s = 0, q = 0;
            for (int c = 0; c < 4; c++) s += (double)T(y)->data[r * 4 + c];
            double mean = s / 4.0;
            for (int c = 0; c < 4; c++) {
                double d = (double)T(y)->data[r * 4 + c] - mean;
                q += d * d;
            }
            CHECK(fabs(mean) < 1e-4, "layernorm Zeilen-Mittel ~0");
            CHECK(fabs(q / 4.0 - 1.0) < 1e-2, "layernorm Zeilen-Varianz ~1");
        }
        moo_release(y); moo_release(x); moo_release(ln);
        moo_ag_reset();
    }

    /* ===== 7. dropout ===== */
    {
        MooValue dr = moo_nn_schicht_dropout(moo_number(0.5));
        float xv[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        MooValue x = t2(2, 4, xv);
        MooValue y = moo_nn_vorwaerts(dr, x);
        bool werte_ok = true; int nullen = 0;
        for (int i = 0; i < 8; i++) {
            float v = T(y)->data[i];
            if (v == 0.0f) nullen++;
            else if (!NAHE(v, 2.0f)) werte_ok = false;
        }
        CHECK(werte_ok, "dropout: Werte in {0, x/(1-rate)}");
        CHECK(nullen > 0 && nullen < 8, "dropout: teils 0, teils skaliert");
        moo_release(y);
        /* aktiv=0 -> Identitaet */
        moo_dict_set(dr, moo_string_new("aktiv"), moo_number(0.0));
        MooValue y2 = moo_nn_vorwaerts(dr, x);
        bool ident = true;
        for (int i = 0; i < 8; i++) if (T(y2)->data[i] != 1.0f) ident = false;
        CHECK(ident, "dropout aktiv=0 = Identitaet");
        moo_release(y2);
        /* deterministisch: frischer Layer, gleicher Seed -> gleiche 1. Maske */
        MooValue dr2 = moo_nn_schicht_dropout(moo_number(0.5));
        MooValue dr3 = moo_nn_schicht_dropout(moo_number(0.5));
        MooValue m1 = moo_nn_vorwaerts(dr2, x);
        MooValue m2 = moo_nn_vorwaerts(dr3, x);
        bool det = true;
        for (int i = 0; i < 8; i++) if (T(m1)->data[i] != T(m2)->data[i]) det = false;
        CHECK(det, "dropout seed-deterministisch");
        moo_release(m1); moo_release(m2); moo_release(dr2); moo_release(dr3);
        moo_release(x); moo_release(dr);
        moo_ag_reset();
    }

    /* ===== 8. embedding ===== */
    {
        MooValue em = moo_nn_schicht_embedding(moo_number(3), moo_number(2),
                                               moo_number(9));
        MooValue w = dget_(em, "w");
        float wv[6] = { 10, 11,   20, 21,   30, 31 };  /* [3,2] */
        for (int i = 0; i < 6; i++) T(w)->data[i] = wv[i];
        moo_release(w);
        float iv[3] = { 0, 2, 1 };
        MooValue idx = t1(3, iv);
        MooValue y = moo_nn_vorwaerts(em, idx);
        CHECK(y.tag == MOO_TENSOR && T(y)->shape[0] == 3 && T(y)->shape[1] == 2,
              "embedding Form [3,2]");
        CHECK(NAHE(T(y)->data[0], 10) && NAHE(T(y)->data[2], 30) &&
              NAHE(T(y)->data[5], 21), "embedding Zeilen-Lookup");
        moo_release(y);
        /* Index ausserhalb wirft */
        fehler_reset();
        float bi[1] = { 7 };
        MooValue bidx = t1(1, bi);
        MooValue yb = moo_nn_vorwaerts(em, bidx);
        CHECK(moo_error_flag == 1 && yb.tag == MOO_NONE, "embedding-Index wirft");
        fehler_reset();
        moo_release(bidx); moo_release(idx); moo_release(em);
        moo_ag_reset();
    }

    /* ===== 9. Optimizer handgerechnet ===== */
    /* sgd: p=[1,2], loss=sum(p) -> g=[1,1]; rate 0.5 -> p=[0.5,1.5] */
    {
        float pv[2] = { 1, 2 };
        MooValue p = t1(2, pv);
        T(p)->requires_grad = true;
        MooValue params = moo_list_new(1);
        moo_retain(p); moo_list_append(params, p);
        MooValue opt = moo_nn_opt_sgd(params, moo_number(0.5), moo_none());
        CHECK(opt.tag == MOO_DICT, "sgd-Optimizer ist Dict");
        MooValue loss = moo_tensor_summe(p, moo_number(-1.0));
        MooValue rw = moo_tensor_rueckwaerts(loss);
        (void)rw;
        MooValue st = moo_nn_opt_schritt(opt);
        (void)st;
        CHECK(NAHE(T(p)->data[0], 0.5f) && NAHE(T(p)->data[1], 1.5f),
              "sgd-Schritt: p -= rate*g");
        bool gnull = (T(p)->grad[0] == 0.0f && T(p)->grad[1] == 0.0f);
        CHECK(gnull, "sgd: Grads nach schritt genullt");
        /* schritt ohne neuen backward: g=0 -> p unveraendert */
        MooValue st2 = moo_nn_opt_schritt(opt);
        (void)st2;
        CHECK(NAHE(T(p)->data[0], 0.5f), "schritt mit g=0 aendert nichts");
        moo_release(loss); moo_release(opt); moo_release(params); moo_release(p);
    }
    /* adam t=1: g=1 -> Update ~ -lr (Bias-Korrektur kompensiert) */
    {
        float pv[1] = { 1.0f };
        MooValue p = t1(1, pv);
        T(p)->requires_grad = true;
        MooValue params = moo_list_new(1);
        moo_retain(p); moo_list_append(params, p);
        MooValue opt = moo_nn_opt_adam(params, moo_number(0.1));
        MooValue loss = moo_tensor_summe(p, moo_number(-1.0));
        moo_release(moo_tensor_rueckwaerts(loss));
        moo_release(moo_nn_opt_schritt(opt));
        /* mhat=1, vhat=1 -> p = 1 - 0.1*1/(1+1e-8) ~ 0.9 */
        CHECK(NAHE(T(p)->data[0], 0.9f), "adam t=1: p ~ 1 - lr");
        moo_release(loss); moo_release(opt); moo_release(params); moo_release(p);
    }
    /* adamw: decoupled decay zuerst: p=1,lr=0.1,wd=0.01 -> 0.999 - 0.1 ~ 0.899 */
    {
        float pv[1] = { 1.0f };
        MooValue p = t1(1, pv);
        T(p)->requires_grad = true;
        MooValue params = moo_list_new(1);
        moo_retain(p); moo_list_append(params, p);
        MooValue opt = moo_nn_opt_adamw(params, moo_number(0.1), moo_number(0.01));
        MooValue loss = moo_tensor_summe(p, moo_number(-1.0));
        moo_release(moo_tensor_rueckwaerts(loss));
        moo_release(moo_nn_opt_schritt(opt));
        CHECK(fabs((double)T(p)->data[0] - 0.899) < 1e-3, "adamw: decay + adam");
        moo_release(loss); moo_release(opt); moo_release(params); moo_release(p);
    }
    /* schritt auf Nicht-Optimizer wirft */
    fehler_reset();
    {
        MooValue nix = moo_dict_new();
        MooValue r = moo_nn_opt_schritt(nix);
        CHECK(moo_error_flag == 1 && r.tag == MOO_NONE, "schritt auf Nicht-Opt wirft");
        fehler_reset();
        moo_release(nix);
    }

    /* ===== 10. XOR-KONVERGENZ-GATE (das C1-Gate auf C-Ebene) ===== */
    {
        MooValue netz = moo_list_new(2);
        moo_list_append(netz, mk_dicht(2, 8, "tanh", 7));
        moo_list_append(netz, mk_dicht(8, 1, "sigmoid", 8));
        MooValue params = moo_nn_parameter(netz);
        MooValue opt = moo_nn_opt_adam(params, moo_number(0.05));
        float xv[8] = { 0,0, 0,1, 1,0, 1,1 };
        float zv[4] = { 0, 1, 1, 0 };
        MooValue x = t2(4, 2, xv);
        MooValue z = t2(4, 1, zv);

        double loss_start = -1.0, loss_ende = -1.0;
        for (int it = 0; it < 400; it++) {
            MooValue y = moo_nn_vorwaerts(netz, x);
            MooValue loss = moo_nn_mse(y, z);
            if (it == 0) loss_start = (double)T(loss)->data[0];
            loss_ende = (double)T(loss)->data[0];
            moo_release(moo_tensor_rueckwaerts(loss));
            moo_release(moo_nn_opt_schritt(opt));
            moo_release(loss); moo_release(y);
        }
        fprintf(stderr, "XOR: Loss %f -> %f (400 Iterationen, adam 0.05)\n",
                loss_start, loss_ende);
        CHECK(loss_ende < 0.01, "XOR konvergiert: Loss < 0.01");
        CHECK(loss_ende < loss_start, "XOR: Loss gesunken");
        /* Vorhersagen: Runden ergibt exakt XOR */
        moo_ag_aus();
        MooValue y = moo_nn_vorwaerts(netz, x);
        CHECK(T(y)->data[0] < 0.5f && T(y)->data[1] > 0.5f &&
              T(y)->data[2] > 0.5f && T(y)->data[3] < 0.5f,
              "XOR-Vorhersagen korrekt gerundet");
        moo_release(y);
        moo_ag_an();
        moo_release(x); moo_release(z); moo_release(opt);
        moo_release(params); moo_release(netz);
        moo_ag_reset();
    }

    /* ===== 11. Kinderleicht-API (Plan-014 D1) ===== */
    /* ki_netz + trainiere: XOR mit mse (kein softmax -> auto=mse) */
    {
        MooValue schichten = moo_list_new(2);
        moo_list_append(schichten, mk_dicht(2, 8, "tanh", 7));
        moo_list_append(schichten, mk_dicht(8, 1, "sigmoid", 8));
        MooValue netz = moo_nn_ki_netz(schichten);
        CHECK(netz.tag == MOO_DICT, "ki_netz ist Dict");

        float xv[8] = { 0,0, 0,1, 1,0, 1,1 };
        float zv[4] = { 0, 1, 1, 0 };
        MooValue x = t2(4, 2, xv);
        MooValue z = t2(4, 1, zv);
        MooValue opts = moo_dict_new();
        moo_dict_set(opts, moo_string_new("epochen"), moo_number(400.0));
        moo_dict_set(opts, moo_string_new("rate"), moo_number(0.05));
        moo_dict_set(opts, moo_string_new("batch"), moo_number(4.0));
        moo_dict_set(opts, moo_string_new("ausgabe"), moo_number(0.0));
        MooValue hist = moo_nn_trainiere(netz, x, z, opts);
        CHECK(hist.tag == MOO_LIST && MV_LIST(hist)->length == 400,
              "trainiere: Historie hat eine Zahl pro Epoche");
        double h0 = MV_NUM(MV_LIST(hist)->items[0]);
        double hE = MV_NUM(MV_LIST(hist)->items[399]);
        fprintf(stderr, "kinderleicht XOR: Fehler %f -> %f\n", h0, hE);
        CHECK(hE < 0.01 && hE < h0, "trainiere: XOR konvergiert");

        /* genauigkeit: 1-Spalten-Runden -> 4/4 korrekt */
        MooValue acc = moo_nn_genauigkeit(netz, x, z);
        CHECK(acc.tag == MOO_NUMBER && NAHE(MV_NUM(acc), 1.0), "genauigkeit == 1.0");

        /* vorhersage laesst den ag-Zustand in Ruhe + zeichnet nichts auf */
        CHECK(moo_ag_ist_an(), "ag ist vor vorhersage an");
        MooValue vh = moo_nn_vorhersage(netz, x);
        CHECK(vh.tag == MOO_TENSOR && moo_ag_ist_an(), "vorhersage: ag-Zustand erhalten");

        /* speichern/laden-Roundtrip: bit-identische Vorhersage */
        MooValue pfad = moo_string_new("/tmp/test_d1_netz.mook");
        moo_release(moo_nn_speichern(netz, pfad));
        MooValue netz2 = moo_nn_laden(pfad);
        CHECK(netz2.tag == MOO_DICT, "ki_laden liefert Netz");
        MooValue vh2 = moo_nn_vorhersage(netz2, x);
        CHECK(vh2.tag == MOO_TENSOR, "geladenes Netz sagt vorher");
        bool bitgleich = true;
        for (int i = 0; i < 4; i++)
            if (T(vh)->data[i] != T(vh2)->data[i]) bitgleich = false;
        CHECK(bitgleich, "Roundtrip: Vorhersage bit-identisch");
        remove("/tmp/test_d1_netz.mook");
        moo_release(vh); moo_release(vh2); moo_release(netz2); moo_release(pfad);
        moo_release(hist); moo_release(opts);
        moo_release(x); moo_release(z);
        moo_release(netz); moo_release(schichten);
        moo_ag_reset();
    }
    /* softmax-Ende -> auto-Kreuzentropie: 3-Klassen-Spielzeug lernt Mapping */
    {
        MooValue schichten = moo_list_new(2);
        moo_list_append(schichten, mk_dicht(2, 8, "tanh", 11));
        moo_list_append(schichten, mk_dicht(8, 3, "softmax", 12));
        MooValue netz = moo_nn_ki_netz(schichten);
        float xv[6] = { 0,0, 1,0, 0,1 };
        float yv[3] = { 0, 1, 2 };   /* Klassen-Indizes */
        MooValue x = t2(3, 2, xv);
        MooValue y = t1(3, yv);
        MooValue opts = moo_dict_new();
        moo_dict_set(opts, moo_string_new("epochen"), moo_number(300.0));
        moo_dict_set(opts, moo_string_new("rate"), moo_number(0.05));
        moo_dict_set(opts, moo_string_new("ausgabe"), moo_number(0.0));
        MooValue hist = moo_nn_trainiere(netz, x, y, opts);
        CHECK(hist.tag == MOO_LIST, "trainiere (CE): laeuft");
        double hE = MV_NUM(MV_LIST(hist)->items[299]);
        double h0 = MV_NUM(MV_LIST(hist)->items[0]);
        fprintf(stderr, "kinderleicht 3-Klassen-CE: Fehler %f -> %f\n", h0, hE);
        CHECK(hE < 0.1 && hE < h0, "trainiere (CE): Loss gesunken");
        MooValue acc = moo_nn_genauigkeit(netz, x, y);
        CHECK(NAHE(MV_NUM(acc), 1.0), "genauigkeit (argmax vs Index) == 1.0");
        moo_release(hist); moo_release(opts); moo_release(x); moo_release(y);
        moo_release(netz); moo_release(schichten);
        moo_ag_reset();
    }
    /* Fehlerfaelle erklaeren statt crashen */
    fehler_reset();
    {
        MooValue schichten = moo_list_new(1);
        moo_list_append(schichten, mk_dicht(4, 1, NULL, -1.0));
        MooValue netz = moo_nn_ki_netz(schichten);
        float xv[6] = { 1,2,3, 4,5,6 };   /* 3 Spalten, Netz will 4 */
        float zv[2] = { 0, 1 };
        MooValue x = t2(2, 3, xv);
        MooValue z = t2(2, 1, zv);
        MooValue r = moo_nn_trainiere(netz, x, z, moo_none());
        CHECK(moo_error_flag == 1 && r.tag == MOO_NONE,
              "trainiere: Shape-Mismatch wirft erklaerend");
        fehler_reset();
        MooValue r2 = moo_nn_laden(moo_number(5.0));
        CHECK(moo_error_flag == 1 && r2.tag == MOO_NONE, "ki_laden: Zahl wirft");
        fehler_reset();
        moo_release(x); moo_release(z); moo_release(netz); moo_release(schichten);
        moo_ag_reset();
    }

    /* ===== 12. Fremd-safetensors-Import (Plan-014 F1) ===== */
    {
        /* Fixture wie von einem fremden Tool: KEIN moo_arch, 2 Tensoren */
        const char* hj = "{\"w\":{\"dtype\":\"F32\",\"shape\":[2,2],"
            "\"data_offsets\":[0,16]},\"b\":{\"dtype\":\"F32\",\"shape\":[2],"
            "\"data_offsets\":[16,24]}}";
        uint64_t hl = (uint64_t)strlen(hj);
        FILE* f = fopen("/tmp/test_f1_fremd.safetensors", "wb");
        fwrite(&hl, 8, 1, f);
        fwrite(hj, 1, (size_t)hl, f);
        float w[4] = { 1.5f, -2.0f, 0.25f, 4.0f };
        float b[2] = { 0.5f, -1.0f };
        fwrite(w, sizeof(float), 4, f);
        fwrite(b, sizeof(float), 2, f);
        fclose(f);

        MooValue p = moo_string_new("/tmp/test_f1_fremd.safetensors");
        MooValue d = moo_nn_safetensors(p);
        moo_release(p);
        CHECK(d.tag == MOO_DICT, "safetensors_laden liefert Dict");
        MooValue wv = dget_(d, "w");
        MooValue bv = dget_(d, "b");
        CHECK(wv.tag == MOO_TENSOR && T(wv)->ndim == 2 &&
              T(wv)->shape[0] == 2 && T(wv)->shape[1] == 2, "Fremd-w [2,2]");
        CHECK(bv.tag == MOO_TENSOR && T(bv)->ndim == 1 && T(bv)->shape[0] == 2,
              "Fremd-b [2]");
        CHECK(T(wv)->data[0] == 1.5f && T(wv)->data[3] == 4.0f &&
              T(bv)->data[1] == -1.0f, "Fremd-Werte bit-exakt");
        moo_release(wv); moo_release(bv); moo_release(d);
        remove("/tmp/test_f1_fremd.safetensors");

        /* .mook-Datei ist GLEICHZEITIG per safetensors_laden lesbar
         * (Format-Kompatibilitaets-Beweis in Gegenrichtung) */
        MooValue schichten = moo_list_new(1);
        moo_list_append(schichten, mk_dicht(2, 2, "keine", 5));
        MooValue netz = moo_nn_ki_netz(schichten);
        MooValue mp = moo_string_new("/tmp/test_f1_eigen.mook");
        moo_release(moo_nn_speichern(netz, mp));
        MooValue roh = moo_nn_safetensors(mp);
        CHECK(roh.tag == MOO_DICT, ".mook via safetensors_laden lesbar");
        MooValue p0 = dget_(roh, "p0");
        MooValue params = moo_nn_parameter(netz);
        CHECK(p0.tag == MOO_TENSOR &&
              p0.tag == MOO_TENSOR &&
              T(p0)->data[0] == T(MV_LIST(params)->items[0])->data[0],
              ".mook-p0 == Netz-Gewicht (bit)");
        moo_release(p0); moo_release(params); moo_release(roh);
        remove("/tmp/test_f1_eigen.mook");
        moo_release(mp); moo_release(netz); moo_release(schichten);

        /* dtype != F32 wirft erklaerend */
        fehler_reset();
        const char* hj16 = "{\"x\":{\"dtype\":\"F16\",\"shape\":[1],"
            "\"data_offsets\":[0,2]}}";
        uint64_t hl16 = (uint64_t)strlen(hj16);
        f = fopen("/tmp/test_f1_f16.safetensors", "wb");
        fwrite(&hl16, 8, 1, f);
        fwrite(hj16, 1, (size_t)hl16, f);
        unsigned char halb[2] = { 0, 60 };
        fwrite(halb, 1, 2, f);
        fclose(f);
        MooValue p16 = moo_string_new("/tmp/test_f1_f16.safetensors");
        MooValue d16 = moo_nn_safetensors(p16);
        moo_release(p16);
        CHECK(moo_error_flag == 1 && d16.tag == MOO_NONE, "F16 wirft erklaerend");
        fehler_reset();
        remove("/tmp/test_f1_f16.safetensors");
        moo_ag_reset();
    }

    /* ===== 13. Trainings-Techniken (Plan-014 E2) ===== */
    {
        float xv[8] = { 0,0, 0,1, 1,0, 1,1 };
        float zv[4] = { 0, 1, 1, 0 };
        /* a) grad_clip handgerechnet: 2 Params, grads (3,4) -> Norm 5,
         *    max 1 -> beide /5 */
        {
            MooValue p1 = t1(1, (float[]){ 0.0f });
            MooValue p2 = t1(1, (float[]){ 0.0f });
            T(p1)->grad = (float*)calloc(1, sizeof(float));
            T(p2)->grad = (float*)calloc(1, sizeof(float));
            T(p1)->grad[0] = 3.0f; T(p2)->grad[0] = 4.0f;
            MooValue liste = moo_list_new(2);
            moo_retain(p1); moo_list_append(liste, p1);
            moo_retain(p2); moo_list_append(liste, p2);
            MooValue norm = moo_nn_grad_clip(liste, moo_number(1.0));
            CHECK(norm.tag == MOO_NUMBER && NAHE(MV_NUM(norm), 5.0),
                  "grad_clip: Rueckgabe = Norm vor Kappen (5)");
            CHECK(NAHE(T(p1)->grad[0], 0.6) && NAHE(T(p2)->grad[0], 0.8),
                  "grad_clip: Grads auf Norm 1 skaliert (0.6, 0.8)");
            moo_release(liste); moo_release(p1); moo_release(p2);
        }
        /* b) Clipping verhindert Explosion: relu+linear (KEINE saettigende
         *    Aktivierung — sigmoid wuerde nur saturieren statt explodieren),
         *    sgd rate=10 divergiert, mit clip bleibt es endlich+klein */
        {
            double ende[2];
            for (int mit_clip = 0; mit_clip < 2; mit_clip++) {
                MooValue schichten = moo_list_new(2);
                moo_list_append(schichten, mk_dicht(2, 8, "relu", 21));
                moo_list_append(schichten, mk_dicht(8, 1, "keine", 22));
                MooValue netz = moo_nn_ki_netz(schichten);
                MooValue x = t2(4, 2, xv);
                MooValue z = t2(4, 1, zv);
                MooValue o = moo_dict_new();
                moo_dict_set(o, moo_string_new("epochen"), moo_number(60.0));
                moo_dict_set(o, moo_string_new("rate"), moo_number(10.0));
                moo_dict_set(o, moo_string_new("optimierer"), moo_string_new("sgd"));
                moo_dict_set(o, moo_string_new("ausgabe"), moo_number(0.0));
                if (mit_clip)
                    moo_dict_set(o, moo_string_new("clip"), moo_number(1.0));
                MooValue h = moo_nn_trainiere(netz, x, z, o);
                MooList* hl = MV_LIST(h);
                ende[mit_clip] = MV_NUM(hl->items[hl->length - 1]);
                moo_release(h); moo_release(o); moo_release(x); moo_release(z);
                moo_release(netz); moo_release(schichten);
                moo_ag_reset();
            }
            fprintf(stderr, "clip-Effekt bei sgd rate=10: ohne=%f mit=%f\n",
                    ende[0], ende[1]);
            CHECK(!isfinite(ende[0]) || ende[0] > 1000.0,
                  "ohne clip: sgd rate=10 explodiert (Beweis der Gefahr)");
            CHECK(isfinite(ende[1]) && ende[1] < 100.0,
                  "clip: Training bleibt endlich+stabil trotz rate=10");
        }
        /* c) geduld: rate=1e-12 -> Updates weit unter min_besserung
         *    (rate=0 lehnt der Optimizer korrekt ab) + checkpoint */
        {
            MooValue schichten = moo_list_new(2);
            moo_list_append(schichten, mk_dicht(2, 8, "tanh", 31));
            moo_list_append(schichten, mk_dicht(8, 1, "sigmoid", 32));
            MooValue netz = moo_nn_ki_netz(schichten);
            MooValue x = t2(4, 2, xv);
            MooValue z = t2(4, 1, zv);
            MooValue o = moo_dict_new();
            moo_dict_set(o, moo_string_new("epochen"), moo_number(50.0));
            moo_dict_set(o, moo_string_new("rate"), moo_number(1e-12));
            moo_dict_set(o, moo_string_new("geduld"), moo_number(3.0));
            moo_dict_set(o, moo_string_new("min_besserung"), moo_number(0.001));
            moo_dict_set(o, moo_string_new("ausgabe"), moo_number(0.0));
            moo_dict_set(o, moo_string_new("checkpoint"),
                         moo_string_new("/tmp/test_e2_ckpt.mook"));
            MooValue h = moo_nn_trainiere(netz, x, z, o);
            CHECK(h.tag == MOO_LIST && MV_LIST(h)->length == 4,
                  "geduld: Stillstand stoppt nach 1+3 Epochen (Historie == 4)");
            FILE* cf = fopen("/tmp/test_e2_ckpt.mook", "rb");
            CHECK(cf != NULL, "checkpoint: Datei existiert");
            if (cf) fclose(cf);
            MooValue cp = moo_string_new("/tmp/test_e2_ckpt.mook");
            MooValue geladen = moo_nn_laden(cp);
            CHECK(geladen.tag == MOO_DICT, "checkpoint: laedt als Netz");
            moo_release(geladen); moo_release(cp);
            remove("/tmp/test_e2_ckpt.mook");
            moo_release(h); moo_release(o);

            /* cosine vs. keiner: gleicher Seed, verschiedene End-Losses */
            double ende[2];
            for (int mit_plan = 0; mit_plan < 2; mit_plan++) {
                MooValue s2 = moo_list_new(2);
                moo_list_append(s2, mk_dicht(2, 8, "tanh", 41));
                moo_list_append(s2, mk_dicht(8, 1, "sigmoid", 42));
                MooValue n2 = moo_nn_ki_netz(s2);
                MooValue o2 = moo_dict_new();
                moo_dict_set(o2, moo_string_new("epochen"), moo_number(40.0));
                moo_dict_set(o2, moo_string_new("rate"), moo_number(0.05));
                moo_dict_set(o2, moo_string_new("ausgabe"), moo_number(0.0));
                if (mit_plan)
                    moo_dict_set(o2, moo_string_new("lr_plan"),
                                 moo_string_new("cosine"));
                MooValue h2 = moo_nn_trainiere(n2, x, z, o2);
                MooList* hl = MV_LIST(h2);
                ende[mit_plan] = MV_NUM(hl->items[hl->length - 1]);
                CHECK(isfinite(ende[mit_plan]), "lr_plan: Loss endlich");
                moo_release(h2); moo_release(o2);
                moo_release(n2); moo_release(s2);
                moo_ag_reset();
            }
            CHECK(ende[0] != ende[1], "lr_plan cosine veraendert den Verlauf");
            moo_release(x); moo_release(z);
            moo_release(netz); moo_release(schichten);
            moo_ag_reset();
        }
    }

    /* ===== 14. Transformer-Bausteine (Plan-014 G1) ===== */
    {
        /* a) Attention: Shape + CAUSAL-Beweis + Grad-Fluss */
        MooValue at = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                                               moo_number(7.0), moo_none(),
                                               moo_none(), moo_none());
        CHECK(at.tag == MOO_DICT, "schicht_attention baut Dict");
        float xv[32];
        for (int i = 0; i < 32; i++) xv[i] = (float)(i % 5) * 0.1f;
        MooValue x = t2(4, 8, xv);
        MooValue y = moo_nn_vorwaerts(at, x);
        CHECK(y.tag == MOO_TENSOR && T(y)->ndim == 2 &&
              T(y)->shape[0] == 4 && T(y)->shape[1] == 8,
              "attention: [4,8] -> [4,8]");

        /* CAUSAL: Zeile 3 der Eingabe aendern darf Zeilen 0-2 der Ausgabe
         * NICHT aendern (Maske blockt Zukunft). */
        float xv2[32];
        memcpy(xv2, xv, sizeof(xv));
        for (int j = 0; j < 8; j++) xv2[24 + j] += 3.0f;
        MooValue x2 = t2(4, 8, xv2);
        MooValue y2 = moo_nn_vorwaerts(at, x2);
        bool causal_ok = (y2.tag == MOO_TENSOR);
        for (int i = 0; causal_ok && i < 24; i++)
            causal_ok = (T(y)->data[i] == T(y2)->data[i]);
        bool zukunft_wirkt = false;
        for (int j = 0; y2.tag == MOO_TENSOR && j < 8; j++)
            if (T(y)->data[24 + j] != T(y2)->data[24 + j]) zukunft_wirkt = true;
        CHECK(causal_ok, "attention: causal — Zukunft aendert Vergangenheit nicht");
        CHECK(zukunft_wirkt, "attention: eigene Zeile reagiert (kein Konstant-Bug)");
        moo_release(y2); moo_release(x2);
        moo_release(y);

        /* Grad-Fluss: mean(attention(x)) rueckwaerts -> Grads auf ALLEN
         * 7 Parametern (2 Koepfe * 3 + wo) */
        moo_ag_reset();
        MooValue xg = moo_tensor_mit_gradient(x);
        MooValue yg = moo_nn_vorwaerts(at, xg);
        MooValue m = moo_tensor_mittel(yg, moo_none());
        moo_release(moo_tensor_rueckwaerts(m));
        MooValue ps = moo_nn_parameter(at);
        CHECK(ps.tag == MOO_LIST && MV_LIST(ps)->length == 7,
              "attention: 7 Parameter (2*3 Koepfe + wo)");
        bool grads_da = true;
        for (int32_t i = 0; i < MV_LIST(ps)->length; i++) {
            MooTensor* p = MV_TENSOR(MV_LIST(ps)->items[i]);
            bool einer = false;
            if (p->grad)
                for (int64_t j = 0; j < p->size && !einer; j++)
                    if (p->grad[j] != 0.0f) einer = true;
            grads_da = grads_da && einer;
        }
        CHECK(grads_da, "attention: Gradient fliesst in alle Parameter");
        moo_release(ps); moo_release(m); moo_release(yg); moo_release(xg);
        moo_release(x); moo_release(at);
        moo_ag_reset();

        /* b) Position: sinus konstant + gelernt bekommt Gradient */
        MooValue art_sin = moo_string_new("sinus");
        MooValue ps_sin = moo_nn_schicht_position(moo_number(4.0), moo_number(8.0),
                                                  art_sin, moo_none());
        moo_release(art_sin);
        MooValue px = t2(4, 8, xv);
        MooValue py = moo_nn_vorwaerts(ps_sin, px);
        CHECK(py.tag == MOO_TENSOR && T(py)->shape[0] == 4, "position sinus laeuft");
        /* sin(0)=0, cos(0)=1: Zeile 0 -> x + [0,1,0,1,...] */
        CHECK(NAHE(T(py)->data[0], xv[0] + 0.0) && NAHE(T(py)->data[1], xv[1] + 1.0),
              "position sinus: Zeile 0 == x + [sin0, cos0, ...]");
        MooValue prm_sin = moo_nn_parameter(ps_sin);
        CHECK(prm_sin.tag == MOO_LIST && MV_LIST(prm_sin)->length == 0,
              "position sinus: KEIN Parameter (konstant)");
        moo_release(prm_sin); moo_release(py); moo_release(ps_sin);

        MooValue ps_l = moo_nn_schicht_position(moo_number(4.0), moo_number(8.0),
                                                moo_none(), moo_number(9.0));
        moo_ag_reset();
        MooValue pxg = moo_tensor_mit_gradient(px);
        MooValue pyl = moo_nn_vorwaerts(ps_l, pxg);
        MooValue pm = moo_tensor_mittel(pyl, moo_none());
        moo_release(moo_tensor_rueckwaerts(pm));
        MooValue prm = moo_nn_parameter(ps_l);
        CHECK(prm.tag == MOO_LIST && MV_LIST(prm)->length == 1,
              "position gelernt: 1 Parameter");
        MooTensor* pt = MV_TENSOR(MV_LIST(prm)->items[0]);
        CHECK(pt->grad && pt->grad[0] != 0.0f,
              "position gelernt: Gradient fliesst in pos");
        moo_release(prm); moo_release(pm); moo_release(pyl); moo_release(pxg);
        moo_release(px); moo_release(ps_l);
        moo_ag_reset();
    }

    /* ===== 15. Transformer-Serialisierung (G1 Teil 2) ===== */
    {
        MooValue schichten = moo_list_new(3);
        moo_list_append(schichten, moo_nn_schicht_position(moo_number(4.0),
                        moo_number(8.0), moo_none(), moo_number(11.0)));
        moo_list_append(schichten, moo_nn_schicht_attention(moo_number(8.0),
                        moo_number(2.0), moo_number(12.0), moo_none(),
                        moo_none(), moo_none()));
        moo_list_append(schichten, mk_dicht(8, 3, "keine", 13));
        MooValue netz = moo_nn_ki_netz(schichten);
        float xv[32];
        for (int i = 0; i < 32; i++) xv[i] = (float)(i % 7) * 0.2f - 0.5f;
        MooValue x = t2(4, 8, xv);
        MooValue y1 = moo_nn_vorwaerts(netz, x);
        MooValue pf = moo_string_new("/tmp/test_g1_tf.mook");
        moo_release(moo_nn_speichern(netz, pf));
        MooValue netz2 = moo_nn_laden(pf);
        CHECK(netz2.tag == MOO_DICT, "Transformer-.mook laedt");
        MooValue y2 = moo_nn_vorwaerts(netz2, x);
        bool gleich = (y1.tag == MOO_TENSOR && y2.tag == MOO_TENSOR &&
                       T(y1)->size == T(y2)->size);
        for (int64_t i = 0; gleich && i < T(y1)->size; i++)
            gleich = (T(y1)->data[i] == T(y2)->data[i]);
        CHECK(gleich, "Transformer-Roundtrip: Vorhersage BIT-gleich");
        remove("/tmp/test_g1_tf.mook");
        moo_release(y1); moo_release(y2); moo_release(netz2);
        moo_release(pf); moo_release(x); moo_release(netz);
        moo_release(schichten);
        moo_ag_reset();
    }

    printf("test_nn_asan: alle %d Checks bestanden\n", checks);
    return 0;
}
