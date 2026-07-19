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

// KIP-G4e Schritt 1 (docs/kip/G4e-gpu-grad-residency-vertrag.md §2, I8/I9/I12):
// GPU-Gegenstueck zu grad_materialisieren -- macht t->gpu_grad autoritativ.
// Symmetrisch zum CPU-Trichter: laedt hoch wenn GPU noch NICHT autoritativ ist
// (deckt sowohl den Fall "CPU hatte zuletzt einen echten Beitrag" (grad_valid
// MOO_V_DATA) als auch "noch nie geschrieben" (grad_valid==0, frischer Tensor)
// ab -- grad_sicherstellen liefert in beiden Faellen einen KORREKTEN Puffer
// (echter Wert bzw. calloc-Null)). Kein Transfer wenn GPU bereits autoritativ
// ist (MOO_V_DEV gesetzt) -- das ist der Performance-Gewinn ggue. Download-
// pro-Op. Setzt grad_valid UNBEDINGT (I9, kein |=). Rueckgabe false = kein
// GPU verfuegbar/Fehler -> Aufrufer MUSS auf den bestehenden CPU-Pfad
// zurueckfallen (I12), t->grad_valid bleibt dabei UNVERAENDERT (kein Teil-
// Update, kein stiller Datenverlust).
static bool grad_materialisieren_gpu(MooTensor* t) {
    if (!t) return false;
    if (!t->gpu_grad) {
        t->gpu_grad = moo_ki_gpu_buf_belegen((int64_t)t->size * (int64_t)sizeof(float));
        if (!t->gpu_grad) return false;
    }
    if (!(t->grad_valid & MOO_V_DEV)) {
        // GPU noch nicht autoritativ -> genau EINMAL hochladen (echter CPU-
        // Beitrag ODER frische Null, siehe Kommentar oben). WICHTIG: der
        // Vulkan-Buffer-Pool zeroet frisch belegte/wiederverwendete Buffer
        // NICHT (buf_holen/buf_anlegen, kein memset bei Slot-Reuse) -- ohne
        // diesen Upload wuerde grad_accum_res auf undefiniertem VRAM-Inhalt
        // akkumulieren.
        grad_sicherstellen(t);
        if (!moo_ki_gpu_upload(t->gpu_grad, t->grad,
                               (int64_t)t->size * (int64_t)sizeof(float))) {
            return false;
        }
    }
    t->grad_valid = MOO_V_DEV;   // GPU jetzt SOLE autoritativ (Spiegel v. grad_materialisieren)
    return true;
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

// KIP-G4c Punkt 4 (Backward-Residenz, Channel-Diskussion 2026-07-10 ~11:53-11:56,
// kip-kern-Muster): berechnet tmp[i] = g[i] * faktor->data[i] auf der GPU, wenn
// faktor bereits resident ist (faktor->valid & MOO_V_DEV, faktor->gpu_buf). g
// ist bereits CPU-materialisiert (o->grad, Preflight-Trichter in rueckwaerts())
// und wird hier NUR transient hochgeladen -- KEIN neuer gpu_grad-Lifecycle,
// keine Persistenz. Ergebnis landet in tmp (Host, Groesse n) fuer die UNVER-
// AENDERTE CPU-Akkumulation danach (accum_bcast/accum_bcast_mul bleiben der
// EINZIGE Akkumulationsort fuer ALLE Op-Typen -- I3-konform, kein Split-
// Akkumulator zwischen ew- und matmul-Backward). Rueckgabe false = kein GPU-
// Pfad genutzt, Aufrufer faellt auf den bestehenden CPU-Pfad zurueck.
static bool gpu_ew_kontribution(int32_t gop, const float* g, MooTensor* faktor,
                                int64_t n, float* tmp) {
    if (!(faktor->valid & MOO_V_DEV) || !faktor->gpu_buf) return false;
    int64_t bytes = n * (int64_t)sizeof(float);
    void* gbuf = moo_ki_gpu_buf_belegen(bytes);
    void* tbuf = gbuf ? moo_ki_gpu_buf_belegen(bytes) : NULL;
    bool ok = false;
    if (tbuf && moo_ki_gpu_upload(gbuf, g, bytes) &&
        moo_ki_gpu_ew_res(gop, gbuf, faktor->gpu_buf, tbuf, n) &&
        moo_ki_gpu_download(tbuf, tmp, bytes)) {
        ok = true;
    }
    moo_ki_gpu_buf_freigeben(gbuf);
    moo_ki_gpu_buf_freigeben(tbuf);
    return ok;
}

// KIP-G4e Schritt 2 (docs/kip/G4e-gpu-grad-residency-vertrag.md §5 Punkt 2,
// I8/I9/I10/I12): echte gpu_grad-Residenz-Variante von gpu_ew_kontribution.
// Berechnet dieselbe Kontribution delta=g*faktor (EIN Compute-Dispatch, wie
// gpu_ew_kontribution) und versucht sie DANACH bevorzugt DIREKT in
// ziel->gpu_grad zu akkumulieren (grad_materialisieren_gpu-Trichter + echter
// GPU-Fan-out-Akkumulator moo_ki_gpu_grad_accum_res) -- KEIN Host-Roundtrip,
// KEIN Beruehren von ziel->grad/grad_sicherstellen in diesem Zweig. Nur wenn
// die persistente Route scheitert (Grad-Buffer-Belegung/-Upload/-Accum-
// Fehler auf einem sonst funktionierenden GPU-Pfad), wird auf den bisherigen
// Host-tmp-Weg ausgewichen (I12: kein Teil-Update -- die ew_res-Kontribution
// selbst ist bereits fertig berechnet, nur ihr Akkumulationsziel wechselt).
// Rueckgabe: 2 = resident in ziel->gpu_grad akkumuliert (ziel->grad NICHT
// veraendert, Aufrufer macht NICHTS weiter), 1 = tmp gefuellt (Aufrufer MUSS
// noch accum_bcast(ziel,o,tmp) rufen, wie beim alten Pfad), 0 = kein
// GPU-Pfad ueberhaupt nutzbar (faktor nicht resident) -> Aufrufer faellt auf
// den vollen CPU-Pfad zurueck.
static int gpu_ew_kontribution2(int32_t gop, const float* g, MooTensor* faktor,
                                MooTensor* ziel, int64_t n, float* tmp) {
    if (!(faktor->valid & MOO_V_DEV) || !faktor->gpu_buf) return 0;
    int64_t bytes = n * (int64_t)sizeof(float);
    void* gbuf = moo_ki_gpu_buf_belegen(bytes);
    void* dbuf = gbuf ? moo_ki_gpu_buf_belegen(bytes) : NULL;
    int result = 0;
    if (dbuf && moo_ki_gpu_upload(gbuf, g, bytes) &&
        moo_ki_gpu_ew_res(gop, gbuf, faktor->gpu_buf, dbuf, n)) {
        if (grad_materialisieren_gpu(ziel) &&
            moo_ki_gpu_grad_accum_res(ziel->gpu_grad, dbuf, n)) {
            result = 2;
        } else if (moo_ki_gpu_download(dbuf, tmp, bytes)) {
            result = 1;
        }
    }
    moo_ki_gpu_buf_freigeben(gbuf);
    moo_ki_gpu_buf_freigeben(dbuf);
    return result;
}

// KIP-G4d: Analog gpu_ew_kontribution, aber fuer die Norm-Backward-Kontribution
// (moo_ki_gpu_norm_bw_res braucht x UND g als Device-Buffer). x (a) muss schon
// resident sein (a->valid&MOO_V_DEV); g wird transient hochgeladen (KEIN neuer
// gpu_grad-Lifecycle, wie bei gpu_ew_kontribution). Ergebnis (reine dx-
// Kontribution, kein +=) landet in tmp fuer die unveraenderte accum_bcast-
// Akkumulation danach (I3-konform, einziger Akkumulationsort).
static bool gpu_norm_bw_kontribution(int32_t normop, MooTensor* a, const float* g,
                                     int64_t rows, int64_t cols, float eps, float* tmp) {
    if (!(a->valid & MOO_V_DEV) || !a->gpu_buf) return false;
    int64_t bytes = a->size * (int64_t)sizeof(float);
    void* gbuf = moo_ki_gpu_buf_belegen(bytes);
    void* dxbuf = gbuf ? moo_ki_gpu_buf_belegen(bytes) : NULL;
    bool ok = false;
    if (dxbuf && moo_ki_gpu_upload(gbuf, g, bytes) &&
        moo_ki_gpu_norm_bw_res(normop, a->gpu_buf, gbuf, dxbuf, (int32_t)rows, (int32_t)cols, eps) &&
        moo_ki_gpu_download(dxbuf, tmp, bytes)) {
        ok = true;
    }
    moo_ki_gpu_buf_freigeben(gbuf);
    moo_ki_gpu_buf_freigeben(dxbuf);
    return ok;
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

    /* KIP-G4c Punkt 4 (kip-kern-Muster 2026-07-10): GPU berechnet NUR die reine
     * elementweise Kontribution (g*Faktor) im gleiche-Form-Fastpath (kein
     * Broadcast zwischen a/b/out -- Schwelle n>=2^20 analog ew_op Stufe 5/6).
     * Die Akkumulation bleibt UNVERAENDERT accum_bcast/accum_bcast_mul (CPU,
     * einziger Akkumulationsort fuer ALLE Op-Typen -- verhindert den Split-
     * Akkumulator zwischen ew- und matmul-Backward, siehe Channel-Diskussion).
     * STRIKT erzwingt Residenz + wirft hart NUR wenn der Fastpath ueberhaupt
     * greift (gleiche_form); Broadcast bleibt dokumentierte CPU-Luecke, kein
     * residenter Pfad dafuer (analog asymmetrischer bw_matmul-Fall). */
    bool gleiche_form =
        a->ndim == o->ndim && memcmp(a->shape, o->shape, sizeof(int32_t) * (size_t)a->ndim) == 0 &&
        b->ndim == o->ndim && memcmp(b->shape, o->shape, sizeof(int32_t) * (size_t)b->ndim) == 0;
    bool strikt = moo_ki_gpu_strikt_aktiv();

    if (a->requires_grad) {           // da += g * b (b auf out-Form gelesen)
        int r = 0;
        if (gleiche_form && (strikt || o->size >= (1LL << 20))) {
            if (strikt) moo_tensor_nach_gpu(b);
            // KIP-G4e Schritt 2: bevorzugt echte gpu_grad-Residenz (r==2, kein
            // Host-Roundtrip); r==1 ist der alte Host-tmp-Fallback (identisches
            // Verhalten zu vorher); r==0 = ew_res selbst nicht resident moeglich.
            r = gpu_ew_kontribution2(2 /* mul, siehe ew_op-Konvention */, g, b, a, o->size, buf);
        }
        if (r == 0 && strikt && gleiche_form) {
            free(buf);
            moo_throw(moo_error("STRIKT: mul-Backward (da) nicht GPU-resident routbar (kein Vulkan/Op-Fehler)"));
            return;
        }
        if (r == 2) {
            // Bereits resident in a->gpu_grad akkumuliert (I8) -- nichts weiter zu tun.
        } else if (r == 1) {
            accum_bcast(a, o, buf);        // buf ist bereits die volle Kontribution g*b
        } else {
            materialize_bcast(b, o, buf);
            accum_bcast_mul(a, o, g, buf);
        }
    }
    if (b->requires_grad) {           // db += g * a
        int r = 0;
        if (gleiche_form && (strikt || o->size >= (1LL << 20))) {
            if (strikt) moo_tensor_nach_gpu(a);
            r = gpu_ew_kontribution2(2 /* mul, siehe ew_op-Konvention */, g, a, b, o->size, buf);
        }
        if (r == 0 && strikt && gleiche_form) {
            free(buf);
            moo_throw(moo_error("STRIKT: mul-Backward (db) nicht GPU-resident routbar (kein Vulkan/Op-Fehler)"));
            return;
        }
        if (r == 2) {
            // Bereits resident in b->gpu_grad akkumuliert (I8) -- nichts weiter zu tun.
        } else if (r == 1) {
            accum_bcast(b, o, buf);        // buf ist bereits die volle Kontribution g*a
        } else {
            materialize_bcast(a, o, buf);
            accum_bcast_mul(b, o, g, buf);
        }
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

    /* KIP-G4c Punkt 4 (Folgeschritt zu bw_mul, gleiches Muster, Channel
     * 2026-07-10): GPU berechnet NUR die da-Kontribution (g/b) im gleiche-
     * Form-Fastpath ueber den bestehenden div-Op (gop=3) -- direkt als
     * ew_res(g,b) ausdrueckbar, identisches Muster zu bw_mul (gop=2). Die
     * db-Kontribution (-g*out/b) braucht eine 3-Operanden-Rechnung (g*out,
     * dann /b, dann negieren), die KEIN bestehender 2-Operanden-ew_res-Op
     * abdeckt -- bewusst NICHT beschleunigt in diesem risikoarmen Folge-
     * schritt (kein neuer Shader-Op). Akkumulation bleibt unveraendert
     * CPU-seitig (accum_bcast/accum_bcast_mul, einziger Akkumulationsort
     * fuer ALLE Op-Typen -- gleicher I3-Schutz wie bw_mul). */
    bool gleiche_form_ab =
        a->ndim == o->ndim && memcmp(a->shape, o->shape, sizeof(int32_t) * (size_t)a->ndim) == 0 &&
        b->ndim == o->ndim && memcmp(b->shape, o->shape, sizeof(int32_t) * (size_t)b->ndim) == 0;
    bool strikt = moo_ki_gpu_strikt_aktiv();

    if (a->requires_grad) {           // da += g / b
        bool done = false;
        if (gleiche_form_ab && (strikt || o->size >= (1LL << 20))) {
            if (strikt) moo_tensor_nach_gpu(b);
            done = gpu_ew_kontribution(3 /* div, siehe ew_op-Konvention */, g, b, o->size, buf);
        }
        if (!done && strikt && gleiche_form_ab) {
            free(bb); free(buf);
            moo_throw(moo_error("STRIKT: div-Backward (da) nicht GPU-resident routbar (kein Vulkan/Op-Fehler)"));
            return;
        }
        if (done) {
            accum_bcast(a, o, buf);        // buf ist bereits die volle Kontribution g/b
        } else {
            for (int64_t i = 0; i < o->size; i++) buf[i] = 1.0f / bb[i];
            accum_bcast_mul(a, o, g, buf);
        }
    }
    if (b->requires_grad) {           // db += -g * a / b^2  = -g * out / b (bewusst CPU, s.o.)
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
    MooTensor* idx = T(n->inputs[1]);
    MooTensor* o = T(n->output);
    if (!w->requires_grad) return;
    int64_t n_idx = idx->size;
    int64_t dim = w->shape[1];
    int64_t vocab = w->shape[0];
    bool strikt = moo_ki_gpu_strikt_aktiv();
    bool resident = (o->grad_valid & MOO_V_DEV) || (idx->valid & MOO_V_DEV);
    bool gross = dim > 0 && n_idx >= ((1LL << 16) + dim - 1) / dim;
    bool gpu_shape = n_idx >= 0 && n_idx <= INT32_MAX &&
                     dim > 0 && dim <= INT32_MAX &&
                     vocab > 0 && vocab <= INT32_MAX;
    bool done = false;

    if (gpu_shape && (strikt || resident || gross)) {
        moo_tensor_nach_gpu(idx);
        if ((idx->valid & MOO_V_DEV) && grad_materialisieren_gpu(o)) {
            int64_t bytes = w->size * (int64_t)sizeof(float);
            void* delta = moo_ki_gpu_buf_belegen(bytes);
            if (delta && moo_ki_gpu_scatter_add_res(
                    o->gpu_grad, idx->gpu_buf, delta,
                    (int32_t)n_idx, (int32_t)dim, (int32_t)vocab)) {
                if (grad_materialisieren_gpu(w)) {
                    if (!moo_ki_gpu_grad_accum_res(w->gpu_grad, delta, w->size)) {
                        moo_ki_gpu_buf_freigeben(delta);
                        moo_throw(moo_error("autograd: gather GPU-Grad-Akkumulation fehlgeschlagen"));
                        return;
                    }
                    done = true;
                }
            }
            moo_ki_gpu_buf_freigeben(delta);
        }
    }
    if (!done && strikt) {
        moo_throw(moo_error(gpu_shape
            ? "STRIKT: gather-Backward nicht GPU-resident routbar"
            : "STRIKT: gather-Backward-Form ueberschreitet int32-GPU-Grenzen"));
        return;
    }
    if (done) return;

    /* CPU-Fallback materialisiert nur bei tatsächlich gescheitertem GPU-Versuch. */
    moo_tensor_f32_sichern(idx);
    const float* g = grad_sicherstellen(o);
    float* zg = grad_sicherstellen(w);
    for (int64_t i = 0; i < n_idx; i++) {
        int64_t r = (int64_t)idx->data[i];
        for (int64_t d = 0; d < dim; d++)
            zg[r * dim + d] += g[i * dim + d];
    }
}

static void bw_matmul(const MooAgNode* n) {
    /* out[m,p] = a[m,k] @ b[k,p]: da += g@b^T, db += a^T@g.
     * KIP-G5 akkumuliert beide reinen Beiträge direkt in gpu_grad. */
    MooTensor* o = T(n->output);
    MooTensor* a = T(n->inputs[0]);
    MooTensor* b = T(n->inputs[1]);
    int64_t m = a->shape[0], k = a->shape[1], p = b->shape[1];
    bool strikt = moo_ki_gpu_strikt_aktiv();
    bool irgendein_grad = a->requires_grad || b->requires_grad;
    if (!irgendein_grad) return;

    int64_t kp = k * p; /* k/p sind int32-Formwerte: Produkt passt in int64. */
    bool gross = kp >= (1LL << 20) ||
        (kp > 0 && m >= (((1LL << 20) + kp - 1) / kp));
    bool resident = (o->grad_valid & MOO_V_DEV) ||
                    (a->valid & MOO_V_DEV) || (b->valid & MOO_V_DEV);
    bool done = false;

    if (strikt || resident || gross) {
        moo_tensor_nach_gpu(a);
        moo_tensor_nach_gpu(b);
        if ((a->valid & MOO_V_DEV) && (b->valid & MOO_V_DEV) &&
            grad_materialisieren_gpu(o)) {
            int64_t abytes = a->size * (int64_t)sizeof(float);
            int64_t bbytes = b->size * (int64_t)sizeof(float);
            void* da = moo_ki_gpu_buf_belegen(abytes);
            void* db = da ? moo_ki_gpu_buf_belegen(bbytes) : NULL;
            if (db && moo_ki_gpu_matmul_bw_res(
                    a->gpu_buf, b->gpu_buf, o->gpu_grad, da, db,
                    (int32_t)m, (int32_t)k, (int32_t)p)) {
                bool ready_a = !a->requires_grad || grad_materialisieren_gpu(a);
                bool ready_b = !b->requires_grad || grad_materialisieren_gpu(b);
                if (ready_a && ready_b) {
                    bool ok_a = !a->requires_grad ||
                        moo_ki_gpu_grad_accum_res(a->gpu_grad, da, a->size);
                    bool ok_b = !b->requires_grad ||
                        moo_ki_gpu_grad_accum_res(b->gpu_grad, db, b->size);
                    if (!ok_a || !ok_b) {
                        moo_ki_gpu_buf_freigeben(da);
                        moo_ki_gpu_buf_freigeben(db);
                        moo_throw(moo_error("autograd: matmul GPU-Grad-Akkumulation fehlgeschlagen"));
                        return;
                    }
                    done = true;
                }
            }
            moo_ki_gpu_buf_freigeben(da);
            moo_ki_gpu_buf_freigeben(db);
        }
    }

    if (!done && strikt) {
        moo_throw(moo_error("STRIKT: matmul-Backward nicht GPU-resident routbar"));
        return;
    }
    if (done) return;

    /* Historischer CPU-Pfad bleibt bit-identisch und materialisiert erst jetzt. */
    moo_tensor_f32_sichern(o);
    moo_tensor_f32_sichern(a);
    moo_tensor_f32_sichern(b);
    const float* g = grad_sicherstellen(o);
    if (a->requires_grad) {
        float* zg = grad_sicherstellen(a);
        for (int64_t i = 0; i < m; i++)
            for (int64_t j = 0; j < p; j++) {
                float gv = g[i * p + j];
                for (int64_t kk = 0; kk < k; kk++)
                    zg[i * k + kk] += gv * b->data[kk * p + j];
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

// KIP-G4d: analytisches Backward fuer die dedizierten Norm-KERN-Ops (affine-
// frei; Stats werden aus x rekonstruiert, exakt wie moo_ki_gpu_norm_bw_res --
// n (=xhat) ist bereits im Forward-Output o->data vorhanden, kein erneutes
// Normalisieren noetig). GPU-Residenz analog bw_mul/bw_div: gpu_norm_bw_
// kontribution liefert die REINE dx-Kontribution, Akkumulation bleibt
// ausschliesslich ueber accum_bcast (einziger Akkumulationsort, I3).
static void bw_norm_kern(const MooAgNode* n, int32_t normop) {
    const MooTensor* o = T(n->output);   // xhat
    const float* g = o->grad;
    MooTensor* a = T(n->inputs[0]);      // x
    if (!a->requires_grad) return;
    int64_t rows = (a->ndim == 2) ? a->shape[0] : 1;
    int64_t cols = (a->ndim == 2) ? a->shape[1] : a->shape[0];
    const float eps = 1e-5f;
    bool strikt = moo_ki_gpu_strikt_aktiv();
    float* buf = (float*)malloc((size_t)o->size * sizeof(float));
    if (!buf) { moo_throw(moo_error("autograd: Speicher voll")); return; }
    bool done = false;
    if (strikt || o->size >= MOO_KI_NORM_FUSED_GPU_MIN_ELEMENTS) {
        done = gpu_norm_bw_kontribution(normop, a, g, rows, cols, eps, buf);
    }
    if (!done && strikt) {
        free(buf);
        moo_throw(moo_error("STRIKT: norm_kern-Backward nicht GPU-resident routbar (kein Vulkan/Op-Fehler)"));
        return;
    }
    if (!done) {
        for (int64_t i = 0; i < rows; i++) {
            const float* x  = a->data + i * cols;
            const float* xh = o->data + i * cols;   // bereits normalisiert (Forward-Cache)
            const float* gr = g + i * cols;
            float* bo = buf + i * cols;
            if (normop == MOO_KI_NORM_LAYER) {
                double meang = 0.0, meangn = 0.0;
                for (int64_t j = 0; j < cols; j++) { meang += (double)gr[j]; meangn += (double)gr[j] * (double)xh[j]; }
                meang /= (double)cols; meangn /= (double)cols;
                double mu = 0.0;
                for (int64_t j = 0; j < cols; j++) mu += (double)x[j];
                mu /= (double)cols;
                double var = 0.0;
                for (int64_t j = 0; j < cols; j++) { double d = (double)x[j] - mu; var += d * d; }
                var /= (double)cols;
                double s = sqrt(var + (double)eps);
                for (int64_t j = 0; j < cols; j++)
                    bo[j] = (float)(((double)gr[j] - meang - (double)xh[j] * meangn) / s);
            } else {
                double meangn = 0.0;
                for (int64_t j = 0; j < cols; j++) meangn += (double)gr[j] * (double)xh[j];
                meangn /= (double)cols;
                double ms = 0.0;
                for (int64_t j = 0; j < cols; j++) ms += (double)x[j] * (double)x[j];
                ms /= (double)cols;
                double s = sqrt(ms + (double)eps);
                for (int64_t j = 0; j < cols; j++)
                    bo[j] = (float)(((double)gr[j] - (double)xh[j] * meangn) / s);
            }
        }
    }
    accum_bcast(a, o, buf);
    free(buf);
}
static void bw_layernorm_kern(const MooAgNode* n) { bw_norm_kern(n, MOO_KI_NORM_LAYER); }
static void bw_rmsnorm_kern(const MooAgNode* n)   { bw_norm_kern(n, MOO_KI_NORM_RMS); }
// KI-MULTI-V2b: stellt den Ausgangsgradienten als residenten Read-Buffer bereit.
// gpu_grad wird geliehen; ein Host-Gradient bekommt einen temporaeren Upload.
static void* bild_grad_quelle_gpu(MooTensor* o, bool* temporaer) {
    *temporaer = false;
    if ((o->grad_valid & MOO_V_DEV) && o->gpu_grad) return o->gpu_grad;
    float* g = grad_sicherstellen(o);
    if (!g) return NULL;
    void* p = moo_ki_gpu_buf_belegen((int64_t)o->size * (int64_t)sizeof(float));
    if (!p || !moo_ki_gpu_upload(p, g, (int64_t)o->size * (int64_t)sizeof(float))) {
        moo_ki_gpu_buf_freigeben(p); return NULL;
    }
    *temporaer = true;
    return p;
}

// KI-MULTI-V2 im2col-Backward = col2im. V2b haelt Beitrag und Fan-out-Akku
// resident; CPU ist nur normaler Fallback ausserhalb Schwelle/ohne Vulkan.
static void bw_im2col(const MooAgNode* n) {
    MooTensor* o = T(n->output), *x = T(n->inputs[0]);
    if (!x->requires_grad) return;
    uint32_t p=(uint32_t)n->skalar;
    int32_t kh=(int32_t)(p&0xFFu),kw=(int32_t)((p>>8)&0xFFu);
    int32_t stride=(int32_t)((p>>16)&0xFFu),pad=(int32_t)((p>>24)&0xFFu);
    int32_t b=x->shape[0],h=x->shape[1],w=x->shape[2],c=x->shape[3];
    bool strikt=moo_ki_gpu_strikt_aktiv(),done=false,tmpg=false;
    if(strikt||(o->grad_valid&MOO_V_DEV)||o->size>=(1LL<<16)){
        void* gbuf=bild_grad_quelle_gpu(o,&tmpg);
        void* dxbuf=gbuf?moo_ki_gpu_buf_belegen((int64_t)x->size*(int64_t)sizeof(float)):NULL;
        if(dxbuf&&grad_materialisieren_gpu(x)&&
           moo_ki_gpu_col2im_res(gbuf,dxbuf,b,h,w,c,kh,kw,stride,pad)&&
           moo_ki_gpu_grad_accum_res(x->gpu_grad,dxbuf,x->size)){
            x->grad_valid=MOO_V_DEV; done=true;
        }
        moo_ki_gpu_buf_freigeben(dxbuf);
        if(tmpg)moo_ki_gpu_buf_freigeben(gbuf);
    }
    if(done)return;
    if(strikt){moo_throw(moo_error("STRIKT: col2im-Backward nicht GPU-resident routbar"));return;}
    moo_tensor_f32_sichern(x); float* xg=grad_sicherstellen(x); float* og=grad_sicherstellen(o);
    int32_t oh=(h+2*pad-kh)/stride+1,ow=(w+2*pad-kw)/stride+1;
    int64_t kcol=(int64_t)kh*kw*c;
    for(int32_t bi=0;bi<b;bi++)for(int32_t oy=0;oy<oh;oy++)for(int32_t ox=0;ox<ow;ox++){
        int64_t row=((int64_t)bi*oh+oy)*ow+ox;const float* grow=og+row*kcol;
        for(int32_t ky=0;ky<kh;ky++){int32_t iy=oy*stride-pad+ky;
            for(int32_t kx=0;kx<kw;kx++){int32_t ix=ox*stride-pad+kx;
                if(iy>=0&&iy<h&&ix>=0&&ix<w){int64_t col=((int64_t)ky*kw+kx)*c;
                    float* dst=xg+(((int64_t)bi*h+iy)*w+ix)*c;
                    for(int32_t ch=0;ch<c;ch++)dst[ch]+=grow[col+ch];
                }
            }
        }
    }
}

// KI-MULTI-V2b pooling-Backward, resident fuer max und mittel.
static void bw_pool(const MooAgNode* n) {
    MooTensor* o=T(n->output),*x=T(n->inputs[0]); if(!x->requires_grad)return;
    uint32_t p=(uint32_t)n->skalar;int32_t art=(int32_t)(p&0xFFu);
    int32_t g=(int32_t)((p>>8)&0xFFu),s=(int32_t)((p>>16)&0xFFu);
    int32_t b=x->shape[0],h=x->shape[1],w=x->shape[2],c=x->shape[3];
    bool strikt=moo_ki_gpu_strikt_aktiv(),done=false,tmpg=false;
    if(strikt||(x->valid&MOO_V_DEV)||(o->grad_valid&MOO_V_DEV)||o->size>=(1LL<<16)){
        moo_tensor_nach_gpu(x);void* gbuf=bild_grad_quelle_gpu(o,&tmpg);
        void* dxbuf=gbuf?moo_ki_gpu_buf_belegen((int64_t)x->size*(int64_t)sizeof(float)):NULL;
        if((x->valid&MOO_V_DEV)&&dxbuf&&grad_materialisieren_gpu(x)&&
           moo_ki_gpu_pool_bw_res(art,x->gpu_buf,gbuf,dxbuf,b,h,w,c,g,s)&&
           moo_ki_gpu_grad_accum_res(x->gpu_grad,dxbuf,x->size)){
            x->grad_valid=MOO_V_DEV;done=true;
        }
        moo_ki_gpu_buf_freigeben(dxbuf);if(tmpg)moo_ki_gpu_buf_freigeben(gbuf);
    }
    if(done)return;
    if(strikt){moo_throw(moo_error("STRIKT: pooling-Backward nicht GPU-resident routbar"));return;}
    moo_tensor_f32_sichern(x);float* xg=grad_sicherstellen(x);float* og=grad_sicherstellen(o);
    int32_t oh=(h-g)/s+1,ow=(w-g)/s+1;float inv=1.0f/(float)(g*g);
    for(int32_t bi=0;bi<b;bi++)for(int32_t oy=0;oy<oh;oy++)for(int32_t ox=0;ox<ow;ox++)
      for(int32_t ch=0;ch<c;ch++){float go=og[(((int64_t)bi*oh+oy)*ow+ox)*c+ch];
        if(art==1){for(int32_t ky=0;ky<g;ky++)for(int32_t kx=0;kx<g;kx++)
          xg[(((int64_t)bi*h+oy*s+ky)*w+ox*s+kx)*c+ch]+=go*inv;
        }else{int32_t by=0,bx=0;float best=0;bool first=true;
          for(int32_t ky=0;ky<g;ky++)for(int32_t kx=0;kx<g;kx++){float v=x->data[(((int64_t)bi*h+oy*s+ky)*w+ox*s+kx)*c+ch];
            if(first||v>best){best=v;by=ky;bx=kx;}first=false;}
          xg[(((int64_t)bi*h+oy*s+by)*w+ox*s+bx)*c+ch]+=go;
        }
      }
}


// KI-Q1 Hadamard-Backward. Die Vorwaertsabbildung A = (1/sqrt(n))*H*D ist
// ORTHOGONAL: A*A^T = (1/n)*H*D*D*H^T = (1/n)*H*H = I (D*D = I weil d_i^2 = 1,
// H symmetrisch mit H*H = n*I). Das exakte backward ist deshalb die
// Transponierte A^T = (1/sqrt(n))*D*H - im Code schlicht die umgekehrte
// Operationsreihenfolge zum Forward (erst WHT, dann skalieren, dann
// Vorzeichen). Kein approximierter Gradient, daher im Gradcheck scharf.
static void bw_hadamard(const MooAgNode* n) {
    MooTensor* o = T(n->output), *x = T(n->inputs[0]);
    if (!x->requires_grad) return;
    int64_t nn = x->shape[x->ndim - 1];
    // Forward haette bei ungueltiger Achse geworfen; defensiv trotzdem pruefen,
    // damit das backward nie ueber eine kaputte Form laeuft.
    if (!moo_quant_ist_zweierpotenz(nn)) return;
    moo_tensor_f32_sichern(x);
    float* xg = grad_sicherstellen(x);
    float* og = grad_sicherstellen(o);
    float* d = (float*)malloc((size_t)nn * sizeof(float));
    float* tmp = (float*)malloc((size_t)nn * sizeof(float));
    if (!d || !tmp) {
        free(d); free(tmp);
        moo_throw(moo_error("hadamard-Backward: Speicher voll"));
        return;
    }
    // Seed kam validiert (ganzzahlig, nichtnegativ) durch den Forward ins Tape.
    moo_quant_vorzeichen((uint64_t)n->skalar, nn, d);
    float skal = 1.0f / sqrtf((float)nn);
    int64_t zeilen = x->size / nn;
    for (int64_t r = 0; r < zeilen; r++) {
        const float* grow = og + r * nn;
        for (int64_t i = 0; i < nn; i++) tmp[i] = grow[i];
        moo_quant_wht_zeile(tmp, nn);
        float* dst = xg + r * nn;
        // Akkumulieren (+=), nicht zuweisen: derselbe Tensor kann mehrfach
        // Eingang sein (Fan-out) - das ist die Autograd-Konvention hier.
        for (int64_t i = 0; i < nn; i++) dst[i] += tmp[i] * skal * d[i];
    }
    free(d); free(tmp);
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
    moo_tensor_op_set_bw("layernorm_kern", bw_layernorm_kern);
    moo_tensor_op_set_bw("rmsnorm_kern",   bw_rmsnorm_kern);
    moo_tensor_op_set_bw("im2col",  bw_im2col);
    moo_tensor_op_set_bw("pooling", bw_pool);
    moo_tensor_op_set_bw("hadamard", bw_hadamard);
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
        /* V2b-Bildops besitzen ihren eigenen GPU/CPU-Trichter. Sie duerfen
         * GPU-only Aktivierungen/Gradienten nicht vor dem residenten Versuch
         * herunterladen. Alle anderen Ops behalten den historischen CPU-
         * Sicherungsvertrag unveraendert. */
        bool eigener_gpu_trichter = strcmp(n->op->name, "im2col") == 0 ||
                                  strcmp(n->op->name, "pooling") == 0 ||
                                  strcmp(n->op->name, "matmul") == 0 ||
                                  strcmp(n->op->name, "gather") == 0;
        if (!eigener_gpu_trichter) {
            moo_tensor_f32_sichern(out);
            grad_sicherstellen(out);
            for (int32_t k = 0; k < n->n_in; k++)
                moo_tensor_f32_sichern(T(n->inputs[k]));
        }
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
    // KIP-FINAL-FIX2 (67efae9e): Guard muss GPU-only Grad akzeptieren (t->grad
    // kann NULL sein, obwohl ein echter Beitrag NUR auf der GPU liegt --
    // grad_valid==MOO_V_DEV, analog opt_schritt/grad_clip). Nur wenn WEDER ein
    // CPU-Puffer existiert NOCH die GPU-Seite autoritativ ist, gibt es
    // wirklich keinen Gradienten.
    if (!t->grad && !(t->grad_valid & MOO_V_DEV)) {
        moo_throw(moo_error("gradient: noch kein Gradient da — erst mit_gradient() "
                            "setzen und rueckwaerts() auf dem Verlust aufrufen"));
        return moo_none();
    }
    // Materialisiert die CPU-Seite (Download aus gpu_grad falls grad_valid nur
    // MOO_V_DEV war) -- OHNE diesen Aufruf lieferte gradient() bei rein GPU-
    // residentem Backward (z.B. bw_mul unter STRIKT) einen stalen/genullten
    // Host-Puffer statt des echten Werts (GPT-Gegenreview KI-PROD-01, echter
    // 4070-Ti-Repro: submits=4/cpu_fallbacks=0, aber gradient(x)==[0,0] statt
    // [0.5,3.0]). Trichter ist derselbe wie fuer den Optimizer (I2/I8).
    moo_tensor_grad_sichern(t);
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
    // KIP-G4e-Folgefix: Host ist nach dem Nullen autoritativ (I9-Muster) —
    // sonst akkumuliert ein residenter bw_* (grad_valid==MOO_V_DEV) auf den
    // stalen gpu_grad weiter (gefunden via ki_gpu_g4d_bench.c: maxdiff≈2×iters).
    // grad==NULL-Fall ist mit abgedeckt: grad_sicherstellen liefert calloc-Null,
    // kein Download mehr von der alten GPU-Seite.
    if (t->grad_valid) t->grad_valid = MOO_V_DATA;
    return moo_none();
}

// KIP-X1b Phase A (Design §4a / §8.6): setzt den Gradienten eines Parameters
// direkt — fuer verteiltes Training wird der gemittelte Gradient in t->grad
// geschrieben, bevor gradienten_kappen()/opt.schritt() folgen.
// Vertrag: Ziel Tensor mit requires_grad==true; Quelle Tensor GLEICHER Form
// ODER flache Zahlenliste mit exakter Elementanzahl; Host-Grad-Puffer wird bei
// Bedarf angelegt (grad_sicherstellen); Werte als f32 nach t->grad; danach
// grad_valid = MOO_V_DATA (Host autoritativ, GPU-Grad stale — Maske nach G0/I9,
// analog gradient_loeschen); KEIN Tape-Knoten; Selbstzuweisung definiert (data
// und grad sind getrennte Puffer); Shape-/Typfehler brechen HART ab OHNE
// Teilzustand (alle Pruefungen laufen vor dem ersten Schreiben).
MooValue moo_tensor_gradient_setzen(MooValue tv, MooValue quelle) {
    if (tv.tag != MOO_TENSOR) {
        moo_throw(moo_error("gradient_setzen: das Ziel ist kein Tensor"));
        return moo_none();
    }
    MooTensor* t = T(tv);
    if (!t->requires_grad) {
        moo_throw(moo_error("gradient_setzen: Ziel-Tensor hat kein requires_grad "
                            "— erst mit_gradient() setzen"));
        return moo_none();
    }

    if (quelle.tag == MOO_TENSOR) {
        MooTensor* s = T(quelle);
        // Shape-Gleichheit strikt (ndim + jede Dimension + Gesamtgroesse)
        bool gleich = (s->ndim == t->ndim && s->size == t->size);
        if (gleich) {
            for (int32_t d = 0; d < t->ndim; d++) {
                if (s->shape[d] != t->shape[d]) { gleich = false; break; }
            }
        }
        if (!gleich) {
            moo_throw(moo_error("gradient_setzen: Quell-Tensor hat andere Form als das Ziel"));
            return moo_none();
        }
        // f32-Daten der Quelle sicherstellen (bf16-store/GPU -> f32 data)
        moo_tensor_f32_sichern(s);
        if (!s->data) {
            moo_throw(moo_error("gradient_setzen: Quell-Tensor hat keine f32-Daten"));
            return moo_none();
        }
        float* g = grad_sicherstellen(t);
        if (!g) {
            moo_throw(moo_error("gradient_setzen: Grad-Puffer konnte nicht angelegt werden"));
            return moo_none();
        }
        memcpy(g, s->data, (size_t)t->size * sizeof(float));
    } else if (quelle.tag == MOO_LIST) {
        int64_t n = (int64_t)MV_NUM(moo_list_length(quelle));
        if (n != t->size) {
            moo_throw(moo_error("gradient_setzen: Zahlenliste hat andere Elementanzahl als das Ziel"));
            return moo_none();
        }
        // Erst ALLE Elemente auf Zahl pruefen (kein Teilzustand bei Fehler).
        // moo_list_get liefert eine eigene (+1) Referenz -> jedes Element wieder
        // freigeben, sonst leakt ein Nicht-Zahl-Element (ASan-Gate).
        for (int64_t i = 0; i < n; i++) {
            MooValue e = moo_list_get(quelle, moo_number((double)i));
            bool ist_zahl = (e.tag == MOO_NUMBER);
            moo_release(e);
            if (!ist_zahl) {
                moo_throw(moo_error("gradient_setzen: Zahlenliste enthaelt einen "
                                    "Nicht-Zahlen-Wert"));
                return moo_none();
            }
        }
        float* g = grad_sicherstellen(t);
        if (!g) {
            moo_throw(moo_error("gradient_setzen: Grad-Puffer konnte nicht angelegt werden"));
            return moo_none();
        }
        for (int64_t i = 0; i < n; i++) {
            MooValue e = moo_list_get(quelle, moo_number((double)i));
            g[i] = (float)MV_NUM(e);
            moo_release(e);
        }
    } else {
        moo_throw(moo_error("gradient_setzen: Quelle muss ein Tensor gleicher Form "
                            "oder eine flache Zahlenliste sein"));
        return moo_none();
    }

    // Host-Grad ist jetzt autoritativ; GPU-Grad ist stale (Maske nach G0/I9).
    t->grad_valid = MOO_V_DATA;
    return moo_none();
}
