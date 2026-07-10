/**
 * moo_autograd.c — Dynamischer Tape, reverse-mode Autograd (Plan-014 B1).
 * ============================================================================
 * PRINZIP (PyTorch-artig): Jede Tensor-Op, deren Input requires_grad hat,
 * zeichnet einen Node auf (Inputs+Output RETAINED). rueckwaerts(loss)
 * traversiert den Tape rueckwaerts und AKKUMULIERT Gradienten (+=, wegen
 * Fan-out). tape_reset() released alle Nodes — das ist die Refcount-
 * Minenzone dieses Plans, ASan-Gate in test_autograd_asan.c.
 *
 * BACKWARD-REGISTRIERUNG: moo_ag_init_bw() traegt fuer ALLE 26 Registry-Ops
 * die backward-Funktion via moo_tensor_op_set_bw ein (lazy beim ersten
 * record/backward). Erweiterbarkeits-Vertrag: neuer Op = forward +
 * Registry-Zeile + set_bw-Zeile + Gradient-Check-Zeile (B2).
 *
 * GRADIENT-MATHE-NOTIZEN (g = output->grad):
 *   add: da=g, db=g            — mit Broadcast-REDUKTION auf Input-Shape!
 *   mul: da=g*b, db=g*a        — dito
 *   matmul: da = g @ b^T, db = a^T @ g
 *   softmax: da = y*(g - sum(g*y)) zeilenweise (y=out)
 *   logsoftmax: da = g - exp(out)*sum(g) zeilenweise
 *   max/maximum: Subgradient — g fliesst nur an die argmax-Position
 * Die Broadcast-Reduktion nutzt dieselbe Stride-0-Maschinerie wie der
 * Forward-Broadcast: Iteration ueber die g-Form, Akkumulation in den
 * Ziel-Gradienten mit Stride 0 in gebroadcasteten Dimensionen.
 *
 * UB-POLICY: Indizes int64_t; keine signed-Wrap-Arithmetik.
 * moo_alloc nullt nicht — grad-Buffer via calloc (lazy).
 * ============================================================================
 */
#include "moo_runtime.h"

// ============================================================
// Tape
// ============================================================
static MooAgNode* tape = NULL;
static int64_t tape_len = 0, tape_cap = 0;
static bool ag_enabled = true;     // autograd_an/aus (Inferenz-Modus)
static bool in_backward = false;   // Ops IN backward zeichnen nicht auf
static bool bw_registriert = false;

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

// grad-Buffer lazy anlegen (genullt).
static float* grad_sicherstellen(MooTensor* t) {
    if (!t->grad) t->grad = (float*)calloc((size_t)t->size, sizeof(float));
    return t->grad;
}

static void moo_ag_init_bw(void);

// intern: zeichnet einen Node auf. a/b/skalar koennen none sein.
void moo_ag_record(const char* op_name, MooValue a, MooValue b,
                   MooValue skalar, MooValue out) {
    if (!ag_enabled || in_backward) return;
    if (out.tag != MOO_TENSOR) return;
    bool a_t = (a.tag == MOO_TENSOR), b_t = (b.tag == MOO_TENSOR);
    bool req = (a_t && T(a)->requires_grad) || (b_t && T(b)->requires_grad);
    if (!req) return;
    if (!bw_registriert) moo_ag_init_bw();

    const MooTensorOp* op = moo_tensor_op_lookup(op_name);
    if (!op || !op->bw) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "autograd: die Operation '%s' kann noch nicht rueckwaerts — "
                 "backward fehlt in der Registry", op_name ? op_name : "?");
        moo_throw(moo_error(msg));
        return;
    }
    if (tape_len == tape_cap) {
        int64_t nc = tape_cap ? tape_cap * 2 : 64;
        MooAgNode* nt = (MooAgNode*)realloc(tape, (size_t)nc * sizeof(MooAgNode));
        if (!nt) { moo_throw(moo_error("autograd: Speicher voll (Tape)")); return; }
        tape = nt; tape_cap = nc;
    }
    MooAgNode* n = &tape[tape_len++];
    n->op = op;
    n->n_in = 0;
    if (a_t) { moo_retain(a); n->inputs[n->n_in++] = a; }
    if (b_t) { moo_retain(b); n->inputs[n->n_in++] = b; }
    moo_retain(out);
    n->output = out;
    n->hat_skalar = (skalar.tag == MOO_NUMBER);
    n->skalar = n->hat_skalar ? MV_NUM(skalar) : 0.0;
    // Gradient-Bedarf propagiert durch den Graphen:
    T(out)->requires_grad = true;
}

