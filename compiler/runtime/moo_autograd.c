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
#include "moo_ki_gpu_api.h"   // KIP-G4c: moo_ki_gpu_download fuer Grad-Materialisierung (Preflight I2/I3)

// ============================================================
// Tape
// ============================================================
static MooAgNode* tape = NULL;
static int64_t tape_len = 0, tape_cap = 0;
static bool ag_enabled = true;     // autograd_an/aus (Inferenz-Modus)
static bool in_backward = false;   // Ops IN backward zeichnen nicht auf
static bool bw_registriert = false;
// KIP-D2 Mixed-Precision: Op-Output-Aktivierungen auf bf16-Praezision runden.
// Parameter-Master (Leaf-Inputs), Gradienten und Optimizer bleiben f32. Default
// AUS -> moo_ag_record verhaelt sich unveraendert (Basisgates gruen).
static bool ag_bf16 = false;
static bool ag_bf16_env_gelesen = false;
static void ag_bf16_env_init(void);   // Definition unten (nach moo_ag_ist_an)

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

// grad-Buffer lazy anlegen (genullt) + KIP-G4c (Preflight I2/I3, kip-kern
// docs/kip/G4c-preflight-ownership-vertrag.md): garantiert dass ->grad die
// GPU-Seite bereits erfasst (Download aus gpu_grad), falls dort resident
// akkumuliert wurde, BEVOR der naechste CPU-Beitrag (+=) draufschreibt —
// sonst geht ein GPU-Fan-out-Beitrag verloren. No-op im reinen CPU/F32-Fall
// (grad_valid bleibt 0/MOO_V_DATA, solange keine residente Backward-Op
// existiert — kommt erst in einer spaeteren G4c-Phase-2-Stufe).
static void grad_materialisieren(MooTensor* t) {
    if (!t) return;
    if (!t->grad) t->grad = (float*)calloc((size_t)t->size, sizeof(float));
    if (!t->grad) { moo_throw(moo_error("autograd: Speicher voll bei Grad-Sicherung")); return; }
    if ((t->grad_valid & MOO_V_DEV) && !(t->grad_valid & MOO_V_DATA)) {
        if (!moo_ki_gpu_download(t->gpu_grad, t->grad,
                                 (int64_t)t->size * (int64_t)sizeof(float))) {
            moo_throw(moo_error("autograd: GPU-Grad-Download fehlgeschlagen")); return;
        }
    }
    t->grad_valid = MOO_V_DATA;   // CPU jetzt autoritativ fuer die naechsten Reads/Writes (I2)
}
static float* grad_sicherstellen(MooTensor* t) {
    grad_materialisieren(t);
    return t->grad;
}
// Oeffentlich fuer moo_nn.c::moo_nn_opt_schritt (Optimizer liest/schreibt
// ->grad direkt, braucht denselben Trichter — KIP-G4c I2).
void moo_tensor_grad_sichern(MooTensor* t) { grad_materialisieren(t); }

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
    // KIP-D2: Aktivierung (Op-Output) auf bf16-Praezision runden, wenn Mixed-
    // Precision aktiv. Leaf-Parameter sind nie `out` -> f32-Master unberuehrt;
    // Gradienten/Optimizer rechnen weiter in f32.
    if (!ag_bf16_env_gelesen) ag_bf16_env_init();
    if (ag_bf16) moo_tensor_bf16_runden(T(out));
}

// ============================================================
// KIP-B4b: Activation Checkpointing
// ------------------------------------------------------------
// Ein Checkpoint-Segment fuehrt eine Schicht-Teilsequenz aus, OHNE die
// Zwischen-Aktivierungen im Tape zu halten (nur Segment-Eingang x und
// -Ausgang out ueberleben). Im backward wird das Segment auf einem
// ISOLIERTEN Sub-Tape re-materialisiert und rueckwaerts propagiert; die
// Gradienten fliessen direkt in die geteilten Parameter- und x-Tensoren.
// Dropout ist zustandsbehaftet (seed+zaehler) -> der zaehler wird pro
// Segment gesnapshottet und vor dem Re-Forward restauriert, damit die
// Maske BIT-identisch ist (harter Gate-Fall). Haupt-Tape bleibt G0-konform
// (genau 1 Node pro Segment, kein Fork der Refcount-Zone).
// ============================================================
// Segment-Re-Forward wird als Funktionszeiger uebergeben (Entkopplung:
// moo_autograd.c linkt NICHT hart gegen die NN-Schicht moo_nn.c).

