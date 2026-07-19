/**
 * test_attnres_asan.c — KI-R1-Gate (PAPER-VERIFY-05): Attention Residuals.
 * ============================================================================
 * GATES:
 *   R1  Uniform-Init: w=0 (NULL-init) => alpha exakt uniform => h == MITTEL
 *       der Quellen (AttnRes:538 — bewusst Mittel, NICHT Residual-Summe).
 *   R2  Konvexitaet: fuer jede Komponente gilt min_i v_i <= h <= max_i v_i
 *       (Softmax-Gewichte summieren zu 1) — auch bei w != 0.
 *   R3  FD-Gradcheck der GESAMT-Komposition (contrastive-Muster):
 *       loss = sum(attnres(s, [v0,v1,v2])); numerischer Gradient vs.
 *       Autograd fuer w UND alle Quellen (tol 1e-2, softmax/f32).
 *   R4  parameter([schicht]) enthaelt genau w (Registry params_attnres).
 *   R5  Vertraege: falsche Schicht, leere Liste, Nicht-Tensor-Quelle,
 *       vorwaerts(schicht, x) wirft mit Anleitung.
 *   R6  Determinismus: gleiche Eingaben => bit-identische Ausgabe.
 * Test-throw-Modell wie test_gradcheck.c (kein moo_error.c gelinkt).
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <math.h>

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
    moo_error_flag = 1;
}

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

static int checks = 0, fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) { checks++; } \
    else { fails++; fprintf(stderr, "FAIL: %s\n", (msg)); } \
} while (0)

enum { TT = 3, DD = 4, NQ = 3 };

static MooValue mk(uint64_t seed, float shift) {
    int32_t sh[2] = { TT, DD };
    MooTensor* t = moo_tensor_raw(2, sh);
    uint64_t st = seed * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    for (int64_t i = 0; i < t->size; i++) {
        st += 0x9E3779B97F4A7C15ULL;
        uint64_t z = st;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        float u = (float)(uint32_t)(z >> 40) / 16777216.0f;
        t->data[i] = u * 1.5f + 0.25f + shift;
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

static double f64_sum(MooValue tv) {
    MooTensor* t = MV_TENSOR(tv);
    double s = 0.0;
    for (int64_t i = 0; i < t->size; i++) s += (double)t->data[i];
    return s;
}

/* Gesamt-Loss der Komposition (Tape aus fuer die numerische Seite). */
static double loss_von(MooValue schicht, MooValue quellen) {
    MooValue h = moo_nn_attnres(schicht, quellen);
    double s = f64_sum(h);
    moo_release(h);
    return s;
}

