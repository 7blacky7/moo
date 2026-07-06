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

/* ============================================================
 * Schicht-Typ-Registry (Typ-Zentralisierung Phase 1a)
 * ============================================================
 * EINE Zeile pro Schicht-Typ: Name + Forward + Parameter-Sammler.
 * Vorbild Op-Registry (B2): neuer Layer = 1 Registry-Zeile + fw_/params_-
 * Funktionen an EINEM Ort. Dispatch (schicht_vorwaerts), Typ-Pruefung
 * (nn_ist_schicht), Parameter-Sammlung (params_von_schicht) und die
 * Fehlermeldungs-Aufzaehlungen iterieren die Tabelle — KEINE per-Typ-
 * strcmp-Ketten mehr. moo_nn_easy.c (Validierung) nutzt moo_nn_layer_lookup.
 * speichern/laden-Hooks folgen in Phase 1b. */
typedef struct {
    const char* name;                                  /* "__nn"-Marker    */
    MooValue (*fw)(MooValue schicht, MooValue x);      /* Pflicht          */
    void (*params)(MooValue schicht, MooValue liste);  /* NULL = parameterlos */
} MooNNLayerDesc;

static const MooNNLayerDesc* nn_layer_lookup(const char* name);
static bool nn_ist_schicht(MooValue d) {
    if (d.tag != MOO_DICT) return false;
    MooValue t = dget(d, "__nn");
    bool ok = (t.tag == MOO_STRING) &&
              nn_layer_lookup(MV_STR(t)->chars) != NULL;
    moo_release(t);
    return ok;
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
 * Aktivierungs-Registry (Typ-Zentralisierung Phase 2)
 * ============================================================
 * EINE Zeile pro Aktivierung: Name + Tensor-Op + Init-Verhalten.
 * fn == NULL bedeutet Identitaet (h wird retained durchgereicht).
 * he_init steuert die Gewichts-Initialisierung in schicht_dicht
 * (He fuer relu/gelu, sonst Xavier/Glorot) — vorher eine versteckte
 * zweite strcmp-Stelle. Dispatch (akt_anwenden), Init-Entscheid und
 * die Fehlermeldungs-Aufzaehlung iterieren die Tabelle. */
typedef struct {
    const char* name;
    MooValue (*fn)(MooValue h);   /* NULL = Identitaet */
    bool he_init;                 /* He- statt Xavier-Init in schicht_dicht */
} MooNNAktDesc;

static const MooNNAktDesc nn_akt_registry[] = {
    { "relu",    moo_tensor_relu,    true  },
    { "sigmoid", moo_tensor_sigmoid, false },
    { "tanh",    moo_tensor_tanh,    false },
    { "gelu",    moo_tensor_gelu,    true  },
    { "softmax", moo_tensor_softmax, false },
    { "keine",   NULL,               false },
    { "none",    NULL,               false },   /* EN-Alias fuer "keine" */
};

static const MooNNAktDesc* nn_akt_lookup(const char* name) {
    for (size_t i = 0; i < sizeof(nn_akt_registry) / sizeof(nn_akt_registry[0]); i++)
        if (strcmp(nn_akt_registry[i].name, name) == 0)
            return &nn_akt_registry[i];
    return NULL;
}

/* Schreibt "relu, sigmoid, ..." (Registry-Reihenfolge) nach out. */
static void nn_akt_namen(char* out, size_t out_len) {
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(nn_akt_registry) / sizeof(nn_akt_registry[0]); i++) {
        int w = snprintf(out + used, out_len - used, "%s%s",
                         i ? ", " : "", nn_akt_registry[i].name);
        if (w < 0 || (size_t)w >= out_len - used) break;
        used += (size_t)w;
    }
}

/* ============================================================
 * Schicht-Konstruktoren
 * ============================================================ */

/* schicht_dicht(ein, aus, aktivierung?, seed?) — W: He-Init wenn die
 * Aktivierung he_init traegt (Registry: relu/gelu; limit = sqrt(6/ein)),
 * sonst Xavier/Glorot (limit = sqrt(6/(ein+aus))). b startet bei 0.
 * aktivierung: siehe nn_akt_registry (+ "" = keine). */
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
    const MooNNAktDesc* akt_d = nn_akt_lookup(akt);
    bool he = (akt_d != NULL) && akt_d->he_init;
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

/* schicht_attention(dim, koepfe, seed?, kv_koepfe?) — Multi-Head Self-
 * Attention mit Causal-Maske (G1). DESIGN: pro Kopf EIGENE Projektionen
 * wq_h/wk_h/wv_h [dim, dh] statt einer grossen Matrix + Spalten-Slice —
 * mathematisch aequivalent, aber komplett aus B2-geprueften Ops
 * komponierbar ("zeilen" hat kein backward). wo [dim, dim] mischt die
 * Koepfe. Bias-frei.
 * KI-M2a (GQA/MQA): kv_koepfe (Default = koepfe) teilt K/V-Projektionen —
 * es existieren nur wk{g}/wv{g} fuer g < kv_koepfe, Kopf h nutzt Gruppe
 * g = h / (koepfe/kv_koepfe). Init-Seeds bleiben s+3h/(+1)/(+2) wie MHA
 * (Luecken bei h >= kv sind ok) => kv==koepfe ist BIT-IDENTISCH zu MHA.
 * KI-M2b (Sliding): maske "causal" (Default) | "sliding" mit fenster W —
 * Sliding ist eine ZWEITE grad-lose Masken-Variante im Layer; das 5:1-
 * Interleaving (MiMo/Gemma 3) ist NETZBAU (Layer-Liste mischt causal- und
 * sliding-Layer), kein Layer-Feature. Sink-Bias bewusst nicht im Scope. */
