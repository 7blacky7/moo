/**
 * moo_nn.c — NN-Schichten + Loss + Optimizer (Plan-014 C1).
 * ============================================================================
 * DESIGN-ENTSCHEIDE (C1):
 *   * Schichten und Optimizer sind DICTS (kein neuer Heap-Typ): automatisch
 *     refcounted ueber moo_dict, mit zeige() debugbar, fuer den User offen
 *     (z.B. dropout["aktiv"] = 0 fuer Inferenz). Marker-Key "__nn".
 *   * KEIN neuer Registry-Op: mse/kreuzentropie/layernorm/embedding sind
 *     KOMPOSITIONEN bestehender Ops (alle 26 haben backward + Gradcheck aus
 *     B2) — das Ehrlichkeits-Gate bleibt vollstaendig gueltig.
 *     Embedding laeuft ueber one-hot @ W (CPU-simpel und korrekt; ein
 *     Gather-Op mit eigenem backward kommt erst, wenn Phase G ihn braucht —
 *     dann MIT Gradcheck-Zeile, B2-Vertrag).
 *   * Kreuzentropie ist FUSED ueber logsoftmax (max-shift-stabil, A2) statt
 *     log(softmax(x)) — keine Instabilitaet bei grossen Logits.
 *   * Der Optimizer-Schritt mutiert data[] IN-PLACE ohne Tensor-Ops (kein
 *     Tape-Record), nullt danach ALLE Param-Gradienten und leert den Tape:
 *     opt.schritt() = eine komplette, saubere Trainings-Iteration.
 *
 * TENSOR-KONVENTION gilt: Args borrowed, Rueckgaben +1 owning. Wer speichert
 * (Optimizer haelt die params-Liste), retained selbst.
 * UB-POLICY: Indizes int64_t; Dropout-RNG ueber uint64 splitmix64 (Wrap ok,
 * unsigned); Zaehler im Dict als double (exakt bis 2^53).
 * ============================================================================
 */
#include "moo_runtime.h"
#include <math.h>

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

/* ============================================================
 * Dict-Helfer (Transfer-/Owning-Konventionen von moo_dict.c)
 * ============================================================ */

/* set: key (+1 frisch) und value werden TRANSFERIERT. */
static void dset(MooValue d, const char* k, MooValue v) {
    moo_dict_set(d, moo_string_new(k), v);
}

/* get: liefert +1 owning (oder none); der frische Key wird intern released. */
static MooValue dget(MooValue d, const char* k) {
    return moo_dict_get(d, moo_string_new(k));
}

/* Zahl aus Dict (Fallback wenn fehlt/kein Number). */
static double dnum(MooValue d, const char* k, double fallback) {
    MooValue v = dget(d, k);
    double r = (v.tag == MOO_NUMBER) ? MV_NUM(v) : fallback;
    moo_release(v);
    return r;
}

/* Layer-/Opt-Typ pruefen: liefert true wenn d ein Dict mit __nn == typ ist. */
static bool nn_ist(MooValue d, const char* typ) {
    if (d.tag != MOO_DICT) return false;
    MooValue t = dget(d, "__nn");
    bool ok = (t.tag == MOO_STRING) && strcmp(MV_STR(t)->chars, typ) == 0;
    moo_release(t);
    return ok;
}

static bool nn_ist_schicht(MooValue d) {
    return nn_ist(d, "dicht") || nn_ist(d, "dropout") ||
           nn_ist(d, "layernorm") || nn_ist(d, "embedding");
}

/* ============================================================
 * Konstruktions-Helfer
 * ============================================================ */

/* [r] bzw. [r,c] Shape-Liste bauen (c<=0 -> 1D). +1 owning. */
static MooValue shape_liste(int64_t r, int64_t c) {
    MooValue l = moo_list_new(2);
    moo_list_append(l, moo_number((double)r));
    if (c > 0) moo_list_append(l, moo_number((double)c));
    return l;
}

/* splitmix64 (Muster moo_tensor.c / moo_noise.c): deterministisch, unsigned. */
static uint64_t sm64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static float sm64_uniform_pm1(uint64_t* s) {  /* [-1, 1) */
    return (float)((double)(sm64(s) >> 11) / 9007199254740992.0) * 2.0f - 1.0f;
}

/* Gewichts-Tensor [ein,aus] uniform in [-limit, limit), seed-deterministisch,
 * requires_grad. +1 owning. */
