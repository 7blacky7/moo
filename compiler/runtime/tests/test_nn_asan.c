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

/* KIP-B2 Gate-Helfer: Decode via KV-Cache (token-weise) == Nicht-Cache-Voll-
 * Forward, BIT-identisch. at wird wiederverwendbar zurueckgelassen (cache-Flag
 * 0, Cache leer). autograd wird aus/wieder-an geschaltet (Cache = Inferenz). */
static bool rope_decode_gleich_voll(MooValue at, int seq, int dim, const float* xv) {
    moo_ag_aus();
    MooValue xf = t2(seq, dim, xv);
    MooValue yfull = moo_nn_vorwaerts(at, xf);        /* Voll, kein Cache */
    bool ok = (yfull.tag == MOO_TENSOR);
    moo_dict_set(at, moo_string_new("cache"), moo_number(1.0));
    moo_nn_cache_leeren(at);
    for (int i = 0; i < seq && ok; i++) {
        MooValue xi = t2(1, dim, &xv[i * dim]);
        MooValue yi = moo_nn_vorwaerts(at, xi);       /* 1 Token via Cache */
        if (yi.tag != MOO_TENSOR) { ok = false; moo_release(xi); break; }
        for (int j = 0; j < dim && ok; j++)
            ok = (T(yi)->data[j] == T(yfull)->data[i * dim + j]);
        moo_release(yi); moo_release(xi);
    }
    moo_dict_set(at, moo_string_new("cache"), moo_number(0.0));
    moo_nn_cache_leeren(at);
    moo_release(yfull); moo_release(xf);
    moo_ag_an();
    return ok;
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

        /* F16 wird jetzt beim Import nach f32 konvertiert (KIP-D3).
         * 0x3C00 (LE {0,60}) == f16 1.0. */
        const char* hj16 = "{\"x\":{\"dtype\":\"F16\",\"shape\":[1],"
            "\"data_offsets\":[0,2]}}";
        uint64_t hl16 = (uint64_t)strlen(hj16);
        f = fopen("/tmp/test_f1_f16.safetensors", "wb");
        fwrite(&hl16, 8, 1, f);
        fwrite(hj16, 1, (size_t)hl16, f);
        unsigned char halb[2] = { 0, 60 };   /* 0x3C00 = f16 1.0 */
        fwrite(halb, 1, 2, f);
        fclose(f);
        MooValue p16 = moo_string_new("/tmp/test_f1_f16.safetensors");
        MooValue d16 = moo_nn_safetensors(p16);
        moo_release(p16);
        CHECK(d16.tag == MOO_DICT, "F16 importiert (->f32)");
        MooValue xv16 = dget_(d16, "x");
        CHECK(xv16.tag == MOO_TENSOR && T(xv16)->data[0] == 1.0f,
              "F16 0x3C00 -> 1.0f");
        moo_release(xv16); moo_release(d16);
        remove("/tmp/test_f1_f16.safetensors");

        /* echter unsupported dtype (F64) wirft weiterhin erklaerend */
        fehler_reset();
        const char* hj64 = "{\"x\":{\"dtype\":\"F64\",\"shape\":[1],"
            "\"data_offsets\":[0,8]}}";
        uint64_t hl64 = (uint64_t)strlen(hj64);
        f = fopen("/tmp/test_f1_f64.safetensors", "wb");
        fwrite(&hl64, 8, 1, f);
        fwrite(hj64, 1, (size_t)hl64, f);
        double dd64 = 1.0; fwrite(&dd64, 8, 1, f);
        fclose(f);
        MooValue p64 = moo_string_new("/tmp/test_f1_f64.safetensors");
        MooValue d64 = moo_nn_safetensors(p64);
        moo_release(p64);
        CHECK(moo_error_flag == 1 && d64.tag == MOO_NONE, "F64 wirft erklaerend");
        fehler_reset();
        remove("/tmp/test_f1_f64.safetensors");
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
                                               moo_none(), moo_none(), moo_none());
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
                        moo_none(), moo_none(), moo_none()));
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

    /* ===== 16. RMSNorm (KIP-B1) ===== */
    {
        /* Forward vs Handrechnung: x=[1,2,3,4], dim=4, g=1, eps=1e-5.
         * mean(x^2)=7.5 ; rms=sqrt(7.5+1e-5) ; y_i = x_i / rms. */
        MooValue rn = moo_nn_schicht_rmsnorm(moo_number(4));
        MooValue x = t2(1, 4, (float[]){ 1, 2, 3, 4 });
        moo_ag_reset();
        MooValue xg = moo_tensor_mit_gradient(x);
        MooValue y = moo_nn_vorwaerts(rn, xg);
        CHECK(y.tag == MOO_TENSOR && T(y)->ndim == 2 &&
              T(y)->shape[0] == 1 && T(y)->shape[1] == 4, "rmsnorm: Form [1,4]");
        double rms = sqrt(7.5 + 1e-5);
        CHECK(NAHE(T(y)->data[0], 1.0 / rms) && NAHE(T(y)->data[3], 4.0 / rms),
              "rmsnorm: Werte == x/rms (Handrechnung)");
        /* Skalierungs-Invariante: Verhaeltnisse exakt erhalten */
        CHECK(NAHE(T(y)->data[1] / T(y)->data[0], 2.0) &&
              NAHE(T(y)->data[2] / T(y)->data[0], 3.0), "rmsnorm: Verhaeltnisse erhalten");
        /* RMS(y) ~ 1 */
        double my2 = 0.0;
        for (int i = 0; i < 4; i++) my2 += (double)T(y)->data[i] * (double)T(y)->data[i];
        CHECK(NAHE(sqrt(my2 / 4.0), 1.0), "rmsnorm: RMS(y) ~ 1");
        /* Gradient fliesst in g */
        MooValue loss = moo_tensor_mittel(y, moo_none());
        moo_release(moo_tensor_rueckwaerts(loss));
        MooValue prm = moo_nn_parameter(rn);
        CHECK(prm.tag == MOO_LIST && MV_LIST(prm)->length == 1,
              "rmsnorm: 1 Parameter (g)");
        MooTensor* gt = MV_TENSOR(MV_LIST(prm)->items[0]);
        CHECK(gt->grad && (gt->grad[0] != 0.0f || gt->grad[3] != 0.0f),
              "rmsnorm: Gradient fliesst in g");
        moo_release(prm); moo_release(loss); moo_release(y);
        moo_release(xg); moo_release(x); moo_release(rn);
        moo_ag_reset();

        /* .mook-Roundtrip bit-identisch */
        MooValue schichten = moo_list_new(1);
        moo_list_append(schichten, moo_nn_schicht_rmsnorm(moo_number(4)));
        MooValue netz = moo_nn_ki_netz(schichten);
        float xv2[8] = { 0.5f, -1.0f, 2.0f, 0.25f, -0.75f, 1.5f, -2.0f, 3.0f };
        MooValue x2 = t2(2, 4, xv2);
        MooValue y1 = moo_nn_vorwaerts(netz, x2);
        MooValue pf = moo_string_new("/tmp/test_b1_rmsnorm.mook");
        moo_release(moo_nn_speichern(netz, pf));
        MooValue netz2 = moo_nn_laden(pf);
        CHECK(netz2.tag == MOO_DICT, "rmsnorm-.mook laedt");
        MooValue y2 = moo_nn_vorwaerts(netz2, x2);
        bool gleich = (y1.tag == MOO_TENSOR && y2.tag == MOO_TENSOR &&
                       T(y1)->size == T(y2)->size);
        for (int64_t i = 0; gleich && i < T(y1)->size; i++)
            gleich = (T(y1)->data[i] == T(y2)->data[i]);
        CHECK(gleich, "rmsnorm-Roundtrip: Vorhersage BIT-gleich");
        remove("/tmp/test_b1_rmsnorm.mook");
        moo_release(y1); moo_release(y2); moo_release(netz2);
        moo_release(pf); moo_release(x2); moo_release(netz);
        moo_release(schichten);
        moo_ag_reset();
    }

    /* ===== 17. SwiGLU / Gated-FFN (KIP-B3) ===== */
    {
        /* Forward [n,dim] + Gradient in W1, W2, W3. */
        MooValue art = moo_string_new("swiglu");
        MooValue ff = moo_nn_schicht_ffn_gated(moo_number(4), moo_number(8), art);
        moo_release(art);
        CHECK(ff.tag == MOO_DICT, "ffn_gated: Schicht gebaut");
        float xv[8] = { 0.5f, -1.0f, 2.0f, 0.25f, -0.75f, 1.5f, -2.0f, 3.0f };
        MooValue x = t2(2, 4, xv);
        moo_ag_reset();
        MooValue xg = moo_tensor_mit_gradient(x);
        MooValue y = moo_nn_vorwaerts(ff, xg);
        CHECK(y.tag == MOO_TENSOR && T(y)->ndim == 2 &&
              T(y)->shape[0] == 2 && T(y)->shape[1] == 4, "ffn_gated: Form [2,4]");
        MooValue loss = moo_tensor_mittel(y, moo_none());
        moo_release(moo_tensor_rueckwaerts(loss));
        MooValue prm = moo_nn_parameter(ff);
        CHECK(prm.tag == MOO_LIST && MV_LIST(prm)->length == 3,
              "ffn_gated: 3 Parameter (W1,W2,W3)");
        bool alle_grad = true;
        for (int i = 0; i < 3; i++) {
            MooTensor* w = MV_TENSOR(MV_LIST(prm)->items[i]);
            bool any = false;
            if (w->grad)
                for (int64_t k = 0; k < w->size; k++)
                    if (w->grad[k] != 0.0f) { any = true; break; }
            alle_grad = alle_grad && any;
        }
        CHECK(alle_grad, "ffn_gated: Gradient fliesst in ALLE 3 Gewichte");
        moo_release(prm); moo_release(loss); moo_release(y);
        moo_release(xg); moo_release(x); moo_release(ff);
        moo_ag_reset();

        /* .mook-Roundtrip bit-identisch */
        MooValue schichten = moo_list_new(1);
        MooValue art2 = moo_string_new("swiglu");
        moo_list_append(schichten,
                        moo_nn_schicht_ffn_gated(moo_number(4), moo_number(8), art2));
        moo_release(art2);
        MooValue netz = moo_nn_ki_netz(schichten);
        float xv2[8] = { 1.0f, 0.5f, -0.5f, 2.0f, -1.0f, 0.25f, 1.5f, -2.0f };
        MooValue x2 = t2(2, 4, xv2);
        MooValue y1 = moo_nn_vorwaerts(netz, x2);
        MooValue pf = moo_string_new("/tmp/test_b3_ffn_gated.mook");
        moo_release(moo_nn_speichern(netz, pf));
        MooValue netz2 = moo_nn_laden(pf);
        CHECK(netz2.tag == MOO_DICT, "ffn_gated-.mook laedt");
        MooValue y2 = moo_nn_vorwaerts(netz2, x2);
        bool gleich = (y1.tag == MOO_TENSOR && y2.tag == MOO_TENSOR &&
                       T(y1)->size == T(y2)->size);
        for (int64_t i = 0; gleich && i < T(y1)->size; i++)
            gleich = (T(y1)->data[i] == T(y2)->data[i]);
        CHECK(gleich, "ffn_gated-Roundtrip: Vorhersage BIT-gleich");
        remove("/tmp/test_b3_ffn_gated.mook");
        moo_release(y1); moo_release(y2); moo_release(netz2);
        moo_release(pf); moo_release(x2); moo_release(netz);
        moo_release(schichten);
        moo_ag_reset();
    }

    /* ===== 18. RoPE in Attention (KIP-B2) ===== */
    {
        MooValue r_on = moo_number(10000.0);   /* inline (MOO_NUMBER) -> reusebar */
        float xv[48];
        for (int i = 0; i < 48; i++) xv[i] = (float)((i * 13) % 17) * 0.07f - 0.6f;

        /* a) even head_dim: dh=4 baut; odd dh=3 (dim6/koepfe2) wirft (Negativ) */
        MooValue at_ok = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), moo_none(), moo_none(), r_on);
        CHECK(at_ok.tag == MOO_DICT, "rope: even head_dim (dh=4) baut Schicht");
        fehler_reset();
        MooValue at_odd = moo_nn_schicht_attention(moo_number(6.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), moo_none(), moo_none(), r_on);
        CHECK(moo_error_flag == 1 && at_odd.tag != MOO_DICT,
              "rope: NEGATIV — odd head_dim (dh=3) wirft");
        fehler_reset();

        /* b) rope wirkt; Position 0 (cos=1,sin=0) BIT-identisch zu ohne rope */
        MooValue x4 = t2(4, 8, xv);
        MooValue y_rope = moo_nn_vorwaerts(at_ok, x4);
        MooValue at_no = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), moo_none(), moo_none(), moo_none());
        MooValue y_no = moo_nn_vorwaerts(at_no, x4);
        bool wirkt = false;
        bool zeile0 = (y_rope.tag == MOO_TENSOR && y_no.tag == MOO_TENSOR);
        for (int i = 0; i < 32 && y_rope.tag==MOO_TENSOR && y_no.tag==MOO_TENSOR; i++)
            if (T(y_rope)->data[i] != T(y_no)->data[i]) wirkt = true;
        for (int j = 0; j < 8 && zeile0; j++)
            zeile0 = (T(y_rope)->data[j] == T(y_no)->data[j]);
        CHECK(wirkt, "rope: veraendert den Output (Rotation wirkt)");
        CHECK(zeile0, "rope: Position 0 (cos=1,sin=0) BIT-identisch zu ohne rope");
        moo_release(y_rope); moo_release(y_no); moo_release(at_no);

        /* Grad-Fluss durch RoPE in alle 7 Parameter */
        moo_ag_reset();
        MooValue xg = moo_tensor_mit_gradient(x4);
        MooValue yg = moo_nn_vorwaerts(at_ok, xg);
        MooValue mg = moo_tensor_mittel(yg, moo_none());
        moo_release(moo_tensor_rueckwaerts(mg));
        MooValue ps = moo_nn_parameter(at_ok);
        bool grads = (ps.tag == MOO_LIST && MV_LIST(ps)->length == 7);
        for (int32_t i = 0; ps.tag==MOO_LIST && i < MV_LIST(ps)->length; i++) {
            MooTensor* p = MV_TENSOR(MV_LIST(ps)->items[i]);
            bool einer = false;
            if (p->grad)
                for (int64_t j = 0; j < p->size && !einer; j++)
                    if (p->grad[j] != 0.0f) einer = true;
            grads = grads && einer;
        }
        CHECK(grads, "rope: Gradient fliesst durch RoPE in alle 7 Parameter");
        moo_release(ps); moo_release(mg); moo_release(yg); moo_release(xg);
        moo_release(x4); moo_release(at_ok);
        moo_ag_reset();

        /* c) KERN-GATE (MHA): Decode via Cache == Nicht-Cache-Voll bit-ident */
        MooValue at_mha = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), moo_none(), moo_none(), r_on);
        CHECK(rope_decode_gleich_voll(at_mha, 5, 8, xv),
              "rope MHA: Decode via Cache == Nicht-Cache BIT-identisch");

        /* d) chunked Prefill (2+3) == Nicht-Cache-Voll bit-ident */
        moo_ag_aus();
        MooValue xf = t2(5, 8, xv);
        MooValue yfull = moo_nn_vorwaerts(at_mha, xf);   /* Nicht-Cache */
        moo_dict_set(at_mha, moo_string_new("cache"), moo_number(1.0));
        moo_nn_cache_leeren(at_mha);
        MooValue xa = t2(2, 8, &xv[0]);
        MooValue ya = moo_nn_vorwaerts(at_mha, xa);      /* Cache: erste 2 */
        MooValue xb = t2(3, 8, &xv[16]);
        MooValue yb = moo_nn_vorwaerts(at_mha, xb);      /* Cache: naechste 3 */
        bool chunk = (yfull.tag==MOO_TENSOR && ya.tag==MOO_TENSOR && yb.tag==MOO_TENSOR);
        for (int i = 0; i < 2 && chunk; i++)
            for (int j = 0; j < 8 && chunk; j++)
                chunk = (T(ya)->data[i*8+j] == T(yfull)->data[i*8+j]);
        for (int i = 0; i < 3 && chunk; i++)
            for (int j = 0; j < 8 && chunk; j++)
                chunk = (T(yb)->data[i*8+j] == T(yfull)->data[(2+i)*8+j]);
        CHECK(chunk, "rope: chunked Prefill (2+3) == Voll BIT-identisch");

        /* e) Cache-Reset: nach leeren frischer Voll-Prefill (t_alt=0) == Voll */
        moo_nn_cache_leeren(at_mha);
        MooValue yreset = moo_nn_vorwaerts(at_mha, xf);
        bool reset_ok = (yreset.tag==MOO_TENSOR && yfull.tag==MOO_TENSOR);
        for (int64_t i = 0; i < 40 && reset_ok; i++)
            reset_ok = (T(yreset)->data[i] == T(yfull)->data[i]);
        CHECK(reset_ok, "rope: Cache-Reset -> frischer Prefill == Voll (t_alt=0)");
        moo_dict_set(at_mha, moo_string_new("cache"), moo_number(0.0));
        moo_nn_cache_leeren(at_mha);
        moo_ag_an();
        moo_release(yfull); moo_release(ya); moo_release(yb); moo_release(yreset);
        moo_release(xa); moo_release(xb); moo_release(xf); moo_release(at_mha);

        /* f) MQA (kv=1) + GQA (koepfe4/kv2, dh=2) + rope: Decode == Voll */
        MooValue at_mqa = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_number(1.0), moo_none(), moo_none(), r_on);
        CHECK(rope_decode_gleich_voll(at_mqa, 5, 8, xv),
              "rope MQA (kv=1): Decode via Cache == Nicht-Cache BIT-identisch");
        moo_release(at_mqa);
        MooValue at_gqa = moo_nn_schicht_attention(moo_number(8.0), moo_number(4.0),
                          moo_number(7.0), moo_number(2.0), moo_none(), moo_none(), r_on);
        CHECK(rope_decode_gleich_voll(at_gqa, 5, 8, xv),
              "rope GQA (koepfe4/kv2, dh=2): Decode via Cache == Nicht-Cache BIT-identisch");
        moo_release(at_gqa);

        /* g) Sliding-Window (W=2) + rope: Decode == Voll */
        MooValue msl = moo_string_new("sliding");
        MooValue at_sl = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), msl, moo_number(2.0), r_on);
        moo_release(msl);
        CHECK(rope_decode_gleich_voll(at_sl, 6, 8, xv),
              "rope Sliding (W=2): Decode via Cache == Nicht-Cache BIT-identisch");
        moo_release(at_sl);

        /* h) Doppel-Positions-Guard: position (additiv) + attention(rope) wirft */
        fehler_reset();
        MooValue guard = moo_list_new(2);
        moo_list_append(guard, moo_nn_schicht_position(moo_number(4.0), moo_number(8.0),
                        moo_none(), moo_number(3.0)));
        moo_list_append(guard, moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                        moo_number(7.0), moo_none(), moo_none(), moo_none(), r_on));
        MooValue gx = t2(4, 8, xv);
        MooValue gy = moo_nn_vorwaerts(guard, gx);
        CHECK(moo_error_flag == 1 && gy.tag != MOO_TENSOR,
              "rope: Doppel-Positions-Guard (position + rope) wirft");
        fehler_reset();
        moo_release(gy); moo_release(gx); moo_release(guard);
        /* Gegenprobe: position + attention OHNE rope laeuft (kein Fehlalarm) */
        MooValue ok2 = moo_list_new(2);
        moo_list_append(ok2, moo_nn_schicht_position(moo_number(4.0), moo_number(8.0),
                        moo_none(), moo_number(3.0)));
        moo_list_append(ok2, moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                        moo_number(7.0), moo_none(), moo_none(), moo_none(), moo_none()));
        MooValue ox = t2(4, 8, xv);
        MooValue oy = moo_nn_vorwaerts(ok2, ox);
        CHECK(oy.tag == MOO_TENSOR,
              "rope: position + attention OHNE rope laeuft (kein Fehlalarm)");
        moo_release(oy); moo_release(ox); moo_release(ok2);
        moo_ag_reset();

        /* i) .mook-Roundtrip: rope-Konfig ueberlebt (Vorhersage BIT-gleich) */
        MooValue sch = moo_list_new(1);
        moo_list_append(sch, moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                        moo_number(7.0), moo_none(), moo_none(), moo_none(), r_on));
        MooValue netz = moo_nn_ki_netz(sch);
        MooValue rx = t2(4, 8, xv);
        MooValue ry1 = moo_nn_vorwaerts(netz, rx);
        MooValue pf = moo_string_new("/tmp/test_b2_rope.mook");
        moo_release(moo_nn_speichern(netz, pf));
        MooValue netz2 = moo_nn_laden(pf);
        CHECK(netz2.tag == MOO_DICT, "rope-.mook laedt");
        MooValue ry2 = moo_nn_vorwaerts(netz2, rx);
        bool rgleich = (ry1.tag==MOO_TENSOR && ry2.tag==MOO_TENSOR &&
                        T(ry1)->size == T(ry2)->size);
        for (int64_t i = 0; rgleich && i < T(ry1)->size; i++)
            rgleich = (T(ry1)->data[i] == T(ry2)->data[i]);
        CHECK(rgleich, "rope-Roundtrip: Vorhersage BIT-gleich (rope-Konfig erhalten)");
        remove("/tmp/test_b2_rope.mook");
        moo_release(ry1); moo_release(ry2); moo_release(netz2);
        moo_release(pf); moo_release(rx); moo_release(netz); moo_release(sch);
        moo_ag_reset();
    }

    /* ===== 18b. RoPE-Kontext-Skalierung linear/NTK (KIP-B2b) ===== */
    {
        float xv[128];
        for (int i = 0; i < 128; i++) xv[i] = (float)((i * 11) % 19) * 0.05f - 0.4f;

        /* a) Linear/PI (faktor=4): staucht Position -> wirkt ab Zeile>0,
         *    Zeile 0 (p=0) BIT-identisch zu Standard-RoPE; deterministisch. */
        MooValue rl = moo_dict_new();
        moo_dict_set(rl, moo_string_new("basis"), moo_number(10000.0));
        moo_dict_set(rl, moo_string_new("skalierung"), moo_string_new("linear"));
        moo_dict_set(rl, moo_string_new("faktor"), moo_number(4.0));
        MooValue at_lin = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), moo_none(), moo_none(), rl);
        moo_release(rl);
        CHECK(at_lin.tag == MOO_DICT, "b2b linear: baut Schicht (dh=4)");
        MooValue x8 = t2(8, 8, xv);
        MooValue at_plain = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                            moo_number(7.0), moo_none(), moo_none(), moo_none(),
                            moo_number(10000.0));
        MooValue yl = moo_nn_vorwaerts(at_lin, x8);
        MooValue yp = moo_nn_vorwaerts(at_plain, x8);
        bool lin_wirkt = false;
        bool lin_z0 = (yl.tag==MOO_TENSOR && yp.tag==MOO_TENSOR);
        for (int i = 0; i < 64 && yl.tag==MOO_TENSOR && yp.tag==MOO_TENSOR; i++)
            if (T(yl)->data[i] != T(yp)->data[i]) lin_wirkt = true;
        for (int j = 0; j < 8 && lin_z0; j++)
            lin_z0 = (T(yl)->data[j] == T(yp)->data[j]);
        CHECK(lin_wirkt, "b2b linear: veraendert Output ggue Standard-RoPE");
        CHECK(lin_z0, "b2b linear: Zeile 0 (p=0) BIT-identisch zu Standard-RoPE");
        MooValue yl2 = moo_nn_vorwaerts(at_lin, x8);
        bool lin_det = (yl2.tag==MOO_TENSOR && yl.tag==MOO_TENSOR);
        for (int64_t i = 0; lin_det && i < T(yl)->size; i++)
            lin_det = (T(yl)->data[i] == T(yl2)->data[i]);
        CHECK(lin_det, "b2b linear: deterministisch (2 Laeufe BIT-identisch)");
        moo_release(yl); moo_release(yl2); moo_release(yp); moo_release(at_plain);
        CHECK(rope_decode_gleich_voll(at_lin, 5, 8, xv),
              "b2b linear: Decode via Cache == Voll BIT-identisch (cache-sicher)");
        moo_release(x8); moo_release(at_lin);

        /* b) NTK-aware (faktor=4): skaliert die Basis -> wirkt; Decode==Voll. */
        MooValue rn = moo_dict_new();
        moo_dict_set(rn, moo_string_new("skalierung"), moo_string_new("ntk"));
        moo_dict_set(rn, moo_string_new("faktor"), moo_number(4.0));
        MooValue at_ntk = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), moo_none(), moo_none(), rn);
        moo_release(rn);
        CHECK(at_ntk.tag == MOO_DICT, "b2b ntk: baut Schicht (dh=4>2)");
        MooValue x8b = t2(8, 8, xv);
        MooValue at_plain2 = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                             moo_number(7.0), moo_none(), moo_none(), moo_none(),
                             moo_number(10000.0));
        MooValue yn = moo_nn_vorwaerts(at_ntk, x8b);
        MooValue yp2 = moo_nn_vorwaerts(at_plain2, x8b);
        bool ntk_wirkt = false;
        for (int i = 0; i < 64 && yn.tag==MOO_TENSOR && yp2.tag==MOO_TENSOR; i++)
            if (T(yn)->data[i] != T(yp2)->data[i]) ntk_wirkt = true;
        CHECK(ntk_wirkt, "b2b ntk: veraendert Output ggue Standard-RoPE");
        moo_release(yn); moo_release(yp2); moo_release(at_plain2);
        CHECK(rope_decode_gleich_voll(at_ntk, 5, 8, xv),
              "b2b ntk: Decode via Cache == Voll BIT-identisch");
        moo_release(x8b); moo_release(at_ntk);

        /* c) Extrapolations-Gate: lange Sequenz (12) vs Trainingslaenge (4);
         *    Extrapolationsverhalten protokolliert, langer Lauf deterministisch. */
        MooValue rlp = moo_dict_new();
        moo_dict_set(rlp, moo_string_new("skalierung"), moo_string_new("linear"));
        moo_dict_set(rlp, moo_string_new("faktor"), moo_number(4.0));
        MooValue at_ex = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                         moo_number(7.0), moo_none(), moo_none(), moo_none(), rlp);
        moo_release(rlp);
        MooValue at_exp = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                          moo_number(7.0), moo_none(), moo_none(), moo_none(),
                          moo_number(10000.0));
        MooValue xlong = t2(12, 8, xv);
        MooValue yex1 = moo_nn_vorwaerts(at_ex, xlong);
        MooValue yex2 = moo_nn_vorwaerts(at_ex, xlong);
        MooValue yexp = moo_nn_vorwaerts(at_exp, xlong);
        bool ex_det = (yex1.tag==MOO_TENSOR && yex2.tag==MOO_TENSOR);
        for (int64_t i = 0; ex_det && i < T(yex1)->size; i++)
            ex_det = (T(yex1)->data[i] == T(yex2)->data[i]);
        double far_scaled = 0.0, far_plain = 0.0;
        int64_t fbase = (int64_t)11 * 8;   /* fernster Token (seq=12) */
        if (yex1.tag==MOO_TENSOR && T(yex1)->size >= fbase + 8)
            for (int64_t i = fbase; i < fbase + 8; i++) far_scaled += fabs(T(yex1)->data[i]);
        if (yexp.tag==MOO_TENSOR && T(yexp)->size >= fbase + 8)
            for (int64_t i = fbase; i < fbase + 8; i++) far_plain += fabs(T(yexp)->data[i]);
        printf("[b2b] Extrapolation ferner Token (seq=12, dh=4, faktor=4): "
               "L1|out| linear=%.6f vs standard=%.6f\n", far_scaled, far_plain);
        CHECK(ex_det, "b2b extrapolation: lange Sequenz (12) deterministisch");
        CHECK(far_scaled != far_plain && far_scaled == far_scaled && far_scaled < 1e30,
              "b2b extrapolation: linear skaliert fernen Token anders als Standard-RoPE (endlich)");
        moo_release(yex1); moo_release(yex2); moo_release(yexp);
        moo_release(xlong); moo_release(at_ex); moo_release(at_exp);

        /* d) .mook-Roundtrip: Skalierungs-Felder ueberleben (Vorhersage gleich). */
        MooValue rrt = moo_dict_new();
        moo_dict_set(rrt, moo_string_new("basis"), moo_number(10000.0));
        moo_dict_set(rrt, moo_string_new("skalierung"), moo_string_new("ntk"));
        moo_dict_set(rrt, moo_string_new("faktor"), moo_number(8.0));
        MooValue schb = moo_list_new(1);
        moo_list_append(schb, moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                        moo_number(7.0), moo_none(), moo_none(), moo_none(), rrt));
        moo_release(rrt);
        MooValue netzb = moo_nn_ki_netz(schb);
        MooValue rxb = t2(4, 8, xv);
        MooValue rby1 = moo_nn_vorwaerts(netzb, rxb);
        MooValue pfb = moo_string_new("/tmp/test_b2b_scale.mook");
        moo_release(moo_nn_speichern(netzb, pfb));
        MooValue netzb2 = moo_nn_laden(pfb);
        CHECK(netzb2.tag == MOO_DICT, "b2b-.mook laedt (Scaling-Felder)");
        MooValue rby2 = moo_nn_vorwaerts(netzb2, rxb);
        bool rbg = (rby1.tag==MOO_TENSOR && rby2.tag==MOO_TENSOR &&
                    T(rby1)->size == T(rby2)->size);
        for (int64_t i = 0; rbg && i < T(rby1)->size; i++)
            rbg = (T(rby1)->data[i] == T(rby2)->data[i]);
        CHECK(rbg, "b2b-Roundtrip: Vorhersage BIT-gleich (ntk skalierung+faktor erhalten)");
        remove("/tmp/test_b2b_scale.mook");
        moo_release(rby1); moo_release(rby2); moo_release(netzb2);
        moo_release(pfb); moo_release(rxb); moo_release(netzb); moo_release(schb);
        moo_ag_reset();

        /* e) NEGATIV: ntk mit dh=2 wirft; Skalierungsfaktor < 1 wirft. */
        fehler_reset();
        MooValue rbad1 = moo_dict_new();
        moo_dict_set(rbad1, moo_string_new("skalierung"), moo_string_new("ntk"));
        moo_dict_set(rbad1, moo_string_new("faktor"), moo_number(4.0));
        MooValue at_bad1 = moo_nn_schicht_attention(moo_number(8.0), moo_number(4.0),
                           moo_number(7.0), moo_none(), moo_none(), moo_none(), rbad1);
        moo_release(rbad1);
        CHECK(moo_error_flag == 1 && at_bad1.tag != MOO_DICT,
              "b2b NEGATIV: ntk mit dh=2 wirft");
        fehler_reset();
        MooValue rbad2 = moo_dict_new();
        moo_dict_set(rbad2, moo_string_new("skalierung"), moo_string_new("linear"));
        moo_dict_set(rbad2, moo_string_new("faktor"), moo_number(0.5));
        MooValue at_bad2 = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                           moo_number(7.0), moo_none(), moo_none(), moo_none(), rbad2);
        moo_release(rbad2);
        CHECK(moo_error_flag == 1 && at_bad2.tag != MOO_DICT,
              "b2b NEGATIV: Skalierungsfaktor < 1 wirft");
        fehler_reset();
    }

    /* ===== 18c. KV-Cache-Quant (KI-Q2, TurboQuant-Stufe-2 Storage-Simulation) ===== */
    {
        MooValue r_on = moo_number(10000.0);
        float xv[48];
        for (int i = 0; i < 48; i++) xv[i] = (float)((i * 7) % 19) * 0.09f - 0.8f;
        MooValue at = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                      moo_number(7.0), moo_none(), moo_none(), moo_none(), r_on);
        moo_ag_aus();
        MooValue xf = t2(5, 8, xv);
        MooValue yfull = moo_nn_vorwaerts(at, xf);        /* Nicht-Cache f32 */

        /* a) int8-Quant-Cache: Token-fuer-Token-Decode bleibt nahe am
         *    f32-Voll-Lauf. Toy-Schranke NORMIERT (max|Delta| / max|ref|,
         *    per-Element-rel explodiert bei ref nahe 0): < 2%. Gemessene
         *    Referenz bei dh=4/int8: ~0.36%. */
        moo_dict_set(at, moo_string_new("cache"), moo_number(1.0));
        moo_dict_set(at, moo_string_new("cache_quant"), moo_number(8.0));
        moo_nn_cache_leeren(at);
        float maxabs = 0.0f, maxref = 0.0f;
        bool q8ok = (yfull.tag == MOO_TENSOR);
        for (int i = 0; i < 5 && q8ok; i++) {
            MooValue xi = t2(1, 8, &xv[i * 8]);
            MooValue yi = moo_nn_vorwaerts(at, xi);
            q8ok = (yi.tag == MOO_TENSOR);
            for (int j = 0; j < 8 && q8ok; j++) {
                float ref = T(yfull)->data[i * 8 + j];
                float d = fabsf(T(yi)->data[j] - ref);
                if (d > maxabs) maxabs = d;
                if (fabsf(ref) > maxref) maxref = fabsf(ref);
                q8ok = isfinite(T(yi)->data[j]);
            }
            moo_release(yi); moo_release(xi);
        }
        CHECK(q8ok && maxref > 0.0f && maxabs < 0.02f * maxref,
              "cache_quant: int8-Decode nahe f32-Voll (normiert < 2%)");

        /* b) Regression: quant aus + cache aus -> Nicht-Cache BIT-identisch */
        moo_dict_set(at, moo_string_new("cache_quant"), moo_number(0.0));
        moo_dict_set(at, moo_string_new("cache"), moo_number(0.0));
        moo_nn_cache_leeren(at);
        MooValue y2 = moo_nn_vorwaerts(at, xf);
        bool bit = (y2.tag == MOO_TENSOR);
        for (int64_t i = 0; i < 40 && bit; i++)
            bit = (T(y2)->data[i] == T(yfull)->data[i]);
        CHECK(bit, "cache_quant: Flag 0 -> Nicht-Cache-Pfad BIT-identisch");
        moo_release(y2);

        /* c) int4 laeuft und liefert endliche Werte */
        moo_dict_set(at, moo_string_new("cache"), moo_number(1.0));
        moo_dict_set(at, moo_string_new("cache_quant"), moo_number(4.0));
        moo_nn_cache_leeren(at);
        bool q4ok = true;
        for (int i = 0; i < 2 && q4ok; i++) {
            MooValue xi = t2(1, 8, &xv[i * 8]);
            MooValue yi = moo_nn_vorwaerts(at, xi);
            q4ok = (yi.tag == MOO_TENSOR);
            for (int j = 0; j < 8 && q4ok; j++)
                q4ok = isfinite(T(yi)->data[j]);
            moo_release(yi); moo_release(xi);
        }
        CHECK(q4ok, "cache_quant: int4-Decode laeuft (endliche Werte)");

        /* c2) KI-Q3: Lloyd-Max-Codebuch-Lookup laeuft (int4 + Codebuch) */
        MooValue cbl = moo_list_new(16);
        static const float cbw[16] = {
            -0.9815f, -0.8046f, -0.6612f, -0.5246f, -0.3952f, -0.2761f,
            -0.1679f, -0.0560f,  0.0560f,  0.1679f,  0.2761f,  0.3952f,
             0.5246f,  0.6612f,  0.8046f,  0.9815f };
        for (int c = 0; c < 16; c++)
            moo_list_append(cbl, moo_number((double)cbw[c]));
        moo_dict_set(at, moo_string_new("cache_quant_codebuch"), cbl);
        moo_nn_cache_leeren(at);
        bool cbok = true;
        for (int i = 0; i < 2 && cbok; i++) {
            MooValue xi = t2(1, 8, &xv[i * 8]);
            MooValue yi = moo_nn_vorwaerts(at, xi);
            cbok = (yi.tag == MOO_TENSOR);
            for (int j = 0; j < 8 && cbok; j++)
                cbok = isfinite(T(yi)->data[j]);
            moo_release(yi); moo_release(xi);
        }
        CHECK(cbok, "cache_quant: Codebuch-Lookup (KI-Q3) laeuft");

        /* c3) NEGATIV: Codebuch mit 17 Eintraegen bei bits=4 wirft */
        MooValue cbg = moo_list_new(17);
        for (int c = 0; c < 17; c++)
            moo_list_append(cbg, moo_number(0.1 * (double)c));
        moo_dict_set(at, moo_string_new("cache_quant_codebuch"), cbg);
        moo_nn_cache_leeren(at);
        {
            MooValue xi = t2(1, 8, xv);
            fehler_reset();
            MooValue yi = moo_nn_vorwaerts(at, xi);
            CHECK(moo_error_flag == 1 && yi.tag != MOO_TENSOR,
                  "cache_quant: NEGATIV — Codebuch > 2^bits wirft");
            fehler_reset();
            moo_release(yi); moo_release(xi);
        }
        moo_dict_set(at, moo_string_new("cache_quant_codebuch"), moo_none());
        moo_dict_set(at, moo_string_new("cache"), moo_number(0.0));
        moo_nn_cache_leeren(at);
        moo_release(yfull); moo_release(xf); moo_release(at);

        /* d) NEGATIV: dh=12 (dim24/koepfe2, even fuer rope, KEINE
         *    Zweierpotenz) -> Cache-Quant wirft */
        MooValue at24 = moo_nn_schicht_attention(moo_number(24.0), moo_number(2.0),
                        moo_number(7.0), moo_none(), moo_none(), moo_none(), r_on);
        moo_dict_set(at24, moo_string_new("cache"), moo_number(1.0));
        moo_dict_set(at24, moo_string_new("cache_quant"), moo_number(8.0));
        float x24v[24];
        for (int i = 0; i < 24; i++) x24v[i] = 0.1f * (float)i - 1.0f;
        MooValue x24 = t2(1, 24, x24v);
        fehler_reset();
        MooValue yf = moo_nn_vorwaerts(at24, x24);
        CHECK(moo_error_flag == 1 && yf.tag != MOO_TENSOR,
              "cache_quant: NEGATIV — Kopf-Dim keine Zweierpotenz wirft");
        fehler_reset();
        moo_release(yf); moo_release(x24); moo_release(at24);
        moo_ag_an();
    }

    /* ===== 19. Maskierte Kreuzentropie (KIP-B4a, X3 §2) ===== */
    {
        moo_ag_reset();
        float lv[12] = { 2.0f, 1.0f, 0.0f, -1.0f,  0.5f, 0.5f, 0.5f, 0.5f,
                         -1.0f, 3.0f, 0.0f, 1.0f };
        MooValue logits = t2(3, 4, lv);
        float ztv[3] = { 0.0f, 2.0f, 1.0f };
        MooValue ziele = t1(3, ztv);
        float mkv[3] = { 1.0f, 0.0f, 1.0f };
        MooValue maske = t1(3, mkv);
        MooValue lm = moo_nn_kreuzentropie_maskiert(logits, ziele, maske);
        CHECK(lm.tag == MOO_TENSOR, "masked-CE: laeuft");
        /* Referenz: Mittel der Einzel-CE aktiver Zeilen (0 und 2), /count=2 */
        float l0[4] = { 2.0f, 1.0f, 0.0f, -1.0f };
        float l2[4] = { -1.0f, 3.0f, 0.0f, 1.0f };
        float z0[1] = { 0.0f }, z2[1] = { 1.0f };
        MooValue L0 = t2(1,4,l0), z0v = t1(1,z0);
        MooValue L2 = t2(1,4,l2), z2v = t1(1,z2);
        MooValue c0 = moo_nn_kreuzentropie(L0, z0v);
        MooValue c2 = moo_nn_kreuzentropie(L2, z2v);
        double ref = (T(c0)->data[0] + T(c2)->data[0]) / 2.0;
        CHECK(NAHE(T(lm)->data[0], ref), "masked-CE == Mittel der aktiven Zeilen-CE");
        moo_release(c0); moo_release(c2); moo_release(L0); moo_release(z0v);
        moo_release(L2); moo_release(z2v); moo_release(lm);
        /* all-1-Maske == unmaskiert (beide /3, BIT-gleich) */
        float m1v[3] = { 1.0f, 1.0f, 1.0f };
        MooValue m1 = t1(3, m1v);
        MooValue lmall = moo_nn_kreuzentropie_maskiert(logits, ziele, m1);
        MooValue lun = moo_nn_kreuzentropie(logits, ziele);
        CHECK(lmall.tag==MOO_TENSOR && lun.tag==MOO_TENSOR &&
              T(lmall)->data[0] == T(lun)->data[0],
              "masked-CE all-1 == unmaskierte CE (BIT-gleich)");
        moo_release(lmall); moo_release(lun); moo_release(m1);
        /* Grad nur in aktiven Zeilen: maskierte Zeile 1 -> 0 Gradient */
        moo_ag_reset();
        MooValue lg = moo_tensor_mit_gradient(logits);
        MooValue lmg = moo_nn_kreuzentropie_maskiert(lg, ziele, maske);
        moo_release(moo_tensor_rueckwaerts(lmg));
        MooTensor* gt = MV_TENSOR(lg);
        bool zeile1_null = true, zeile0_da = false;
        if (gt->grad) {
            for (int j = 0; j < 4; j++) if (gt->grad[4+j] != 0.0f) zeile1_null = false;
            for (int j = 0; j < 4; j++) if (gt->grad[j]   != 0.0f) zeile0_da  = true;
        }
        CHECK(zeile1_null, "masked-CE: maskierte Zeile bekommt KEINEN Gradient");
        CHECK(zeile0_da,  "masked-CE: aktive Zeile bekommt Gradient");
        moo_release(lmg); moo_release(lg); moo_release(maske); moo_release(ziele); moo_release(logits);
        moo_ag_reset();
    }

    /* ===== 20. Sequence Packing (KIP-B4a) ===== */
    {
        moo_ag_reset();
        float da[3] = { 1.0f, 2.0f, 3.0f };
        float db[2] = { 4.0f, 5.0f };
        MooValue docA = t1(3, da), docB = t1(2, db);
        MooValue docs = moo_list_new(2);
        moo_list_append(docs, docA);   /* append transferiert +1 -> Liste besitzt */
        moo_list_append(docs, docB);
        MooValue pack = moo_nn_sequence_packen(docs, moo_number(6.0));  /* 1 Padding */
        CHECK(pack.tag == MOO_DICT, "packen: liefert Dict");
        MooValue pids = dget_(pack, "ids");
        MooValue pmask = dget_(pack, "attn_maske");
        MooValue plm = dget_(pack, "loss_maske");
        MooValue ppos = dget_(pack, "positionen");
        MooValue poff = dget_(pack, "doc_offsets");
        CHECK(pids.tag==MOO_TENSOR && T(pids)->size==6, "packen: ids[6]");
        CHECK(T(plm)->data[0]==1 && T(plm)->data[1]==1 && T(plm)->data[2]==0 &&
              T(plm)->data[3]==1 && T(plm)->data[4]==0 && T(plm)->data[5]==0,
              "packen: loss_maske 0 an Doc-Grenzen + Padding");
        CHECK(T(ppos)->data[0]==0 && T(ppos)->data[1]==1 && T(ppos)->data[2]==2 &&
              T(ppos)->data[3]==0 && T(ppos)->data[4]==1,
              "packen: Per-Doc-Positions-Reset");
        CHECK(T(poff)->size==3 && T(poff)->data[0]==0 && T(poff)->data[1]==3 &&
              T(poff)->data[2]==5, "packen: doc_offsets [0,3,5]");
        CHECK(T(pmask)->data[0*6+3] < -1e8 && T(pmask)->data[3*6+0] < -1e8 &&
              T(pmask)->data[2*6+2]==0 && T(pmask)->data[0*6+1] < -1e8 &&
              T(pmask)->data[3*6+3]==0 && T(pmask)->data[4*6+3]==0 &&
              T(pmask)->data[0*6+5] < -1e8,
              "packen: block-diagonale + kausale Maske (Padding-Spalte blockt)");

        /* GATE: gepackt == ungepackt (Attention-Output pro Doc BIT-identisch) */
        moo_ag_aus();
        MooValue emb = moo_nn_schicht_embedding(moo_number(8.0), moo_number(8.0),
                                                moo_number(1.0));
        MooValue at = moo_nn_schicht_attention(moo_number(8.0), moo_number(2.0),
                       moo_number(7.0), moo_none(), moo_none(), moo_none(),
                       moo_number(10000.0));
        MooValue xp = moo_nn_vorwaerts(emb, pids);            /* [6,8] */
        moo_release(moo_nn_packung_setzen(at, pmask, ppos));
        MooValue yp = moo_nn_vorwaerts(at, xp);               /* [6,8] gepackt */
        moo_release(moo_nn_packung_leeren(at));
        MooValue xA = moo_nn_vorwaerts(emb, docA);            /* [3,8] */
        MooValue yA = moo_nn_vorwaerts(at, xA);               /* ungepackt A (causal) */
        MooValue xB = moo_nn_vorwaerts(emb, docB);            /* [2,8] */
        MooValue yB = moo_nn_vorwaerts(at, xB);               /* ungepackt B */
        bool gleichA = (yp.tag==MOO_TENSOR && yA.tag==MOO_TENSOR);
        for (int i=0;i<3 && gleichA;i++) for (int j=0;j<8 && gleichA;j++)
            gleichA = (T(yp)->data[i*8+j] == T(yA)->data[i*8+j]);
        bool gleichB = (yp.tag==MOO_TENSOR && yB.tag==MOO_TENSOR);
        for (int i=0;i<2 && gleichB;i++) for (int j=0;j<8 && gleichB;j++)
            gleichB = (T(yp)->data[(3+i)*8+j] == T(yB)->data[i*8+j]);
        CHECK(gleichA, "packen-Gate: Doc A gepackt == ungepackt BIT-identisch");
        CHECK(gleichB, "packen-Gate: Doc B gepackt == ungepackt BIT-identisch");
        moo_ag_an();
        moo_release(xp); moo_release(yp); moo_release(xA); moo_release(yA);
        moo_release(xB); moo_release(yB); moo_release(emb); moo_release(at);
        moo_release(pids); moo_release(pmask); moo_release(plm);
        moo_release(ppos); moo_release(poff);
        moo_release(pack); moo_release(docs);   /* docs-Liste released docA/docB */
        moo_ag_reset();
    }

    /* KI-MULTI-V2: Conv2D -> Pooling -> Flatten + Save/Load-Bitgate. */
    {
        int32_t xs[4]={1,4,4,1}; MooTensor* xt=moo_tensor_raw(4,xs);
        for(int i=0;i<16;i++) xt->data[i]=(float)(i+1)/16.0f;
        MooValue x; x.tag=MOO_TENSOR; moo_val_set_ptr(&x,xt);
        MooValue akt=moo_string_new("relu");
        MooValue conv=moo_nn_schicht_faltung(moo_number(1),moo_number(2),moo_number(3),
            moo_number(1),moo_number(1),akt,moo_number(77)); moo_release(akt);
        MooValue art=moo_string_new("max");
        MooValue pool=moo_nn_schicht_pooling(art,moo_number(2),moo_number(2)); moo_release(art);
        MooValue flat=moo_nn_schicht_flach();
        MooValue y=moo_nn_vorwaerts(conv,x);
        CHECK(y.tag==MOO_TENSOR && T(y)->ndim==4 && T(y)->shape[0]==1 &&
              T(y)->shape[1]==4 && T(y)->shape[2]==4 && T(y)->shape[3]==2,
              "faltung: NHWC-Ausgabegeometrie");
        MooValue z=moo_nn_vorwaerts(pool,y);
        CHECK(z.tag==MOO_TENSOR && T(z)->ndim==4 && T(z)->shape[1]==2 &&
              T(z)->shape[2]==2 && T(z)->shape[3]==2, "pooling: Ausgabegeometrie");
        MooValue q=moo_nn_vorwaerts(flat,z);
        CHECK(q.tag==MOO_TENSOR && T(q)->ndim==2 && T(q)->shape[0]==1 &&
              T(q)->shape[1]==8, "flach: 4D nach [batch,features]");

        MooValue net=moo_list_new(3); moo_list_append(net,conv); moo_list_append(net,pool); moo_list_append(net,flat);
        MooValue knet=moo_nn_ki_netz(net);
        CHECK(knet.tag==MOO_DICT,"conv ki_netz erstellen");
        MooValue pf=moo_string_new("/tmp/moo_v2_conv_roundtrip.mook");
        moo_nn_speichern(knet,pf);
        MooValue copy=moo_nn_laden(pf); CHECK(copy.tag==MOO_DICT,"conv laden");
        MooValue p1=moo_nn_parameter(knet), p2=moo_nn_parameter(copy);
        CHECK(MV_LIST(p1)->length==2 && MV_LIST(p2)->length==2,"conv w/b Parameter roundtrip");
        bool bits=T(MV_LIST(p1)->items[0])->size==T(MV_LIST(p2)->items[0])->size &&
          memcmp(T(MV_LIST(p1)->items[0])->data,T(MV_LIST(p2)->items[0])->data,
          (size_t)T(MV_LIST(p1)->items[0])->size*sizeof(float))==0;
        CHECK(bits,"conv Gewichte nach Checkpoint bitidentisch");
        remove("/tmp/moo_v2_conv_roundtrip.mook");
        /* BF16-Datei muss von ki_laden gelesen und deterministisch expandiert werden. */
        setenv("MOO_KI_SPEICHERN_BF16","1",1);
        MooValue pfb=moo_string_new("/tmp/moo_v2_conv_roundtrip_bf16.mook");
        moo_nn_speichern(knet,pfb);
        unsetenv("MOO_KI_SPEICHERN_BF16");
        MooValue copyb=moo_nn_laden(pfb);
        CHECK(copyb.tag==MOO_DICT,"conv BF16 laden");
        MooValue pb=moo_nn_parameter(copyb);
        CHECK(pb.tag==MOO_LIST && MV_LIST(pb)->length==2,"conv BF16 w/b Parameter");
        bool bf16_ok=true;
        MooTensor* orig=T(MV_LIST(p1)->items[0]);
        MooTensor* back=T(MV_LIST(pb)->items[0]);
        for(int64_t i=0;i<orig->size && bf16_ok;i++){
            uint32_t u; memcpy(&u,&orig->data[i],4);
            uint16_t h=(uint16_t)((u + 0x7FFFu + ((u>>16)&1u)) >> 16);
            uint32_t expanded=(uint32_t)h<<16, got; memcpy(&got,&back->data[i],4);
            if(got!=expanded) bf16_ok=false;
        }
        CHECK(bf16_ok,"conv BF16 Checkpoint deterministisch expandiert");
        remove("/tmp/moo_v2_conv_roundtrip_bf16.mook");
        moo_release(pfb);moo_release(copyb);moo_release(pb);
        moo_release(pf);moo_release(copy);moo_release(p1);moo_release(p2);
        moo_release(x);moo_release(y);moo_release(z);moo_release(q);moo_release(knet);moo_release(net);
        moo_ag_reset();
    }

    printf("test_nn_asan: alle %d Checks bestanden\n", checks);
    return 0;
}