MooValue moo_nn_schicht_attention(MooValue dim, MooValue koepfe, MooValue seed,
                                  MooValue kv_koepfe, MooValue maske,
                                  MooValue fenster) {
    int32_t nd = ganze_zahl(dim, "schicht_attention (Dimension)", 1);
    if (nd < 0) return moo_none();
    int32_t nk = ganze_zahl(koepfe, "schicht_attention (Koepfe)", 1);
    if (nk < 0) return moo_none();
    if (nk > 16 || nd % nk != 0) {
        moo_throw(moo_error("schicht_attention: die Dimension muss durch die "
                            "Koepfe teilbar sein (und hoechstens 16 Koepfe)"));
        return moo_none();
    }
    int32_t kv = nk;
    if (kv_koepfe.tag != MOO_NONE) {
        kv = ganze_zahl(kv_koepfe, "schicht_attention (KV-Koepfe)", 1);
        if (kv < 0) return moo_none();
        if (kv > nk || nk % kv != 0) {
            moo_throw(moo_error("schicht_attention: die Koepfe muessen durch "
                                "die KV-Koepfe teilbar sein (GQA: kv_koepfe "
                                "<= koepfe)"));
            return moo_none();
        }
    }
    int32_t dh = nd / nk;
    /* KI-M2b: Masken-Art + Fenster validieren. */
    const char* mart = "causal";
    if (maske.tag == MOO_STRING) mart = MV_STR(maske)->chars;
    else if (maske.tag != MOO_NONE) {
        moo_throw(moo_error("schicht_attention: Maske muss ein Text sein — "
                            "\"causal\" oder \"sliding\""));
        return moo_none();
    }
    bool sliding = (strcmp(mart, "sliding") == 0);
    if (!sliding && strcmp(mart, "causal") != 0) {
        moo_throw(moo_error("schicht_attention: Maske kann \"causal\" oder "
                            "\"sliding\" sein"));
        return moo_none();
    }
    int32_t fw = 0;
    if (sliding) {
        fw = ganze_zahl(fenster, "schicht_attention (Fenster)", 1);
        if (fw < 0) return moo_none();
    } else if (fenster.tag != MOO_NONE) {
        moo_throw(moo_error("schicht_attention: Fenster gilt nur fuer die "
                            "\"sliding\"-Maske"));
        return moo_none();
    }
    uint64_t s = (seed.tag == MOO_NUMBER) ? (uint64_t)(int64_t)MV_NUM(seed) : 42ULL;
    double limit = sqrt(6.0 / ((double)nd + (double)dh));

    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("attention"));
    dset(d, "dim", moo_number((double)nd));
    dset(d, "koepfe", moo_number((double)nk));
    dset(d, "kv_koepfe", moo_number((double)kv));
    dset(d, "maske", moo_string_new(sliding ? "sliding" : "causal"));
    if (sliding) dset(d, "fenster", moo_number((double)fw));
    char name[24];
    for (int32_t h = 0; h < nk; h++) {
        snprintf(name, sizeof(name), "wq%d", h);
        dset(d, name, gewicht_init(nd, dh, limit, s + (uint64_t)(3 * h)));
        if (h < kv) {
            snprintf(name, sizeof(name), "wk%d", h);
            dset(d, name, gewicht_init(nd, dh, limit, s + (uint64_t)(3 * h) + 1));
            snprintf(name, sizeof(name), "wv%d", h);
            dset(d, name, gewicht_init(nd, dh, limit, s + (uint64_t)(3 * h) + 2));
        }
    }
    dset(d, "wo", gewicht_init(nd, nd, sqrt(6.0 / (2.0 * (double)nd)),
                               s + (uint64_t)(3 * nk)));
    return d;
}

/* schicht_position(max_laenge, dim, art?, seed?) — Positions-Encoding.
 * art "gelernt" (Standard): Parameter [max, dim] MIT Gradient.
 * art "sinus": klassisch sin/cos, konstant (kein Parameter).
 * Training laeuft mit VOLLEN Bloecken (seq == max_laenge) — siehe fw. */