static MooValue gewicht_init(int32_t ein, int32_t aus, double limit, uint64_t seed) {
    int32_t shape[2] = { ein, aus };
    MooTensor* w = moo_tensor_raw(2, shape);
    if (!w) return moo_none();
    uint64_t state = seed * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    for (int64_t i = 0; i < w->size; i++)
        w->data[i] = sm64_uniform_pm1(&state) * (float)limit;
    w->requires_grad = true;
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, w);
    return v;
}

/* Konstanter Tensor [1,dim] (Bias/gamma/beta), requires_grad. +1 owning. */
static MooValue param_konst(int32_t dim, float wert) {
    int32_t shape[2] = { 1, dim };
    MooTensor* t = moo_tensor_raw(2, shape);
    if (!t) return moo_none();
    if (wert != 0.0f)
        for (int64_t i = 0; i < t->size; i++) t->data[i] = wert;
    t->requires_grad = true;
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

static int32_t ganze_zahl(MooValue v, const char* wo, int32_t min) {
    if (v.tag != MOO_NUMBER) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: erwarte eine ganze Zahl", wo);
        moo_throw(moo_error(msg));
        return -1;
    }
    double d = MV_NUM(v);
    if (d < (double)min || d > 2147483647.0 || d != (double)(int32_t)d) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: erwarte eine ganze Zahl >= %d", wo, min);
        moo_throw(moo_error(msg));
        return -1;
    }
    return (int32_t)d;
}

/* ============================================================
 * Schicht-Konstruktoren
 * ============================================================ */

/* schicht_dicht(ein, aus, aktivierung?, seed?) — W: He-Init fuer relu/gelu
 * (limit = sqrt(6/ein)), sonst Xavier/Glorot (limit = sqrt(6/(ein+aus))).
 * b startet bei 0. aktivierung: "relu"|"sigmoid"|"tanh"|"gelu"|"softmax"|
 * "keine"/none. */
MooValue moo_nn_schicht_dicht(MooValue ein, MooValue aus,
                              MooValue aktivierung, MooValue seed) {
    int32_t ne = ganze_zahl(ein, "schicht_dicht (Eingaben)", 1);
    if (ne < 0) return moo_none();
    int32_t na = ganze_zahl(aus, "schicht_dicht (Ausgaben)", 1);
    if (na < 0) return moo_none();

    const char* akt = "keine";
    if (aktivierung.tag == MOO_STRING) akt = MV_STR(aktivierung)->chars;
    else if (aktivierung.tag != MOO_NONE) {
        moo_throw(moo_error("schicht_dicht: Aktivierung muss ein Text sein, "
                            "z.B. \"relu\" oder \"sigmoid\""));
        return moo_none();
    }
    bool he = (strcmp(akt, "relu") == 0 || strcmp(akt, "gelu") == 0);
    double limit = he ? sqrt(6.0 / (double)ne)
                      : sqrt(6.0 / ((double)ne + (double)na));
    uint64_t s = (seed.tag == MOO_NUMBER) ? (uint64_t)(int64_t)MV_NUM(seed) : 42ULL;

    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("dicht"));
    dset(d, "w", gewicht_init(ne, na, limit, s));
    dset(d, "b", param_konst(na, 0.0f));
    dset(d, "aktivierung", moo_string_new(akt));
    return d;
}

/* schicht_dropout(rate) — "aktiv" (1/0) schaltet Training/Inferenz,
 * fuer den User direkt am Dict aenderbar. Deterministisch via Basis-Seed +
 * Aufruf-Zaehler. */
MooValue moo_nn_schicht_dropout(MooValue rate) {
    if (rate.tag != MOO_NUMBER || MV_NUM(rate) < 0.0 || MV_NUM(rate) >= 1.0) {
        moo_throw(moo_error("schicht_dropout: Rate muss eine Zahl in [0, 1) sein"));
        return moo_none();
    }
    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("dropout"));
    dset(d, "rate", moo_number(MV_NUM(rate)));
    dset(d, "aktiv", moo_number(1.0));
    dset(d, "seed", moo_number(42.0));
    dset(d, "zaehler", moo_number(0.0));
    return d;
}

/* schicht_layernorm(dim) — gamma=1, beta=0, eps=1e-5. Normalisiert die
 * LETZTE Achse (zeilenweise bei [batch, dim]). */