MooValue moo_ag_reset(void) {
    for (int64_t i = 0; i < tape_len; i++) {
        for (int32_t j = 0; j < tape[i].n_in; j++) moo_release(tape[i].inputs[j]);
        moo_release(tape[i].output);
    }
    tape_len = 0;
    return moo_none();
}

MooValue moo_ag_an(void)  { ag_enabled = true;  return moo_none(); }
MooValue moo_ag_aus(void) { ag_enabled = false; return moo_none(); }
/* D1: Zustand abfragbar — vorhersage() schaltet temporaer aus und stellt
 * den vorherigen Zustand wieder her (statt blind an zu schalten). */
bool moo_ag_ist_an(void) { return ag_enabled; }

// ============================================================
// Broadcast-Reduktion: akkumuliere g (Form von out) in ziel->grad
// (Form von input). Stride 0 in Dimensionen, die der Input broadcastet
// hat — dieselbe Index-Maschinerie wie ew_generic im Forward.
// ============================================================
static void accum_bcast(MooTensor* ziel, const MooTensor* out_t, const float* g) {
    float* zg = grad_sicherstellen(ziel);
    if (ziel->ndim == out_t->ndim &&
        memcmp(ziel->shape, out_t->shape, sizeof(int32_t) * (size_t)ziel->ndim) == 0) {
        for (int64_t i = 0; i < out_t->size; i++) zg[i] += g[i];   // Fastpath
        return;
    }
    int32_t nd = out_t->ndim;
    int64_t sz[MOO_TENSOR_MAX_DIMS];
    for (int32_t d = 0; d < nd; d++) {
        int32_t zd = (d >= nd - ziel->ndim) ? ziel->shape[d - (nd - ziel->ndim)] : 1;
        sz[d] = (zd == 1) ? 0 : ziel->strides[d - (nd - ziel->ndim)];
    }
    int32_t idx[MOO_TENSOR_MAX_DIMS] = {0};
    int64_t oz = 0;
    for (int64_t i = 0; i < out_t->size; i++) {
        zg[oz] += g[i];
        for (int32_t d = nd - 1; d >= 0; d--) {
            idx[d]++;
            oz += sz[d];
            if (idx[d] < out_t->shape[d]) break;
            oz -= (int64_t)idx[d] * sz[d];
            idx[d] = 0;
        }
    }
}

// Elementweise Akkumulation zg[i] += g[i] * faktor[i] (gleiche Form).
static void accum_ew(MooTensor* ziel, const float* g, const float* faktor) {
    float* zg = grad_sicherstellen(ziel);
    for (int64_t i = 0; i < ziel->size; i++) zg[i] += g[i] * faktor[i];
}

// Wie accum_bcast, aber g elementweise mit faktor (out-Form) multipliziert:
// fuer mul/div-Gradienten mit Broadcast.
static void accum_bcast_mul(MooTensor* ziel, const MooTensor* out_t,
                            const float* g, const float* faktor_out_form) {
    // Zwischenpuffer g*faktor in out-Form, dann normale Reduktion.
    float* tmp = (float*)malloc((size_t)out_t->size * sizeof(float));
    if (!tmp) { moo_throw(moo_error("autograd: Speicher voll")); return; }
    for (int64_t i = 0; i < out_t->size; i++) tmp[i] = g[i] * faktor_out_form[i];
    accum_bcast(ziel, out_t, tmp);
    free(tmp);
}

// Broadcast-Lesehilfe: liefert Wert von t an out-Position i (Strides 0).
// Fuer kleine Faktor-Berechnungen im generischen Pfad.
static void materialize_bcast(const MooTensor* t, const MooTensor* out_t, float* dst) {
    if (t->ndim == out_t->ndim &&
        memcmp(t->shape, out_t->shape, sizeof(int32_t) * (size_t)t->ndim) == 0) {
        memcpy(dst, t->data, (size_t)out_t->size * sizeof(float));
        return;
    }
    int32_t nd = out_t->ndim;
    int64_t st[MOO_TENSOR_MAX_DIMS];
    for (int32_t d = 0; d < nd; d++) {
        int32_t td = (d >= nd - t->ndim) ? t->shape[d - (nd - t->ndim)] : 1;
        st[d] = (td == 1) ? 0 : t->strides[d - (nd - t->ndim)];
    }
    int32_t idx[MOO_TENSOR_MAX_DIMS] = {0};
    int64_t ot = 0;
    for (int64_t i = 0; i < out_t->size; i++) {
        dst[i] = t->data[ot];
        for (int32_t d = nd - 1; d >= 0; d--) {
            idx[d]++;
            ot += st[d];
            if (idx[d] < out_t->shape[d]) break;
            ot -= (int64_t)idx[d] * st[d];
            idx[d] = 0;
        }
    }
}