MooValue moo_nn_schicht_position(MooValue max_laenge, MooValue dim,
                                 MooValue art, MooValue seed) {
    int32_t nm = ganze_zahl(max_laenge, "schicht_position (max. Laenge)", 1);
    if (nm < 0) return moo_none();
    int32_t nd = ganze_zahl(dim, "schicht_position (Dimension)", 1);
    if (nd < 0) return moo_none();
    const char* a = "gelernt";
    if (art.tag == MOO_STRING) a = MV_STR(art)->chars;
    else if (art.tag != MOO_NONE) {
        moo_throw(moo_error("schicht_position: Art muss ein Text sein — "
                            "\"gelernt\" oder \"sinus\""));
        return moo_none();
    }
    bool sinus = (strcmp(a, "sinus") == 0 || strcmp(a, "sinusoidal") == 0);
    if (!sinus && strcmp(a, "gelernt") != 0 && strcmp(a, "learned") != 0) {
        moo_throw(moo_error("schicht_position: Art kann \"gelernt\" oder "
                            "\"sinus\" sein"));
        return moo_none();
    }
    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("position"));
    dset(d, "max", moo_number((double)nm));
    dset(d, "art", moo_string_new(sinus ? "sinus" : "gelernt"));
    if (sinus) {
        int32_t shape[2] = { nm, nd };
        MooTensor* p = moo_tensor_raw(2, shape);
        if (!p) { moo_release(d); return moo_none(); }
        for (int32_t pos = 0; pos < nm; pos++)
            for (int32_t i = 0; i < nd; i++) {
                double w = (double)pos /
                           pow(10000.0, (double)(2 * (i / 2)) / (double)nd);
                p->data[(int64_t)pos * nd + i] =
                    (float)((i % 2 == 0) ? sin(w) : cos(w));
            }
        MooValue pv; pv.tag = MOO_TENSOR; moo_val_set_ptr(&pv, p);
        dset(d, "pos", pv);   /* KONSTANTE — kein Gradient, kein Parameter */
    } else {
        uint64_t s = (seed.tag == MOO_NUMBER) ? (uint64_t)(int64_t)MV_NUM(seed)
                                              : 42ULL;
        /* kleine Uniform-Init (GPT-Praxis: kleine Werte) */
        dset(d, "pos", gewicht_init(nm, nd, 0.05, s));
    }
    return d;
}

/* schicht_moe(dim, versteckt, n_experten, k, seed?) — Mini-Mixture-of-Experts
 * (KI-M1, VERIFY-01). n_experten Zwei-Schicht-ReLU-Experten [dim->versteckt->
 * dim] + BIAS-FREIER Router [dim, n] (Gl. 5: h = x @ router, KEINE Bias-
 * Spalte, VERIFY-Korrektur 2). Auslastungs-Zaehler "auslastung_e{i}"
 * (dropout-zaehler-Muster, double im Dict) zaehlen geroutete Tokens im
 * Forward. Der Balance-Verlust (Gl. 12) landet im Forward unter "bal". */
MooValue moo_nn_schicht_moe(MooValue dim, MooValue versteckt,
                            MooValue n_experten, MooValue k, MooValue seed) {
    int32_t nd = ganze_zahl(dim, "schicht_moe (Dimension)", 1);
    if (nd < 0) return moo_none();
    int32_t nv = ganze_zahl(versteckt, "schicht_moe (versteckt)", 1);
    if (nv < 0) return moo_none();
    int32_t n = ganze_zahl(n_experten, "schicht_moe (Experten)", 2);
    if (n < 0) return moo_none();
    int32_t nk = ganze_zahl(k, "schicht_moe (k)", 1);
    if (nk < 0) return moo_none();
    if (n > 64) {
        moo_throw(moo_error("schicht_moe: hoechstens 64 Experten"));
        return moo_none();
    }
    if (nk > n) {
        moo_throw(moo_error("schicht_moe: k darf die Experten-Anzahl nicht "
                            "uebersteigen (top-k von n Experten)"));
        return moo_none();
    }
    uint64_t s = (seed.tag == MOO_NUMBER) ? (uint64_t)(int64_t)MV_NUM(seed) : 42ULL;
    double lim1 = sqrt(6.0 / (double)nd);                  /* He (relu)   */
    double lim2 = sqrt(6.0 / ((double)nv + (double)nd));   /* Xavier      */

    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("moe"));
    dset(d, "dim", moo_number((double)nd));
    dset(d, "versteckt", moo_number((double)nv));
    dset(d, "n", moo_number((double)n));
    dset(d, "k", moo_number((double)nk));
    char name[32];
    for (int32_t i = 0; i < n; i++) {
        snprintf(name, sizeof(name), "e%d_w1", i);
        dset(d, name, gewicht_init(nd, nv, lim1, s + (uint64_t)(4 * i)));
        snprintf(name, sizeof(name), "e%d_b1", i);
        dset(d, name, param_konst(nv, 0.0f));
        snprintf(name, sizeof(name), "e%d_w2", i);
        dset(d, name, gewicht_init(nv, nd, lim2, s + (uint64_t)(4 * i) + 1));
        snprintf(name, sizeof(name), "e%d_b2", i);
        dset(d, name, param_konst(nd, 0.0f));
        snprintf(name, sizeof(name), "auslastung_e%d", i);
        dset(d, name, moo_number(0.0));
    }
    dset(d, "router", gewicht_init(nd, n, sqrt(6.0 / ((double)nd + (double)n)),
                                   s + (uint64_t)(4 * n)));
    return d;
}

/* ============================================================
 * Vorwaerts
 * ============================================================ */

static MooValue akt_anwenden(const char* akt, MooValue h) {
    /* Registry-Dispatch (Phase 2). ""-Sonderfall = Identitaet wie "keine". */
    if (akt[0] == '\0') {
        moo_retain(h);
        return h;
    }
    const MooNNAktDesc* d = nn_akt_lookup(akt);
    if (d) {
        if (d->fn) return d->fn(h);
        moo_retain(h);
        return h;
    }
    char namen[128];
    nn_akt_namen(namen, sizeof(namen));
    char msg[256];
    snprintf(msg, sizeof(msg),
             "vorwaerts: unbekannte Aktivierung '%s' (moeglich: %s)", akt, namen);
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

/* Causal-Maske [seq, seq]: 0 auf/unter der Diagonale, -1e9 darueber —
 * als KONSTANTE (kein Gradient) auf die Scores addiert. */
static MooValue causal_maske(int32_t seq) {
    int32_t shape[2] = { seq, seq };
    MooTensor* m = moo_tensor_raw(2, shape);
    if (!m) return moo_none();
    for (int32_t i = 0; i < seq; i++)
        for (int32_t j = 0; j < seq; j++)
            m->data[(int64_t)i * seq + j] = (j > i) ? -1e9f : 0.0f;
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, m);
    return v;
}