MooValue moo_nn_schicht_layernorm(MooValue dim) {
    int32_t nd = ganze_zahl(dim, "schicht_layernorm (Dimension)", 1);
    if (nd < 0) return moo_none();
    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("layernorm"));
    dset(d, "gamma", param_konst(nd, 1.0f));
    dset(d, "beta", param_konst(nd, 0.0f));
    return d;
}

/* schicht_embedding(vokabular, dim, seed?) — W [vokabular, dim], Xavier.
 * Vorwaerts erwartet Indizes [batch] oder [batch,1]. */
MooValue moo_nn_schicht_embedding(MooValue vokabular, MooValue dim, MooValue seed) {
    int32_t nv = ganze_zahl(vokabular, "schicht_embedding (Vokabular)", 1);
    if (nv < 0) return moo_none();
    int32_t nd = ganze_zahl(dim, "schicht_embedding (Dimension)", 1);
    if (nd < 0) return moo_none();
    uint64_t s = (seed.tag == MOO_NUMBER) ? (uint64_t)(int64_t)MV_NUM(seed) : 42ULL;
    double limit = sqrt(6.0 / ((double)nv + (double)nd));
    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("embedding"));
    dset(d, "w", gewicht_init(nv, nd, limit, s));
    return d;
}

/* ============================================================
 * Vorwaerts
 * ============================================================ */

static MooValue akt_anwenden(const char* akt, MooValue h) {
    if (strcmp(akt, "relu") == 0)    return moo_tensor_relu(h);
    if (strcmp(akt, "sigmoid") == 0) return moo_tensor_sigmoid(h);
    if (strcmp(akt, "tanh") == 0)    return moo_tensor_tanh(h);
    if (strcmp(akt, "gelu") == 0)    return moo_tensor_gelu(h);
    if (strcmp(akt, "softmax") == 0) return moo_tensor_softmax(h);
    if (strcmp(akt, "keine") == 0 || strcmp(akt, "none") == 0 || akt[0] == '\0') {
        moo_retain(h);
        return h;
    }
    char msg[160];
    snprintf(msg, sizeof(msg),
             "vorwaerts: unbekannte Aktivierung '%s' (moeglich: relu, sigmoid, "
             "tanh, gelu, softmax, keine)", akt);
    moo_throw(moo_error(msg));
    return moo_none();
}

static MooValue fw_dicht(MooValue schicht, MooValue x) {
    MooValue w = dget(schicht, "w");
    MooValue b = dget(schicht, "b");
    MooValue aktv = dget(schicht, "aktivierung");
    MooValue out = moo_none();
    if (w.tag == MOO_TENSOR && b.tag == MOO_TENSOR) {
        MooValue h = moo_tensor_matmul(x, w);
        if (h.tag == MOO_TENSOR) {
            MooValue h2 = moo_tensor_add(h, b);
            moo_release(h);
            if (h2.tag == MOO_TENSOR) {
                const char* akt = (aktv.tag == MOO_STRING) ? MV_STR(aktv)->chars : "keine";
                out = akt_anwenden(akt, h2);
                moo_release(h2);
            }
        }
    } else {
        moo_throw(moo_error("vorwaerts: kaputte dichte Schicht (w/b fehlen)"));
    }
    moo_release(w); moo_release(b); moo_release(aktv);
    return out;
}

static MooValue fw_dropout(MooValue schicht, MooValue x) {
    MooTensor* xt = T(x);
    double rate = dnum(schicht, "rate", 0.0);
    double aktiv = dnum(schicht, "aktiv", 1.0);
    if (aktiv == 0.0 || rate <= 0.0) {
        moo_retain(x);
        return x;
    }
    /* Deterministische Maske: seed + zaehler (zaehler danach +1 im Dict). */
    double basis = dnum(schicht, "seed", 42.0);
    double zaehler = dnum(schicht, "zaehler", 0.0);
    dset(schicht, "zaehler", moo_number(zaehler + 1.0));
    uint64_t state = ((uint64_t)(int64_t)basis + (uint64_t)(int64_t)zaehler)
                     * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    float skala = (float)(1.0 / (1.0 - rate));
    MooTensor* m = moo_tensor_raw(xt->ndim, xt->shape);
    if (!m) return moo_none();
    for (int64_t i = 0; i < m->size; i++) {
        double u = (double)(sm64(&state) >> 11) / 9007199254740992.0; /* [0,1) */
        m->data[i] = (u < rate) ? 0.0f : skala;
    }
    MooValue mv; mv.tag = MOO_TENSOR; moo_val_set_ptr(&mv, m);
    MooValue out = moo_tensor_mul(x, mv);   /* mul hat backward -> Maske wirkt im Grad */
    moo_release(mv);
    return out;
}

