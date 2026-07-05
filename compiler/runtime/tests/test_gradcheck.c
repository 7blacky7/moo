/**
 * test_gradcheck.c — Plan-014 B2: DAS Ehrlichkeits-Gate des KI-Stacks.
 * ============================================================================
 * Numerischer Gradient ((f(x+h) - f(x-h)) / 2h, h=1e-3, f64-Loss-Summe)
 * gegen den Autograd-Gradienten — fuer JEDEN Registry-Op mit backward,
 * AUTOMATISCH ueber moo_tensor_op_count()/at() iteriert: ein neuer Op ohne
 * Gradcheck faellt hier sofort auf (unbekannter Name -> Test schlaegt fehl).
 *
 * METHODE pro Op:
 *   1. Zufalls-Input(s) (seed-deterministisch via tensor_zufall), positiv
 *      verschoben fuer log/sqrt (Definitionsbereich!), requires_grad.
 *   2. loss = sum(f(...)) — Skalar; Autograd: rueckwaerts, grad sichern.
 *   3. Fuer jede Input-Position i: x[i] += h -> loss+; x[i] -= 2h -> loss-;
 *      numerisch = (loss+ - loss-) / 2h. Loss-Berechnung mit autograd_aus
 *      (kein Tape-Muell) und f64-Summe der Ausgabe.
 *   4. Vergleich: |num - ag| / max(1, |num|, |ag|) < 1e-2 fuer f32-Ops mit
 *      h=1e-3 (f32-Rundung dominiert; matmul/softmax brauchen die Toleranz),
 *      Standard-Ops < 1e-3. NICHT-differenzierbare Punkte (relu bei 0, max
 *      bei Gleichstand) werden durch die Zufallswahl praktisch vermieden.
 *
 * SONDERFAELLE:
 *   - Broadcast-Formen werden fuer add/sub/mul/div ZUSAETZLICH geprueft
 *     ([3x4] gegen [1x4] — der Bias-Fall).
 *   - max: Subgradient — Check nur an Nicht-Gleichstand-Inputs (Zufall ok).
 *   - Ops ohne bw in der Registry: FEHLER (B2-Vertrag: alle brauchen bw).
 * ============================================================================
 */
#include "../moo_runtime.h"

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
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

static int checks = 0, ops_geprueft = 0;
static int fails = 0;