typedef struct {
    MooValue schichten;      // retained: Segment-Schichten
    MooValue (*vorwaerts)(MooValue, MooValue);  // Segment-Re-Forward (moo_nn.c)
    MooValue* drop_dicts;    // retained: Dropout-Layer-Dicts im Segment
    double*   drop_zaehler;  // zaehler-Snapshot beim Segment-Eintritt
    int32_t   n_drop;
} AgCkpt;
static AgCkpt* ckpts = NULL;
static int64_t ckpt_len = 0, ckpt_cap = 0;

static void bw_checkpoint(const MooAgNode* n);
static const MooTensorOp ckpt_op = {
    "__checkpoint", MOO_OP_UNARY, NULL, NULL, bw_checkpoint, 0
};

static void ckpt_frei(AgCkpt* c) {
    moo_release(c->schichten);
    for (int32_t j = 0; j < c->n_drop; j++) moo_release(c->drop_dicts[j]);
    free(c->drop_dicts);
    free(c->drop_zaehler);
    c->schichten = moo_none();
    c->drop_dicts = NULL;
    c->drop_zaehler = NULL;
    c->n_drop = 0;
}

// Forward: Segment unter autograd-AUS ausfuehren (keine Zwischen-Nodes),
// EINEN Checkpoint-Node an das Haupt-Tape haengen. schichten/x borrowed,
// drop_dicts/drop_zaehler borrowed (werden kopiert). Rueckgabe +1.
MooValue moo_ag_checkpoint(MooValue (*vorwaerts)(MooValue, MooValue),
                           MooValue schichten, MooValue x,
                           MooValue* drop_dicts, const double* drop_zaehler,
                           int32_t n_drop) {
    if (x.tag != MOO_TENSOR) {
        moo_throw(moo_error("checkpoint: die Eingabe ist kein Tensor"));
        return moo_none();
    }
    // Inferenz / Aufzeichnung aus: normaler Forward, kein Node.
    if (!ag_enabled || in_backward) return vorwaerts(schichten, x);
    if (!bw_registriert) moo_ag_init_bw();

    // Forward OHNE Zwischen-Nodes; Dropout advanced zaehler pre->post.
    bool save = ag_enabled;
    ag_enabled = false;
    MooValue out = vorwaerts(schichten, x);
    ag_enabled = save;
    if (out.tag != MOO_TENSOR) return out;   // Fehler geworfen

    // Checkpoint-Kontext (Index via node->skalar).
    if (ckpt_len == ckpt_cap) {
        int64_t nc = ckpt_cap ? ckpt_cap * 2 : 8;
        AgCkpt* na = (AgCkpt*)realloc(ckpts, (size_t)nc * sizeof(AgCkpt));
        if (!na) { moo_release(out); moo_throw(moo_error("checkpoint: Speicher voll")); return moo_none(); }
        ckpts = na; ckpt_cap = nc;
    }
    int64_t idx = ckpt_len++;
    AgCkpt* c = &ckpts[idx];
    moo_retain(schichten);
    c->schichten = schichten;
    c->vorwaerts = vorwaerts;
    c->n_drop = n_drop;
    c->drop_dicts = NULL;
    c->drop_zaehler = NULL;
    if (n_drop > 0) {
        c->drop_dicts = (MooValue*)malloc((size_t)n_drop * sizeof(MooValue));
        c->drop_zaehler = (double*)malloc((size_t)n_drop * sizeof(double));
        if (!c->drop_dicts || !c->drop_zaehler) {
            free(c->drop_dicts); free(c->drop_zaehler);
            moo_release(schichten); ckpt_len--; moo_release(out);
            moo_throw(moo_error("checkpoint: Speicher voll")); return moo_none();
        }
        for (int32_t j = 0; j < n_drop; j++) {
            moo_retain(drop_dicts[j]);
            c->drop_dicts[j] = drop_dicts[j];
            c->drop_zaehler[j] = drop_zaehler[j];
        }
    }

    // Checkpoint-Node an das Haupt-Tape (Kapazitaet wie moo_ag_record).
    if (tape_len == tape_cap) {
        int64_t nc = tape_cap ? tape_cap * 2 : 64;
        MooAgNode* nt = (MooAgNode*)realloc(tape, (size_t)nc * sizeof(MooAgNode));
        if (!nt) { moo_release(out); moo_throw(moo_error("checkpoint: Speicher voll (Tape)")); return moo_none(); }
        tape = nt; tape_cap = nc;
    }
    MooAgNode* node = &tape[tape_len++];
    node->op = &ckpt_op;
    node->n_in = 1;
    moo_retain(x);
    node->inputs[0] = x;
    moo_retain(out);
    node->output = out;
    node->hat_skalar = true;
    node->skalar = (double)idx;
    T(out)->requires_grad = true;
    return out;
}