static MooValue fw_layernorm(MooValue schicht, MooValue x) {
    MooValue gamma = dget(schicht, "gamma");
    MooValue beta = dget(schicht, "beta");
    MooValue out = moo_none();
    if (gamma.tag == MOO_TENSOR && beta.tag == MOO_TENSOR) {
        /* letzte Achse: bei [r,c] ist das achse 1 (keepdims [r,1]) */
        MooValue achse = moo_number((double)(T(x)->ndim - 1));
        MooValue m  = moo_tensor_mittel(x, achse);
        MooValue d  = moo_tensor_sub(x, m);
        MooValue d2 = moo_tensor_mul(d, d);
        MooValue v  = moo_tensor_mittel(d2, achse);
        MooValue ve = moo_tensor_adds(v, moo_number(1e-5));
        MooValue s  = moo_tensor_sqrt(ve);
        MooValue n  = moo_tensor_div(d, s);
        MooValue g  = moo_tensor_mul(n, gamma);
        out = moo_tensor_add(g, beta);
        moo_release(m);  moo_release(d); moo_release(d2); moo_release(v);
        moo_release(ve); moo_release(s); moo_release(n);  moo_release(g);
    } else {
        moo_throw(moo_error("vorwaerts: kaputte LayerNorm-Schicht (gamma/beta fehlen)"));
    }
    moo_release(gamma); moo_release(beta);
    return out;
}

static MooValue fw_embedding(MooValue schicht, MooValue x) {
    MooValue w = dget(schicht, "w");
    if (w.tag != MOO_TENSOR) {
        moo_release(w);
        moo_throw(moo_error("vorwaerts: kaputte Embedding-Schicht (w fehlt)"));
        return moo_none();
    }
    MooTensor* xt = T(x);
    MooTensor* wt = T(w);
    bool form_ok = (xt->ndim == 1) ||
                   (xt->ndim == 2 && xt->shape[1] == 1);
    if (!form_ok) {
        moo_release(w);
        moo_throw(moo_error("vorwaerts (embedding): erwarte Indizes als Tensor "
                            "[batch] oder [batch, 1]"));
        return moo_none();
    }
    int32_t batch = xt->shape[0];
    int32_t vokab = wt->shape[0];
    /* one-hot [batch, vokab] OHNE grad; matmul (hat backward) traegt den
     * Gradienten in W — mathematisch identisch zum Gather-Scatter. */
    int32_t oh_shape[2] = { batch, vokab };
    MooTensor* oh = moo_tensor_raw(2, oh_shape);
    if (!oh) { moo_release(w); return moo_none(); }
    for (int32_t r = 0; r < batch; r++) {
        double d = (double)xt->data[r];
        if (d < 0 || d >= (double)vokab || d != (double)(int64_t)d) {
            MooValue ohv_f; ohv_f.tag = MOO_TENSOR; moo_val_set_ptr(&ohv_f, oh);
            moo_release(ohv_f);
            moo_release(w);
            char msg[128];
            snprintf(msg, sizeof(msg), "vorwaerts (embedding): Index %g liegt "
                     "nicht in [0, %d)", d, vokab);
            moo_throw(moo_error(msg));
            return moo_none();
        }
        oh->data[(int64_t)r * vokab + (int64_t)d] = 1.0f;
    }
    MooValue ohv; ohv.tag = MOO_TENSOR; moo_val_set_ptr(&ohv, oh);
    MooValue out = moo_tensor_matmul(ohv, w);
    moo_release(ohv);
    moo_release(w);
    return out;
}

/* Eine Schicht vorwaerts. x borrowed, Rueckgabe +1. */
static MooValue schicht_vorwaerts(MooValue schicht, MooValue x) {
    if (x.tag != MOO_TENSOR) {
        moo_throw(moo_error("vorwaerts: die Eingabe ist kein Tensor"));
        return moo_none();
    }
    if (nn_ist(schicht, "dicht"))     return fw_dicht(schicht, x);
    if (nn_ist(schicht, "dropout"))   return fw_dropout(schicht, x);
    if (nn_ist(schicht, "layernorm")) return fw_layernorm(schicht, x);
    if (nn_ist(schicht, "embedding")) return fw_embedding(schicht, x);
    moo_throw(moo_error("vorwaerts: das ist keine Schicht (erwarte "
                        "schicht_dicht/dropout/layernorm/embedding oder eine "
                        "Liste davon)"));
    return moo_none();
}