// ============================================================
// backward-Funktionen (Signatur: void bw(const MooAgNode* n))
// g = n->output->grad (existiert wenn bw gerufen wird).
// ============================================================

static void bw_add(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    const float* g = o->grad;
    if (T(n->inputs[0])->requires_grad) accum_bcast(T(n->inputs[0]), o, g);
    if (n->n_in > 1 && T(n->inputs[1])->requires_grad) accum_bcast(T(n->inputs[1]), o, g);
}

static void bw_sub(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    const float* g = o->grad;
    if (T(n->inputs[0])->requires_grad) accum_bcast(T(n->inputs[0]), o, g);
    if (n->n_in > 1 && T(n->inputs[1])->requires_grad) {
        float* neg = (float*)malloc((size_t)o->size * sizeof(float));
        if (!neg) { moo_throw(moo_error("autograd: Speicher voll")); return; }
        for (int64_t i = 0; i < o->size; i++) neg[i] = -g[i];
        accum_bcast(T(n->inputs[1]), o, neg);
        free(neg);
    }
}

static void bw_mul(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    const float* g = o->grad;
    MooTensor* a = T(n->inputs[0]);
    MooTensor* b = T(n->inputs[1]);
    float* buf = (float*)malloc((size_t)o->size * sizeof(float));
    if (!buf) { moo_throw(moo_error("autograd: Speicher voll")); return; }
    if (a->requires_grad) {           // da += g * b (b auf out-Form gelesen)
        materialize_bcast(b, o, buf);
        accum_bcast_mul(a, o, g, buf);
    }
    if (b->requires_grad) {           // db += g * a
        materialize_bcast(a, o, buf);
        accum_bcast_mul(b, o, g, buf);
    }
    free(buf);
}

static void bw_div(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    const float* g = o->grad;
    MooTensor* a = T(n->inputs[0]);
    MooTensor* b = T(n->inputs[1]);
    float* bb = (float*)malloc((size_t)o->size * sizeof(float));
    float* buf = (float*)malloc((size_t)o->size * sizeof(float));
    if (!bb || !buf) { free(bb); free(buf); moo_throw(moo_error("autograd: Speicher voll")); return; }
    materialize_bcast(b, o, bb);
    if (a->requires_grad) {           // da += g / b
        for (int64_t i = 0; i < o->size; i++) buf[i] = 1.0f / bb[i];
        accum_bcast_mul(a, o, g, buf);
    }
    if (b->requires_grad) {           // db += -g * a / b^2  = -g * out / b
        const float* y = o->data;
        for (int64_t i = 0; i < o->size; i++) buf[i] = -y[i] / bb[i];
        accum_bcast_mul(b, o, g, buf);
    }
    free(bb); free(buf);
}

// Skalar-Varianten: nur Input 0 hat Gradient (Zahl ist kein Tensor).
static void bw_adds(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    if (T(n->inputs[0])->requires_grad) accum_bcast(T(n->inputs[0]), o, o->grad);
}
static void bw_subs(const MooAgNode* n) { bw_adds(n); }   // d(a-s)/da = 1
static void bw_muls(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    float s = (float)n->skalar;
    for (int64_t i = 0; i < o->size; i++) zg[i] += o->grad[i] * s;
}
static void bw_divs(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    float s = (float)n->skalar;
    for (int64_t i = 0; i < o->size; i++) zg[i] += o->grad[i] / s;
}

// gather-Backward: scatter-add. out[i,:] = W[idx_i,:]  ->  gW[idx_i,:] += g[i,:].
// Duplikat-Indizes summieren durch sequentielle Akkumulation (deterministisch,
// bit-reproduzierbar). Der Index-Tensor inputs[1] bekommt NIE Gradient.
static void bw_gather(const MooAgNode* n) {
    MooTensor* w = T(n->inputs[0]);
    const MooTensor* idx = T(n->inputs[1]);
    const MooTensor* o = T(n->output);
    if (!w->requires_grad) return;
    const float* g = o->grad;
    float* zg = grad_sicherstellen(w);
    int64_t n_idx = idx->size;
    int64_t dim = w->shape[1];
    for (int64_t i = 0; i < n_idx; i++) {
        int64_t r = (int64_t)idx->data[i];
        for (int64_t d = 0; d < dim; d++)
            zg[r * dim + d] += g[i * dim + d];
    }
}