static MooValue mk_rand(int32_t ndim, const int32_t* shape, uint64_t seed, float shift) {
    MooTensor* t = moo_tensor_raw(ndim, shape);
    uint64_t st = seed * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    for (int64_t i = 0; i < t->size; i++) {
        st += 0x9E3779B97F4A7C15ULL;
        uint64_t z = st;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        float u = (float)(uint32_t)(z >> 40) / 16777216.0f;   // [0,1)
        t->data[i] = u * 1.5f + 0.25f + shift;                 // [0.25+s, 1.75+s)
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

// f64-Summe eines Tensors (Loss-Ersatz fuer die numerische Seite).
static double f64_sum(MooValue tv) {
    MooTensor* t = MV_TENSOR(tv);
    double s = 0.0;
    for (int64_t i = 0; i < t->size; i++) s += (double)t->data[i];
    return s;
}

typedef MooValue (*ForwardEval)(const MooTensorOp* op, MooValue a, MooValue b, double skalar);

static MooValue eval_op(const MooTensorOp* op, MooValue a, MooValue b, double skalar) {
    switch (op->art) {
        case MOO_OP_UNARY:  return op->fw1(a);
        case MOO_OP_BINARY: return op->fw2(a, b);
        case MOO_OP_BINARY_SCALAR: return op->fw2(a, moo_number(skalar));
    }
    return moo_none();
}

// Prueft einen Op mit gegebenen Inputs; nutzt Toleranz tol.
static void check_op(const MooTensorOp* op, MooValue A, MooValue B,
                     double skalar, double tol, const char* variante) {
    const double h = 1e-3;
    // --- Autograd-Seite ---
    moo_ag_reset();
    MV_TENSOR(A)->requires_grad = true;
    if (MV_TENSOR(A)->grad) { free(MV_TENSOR(A)->grad); MV_TENSOR(A)->grad = NULL; }
    bool hat_b = (B.tag == MOO_TENSOR);
    if (hat_b) {
        MV_TENSOR(B)->requires_grad = true;
        if (MV_TENSOR(B)->grad) { free(MV_TENSOR(B)->grad); MV_TENSOR(B)->grad = NULL; }
    }
    MooValue out = eval_op(op, A, B, skalar);
    MooValue loss = moo_tensor_summe(out, moo_number(-1));
    moo_tensor_rueckwaerts(loss);
    moo_release(out); moo_release(loss);

    // --- Numerische Seite (Tape aus) ---
    moo_ag_aus();
    MooTensor* eingaben[2] = { MV_TENSOR(A), hat_b ? MV_TENSOR(B) : NULL };
    for (int e = 0; e < (hat_b ? 2 : 1); e++) {
        MooTensor* t = eingaben[e];
        if (!t->grad) { fails++; fprintf(stderr, "GRADCHECK %s(%s): kein grad an Input %d\n", op->name, variante, e); continue; }
        for (int64_t i = 0; i < t->size; i++) {
            float orig = t->data[i];
            t->data[i] = orig + (float)h;
            MooValue op_ = eval_op(op, A, B, skalar);
            double lp = f64_sum(op_); moo_release(op_);
            t->data[i] = orig - (float)h;
            MooValue om = eval_op(op, A, B, skalar);
            double lm = f64_sum(om); moo_release(om);
            t->data[i] = orig;
            double num = (lp - lm) / (2.0 * h);
            double ag  = (double)t->grad[i];
            double rel = fabs(num - ag) / fmax(1.0, fmax(fabs(num), fabs(ag)));
            if (rel > tol) {
                fails++;
                fprintf(stderr, "GRADCHECK FAIL %s(%s) input%d[%lld]: num=%.6f ag=%.6f rel=%.2e\n",
                        op->name, variante, e, (long long)i, num, ag, rel);
            } else {
                checks++;
            }
        }
    }
    moo_ag_an();
    moo_ag_reset();
}

int main(void) {
    int32_t s34[2] = { 3, 4 };
    int32_t s14[2] = { 1, 4 };
    int32_t s43[2] = { 4, 3 };

    for (int oi = 0; oi < moo_tensor_op_count(); oi++) {
        const MooTensorOp* op = moo_tensor_op_at(oi);
        ops_geprueft++;

        // Definitionsbereich: log/sqrt/pow/div brauchen positive Inputs
        // (mk_rand liefert eh [0.25, 1.75) — ueberall sicher differenzierbar,
        // relu bekommt eine geshiftete Mischform fuer beide Zweige).
        float shift = (strcmp(op->name, "relu") == 0 || strcmp(op->name, "gelu") == 0 ||
                       strcmp(op->name, "tanh") == 0 || strcmp(op->name, "sigmoid") == 0)
                      ? -1.0f : 0.0f;   // [-0.75, 0.75): beide relu-Zweige
        double tol = 1e-3;
        if (strcmp(op->name, "matmul") == 0 || strcmp(op->name, "softmax") == 0 ||
            strcmp(op->name, "logsoftmax") == 0 || strcmp(op->name, "gelu") == 0 ||
            strcmp(op->name, "exp") == 0)
            tol = 1e-2;   // f32-Akkumulation/steile Kruemmung bei h=1e-3

        if (op->art == MOO_OP_UNARY) {
            MooValue A = mk_rand(2, s34, 1000 + (uint64_t)oi, shift);
            check_op(op, A, moo_none(), 0.0, tol, "std");
            moo_release(A);
        } else if (op->art == MOO_OP_BINARY_SCALAR) {
            // reshape ist als BINARY registriert (Liste), sum/mean/max Achse -1
            double sk = 0.0;
            if (strcmp(op->name, "pow") == 0)  sk = 2.0;
            if (strcmp(op->name, "adds") == 0 || strcmp(op->name, "subs") == 0) sk = 0.7;
            if (strcmp(op->name, "muls") == 0 || strcmp(op->name, "divs") == 0) sk = 1.7;
            if (strcmp(op->name, "sum") == 0 || strcmp(op->name, "mean") == 0 ||
                strcmp(op->name, "max") == 0) sk = -1.0;
            MooValue A = mk_rand(2, s34, 2000 + (uint64_t)oi, shift);
            check_op(op, A, moo_none(), sk, tol, "std");
            // Achsen-Varianten der Reduktionen:
            if (strcmp(op->name, "sum") == 0 || strcmp(op->name, "mean") == 0 ||
                strcmp(op->name, "max") == 0) {
                check_op(op, A, moo_none(), 0.0, tol, "achse0");
                check_op(op, A, moo_none(), 1.0, tol, "achse1");
            }
            moo_release(A);
        } else {   // BINARY
            if (strcmp(op->name, "matmul") == 0) {
                MooValue A = mk_rand(2, s34, 3000, 0.0f);
                MooValue B = mk_rand(2, s43, 3001, 0.0f);
                check_op(op, A, B, 0.0, tol, "std");
                moo_release(A); moo_release(B);
            } else if (strcmp(op->name, "reshape") == 0) {
                // reshape(a, form-liste): eigener Check mit fixer Ziel-Form.
                MooValue A = mk_rand(2, s34, 3100, 0.0f);
                MooValue form = moo_list_new(1);
                moo_list_append(form, moo_number(12));
                check_op(op, A, form, 0.0, tol, "flach");
                moo_release(A); moo_release(form);
            } else if (strcmp(op->name, "concat") == 0) {
                MooValue A = mk_rand(2, s34, 3200, 0.0f);
                MooValue B = mk_rand(2, s34, 3201, 0.0f);
                check_op(op, A, B, 0.0, tol, "achse0");
                moo_release(A); moo_release(B);
            } else {   // add/sub/mul/div: same-shape + Broadcast (Bias-Fall)
                MooValue A = mk_rand(2, s34, 4000 + (uint64_t)oi, 0.0f);
                MooValue B = mk_rand(2, s34, 5000 + (uint64_t)oi, 0.0f);
                check_op(op, A, B, 0.0, tol, "same");
                MooValue Bb = mk_rand(2, s14, 6000 + (uint64_t)oi, 0.0f);
                check_op(op, A, Bb, 0.0, tol, "broadcast[1,4]");
                moo_release(A); moo_release(B); moo_release(Bb);
            }
        }
    }

    if (fails) {
        fprintf(stderr, "test_gradcheck: %d FEHLER (%d ok, %d Ops)\n", fails, checks, ops_geprueft);
        return 1;
    }
    printf("test_gradcheck: %d Vergleiche ueber %d Ops OK (alle < Toleranz)\n",
           checks, ops_geprueft);
    return 0;
}