/* vorwaerts(netz, x): netz = Schicht, Liste von Schichten ODER ki_netz-Dict. */
MooValue moo_nn_vorwaerts(MooValue netz, MooValue x) {
    if (nn_ist(netz, "netz")) {   /* D1: Kinderleicht-Netz delegiert */
        MooValue schichten = dget(netz, "schichten");
        MooValue r = moo_nn_vorwaerts(schichten, x);
        moo_release(schichten);
        return r;
    }
    if (netz.tag == MOO_DICT) return schicht_vorwaerts(netz, x);
    if (netz.tag != MOO_LIST) {
        moo_throw(moo_error("vorwaerts: erwarte eine Schicht oder eine Liste "
                            "von Schichten"));
        return moo_none();
    }
    MooList* l = MV_LIST(netz);
    if (l->length == 0) {
        moo_throw(moo_error("vorwaerts: das Netz ist leer"));
        return moo_none();
    }
    moo_retain(x);
    MooValue cur = x;
    for (int32_t i = 0; i < l->length; i++) {
        MooValue next = schicht_vorwaerts(l->items[i], cur);
        moo_release(cur);
        if (next.tag != MOO_TENSOR) return moo_none();  /* Fehler geworfen */
        cur = next;
    }
    return cur;
}

/* ============================================================
 * Parameter-Sammlung
 * ============================================================ */

static void params_von_schicht(MooValue schicht, MooValue liste) {
    /* dget liefert +1; list_append transferiert -> genau richtig. */
    if (nn_ist(schicht, "dicht")) {
        moo_list_append(liste, dget(schicht, "w"));
        moo_list_append(liste, dget(schicht, "b"));
    } else if (nn_ist(schicht, "layernorm")) {
        moo_list_append(liste, dget(schicht, "gamma"));
        moo_list_append(liste, dget(schicht, "beta"));
    } else if (nn_ist(schicht, "embedding")) {
        moo_list_append(liste, dget(schicht, "w"));
    }
    /* dropout: keine Parameter */
}

/* gradienten_kappen(params, max_norm): globales L2-Norm-Clipping (E2).
 * Skaliert ALLE Gradienten mit max/norm wenn die Gesamt-Norm drueber
 * liegt. Rueckgabe: die Norm VOR dem Kappen (Monitoring). */
MooValue moo_nn_grad_clip(MooValue params, MooValue max_norm) {
    if (params.tag != MOO_LIST || max_norm.tag != MOO_NUMBER ||
        MV_NUM(max_norm) <= 0.0) {
        moo_throw(moo_error("gradienten_kappen: erwarte (Parameter-Liste, "
                            "maximale Norm > 0)"));
        return moo_none();
    }
    MooList* pl = MV_LIST(params);
    double q = 0.0;
    for (int32_t i = 0; i < pl->length; i++) {
        if (pl->items[i].tag != MOO_TENSOR) continue;
        MooTensor* p = MV_TENSOR(pl->items[i]);
        if (!p->grad) continue;
        for (int64_t j = 0; j < p->size; j++)
            q += (double)p->grad[j] * (double)p->grad[j];
    }
    double norm = sqrt(q);
    double max = MV_NUM(max_norm);
    if (norm > max && norm > 0.0) {
        float faktor = (float)(max / norm);
        for (int32_t i = 0; i < pl->length; i++) {
            if (pl->items[i].tag != MOO_TENSOR) continue;
            MooTensor* p = MV_TENSOR(pl->items[i]);
            if (!p->grad) continue;
            for (int64_t j = 0; j < p->size; j++) p->grad[j] *= faktor;
        }
    }
    return moo_number(norm);
}