// Backward: Sub-Tape Re-Forward + Sub-Tape-Backward. Seedet out_new->grad
// aus out_stored->grad; akkumuliert in x->grad UND (geteilte) param->grad.
static void bw_checkpoint(const MooAgNode* n) {
    int64_t idx = (int64_t)n->skalar;
    AgCkpt* c = &ckpts[idx];
    MooValue out_stored = n->output;
    MooValue x = n->inputs[0];
    if (!T(out_stored)->grad) return;

    // Dropout-Zaehler auf Segment-Eintritt zuruecksetzen -> Maske bit-identisch.
    for (int32_t j = 0; j < c->n_drop; j++)
        moo_dict_set(c->drop_dicts[j], moo_string_new("zaehler"),
                     moo_number(c->drop_zaehler[j]));

    // Sub-Tape installieren, Haupt-Tape-Zustand sichern.
    MooAgNode* save_tape = tape;
    int64_t save_len = tape_len, save_cap = tape_cap;
    bool save_inbw = in_backward, save_ag = ag_enabled;
    tape = NULL; tape_len = 0; tape_cap = 0;
    in_backward = false; ag_enabled = true;

    MooValue out_new = c->vorwaerts(c->schichten, x);   // records auf Sub-Tape
    if (out_new.tag == MOO_TENSOR) {
        MooTensor* on = T(out_new);
        float* ng = grad_sicherstellen(on);
        memcpy(ng, T(out_stored)->grad, (size_t)on->size * sizeof(float));  // Seed
        in_backward = true;
        for (int64_t i = tape_len - 1; i >= 0; i--) {
            if (!T(tape[i].output)->grad) continue;
            tape[i].op->bw(&tape[i]);
        }
    }

    // Sub-Tape freigeben (re-materialisierte Aktivierungen).
    for (int64_t i = 0; i < tape_len; i++) {
        for (int32_t j = 0; j < tape[i].n_in; j++) moo_release(tape[i].inputs[j]);
        moo_release(tape[i].output);
    }
    free(tape);
    if (out_new.tag == MOO_TENSOR) moo_release(out_new);

    // Haupt-Tape wiederherstellen.
    tape = save_tape; tape_len = save_len; tape_cap = save_cap;
    in_backward = save_inbw; ag_enabled = save_ag;
}

MooValue moo_ag_reset(void) {
    for (int64_t i = 0; i < tape_len; i++) {
        for (int32_t j = 0; j < tape[i].n_in; j++) moo_release(tape[i].inputs[j]);
        moo_release(tape[i].output);
    }
    tape_len = 0;
    for (int64_t i = 0; i < ckpt_len; i++) ckpt_frei(&ckpts[i]);   // KIP-B4b
    ckpt_len = 0;
    return moo_none();
}

MooValue moo_ag_an(void)  { ag_enabled = true;  return moo_none(); }
MooValue moo_ag_aus(void) { ag_enabled = false; return moo_none(); }
/* D1: Zustand abfragbar — vorhersage() schaltet temporaer aus und stellt
 * den vorherigen Zustand wieder her (statt blind an zu schalten). */
bool moo_ag_ist_an(void) { return ag_enabled; }