static void bw_matmul(const MooAgNode* n) {
    // out[m,p] = a[m,k] @ b[k,p]:  da += g @ b^T ; db += a^T @ g
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    MooTensor* b = T(n->inputs[1]);
    const float* g = o->grad;
    int64_t m = a->shape[0], k = a->shape[1], p = b->shape[1];
    if (a->requires_grad) {
        float* zg = grad_sicherstellen(a);
        for (int64_t i = 0; i < m; i++)
            for (int64_t j = 0; j < p; j++) {
                float gv = g[i * p + j];
                const float* brow = b->data + 0;
                for (int64_t kk = 0; kk < k; kk++)
                    zg[i * k + kk] += gv * brow[kk * p + j];
            }
    }
    if (b->requires_grad) {
        float* zg = grad_sicherstellen(b);
        for (int64_t kk = 0; kk < k; kk++)
            for (int64_t i = 0; i < m; i++) {
                float av = a->data[i * k + kk];
                for (int64_t j = 0; j < p; j++)
                    zg[kk * p + j] += av * g[i * p + j];
            }
    }
}

static void bw_transpose(const MooAgNode* n) {
    const MooTensor* o = T(n->output);   // [c, r]
    MooTensor* a = T(n->inputs[0]);      // [r, c]
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    int64_t r = a->shape[0], c = a->shape[1];
    for (int64_t i = 0; i < r; i++)
        for (int64_t j = 0; j < c; j++)
            zg[i * c + j] += o->grad[j * r + i];
}

static void bw_reshape(const MooAgNode* n) {
    // Kopie-Semantik, gleiche Elementreihenfolge: flacher Durchlauf.
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    for (int64_t i = 0; i < o->size; i++) zg[i] += o->grad[i];
}

static void bw_concat(const MooAgNode* n) {
    // Achse 0: g in die beiden Bloecke splitten.
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    MooTensor* b = T(n->inputs[1]);
    if (a->requires_grad) {
        float* zg = grad_sicherstellen(a);
        for (int64_t i = 0; i < a->size; i++) zg[i] += o->grad[i];
    }
    if (b->requires_grad) {
        float* zg = grad_sicherstellen(b);
        for (int64_t i = 0; i < b->size; i++) zg[i] += o->grad[a->size + i];
    }
}

// Reduktionen: skalar = Achse (-1 alles). out hat keepdims-Form ->
// accum_bcast broadcastet g automatisch korrekt zurueck.
static void bw_sum(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    // da += g gebroadcastet auf a-Form: Iteration ueber a, g mit Stride 0.
    float* zg = grad_sicherstellen(a);
    if (o->size == 1) {                       // alles-Reduktion
        float gv = o->grad[0];
        for (int64_t i = 0; i < a->size; i++) zg[i] += gv;
        return;
    }
    int64_t r = a->shape[0], c = a->shape[1];
    if (o->shape[0] == 1) {                   // achse 0 -> g[1,c]
        for (int64_t i = 0; i < r; i++)
            for (int64_t j = 0; j < c; j++) zg[i * c + j] += o->grad[j];
    } else {                                  // achse 1 -> g[r,1]
        for (int64_t i = 0; i < r; i++)
            for (int64_t j = 0; j < c; j++) zg[i * c + j] += o->grad[i];
    }
}

static void bw_mean(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    if (o->size == 1) {
        float gv = o->grad[0] / (float)a->size;
        for (int64_t i = 0; i < a->size; i++) zg[i] += gv;
        return;
    }
    int64_t r = a->shape[0], c = a->shape[1];
    if (o->shape[0] == 1) {
        for (int64_t i = 0; i < r; i++)
            for (int64_t j = 0; j < c; j++) zg[i * c + j] += o->grad[j] / (float)r;
    } else {
        for (int64_t i = 0; i < r; i++)
            for (int64_t j = 0; j < c; j++) zg[i * c + j] += o->grad[i] / (float)c;
    }
}