/* parameter(netz): alle trainierbaren Tensoren als Liste (+1). */
MooValue moo_nn_parameter(MooValue netz) {
    if (nn_ist(netz, "netz")) {   /* D1: Kinderleicht-Netz delegiert */
        MooValue schichten = dget(netz, "schichten");
        MooValue r = moo_nn_parameter(schichten);
        moo_release(schichten);
        return r;
    }
    MooValue liste = moo_list_new(8);
    if (netz.tag == MOO_DICT && nn_ist_schicht(netz)) {
        params_von_schicht(netz, liste);
        return liste;
    }
    if (netz.tag == MOO_LIST) {
        MooList* l = MV_LIST(netz);
        for (int32_t i = 0; i < l->length; i++) {
            if (l->items[i].tag == MOO_DICT && nn_ist_schicht(l->items[i]))
                params_von_schicht(l->items[i], liste);
        }
        if (MV_LIST(liste)->length > 0) return liste;
    }
    moo_release(liste);
    moo_throw(moo_error("parameter: erwarte eine Schicht oder eine Liste von "
                        "Schichten (mit mindestens einem trainierbaren Parameter)"));
    return moo_none();
}

/* ============================================================
 * Loss
 * ============================================================ */

/* mse(a, b) = mittel((a-b)^2) — Skalar-Tensor [1]. */
MooValue moo_nn_mse(MooValue a, MooValue b) {
    if (a.tag != MOO_TENSOR || b.tag != MOO_TENSOR) {
        moo_throw(moo_error("mse: erwarte zwei Tensoren (Vorhersage, Ziel)"));
        return moo_none();
    }
    MooValue d = moo_tensor_sub(a, b);
    if (d.tag != MOO_TENSOR) return moo_none();
    MooValue q = moo_tensor_mul(d, d);
    moo_release(d);
    if (q.tag != MOO_TENSOR) return moo_none();
    MooValue m = moo_tensor_mittel(q, moo_number(-1.0));
    moo_release(q);
    return m;
}

/* kreuzentropie(logits, ziele):
 *   ziele gleiche Form wie logits -> one-hot/Soft-Targets direkt;
 *   ziele [batch] oder [batch,1]  -> Klassen-Indizes, intern one-hot.
 * loss = -sum(ziele * logsoftmax(logits)) / batch  — fused, max-shift-stabil. */
MooValue moo_nn_kreuzentropie(MooValue logits, MooValue ziele) {
    if (logits.tag != MOO_TENSOR || ziele.tag != MOO_TENSOR) {
        moo_throw(moo_error("kreuzentropie: erwarte zwei Tensoren (Logits, Ziele)"));
        return moo_none();
    }
    MooTensor* lt = T(logits);
    MooTensor* zt = T(ziele);
    if (lt->ndim != 2) {
        moo_throw(moo_error("kreuzentropie: Logits muessen 2D sein [batch, klassen]"));
        return moo_none();
    }
    int32_t batch = lt->shape[0], klassen = lt->shape[1];

    MooValue onehot;
    if (zt->ndim == lt->ndim && zt->shape[0] == batch && zt->shape[1] == klassen) {
        moo_retain(ziele);
        onehot = ziele;
    } else if ((zt->ndim == 1 && zt->shape[0] == batch) ||
               (zt->ndim == 2 && zt->shape[0] == batch && zt->shape[1] == 1)) {
        int32_t oh_shape[2] = { batch, klassen };
        MooTensor* oh = moo_tensor_raw(2, oh_shape);
        if (!oh) return moo_none();
        for (int32_t r = 0; r < batch; r++) {
            double d = (double)zt->data[r];
            if (d < 0 || d >= (double)klassen || d != (double)(int64_t)d) {
                MooValue ohv_f; ohv_f.tag = MOO_TENSOR; moo_val_set_ptr(&ohv_f, oh);
                moo_release(ohv_f);
                char msg[128];
                snprintf(msg, sizeof(msg), "kreuzentropie: Klassen-Index %g liegt "
                         "nicht in [0, %d)", d, klassen);
                moo_throw(moo_error(msg));
                return moo_none();
            }
            oh->data[(int64_t)r * klassen + (int64_t)d] = 1.0f;
        }
        onehot.tag = MOO_TENSOR; moo_val_set_ptr(&onehot, oh);
    } else {
        moo_throw(moo_error("kreuzentropie: Ziele muessen die Logits-Form haben "
                            "(one-hot) oder Klassen-Indizes [batch] sein"));
        return moo_none();
    }

    MooValue ls = moo_tensor_logsoftmax(logits);
    MooValue p  = (ls.tag == MOO_TENSOR) ? moo_tensor_mul(onehot, ls) : moo_none();
    MooValue s  = (p.tag == MOO_TENSOR) ? moo_tensor_summe(p, moo_number(-1.0)) : moo_none();
    MooValue loss = (s.tag == MOO_TENSOR)
        ? moo_tensor_muls(s, moo_number(-1.0 / (double)batch)) : moo_none();
    moo_release(ls); moo_release(p); moo_release(s); moo_release(onehot);
    return loss;
}