// KIP-D2: Mixed-Precision-Schalter. Einmalig lazy aus MOO_KI_BF16 (== "1").
static void ag_bf16_env_init(void) {
    if (ag_bf16_env_gelesen) return;
    ag_bf16_env_gelesen = true;
    const char* e = getenv("MOO_KI_BF16");
    if (e && e[0] == '1' && e[1] == '\0') ag_bf16 = true;
}
void moo_ag_bf16_setzen(bool an) { ag_bf16 = an; ag_bf16_env_gelesen = true; }
bool moo_ag_bf16_an(void) { ag_bf16_env_init(); return ag_bf16; }

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

    // KIP-G4c Stufe 2 (docs/kip/G4c-production-wiring-plan.md): residenter
    // Backward-Pfad, analog zu Stufe 1 (Forward-Routing in moo_tensor_ops.c).
    // Nur wenn BEIDE Inputs Grad brauchen (moo_ki_gpu_matmul_bw_res verlangt
    // da UND db != NULL, G3d-e-Vertrag) und beide bereits GPU-resident sind
    // (aus Stufe 1) und die Groesse ueber der Schwelle liegt (gleiche Schwelle
    // wie Stufe 1, 2^24). da/db sind REINE Beitraege ohne += (wie der CPU-Pfad
    // unten) — Download + Akkumulation in zg identisch zur bestehenden CPU-
    // Semantik (I3-Vertrag aus PREFLIGHT be01ac8: der Akkumulator zg bleibt in
    // diesem Schritt immer CPU-seitig, GPU dient nur als Rechenbeschleuniger
    // fuer das Delta — kein gemischter CPU/GPU-Akkumulator auf demselben Ziel).
    // KIP-G4c Stufe 4 (STRIKT-Vertrag): unter STRIKT Groessen-Schwelle ignorieren
    // und beide Inputs zwangsweise resident machen. STRIKT-Durchsetzung ist hier
    // bewusst nur fuer den symmetrischen Fall (BEIDE Inputs requires_grad) aktiv
    // -- das ist die einzige Form, fuer die ein residenter Pfad ueberhaupt
    // existiert (matmul_bw_res verlangt da UND db, G3d-e). Der einseitige Fall
    // (nur a ODER b requires_grad) hat noch KEINEN residenten Pfad und bleibt
    // daher auch unter STRIKT CPU (dokumentierte Luecke, kein stiller Fallback
    // im symmetrischen Fall).
    bool strikt = moo_ki_gpu_strikt_aktiv();
    bool done = false;
    bool symmetrisch = a->requires_grad && b->requires_grad;   /* einzige Form mit residentem Pfad */
    if (symmetrisch &&
        (strikt || m * k * p >= (1LL << 24))) {
        if (strikt) { moo_tensor_nach_gpu(a); moo_tensor_nach_gpu(b); }
        if ((a->valid & MOO_V_DEV) && (b->valid & MOO_V_DEV)) {
        int64_t obytes = (int64_t)o->size * (int64_t)sizeof(float);
        int64_t abytes = (int64_t)a->size * (int64_t)sizeof(float);
        int64_t bbytes = (int64_t)b->size * (int64_t)sizeof(float);
        void* gbuf = moo_ki_gpu_buf_belegen(obytes);
        void* dabuf = gbuf ? moo_ki_gpu_buf_belegen(abytes) : NULL;
        void* dbbuf = dabuf ? moo_ki_gpu_buf_belegen(bbytes) : NULL;
        float* tmp_a = NULL;
        float* tmp_b = NULL;
        if (dbbuf && moo_ki_gpu_upload(gbuf, g, obytes) &&
            moo_ki_gpu_matmul_bw_res(a->gpu_buf, b->gpu_buf, gbuf, dabuf, dbbuf,
                                      (int32_t)m, (int32_t)k, (int32_t)p)) {
            tmp_a = (float*)malloc((size_t)abytes);
            tmp_b = (float*)malloc((size_t)bbytes);
            if (tmp_a && tmp_b &&
                moo_ki_gpu_download(dabuf, tmp_a, abytes) &&
                moo_ki_gpu_download(dbbuf, tmp_b, bbytes)) {
                float* zga = grad_sicherstellen(a);
                for (int64_t i = 0; i < a->size; i++) zga[i] += tmp_a[i];
                float* zgb = grad_sicherstellen(b);
                for (int64_t i = 0; i < b->size; i++) zgb[i] += tmp_b[i];
                done = true;
            }
        }
        free(tmp_a);
        free(tmp_b);
        moo_ki_gpu_buf_freigeben(gbuf);
        moo_ki_gpu_buf_freigeben(dabuf);
        moo_ki_gpu_buf_freigeben(dbbuf);
        }
    }

    if (!done && strikt && symmetrisch) {
        moo_throw(moo_error("STRIKT: matmul-Backward nicht GPU-resident routbar (kein Vulkan/Op-Fehler)"));
        return;
    }
    if (!done) {
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
        MooTensor* out = T(n->output);
        if (!out->grad && !(out->grad_valid & MOO_V_DEV)) continue;  // Zweig traegt nicht zum Loss bei
        // KIP-G4c Trichter (Preflight I1/I2, kip-kern docs/kip/G4c-preflight-
        // ownership-vertrag.md): garantiert gueltige CPU-Sicht auf data/grad
        // BEVOR bw_* direkt darauf zugreift. No-op im reinen CPU/F32-Fall
        // (Basisgates bit-identisch) — Voraussetzung fuer jedes residente
        // Backward-Routing in einer spaeteren G4c-Stufe.
        moo_tensor_f32_sichern(out);
        grad_sicherstellen(out);
        for (int32_t k = 0; k < n->n_in; k++)
            moo_tensor_f32_sichern(T(n->inputs[k]));
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