/* Sliding-Window-Maske [seq, seq] (KI-M2b, MiMo-/Mistral-Muster): kausal
 * UND hoechstens W Tokens zurueck — 0 wenn i-W < j <= i, sonst -1e9.
 * Grad-lose Konstante wie causal_maske. Bei W >= seq identisch zu causal. */
static MooValue sliding_maske(int32_t seq, int32_t fenster) {
    int32_t shape[2] = { seq, seq };
    MooTensor* m = moo_tensor_raw(2, shape);
    if (!m) return moo_none();
    for (int32_t i = 0; i < seq; i++)
        for (int32_t j = 0; j < seq; j++)
            m->data[(int64_t)i * seq + j] =
                (j > i || i - j >= fenster) ? -1e9f : 0.0f;
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, m);
    return v;
}

/* Multi-Head Self-Attention, komplett aus Registry-Ops komponiert (jeder
 * Schritt landet auf dem Tape, backward laeuft ueber bw_matmul/transpose/
 * muls/add/softmax/concat). x [seq, dim] -> [seq, dim]. */
static MooValue fw_attention(MooValue schicht, MooValue x) {
    MooTensor* xt = T(x);
    if (xt->ndim != 2) {
        moo_throw(moo_error("attention: erwarte einen 2D-Tensor [Sequenz, "
                            "Dimension]"));
        return moo_none();
    }
    int32_t nd = (int32_t)dnum(schicht, "dim", 0.0);
    int32_t nk = (int32_t)dnum(schicht, "koepfe", 1.0);
    /* KI-M2a: alte Dicts ohne kv_koepfe => kv = koepfe (MHA). */
    int32_t kv = (int32_t)dnum(schicht, "kv_koepfe", (double)nk);
    if (kv < 1 || kv > nk || nk % kv != 0) {
        moo_throw(moo_error("attention: kaputte Schicht (kv_koepfe passt "
                            "nicht zu koepfe)"));
        return moo_none();
    }
    int32_t rep = nk / kv;
    if (xt->shape[1] != nd) {
        char msg[160];
        snprintf(msg, sizeof(msg), "attention: Eingabe hat Dimension %d, die "
                 "Schicht erwartet %d", xt->shape[1], nd);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int32_t seq = xt->shape[0];
    int32_t dh = nd / nk;
    /* KI-M2b: Masken-Auswahl — Dicts ohne "maske"-Key (vor M2b) sind causal. */
    MooValue mart = dget(schicht, "maske");
    bool sliding = (mart.tag == MOO_STRING &&
                    strcmp(MV_STR(mart)->chars, "sliding") == 0);
    moo_release(mart);
    MooValue maske = sliding
        ? sliding_maske(seq, (int32_t)dnum(schicht, "fenster", 1.0))
        : causal_maske(seq);
    if (maske.tag != MOO_TENSOR) return moo_none();
    MooValue skal = moo_number(1.0 / sqrt((double)dh));

    /* Koepfe rechnen und entlang Achse 1 sammeln: verbinden ist Achse 0,
     * also transpose -> concat -> am Ende zurueck-transponieren. */
    MooValue acc = moo_none();   /* [dh*h, seq] waechst pro Kopf */
    char name[24];
    bool fehler = false;
    for (int32_t h = 0; h < nk && !fehler; h++) {
        int32_t g = h / rep;   /* KV-Gruppe (GQA); rep==1 => g==h (MHA) */
        snprintf(name, sizeof(name), "wq%d", h);
        MooValue wq = dget(schicht, name);
        snprintf(name, sizeof(name), "wk%d", g);
        MooValue wk = dget(schicht, name);
        snprintf(name, sizeof(name), "wv%d", g);
        MooValue wv = dget(schicht, name);
        MooValue q = moo_tensor_matmul(x, wq);
        MooValue k = moo_tensor_matmul(x, wk);
        MooValue v = moo_tensor_matmul(x, wv);
        moo_release(wq); moo_release(wk); moo_release(wv);
        MooValue kt = moo_tensor_transponieren(k);
        MooValue s = moo_tensor_matmul(q, kt);
        MooValue ss = moo_tensor_muls(s, skal);
        MooValue sm = moo_tensor_add(ss, maske);
        MooValue a = moo_tensor_softmax(sm);
        MooValue oh = moo_tensor_matmul(a, v);
        MooValue oht = moo_tensor_transponieren(oh);   /* [dh, seq] */
        moo_release(q); moo_release(k); moo_release(v);
        moo_release(kt); moo_release(s); moo_release(ss);
        moo_release(sm); moo_release(a); moo_release(oh);
        if (oht.tag != MOO_TENSOR) { fehler = true; moo_release(oht); break; }
        if (acc.tag == MOO_NONE) {
            acc = oht;
        } else {
            MooValue neu = moo_tensor_verbinden(acc, oht);
            moo_release(acc); moo_release(oht);
            if (neu.tag != MOO_TENSOR) { fehler = true; moo_release(neu); break; }
            acc = neu;
        }
    }
    moo_release(maske);
    if (fehler || acc.tag != MOO_TENSOR) {
        moo_release(acc);
        return moo_none();
    }
    MooValue zusammen = moo_tensor_transponieren(acc);   /* [seq, dim] */
    moo_release(acc);
    MooValue wo = dget(schicht, "wo");
    MooValue out = moo_tensor_matmul(zusammen, wo);
    moo_release(zusammen); moo_release(wo);
    return out;
}

/* Positions-Encoding: voller Block (seq == max) -> Parameter-add, der
 * Gradient fliesst in "pos". Kuerzere Sequenzen nur bei Autograd-AUS
 * (Generierung): "zeilen" hat kein backward — lieber ein ehrlicher Fehler
 * als still abgeschnittene Gradienten. */
static MooValue fw_position(MooValue schicht, MooValue x) {
    MooTensor* xt = T(x);
    MooValue pos = dget(schicht, "pos");
    if (pos.tag != MOO_TENSOR || xt->ndim != 2 ||
        xt->shape[1] != T(pos)->shape[1]) {
        moo_release(pos);
        moo_throw(moo_error("position: Eingabe passt nicht zur Schicht "
                            "(erwarte [Sequenz, Dimension])"));
        return moo_none();
    }
    int32_t max = T(pos)->shape[0];
    if (xt->shape[0] == max) {
        MooValue out = moo_tensor_add(x, pos);
        moo_release(pos);
        return out;
    }
    if (xt->shape[0] > max) {
        moo_release(pos);
        moo_throw(moo_error("position: die Sequenz ist laenger als die "
                            "max. Laenge der Schicht"));
        return moo_none();
    }
    if (moo_ag_ist_an()) {
        moo_release(pos);
        moo_throw(moo_error("position: beim Training muss die Sequenz genau "
                            "max. Laenge haben (volle Bloecke) — kuerzere "
                            "Eingaben gehen nur beim Generieren (ohne_gradient)"));
        return moo_none();
    }
    /* Inferenz: konstante Teilkopie der ersten seq Zeilen */
    int32_t shape[2] = { xt->shape[0], xt->shape[1] };
    MooTensor* teil = moo_tensor_raw(2, shape);
    if (!teil) { moo_release(pos); return moo_none(); }
    memcpy(teil->data, T(pos)->data,
           (size_t)xt->shape[0] * (size_t)xt->shape[1] * sizeof(float));
    moo_release(pos);
    MooValue tv; tv.tag = MOO_TENSOR; moo_val_set_ptr(&tv, teil);
    MooValue out = moo_tensor_add(x, tv);
    moo_release(tv);
    return out;
}

/* Top-k-Maske [T,n] zur Router-Verteilung: 1 fuer die k groessten Eintraege
 * je Zeile, sonst 0 — GRAD-LOSE Konstante (causal_maske-Muster). Ties
 * deterministisch: kleinster Index gewinnt. Aktualisiert nebenbei die
 * Auslastungs-Zaehler im Schicht-Dict (dropout-zaehler-Muster). */
static MooValue moe_topk_maske(MooValue schicht, MooTensor* p, int32_t k) {
    int32_t zeilen = p->shape[0];
    int32_t n = p->shape[1];
    int32_t shape[2] = { zeilen, n };
    MooTensor* m = moo_tensor_raw(2, shape);
    if (!m) return moo_none();
    char name[32];
    for (int32_t t = 0; t < zeilen; t++) {
        const float* pz = p->data + (int64_t)t * n;
        float* mz = m->data + (int64_t)t * n;
        for (int32_t j = 0; j < n; j++) mz[j] = 0.0f;
        for (int32_t w = 0; w < k; w++) {
            int32_t best = -1;
            float bv = 0.0f;
            for (int32_t j = 0; j < n; j++) {
                if (mz[j] != 0.0f) continue;            /* schon gewaehlt */
                if (best < 0 || pz[j] > bv) { best = j; bv = pz[j]; }
            }
            if (best < 0) break;
            mz[best] = 1.0f;
            snprintf(name, sizeof(name), "auslastung_e%d", best);
            dset(schicht, name, moo_number(dnum(schicht, name, 0.0) + 1.0));
        }
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, m);
    return v;
}

/* Mini-MoE Forward (KI-M1, VERIFY-01) — reine Op-Komposition, jeder Schritt
 * auf dem Tape (B2 haelt, KEIN neuer Registry-Op). x [T,dim] -> [T,dim]:
 *   probs = softmax(x @ router)               (Gl. 5, Router bias-frei)
 *   maske = topk(probs, k)                    (grad-lose Konstante)
 *   g     = probs * maske                     (Gl. 4 — Grad fliesst ueber
 *                                              probs in den Router)
 *   out   = sum_i g[:,i]*Experte_i(x) + x     (Gl. 3 — RESIDUAL +x,
 *                                              VERIFY-Korrektur 1)
 *   bal   = n * sum_i f_i * P_i               (Gl. 12, VERIFY-Korrektur 3;
 *                                              im Dict unter "bal")
 * Die g-Spalte je Experte kommt per Broadcast-Trick aus zwei grad-losen
 * Konstanten: gcol = g @ onehot_i [n,1]; G = gcol @ einsen [1,dim].
 * HINWEIS (M1-Scope, dokumentiert): es rechnen ALLE Experten dense — die
 * Sparsity wirkt nur mathematisch ueber die Maske. */
static MooValue fw_moe(MooValue schicht, MooValue x) {
    MooTensor* xt = T(x);
    if (xt->ndim != 2) {
        moo_throw(moo_error("moe: erwarte einen 2D-Tensor [Sequenz, Dimension]"));
        return moo_none();
    }
    int32_t nd = (int32_t)dnum(schicht, "dim", 0.0);
    int32_t n = (int32_t)dnum(schicht, "n", 0.0);
    int32_t k = (int32_t)dnum(schicht, "k", 1.0);
    if (xt->shape[1] != nd || n < 1 || k < 1 || k > n) {
        char msg[160];
        snprintf(msg, sizeof(msg), "moe: Eingabe hat Dimension %d, die Schicht "
                 "erwartet %d (kaputte Schicht?)", xt->shape[1], nd);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int32_t zeilen = xt->shape[0];

    /* 1) Router bias-frei: probs = softmax(x @ router)  [T,n] */
    MooValue router = dget(schicht, "router");
    MooValue h = moo_tensor_matmul(x, router);
    moo_release(router);
    if (h.tag != MOO_TENSOR) { moo_release(h); return moo_none(); }
    MooValue probs = moo_tensor_softmax(h);
    moo_release(h);
    if (probs.tag != MOO_TENSOR) { moo_release(probs); return moo_none(); }

    /* 2) Top-k-Maske (grad-los) + 3) g = probs * maske  [T,n] */
    MooValue maske = moe_topk_maske(schicht, T(probs), k);
    if (maske.tag != MOO_TENSOR) { moo_release(maske); moo_release(probs); return moo_none(); }
    MooValue g = moo_tensor_mul(probs, maske);
    if (g.tag != MOO_TENSOR) {
        moo_release(g); moo_release(maske); moo_release(probs);
        return moo_none();
    }

    /* 4) Experten (dense) mit g-Spalte gewichten und akkumulieren. */
    MooValue acc = moo_none();
    char name[32];
    bool fehler = false;
    for (int32_t i = 0; i < n && !fehler; i++) {
        int32_t sh1[2] = { n, 1 };
        MooTensor* oh = moo_tensor_raw(2, sh1);
        if (!oh) { fehler = true; break; }
        oh->data[i] = 1.0f;
        MooValue ohv; ohv.tag = MOO_TENSOR; moo_val_set_ptr(&ohv, oh);
        int32_t sh2[2] = { 1, nd };
        MooTensor* ei = moo_tensor_raw(2, sh2);
        if (!ei) { moo_release(ohv); fehler = true; break; }
        for (int32_t j = 0; j < nd; j++) ei->data[j] = 1.0f;
        MooValue eiv; eiv.tag = MOO_TENSOR; moo_val_set_ptr(&eiv, ei);
        MooValue gcol = moo_tensor_matmul(g, ohv);              /* [T,1]   */
        MooValue G = (gcol.tag == MOO_TENSOR)
                     ? moo_tensor_matmul(gcol, eiv) : moo_none(); /* [T,dim] */
        moo_release(ohv); moo_release(eiv); moo_release(gcol);

        snprintf(name, sizeof(name), "e%d_w1", i);
        MooValue w1 = dget(schicht, name);
        snprintf(name, sizeof(name), "e%d_b1", i);
        MooValue b1 = dget(schicht, name);
        snprintf(name, sizeof(name), "e%d_w2", i);
        MooValue w2 = dget(schicht, name);
        snprintf(name, sizeof(name), "e%d_b2", i);
        MooValue b2 = dget(schicht, name);
        MooValue h1  = moo_tensor_matmul(x, w1);
        MooValue h1b = (h1.tag == MOO_TENSOR) ? moo_tensor_add(h1, b1) : moo_none();
        MooValue re  = (h1b.tag == MOO_TENSOR) ? moo_tensor_relu(h1b) : moo_none();
        MooValue h2  = (re.tag == MOO_TENSOR) ? moo_tensor_matmul(re, w2) : moo_none();
        MooValue ex  = (h2.tag == MOO_TENSOR) ? moo_tensor_add(h2, b2) : moo_none();
        moo_release(w1); moo_release(b1); moo_release(w2); moo_release(b2);
        moo_release(h1); moo_release(h1b); moo_release(re); moo_release(h2);

        MooValue wtd = (ex.tag == MOO_TENSOR && G.tag == MOO_TENSOR)
                       ? moo_tensor_mul(ex, G) : moo_none();
        moo_release(ex); moo_release(G);
        if (wtd.tag != MOO_TENSOR) { moo_release(wtd); fehler = true; break; }
        if (acc.tag == MOO_NONE) {
            acc = wtd;
        } else {
            MooValue neu = moo_tensor_add(acc, wtd);
            moo_release(acc); moo_release(wtd);
            if (neu.tag != MOO_TENSOR) { moo_release(neu); fehler = true; break; }
            acc = neu;
        }
    }
    if (fehler || acc.tag != MOO_TENSOR) {
        moo_release(acc); moo_release(g); moo_release(maske); moo_release(probs);
        return moo_none();
    }

    /* 5) RESIDUAL (Gl. 3): out = acc + x */
    MooValue out = moo_tensor_add(acc, x);
    moo_release(acc);

    /* 6) Balance-Verlust (Gl. 12): f_vec [1,n] grad-lose Token-Anteile
     *    (count_i / (T*k), Summe 1), P = mittel(probs, achse 0) MIT Grad,
     *    bal = n * summe(P * f_vec) — [1]-Tensor mit Tape-Anbindung.
     *    Landet im Dict unter "bal" (dset ersetzt den alten Wert). */
    if (out.tag == MOO_TENSOR) {
        int32_t shf[2] = { 1, n };
        MooTensor* fv = moo_tensor_raw(2, shf);
        if (fv) {
            MooTensor* mt = T(maske);
            double norm = (double)zeilen * (double)k;
            for (int32_t j = 0; j < n; j++) {
                double c = 0.0;
                for (int32_t t = 0; t < zeilen; t++)
                    c += (double)mt->data[(int64_t)t * n + j];
                fv->data[j] = (float)(c / norm);
            }
            MooValue fvv; fvv.tag = MOO_TENSOR; moo_val_set_ptr(&fvv, fv);
            MooValue P  = moo_tensor_mittel(probs, moo_number(0.0));
            MooValue pf = (P.tag == MOO_TENSOR) ? moo_tensor_mul(P, fvv)
                                                : moo_none();
            MooValue sm = (pf.tag == MOO_TENSOR)
                          ? moo_tensor_summe(pf, moo_number(-1.0)) : moo_none();
            MooValue bal = (sm.tag == MOO_TENSOR)
                           ? moo_tensor_muls(sm, moo_number((double)n))
                           : moo_none();
            moo_release(fvv); moo_release(P); moo_release(pf); moo_release(sm);
            if (bal.tag == MOO_TENSOR) dset(schicht, "bal", bal);
            else moo_release(bal);
        }
    }
    moo_release(g); moo_release(maske); moo_release(probs);
    return out;
}

/* Eine Schicht vorwaerts. x borrowed, Rueckgabe +1. Registry-Dispatch:
 * 1 dget + Lookup statt 6 nn_ist-Aufrufe (je dget+strcmp). */
static MooValue schicht_vorwaerts(MooValue schicht, MooValue x) {
    if (x.tag != MOO_TENSOR) {
        moo_throw(moo_error("vorwaerts: die Eingabe ist kein Tensor"));
        return moo_none();
    }
    if (schicht.tag == MOO_DICT) {
        MooValue t = dget(schicht, "__nn");
        const MooNNLayerDesc* d = (t.tag == MOO_STRING)
            ? nn_layer_lookup(MV_STR(t)->chars) : NULL;
        moo_release(t);
        if (d) return d->fw(schicht, x);
    }
    char msg[256];
    char namen[160];
    moo_nn_layer_namen(namen, sizeof(namen));
    snprintf(msg, sizeof(msg), "vorwaerts: das ist keine Schicht (erwarte "
             "schicht_%s oder eine Liste davon)", namen);
    moo_throw(moo_error(msg));
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

/* Parameter-Sammler pro Typ (Registry). dget liefert +1; list_append
 * transferiert -> genau richtig. dropout hat KEINE (Registry: NULL). */
static void params_dicht(MooValue schicht, MooValue liste) {
    moo_list_append(liste, dget(schicht, "w"));
    moo_list_append(liste, dget(schicht, "b"));
}
static void params_layernorm(MooValue schicht, MooValue liste) {
    moo_list_append(liste, dget(schicht, "gamma"));
    moo_list_append(liste, dget(schicht, "beta"));
}
static void params_embedding(MooValue schicht, MooValue liste) {
    moo_list_append(liste, dget(schicht, "w"));
}
static void params_attention(MooValue schicht, MooValue liste) {
    /* DETERMINISTISCHE Reihenfolge (Vertrag fuer .mook, KI-M2a): fuer
     * h in 0..koepfe immer wq{h}; wenn h < kv_koepfe zusaetzlich wk{h},
     * wv{h}; danach wo. Degeneriert bei kv==koepfe EXAKT zur alten
     * MHA-Reihenfolge wq0,wk0,wv0,...,wo. */
    int32_t nk = (int32_t)dnum(schicht, "koepfe", 1.0);
    int32_t kv = (int32_t)dnum(schicht, "kv_koepfe", (double)nk);
    char name[24];
    for (int32_t h = 0; h < nk; h++) {
        snprintf(name, sizeof(name), "wq%d", h);
        moo_list_append(liste, dget(schicht, name));
        if (h < kv) {
            snprintf(name, sizeof(name), "wk%d", h);
            moo_list_append(liste, dget(schicht, name));
            snprintf(name, sizeof(name), "wv%d", h);
            moo_list_append(liste, dget(schicht, name));
        }
    }
    moo_list_append(liste, dget(schicht, "wo"));
}
static void params_position(MooValue schicht, MooValue liste) {
    /* nur "gelernt" ist ein Parameter — sinus ist konstant und wird
     * beim Laden rekonstruiert. */
    MooValue art = dget(schicht, "art");
    bool gelernt = (art.tag == MOO_STRING &&
                    strcmp(MV_STR(art)->chars, "gelernt") == 0);
    moo_release(art);
    if (gelernt) moo_list_append(liste, dget(schicht, "pos"));
}
static void params_moe(MooValue schicht, MooValue liste) {
    /* DETERMINISTISCHE Reihenfolge (Vertrag fuer .mook): pro Experte
     * w1,b1,w2,b2 — dann router. */
    int32_t n = (int32_t)dnum(schicht, "n", 0.0);
    char name[32];
    for (int32_t i = 0; i < n; i++) {
        snprintf(name, sizeof(name), "e%d_w1", i);
        moo_list_append(liste, dget(schicht, name));
        snprintf(name, sizeof(name), "e%d_b1", i);
        moo_list_append(liste, dget(schicht, name));
        snprintf(name, sizeof(name), "e%d_w2", i);
        moo_list_append(liste, dget(schicht, name));
        snprintf(name, sizeof(name), "e%d_b2", i);
        moo_list_append(liste, dget(schicht, name));
    }
    moo_list_append(liste, dget(schicht, "router"));
}

/* === Die Registry-Tabelle. EINE Zeile pro Schicht-Typ. === */
static const MooNNLayerDesc nn_layer_registry[] = {
    { "dicht",     fw_dicht,     params_dicht     },
    { "dropout",   fw_dropout,   NULL             },
    { "layernorm", fw_layernorm, params_layernorm },
    { "embedding", fw_embedding, params_embedding },
    { "attention", fw_attention, params_attention },
    { "position",  fw_position,  params_position  },
    { "moe",       fw_moe,       params_moe       },
};

static const MooNNLayerDesc* nn_layer_lookup(const char* name) {
    for (size_t i = 0; i < sizeof(nn_layer_registry) / sizeof(nn_layer_registry[0]); i++)
        if (strcmp(nn_layer_registry[i].name, name) == 0)
            return &nn_layer_registry[i];
    return NULL;
}

/* Exportierte Lookups fuer moo_nn_easy.c (Validierung/Fehlermeldungen). */
bool moo_nn_layer_bekannt(const char* name) {
    return nn_layer_lookup(name) != NULL;
}
/* Iterator-API (Phase 1b): erlaubt easy, die Save/Load-Hook-Tabelle
 * VOLLSTAENDIG gegen die Registry zu pruefen, ohne dass Buf oder die
 * Hook-Signaturen diesen Header verlassen. Namen sind borrowed statics. */
int32_t moo_nn_layer_anzahl(void) {
    return (int32_t)(sizeof(nn_layer_registry) / sizeof(nn_layer_registry[0]));
}
const char* moo_nn_layer_name(int32_t i) {
    if (i < 0 || i >= moo_nn_layer_anzahl()) return NULL;
    return nn_layer_registry[i].name;
}
/* Schreibt "dicht/dropout/..." (Registry-Reihenfolge) nach out. */
void moo_nn_layer_namen(char* out, size_t out_len) {
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(nn_layer_registry) / sizeof(nn_layer_registry[0]); i++) {
        int w = snprintf(out + used, out_len - used, "%s%s",
                         i ? "/" : "", nn_layer_registry[i].name);
        if (w < 0 || (size_t)w >= out_len - used) break;
        used += (size_t)w;
    }
}

static void params_von_schicht(MooValue schicht, MooValue liste) {
    if (schicht.tag != MOO_DICT) return;
    MooValue t = dget(schicht, "__nn");
    const MooNNLayerDesc* d = (t.tag == MOO_STRING)
        ? nn_layer_lookup(MV_STR(t)->chars) : NULL;
    moo_release(t);
    if (d && d->params) d->params(schicht, liste);
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

/* moe_balance(netz): summiert die "bal"-Skalare ALLER moe-Schichten (+1).
 * User: loss = kreuzentropie(...) + moe_balance(netz) * alpha.
 * Wirft erklaerend ohne moe-Schicht oder ohne vorherigen vorwaerts(). */
static bool moe_bal_sammeln(MooValue schicht, MooValue* acc, bool* gefunden,
                            bool* ohne_fw) {
    if (!nn_ist(schicht, "moe")) return true;
    *gefunden = true;
    MooValue b = dget(schicht, "bal");
    if (b.tag != MOO_TENSOR) { moo_release(b); *ohne_fw = true; return false; }
    if (acc->tag == MOO_NONE) { *acc = b; return true; }
    MooValue neu = moo_tensor_add(*acc, b);
    moo_release(*acc); moo_release(b);
    *acc = moo_none();
    if (neu.tag != MOO_TENSOR) { moo_release(neu); return false; }
    *acc = neu;
    return true;
}
MooValue moo_nn_moe_balance(MooValue netz) {
    if (nn_ist(netz, "netz")) {   /* D1: Kinderleicht-Netz delegiert */
        MooValue schichten = dget(netz, "schichten");
        MooValue r = moo_nn_moe_balance(schichten);
        moo_release(schichten);
        return r;
    }
    MooValue acc = moo_none();
    bool gefunden = false, ohne_fw = false, ok = true;
    if (netz.tag == MOO_DICT) {
        ok = moe_bal_sammeln(netz, &acc, &gefunden, &ohne_fw);
    } else if (netz.tag == MOO_LIST) {
        MooList* l = MV_LIST(netz);
        for (int32_t i = 0; i < l->length && ok; i++)
            ok = moe_bal_sammeln(l->items[i], &acc, &gefunden, &ohne_fw);
    } else {
        moo_throw(moo_error("moe_balance: erwarte eine Schicht, eine Liste "
                            "von Schichten oder ein ki_netz"));
        return moo_none();
    }
    if (ok && gefunden && acc.tag == MOO_TENSOR) return acc;
    moo_release(acc);
    if (!gefunden)
        moo_throw(moo_error("moe_balance: das Netz enthaelt keine schicht_moe"));
    else if (ohne_fw)
        moo_throw(moo_error("moe_balance: erst vorwaerts() aufrufen — der "
                            "Balance-Verlust entsteht im Forward"));
    else
        moo_throw(moo_error("moe_balance: konnte den Balance-Verlust nicht "
                            "summieren"));
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