int main(void) {
    MooValue s = moo_nn_schicht_attnres(moo_number(DD));
    CHECK(moo_error_flag == 0 && s.tag == MOO_DICT, "Schicht erstellt");

    MooValue v0 = mk(100, 0.0f);
    MooValue v1 = mk(200, -0.5f);
    MooValue v2 = mk(300, 0.5f);
    MooValue quellen = moo_list_new(NQ);
    moo_retain(v0); moo_list_append(quellen, v0);
    moo_retain(v1); moo_list_append(quellen, v1);
    moo_retain(v2); moo_list_append(quellen, v2);

    /* ---------- R1: Uniform-Init => Mittel ---------- */
    moo_ag_aus();
    moo_error_flag = 0;
    MooValue h0 = moo_nn_attnres(s, quellen);
    CHECK(moo_error_flag == 0 && h0.tag == MOO_TENSOR, "R1: Forward laeuft");
    if (h0.tag == MOO_TENSOR) {
        int ok = 1;
        for (int64_t i = 0; i < TT * DD; i++) {
            float m = (MV_TENSOR(v0)->data[i] + MV_TENSOR(v1)->data[i] +
                       MV_TENSOR(v2)->data[i]) / 3.0f;
            if (fabsf(MV_TENSOR(h0)->data[i] - m) > 1e-6f) ok = 0;
        }
        CHECK(ok == 1, "R1: w=0 => h == Mittel der Quellen (uniforme alpha)");
    }

    /* ---------- R6: Determinismus ---------- */
    {
        MooValue h0b = moo_nn_attnres(s, quellen);
        int det = 1;
        for (int64_t i = 0; i < TT * DD; i++)
            if (MV_TENSOR(h0)->data[i] != MV_TENSOR(h0b)->data[i]) det = 0;
        CHECK(det == 1, "R6: bit-identische Wiederholung");
        moo_release(h0b);
    }
    moo_release(h0);

    /* ---------- R2: Konvexitaet bei w != 0 ---------- */
    {
        MooValue w = moo_dict_get(s, moo_string_new("w"));
        for (int64_t i = 0; i < DD; i++)
            MV_TENSOR(w)->data[i] = 0.7f * (float)(i + 1) - 1.0f;
        moo_release(w);
        MooValue h = moo_nn_attnres(s, quellen);
        CHECK(h.tag == MOO_TENSOR, "R2: Forward mit w != 0");
        int ok = 1;
        for (int64_t i = 0; i < TT * DD && h.tag == MOO_TENSOR; i++) {
            float a = MV_TENSOR(v0)->data[i], b = MV_TENSOR(v1)->data[i];
            float c = MV_TENSOR(v2)->data[i];
            float lo = fminf(a, fminf(b, c)) - 1e-5f;
            float hi = fmaxf(a, fmaxf(b, c)) + 1e-5f;
            float x = MV_TENSOR(h)->data[i];
            if (x < lo || x > hi) ok = 0;
        }
        CHECK(ok == 1, "R2: h ist konvexe Kombination (alpha-Summe == 1)");
        moo_release(h);
    }

    /* ---------- R3: FD-Gradcheck der Komposition (w + Quellen) ---------- */
    moo_ag_an();
    {
        const double hh = 1e-3, tol = 1e-2;
        moo_ag_reset();
        MV_TENSOR(v0)->requires_grad = true;
        MV_TENSOR(v1)->requires_grad = true;
        MV_TENSOR(v2)->requires_grad = true;
        MooValue h = moo_nn_attnres(s, quellen);
        MooValue loss = moo_tensor_summe(h, moo_number(-1));
        moo_tensor_rueckwaerts(loss);
        moo_release(h); moo_release(loss);

        moo_ag_aus();
        MooValue wv = moo_dict_get(s, moo_string_new("w"));
        MooTensor* eingaben[4] = { MV_TENSOR(wv), MV_TENSOR(v0),
                                   MV_TENSOR(v1), MV_TENSOR(v2) };
        const char* namen[4] = { "w", "v0", "v1", "v2" };
        for (int e = 0; e < 4; e++) {
            MooTensor* t = eingaben[e];
            if (!t->grad) {
                fails++;
                fprintf(stderr, "R3 FAIL: kein grad an %s\n", namen[e]);
                continue;
            }
            int ok = 1;
            for (int64_t i = 0; i < t->size; i++) {
                float orig = t->data[i];
                t->data[i] = orig + (float)hh;
                double lp = loss_von(s, quellen);
                t->data[i] = orig - (float)hh;
                double lm = loss_von(s, quellen);
                t->data[i] = orig;
                double num = (lp - lm) / (2.0 * hh);
                double ag = (double)t->grad[i];
                double rel = fabs(num - ag) / fmax(1.0, fmax(fabs(num), fabs(ag)));
                if (rel > tol) {
                    ok = 0;
                    fprintf(stderr, "R3 FAIL %s[%lld]: num=%.6f ag=%.6f rel=%.2e\n",
                            namen[e], (long long)i, num, ag, rel);
                }
            }
            CHECK(ok == 1, "R3: FD-Gradcheck Eingang");
        }
        moo_release(wv);
        moo_ag_an();
        moo_ag_reset();
    }

    /* ---------- R4: parameter() sammelt w ---------- */
    {
        MooValue liste = moo_list_new(1);
        moo_retain(s); moo_list_append(liste, s);
        moo_error_flag = 0;
        MooValue params = moo_nn_parameter(liste);
        CHECK(moo_error_flag == 0 && params.tag == MOO_LIST &&
              MV_LIST(params)->length == 1, "R4: parameter() liefert genau w");
        if (params.tag == MOO_LIST && MV_LIST(params)->length == 1)
            CHECK(MV_LIST(params)->items[0].tag == MOO_TENSOR &&
                  MV_TENSOR(MV_LIST(params)->items[0])->requires_grad,
                  "R4: w ist trainierbarer Tensor");
        moo_release(params); moo_release(liste);
    }

    /* ---------- R5: Vertraege ---------- */
    moo_ag_aus();
    moo_error_flag = 0;
    { MooValue e = moo_nn_attnres(v0, quellen);   /* kein attnres-Dict */
      CHECK(moo_error_flag == 1 && e.tag == MOO_NONE, "R5: falsche Schicht wirft"); }
    moo_error_flag = 0;
    { MooValue leer = moo_list_new(0);
      MooValue e = moo_nn_attnres(s, leer);
      CHECK(moo_error_flag == 1 && e.tag == MOO_NONE, "R5: leere Liste wirft");
      moo_release(leer); }
    moo_error_flag = 0;
    { MooValue gemischt = moo_list_new(2);
      moo_retain(v0); moo_list_append(gemischt, v0);
      moo_list_append(gemischt, moo_number(3.0));
      MooValue e = moo_nn_attnres(s, gemischt);
      CHECK(moo_error_flag == 1 && e.tag == MOO_NONE, "R5: Nicht-Tensor-Quelle wirft");
      moo_release(gemischt); }
    moo_error_flag = 0;
    { MooValue e = moo_nn_vorwaerts(s, v0);   /* Registry-fw wirft mit Anleitung */
      CHECK(moo_error_flag == 1 && e.tag == MOO_NONE, "R5: vorwaerts(attnres, x) wirft"); }
    moo_error_flag = 0;
    moo_ag_an();

    moo_release(quellen);
    moo_release(v0); moo_release(v1); moo_release(v2);
    moo_release(s);

    if (fails) {
        fprintf(stderr, "test_attnres_asan: %d FEHLER (%d ok)\n", fails, checks);
        return 1;
    }
    printf("test_attnres_asan: PASS (%d Checks: R1-R6)\n", checks);
    return 0;
}