/* ============================================================
 * Optimizer (in-place, kein Tape-Record)
 * ============================================================ */

/* Gemeinsame Pruefung + Grundgeruest. params borrowed -> retained im Dict. */
static MooValue opt_basis(MooValue params, MooValue rate, const char* art,
                          bool mit_v, const char* wo) {
    if (params.tag != MOO_LIST || MV_LIST(params)->length == 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: erwarte eine nicht-leere Parameter-Liste "
                 "(von parameter(netz))", wo);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    MooList* pl = MV_LIST(params);
    for (int32_t i = 0; i < pl->length; i++) {
        if (pl->items[i].tag != MOO_TENSOR) {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: Eintrag %d ist kein Tensor", wo, i);
            moo_throw(moo_error(msg));
            return moo_none();
        }
    }
    if (rate.tag != MOO_NUMBER || MV_NUM(rate) <= 0.0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: die Lernrate muss eine Zahl > 0 sein", wo);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("opt"));
    dset(d, "art", moo_string_new(art));
    dset(d, "rate", moo_number(MV_NUM(rate)));
    dset(d, "t", moo_number(0.0));
    moo_retain(params);
    dset(d, "params", params);
    /* Zustands-Puffer (Momentum m, Adam m+v): Null-Tensoren gleicher Form,
     * OHNE requires_grad. */
    MooValue ml = moo_list_new(pl->length);
    for (int32_t i = 0; i < pl->length; i++) {
        MooTensor* p = T(pl->items[i]);
        MooTensor* z = moo_tensor_raw(p->ndim, p->shape);
        MooValue zv; zv.tag = MOO_TENSOR; moo_val_set_ptr(&zv, z);
        moo_list_append(ml, zv);
    }
    dset(d, "m", ml);
    if (mit_v) {
        MooValue vl = moo_list_new(pl->length);
        for (int32_t i = 0; i < pl->length; i++) {
            MooTensor* p = T(pl->items[i]);
            MooTensor* z = moo_tensor_raw(p->ndim, p->shape);
            MooValue zv; zv.tag = MOO_TENSOR; moo_val_set_ptr(&zv, z);
            moo_list_append(vl, zv);
        }
        dset(d, "v", vl);
    }
    return d;
}

/* optimierer_sgd(params, rate, momentum?) — momentum Standard 0. */
MooValue moo_nn_opt_sgd(MooValue params, MooValue rate, MooValue momentum) {
    double mu = 0.0;
    if (momentum.tag == MOO_NUMBER) mu = MV_NUM(momentum);
    else if (momentum.tag != MOO_NONE) {
        moo_throw(moo_error("optimierer_sgd: Momentum muss eine Zahl sein"));
        return moo_none();
    }
    if (mu < 0.0 || mu >= 1.0) {
        moo_throw(moo_error("optimierer_sgd: Momentum muss in [0, 1) liegen"));
        return moo_none();
    }
    MooValue d = opt_basis(params, rate, "sgd", false, "optimierer_sgd");
    if (d.tag != MOO_DICT) return d;
    dset(d, "momentum", moo_number(mu));
    return d;
}

/* optimierer_adam(params, rate) — beta1=0.9, beta2=0.999, eps=1e-8. */
MooValue moo_nn_opt_adam(MooValue params, MooValue rate) {
    MooValue d = opt_basis(params, rate, "adam", true, "optimierer_adam");
    if (d.tag != MOO_DICT) return d;
    dset(d, "beta1", moo_number(0.9));
    dset(d, "beta2", moo_number(0.999));
    dset(d, "eps", moo_number(1e-8));
    return d;
}

/* optimierer_adamw(params, rate, decay?) — decoupled weight decay,
 * Standard 0.01. */
MooValue moo_nn_opt_adamw(MooValue params, MooValue rate, MooValue decay) {
    double wd = 0.01;
    if (decay.tag == MOO_NUMBER) wd = MV_NUM(decay);
    else if (decay.tag != MOO_NONE) {
        moo_throw(moo_error("optimierer_adamw: Weight-Decay muss eine Zahl sein"));
        return moo_none();
    }
    if (wd < 0.0) {
        moo_throw(moo_error("optimierer_adamw: Weight-Decay muss >= 0 sein"));
        return moo_none();
    }
    MooValue d = opt_basis(params, rate, "adamw", true, "optimierer_adamw");
    if (d.tag != MOO_DICT) return d;
    dset(d, "beta1", moo_number(0.9));
    dset(d, "beta2", moo_number(0.999));
    dset(d, "eps", moo_number(1e-8));
    dset(d, "decay", moo_number(wd));
    return d;
}

/* opt.schritt(): Update ALLER Parameter aus ihren Gradienten (in-place,
 * ohne Tape-Record), danach Gradienten nullen + Tape leeren.
 * Ein Aufruf = eine komplette Trainings-Iteration nach rueckwaerts(). */
MooValue moo_nn_opt_schritt(MooValue opt) {
    if (!nn_ist(opt, "opt")) {
        moo_throw(moo_error("schritt: das ist kein Optimierer (erwarte "
                            "optimierer_sgd/adam/adamw)"));
        return moo_none();
    }
    MooValue artv = dget(opt, "art");
    const char* art = (artv.tag == MOO_STRING) ? MV_STR(artv)->chars : "";
    MooValue params = dget(opt, "params");
    MooValue ml = dget(opt, "m");
    MooValue vl = dget(opt, "v");   /* none bei sgd */
    double rate = dnum(opt, "rate", 0.01);
    double t = dnum(opt, "t", 0.0) + 1.0;
    dset(opt, "t", moo_number(t));

    if (params.tag == MOO_LIST && ml.tag == MOO_LIST) {
        MooList* pl = MV_LIST(params);
        MooList* mlist = MV_LIST(ml);
        MooList* vlist = (vl.tag == MOO_LIST) ? MV_LIST(vl) : NULL;

        if (strcmp(art, "sgd") == 0) {
            float lr = (float)rate;
            float mu = (float)dnum(opt, "momentum", 0.0);
            for (int32_t i = 0; i < pl->length; i++) {
                MooTensor* p = T(pl->items[i]);
                if (!p->grad) continue;   /* Param traegt nicht zum Loss bei */
                float* m = T(mlist->items[i])->data;
                for (int64_t j = 0; j < p->size; j++) {
                    m[j] = mu * m[j] + p->grad[j];
                    p->data[j] -= lr * m[j];
                }
            }
        } else {  /* adam / adamw */
            bool w = (strcmp(art, "adamw") == 0);
            float lr = (float)rate;
            double b1 = dnum(opt, "beta1", 0.9);
            double b2 = dnum(opt, "beta2", 0.999);
            float eps = (float)dnum(opt, "eps", 1e-8);
            float wd = w ? (float)dnum(opt, "decay", 0.01) : 0.0f;
            float bc1 = (float)(1.0 - pow(b1, t));   /* Bias-Korrektur */
            float bc2 = (float)(1.0 - pow(b2, t));
            float fb1 = (float)b1, fb2 = (float)b2;
            for (int32_t i = 0; i < pl->length; i++) {
                MooTensor* p = T(pl->items[i]);
                if (!p->grad) continue;
                float* m = T(mlist->items[i])->data;
                float* v = vlist ? T(vlist->items[i])->data : NULL;
                if (!v) continue;
                for (int64_t j = 0; j < p->size; j++) {
                    float g = p->grad[j];
                    if (w) p->data[j] -= lr * wd * p->data[j];  /* decoupled */
                    m[j] = fb1 * m[j] + (1.0f - fb1) * g;
                    v[j] = fb2 * v[j] + (1.0f - fb2) * g * g;
                    float mhat = m[j] / bc1;
                    float vhat = v[j] / bc2;
                    p->data[j] -= lr * mhat / (sqrtf(vhat) + eps);
                }
            }
        }
        /* Gradienten nullen: naechste Iteration startet sauber. */
        for (int32_t i = 0; i < pl->length; i++) {
            MooTensor* p = T(pl->items[i]);
            if (p->grad) memset(p->grad, 0, (size_t)p->size * sizeof(float));
        }
    }
    moo_release(artv); moo_release(params); moo_release(ml); moo_release(vl);
    /* Tape leeren — die aufgezeichneten Nodes dieser Iteration freigeben. */
    moo_ag_reset();
    return moo_none();
}