static void bw_max(const MooAgNode* n) {
    // Subgradient: g fliesst nur an die (erste) argmax-Position.
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    if (o->size == 1) {
        int64_t am = 0;
        for (int64_t i = 1; i < a->size; i++) if (a->data[i] > a->data[am]) am = i;
        zg[am] += o->grad[0];
        return;
    }
    int64_t r = a->shape[0], c = a->shape[1];
    if (o->shape[0] == 1) {                   // achse 0: argmax je Spalte
        for (int64_t j = 0; j < c; j++) {
            int64_t am = 0;
            for (int64_t i = 1; i < r; i++)
                if (a->data[i * c + j] > a->data[am * c + j]) am = i;
            zg[am * c + j] += o->grad[j];
        }
    } else {                                  // achse 1: argmax je Zeile
        for (int64_t i = 0; i < r; i++) {
            int64_t am = 0;
            for (int64_t j = 1; j < c; j++)
                if (a->data[i * c + j] > a->data[i * c + am]) am = j;
            zg[i * c + am] += o->grad[i];
        }
    }
}

static void bw_exp(const MooAgNode* n) {      // d exp = exp = out
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (a->requires_grad) accum_ew(a, o->grad, o->data);
}
static void bw_log(const MooAgNode* n) {      // d log(a) = 1/a
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    for (int64_t i = 0; i < a->size; i++) zg[i] += o->grad[i] / a->data[i];
}
static void bw_sqrt(const MooAgNode* n) {     // d sqrt = 0.5/out
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    for (int64_t i = 0; i < a->size; i++) zg[i] += o->grad[i] * 0.5f / o->data[i];
}
static void bw_neg(const MooAgNode* n) {
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    for (int64_t i = 0; i < a->size; i++) zg[i] -= o->grad[i];
}
static void bw_pow(const MooAgNode* n) {      // d a^p = p * a^(p-1)
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    float p = (float)n->skalar;
    for (int64_t i = 0; i < a->size; i++)
        zg[i] += o->grad[i] * p * powf(a->data[i], p - 1.0f);
}

static void bw_relu(const MooAgNode* n) {     // g * (a > 0)
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    for (int64_t i = 0; i < a->size; i++)
        if (a->data[i] > 0.0f) zg[i] += o->grad[i];
}
static void bw_sigmoid(const MooAgNode* n) {  // g * y * (1-y)
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    const float* y = o->data;
    for (int64_t i = 0; i < a->size; i++)
        zg[i] += o->grad[i] * y[i] * (1.0f - y[i]);
}
static void bw_tanh(const MooAgNode* n) {     // g * (1 - y^2)
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    const float* y = o->data;
    for (int64_t i = 0; i < a->size; i++)
        zg[i] += o->grad[i] * (1.0f - y[i] * y[i]);
}
static void bw_gelu(const MooAgNode* n) {
    // Ableitung der tanh-Approximation:
    // gelu(x) = 0.5x(1+tanh(u)), u = k(x + 0.044715 x^3), k = sqrt(2/pi)
    // d/dx  = 0.5(1+tanh(u)) + 0.5x(1-tanh^2(u)) * k(1 + 3*0.044715 x^2)
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    const float k = 0.7978845608f;
    for (int64_t i = 0; i < a->size; i++) {
        float x = a->data[i];
        float u = k * (x + 0.044715f * x * x * x);
        float th = tanhf(u);
        float du = k * (1.0f + 3.0f * 0.044715f * x * x);
        float d = 0.5f * (1.0f + th) + 0.5f * x * (1.0f - th * th) * du;
        zg[i] += o->grad[i] * d;
    }
}

static void bw_softmax(const MooAgNode* n) {
    // Zeilenweise: da = y * (g - sum(g*y))
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    int64_t rows = (o->ndim == 2) ? o->shape[0] : 1;
    int64_t cols = (o->ndim == 2) ? o->shape[1] : o->shape[0];
    for (int64_t i = 0; i < rows; i++) {
        const float* y = o->data + i * cols;
        const float* g = o->grad + i * cols;
        double dot = 0.0;
        for (int64_t j = 0; j < cols; j++) dot += (double)g[j] * y[j];
        for (int64_t j = 0; j < cols; j++)
            zg[i * cols + j] += y[j] * (g[j] - (float)dot);
    }
}

static void bw_logsoftmax(const MooAgNode* n) {
    // Zeilenweise: da = g - exp(out) * sum(g)
    const MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    if (!a->requires_grad) return;
    float* zg = grad_sicherstellen(a);
    int64_t rows = (o->ndim == 2) ? o->shape[0] : 1;
    int64_t cols = (o->ndim == 2) ? o->shape[1] : o->shape[0];
    for (int64_t i = 0; i < rows; i++) {
        const float* y = o->data + i * cols;
        const float* g = o->grad + i * cols;
        double gs = 0.0;
        for (int64_t j = 0; j < cols; j++) gs += (double)g[j];
        for (int64_t j = 0; j < cols; j++)
            zg[i * cols + j] += g[j] - expf(y[j]) * (float)gs;
    }
}

static void moo_ag_init_bw(void) {
    moo_tensor_op_set_bw("add", bw_add);
    moo_tensor_op_set_bw("sub", bw_sub);
    moo_tensor_op_set_bw("mul", bw_mul);
    moo_tensor_op_set_bw("div", bw_div);
    moo_tensor_op_set_bw("adds", bw_adds);
    moo_tensor_op_set_bw("subs", bw_subs);
    moo_tensor_op_set_bw("muls", bw_muls);
    moo_tensor_op_set_bw("divs", bw_divs);
    moo_tensor_op_set_bw("matmul", bw_matmul);
    moo_tensor_op_set_bw("gather", bw_gather);
    moo_tensor_op_set_bw("transpose", bw_transpose);
    moo_tensor_op_set_bw("reshape", bw_reshape);
    moo_tensor_op_set_bw("concat", bw_concat);
    moo_tensor_op_set_bw("sum", bw_sum);
    moo_tensor_op_set_bw("mean", bw_mean);
    moo_tensor_op_set_bw("max", bw_max);
    moo_tensor_op_set_bw("exp", bw_exp);
    moo_tensor_op_set_bw("log", bw_log);
    moo_tensor_op_set_bw("sqrt", bw_sqrt);
    moo_tensor_op_set_bw("neg", bw_neg);
    moo_tensor_op_set_bw("pow", bw_pow);
    moo_tensor_op_set_bw("relu", bw_relu);
    moo_tensor_op_set_bw("sigmoid", bw_sigmoid);
    moo_tensor_op_set_bw("tanh", bw_tanh);
    moo_tensor_op_set_bw("gelu", bw_gelu);
    moo_tensor_op_set_bw("softmax", bw_softmax);
    moo_tensor_op_set_bw("logsoftmax", bw_logsoftmax);
    bw_registriert = true;
}

// ============================================================
// Oeffentliche API (Tensor-Konvention: Args borrowed, Rueckgabe +1)
// ============================================================

MooValue moo_tensor_mit_gradient(MooValue tv) {
    if (tv.tag != MOO_TENSOR) {
        moo_throw(moo_error("mit_gradient: das ist kein Tensor"));
        return moo_none();
    }
    T(tv)->requires_grad = true;
    moo_retain(tv);
    return tv;
}

MooValue moo_tensor_rueckwaerts(MooValue loss) {
    if (loss.tag != MOO_TENSOR) {
        moo_throw(moo_error("rueckwaerts: das ist kein Tensor"));
        return moo_none();
    }
    MooTensor* l = T(loss);
    if (l->size != 1) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "rueckwaerts: der Verlust muss EIN Wert sein (dein Tensor hat %lld) "
                 "— nutze .summe() oder .mittel() davor", (long long)l->size);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    grad_sicherstellen(l)[0] = 1.0f;
    in_backward = true;
    for (int64_t i = tape_len - 1; i >= 0; i--) {
        MooAgNode* n = &tape[i];
        if (!T(n->output)->grad) continue;    // Zweig traegt nicht zum Loss bei
        n->op->bw(n);
    }
    in_backward = false;
    return moo_none();
}

MooValue moo_tensor_gradient(MooValue tv) {
    if (tv.tag != MOO_TENSOR) {
        moo_throw(moo_error("gradient: das ist kein Tensor"));
        return moo_none();
    }
    MooTensor* t = T(tv);
    if (!t->grad) {
        moo_throw(moo_error("gradient: noch kein Gradient da — erst mit_gradient() "
                            "setzen und rueckwaerts() auf dem Verlust aufrufen"));
        return moo_none();
    }
    MooTensor* g = moo_tensor_raw(t->ndim, t->shape);
    if (!g) return moo_none();
    memcpy(g->data, t->grad, (size_t)t->size * sizeof(float));
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, g);
    return v;
}

MooValue moo_tensor_gradient_loeschen(MooValue tv) {
    if (tv.tag != MOO_TENSOR) {
        moo_throw(moo_error("gradient_loeschen: das ist kein Tensor"));
        return moo_none();
    }
    MooTensor* t = T(tv);
    if (t->grad) memset(t->grad, 0, (size_t)t->size * sizeof(float));
    return moo_none();
}
