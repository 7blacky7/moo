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

/* schicht_rmsnorm(dim) — g=1, eps=1e-5. RMS-Normalisierung der LETZTEN Achse:
 * y = x * rsqrt(mean(x^2)+eps) * g. Kein Mittelwert-Abzug, kein Bias (Gegensatz
 * zu LayerNorm) — Standard moderner LLMs. Reine Op-Komposition, kein neuer
 * Registry-Op (rsqrt via div/sqrt komponiert). */
MooValue moo_nn_schicht_rmsnorm(MooValue dim) {
    int32_t nd = ganze_zahl(dim, "schicht_rmsnorm (Dimension)", 1);
    if (nd < 0) return moo_none();
    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("rmsnorm"));
    dset(d, "g", param_konst(nd, 1.0f));
    return d;
}

/* schicht_ffn_gated(dim, versteckt, art?) — Gated-FFN:
 *   swiglu: (silu(x@W1) * (x@W3)) @ W2   mit silu(z)=z*sigmoid(z)
 *   geglu:  (gelu(x@W1) * (x@W3)) @ W2
 * Reine Op-Komposition (kein neuer Registry-Op). W1,W3: [dim,versteckt];
 * W2: [versteckt,dim]; alle Xavier/Glorot, requires_grad. Deterministische
 * Seeds aus (dim,versteckt) — die Task-Signatur traegt keinen seed-Parameter. */
MooValue moo_nn_schicht_ffn_gated(MooValue dim, MooValue versteckt, MooValue art) {
    int32_t nd = ganze_zahl(dim, "schicht_ffn_gated (Dimension)", 1);
    if (nd < 0) return moo_none();
    int32_t nh = ganze_zahl(versteckt, "schicht_ffn_gated (versteckte Dimension)", 1);
    if (nh < 0) return moo_none();
    const char* a = "swiglu";
    if (art.tag == MOO_STRING) a = MV_STR(art)->chars;
    else if (art.tag != MOO_NONE) {
        moo_throw(moo_error("schicht_ffn_gated: art muss ein Text sein "
                            "(\"swiglu\" oder \"geglu\")"));
        return moo_none();
    }
    if (strcmp(a, "swiglu") != 0 && strcmp(a, "geglu") != 0) {
        moo_throw(moo_error("schicht_ffn_gated: art muss \"swiglu\" oder "
                            "\"geglu\" sein"));
        return moo_none();
    }
    double lim_in  = sqrt(6.0 / ((double)nd + (double)nh));
    double lim_out = sqrt(6.0 / ((double)nh + (double)nd));
    uint64_t base = ((uint64_t)nd * 73856093ULL) ^ ((uint64_t)nh * 19349663ULL)
                    ^ 0x9E3779B97F4A7C15ULL;
    MooValue d = moo_dict_new();
    dset(d, "__nn", moo_string_new("ffn_gated"));
    dset(d, "art", moo_string_new(a));
    dset(d, "w1", gewicht_init(nd, nh, lim_in,  base + 1));   /* Gate  x@W1 */
    dset(d, "w2", gewicht_init(nh, nd, lim_out, base + 2));   /* Down  g@W2 */
    dset(d, "w3", gewicht_init(nd, nh, lim_in,  base + 3));   /* Up    x@W3 */
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
                                  MooValue fenster, MooValue rope) {
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
    /* KIP-B2: RoPE-Konfiguration (7. Param, X2 §2). NONE = aus (Default,
     * Abwaertskompat: bestehende Layer/Gates bit-identisch). bool true /
     * Zahl>0 = an (basis = Zahl bzw. 10000 bei true/1.0); String "rope"/"an".
     * Cache speichert ROTIERTE K -> basis ist Teil des Cache-Zustands und
     * wird beim Laden (rb_attention) rekonstruiert / validiert. */
    bool rope_an = false;
    double rope_basis = 10000.0;
    int32_t rope_skal = 0;      /* KIP-B2b: 0=keine, 1=linear/PI, 2=ntk-aware */
    double rope_faktor = 1.0;
    if (rope.tag == MOO_BOOL) {
        rope_an = MV_BOOL(rope);
    } else if (rope.tag == MOO_NUMBER) {
        double rb = MV_NUM(rope);
        if (rb < 0.0) {
            moo_throw(moo_error("schicht_attention: die RoPE-Basis darf nicht "
                                "negativ sein"));
            return moo_none();
        }
        rope_an = (rb > 0.0);
        if (rope_an && rb != 1.0) rope_basis = rb;   /* 1.0 = an, Default-Basis */
    } else if (rope.tag == MOO_STRING) {
        const char* rs = MV_STR(rope)->chars;
        if (strcmp(rs, "rope") == 0 || strcmp(rs, "an") == 0) rope_an = true;
        else if (strcmp(rs, "aus") == 0 || strcmp(rs, "keine") == 0) rope_an = false;
        else {
            moo_throw(moo_error("schicht_attention: rope kann \"rope\"/\"an\" "
                                "oder \"aus\" sein"));
            return moo_none();
        }
    } else if (rope.tag == MOO_DICT) {
        /* KIP-B2b: {basis, skalierung, faktor}. Dict => rope AN. */
        rope_an = true;
        MooValue vb = dget(rope, "basis");
        if (vb.tag == MOO_NUMBER) {
            double rb = MV_NUM(vb);
            if (rb <= 0.0) {
                moo_release(vb);
                moo_throw(moo_error("schicht_attention: die RoPE-Basis muss "
                                    "positiv sein"));
                return moo_none();
            }
            rope_basis = rb;
        }
        moo_release(vb);
        MooValue vf = dget(rope, "faktor");
        if (vf.tag == MOO_NUMBER) rope_faktor = MV_NUM(vf);
        moo_release(vf);
        MooValue vs = dget(rope, "skalierung");
        if (vs.tag == MOO_STRING) {
            const char* ss = MV_STR(vs)->chars;
            if (strcmp(ss, "keine") == 0 || strcmp(ss, "aus") == 0 ||
                strcmp(ss, "none") == 0) rope_skal = 0;
            else if (strcmp(ss, "linear") == 0 || strcmp(ss, "pi") == 0) rope_skal = 1;
            else if (strcmp(ss, "ntk") == 0 || strcmp(ss, "ntk-aware") == 0) rope_skal = 2;
            else {
                moo_release(vs);
                moo_throw(moo_error("schicht_attention: rope.skalierung kann "
                                    "\"keine\", \"linear\" oder \"ntk\" sein"));
                return moo_none();
            }
        } else if (vs.tag == MOO_NUMBER) {
            int32_t sc = (int32_t)MV_NUM(vs);
            if (sc < 0 || sc > 2) {
                moo_release(vs);
                moo_throw(moo_error("schicht_attention: rope.skalierung als Zahl "
                                    "muss 0, 1 oder 2 sein"));
                return moo_none();
            }
            rope_skal = sc;
        }
        moo_release(vs);
    } else if (rope.tag != MOO_NONE) {
        moo_throw(moo_error("schicht_attention: rope muss ein Wahrheitswert, "
                            "eine Zahl (Basis), ein Text oder ein Woerterbuch "
                            "(Skalierung) sein"));
        return moo_none();
    }
    if (rope_an && (dh % 2 != 0)) {
        moo_throw(moo_error("schicht_attention: RoPE braucht eine GERADE "
                            "Kopf-Dimension (dim/koepfe) — Paar-Rotation"));
        return moo_none();
    }
    /* KIP-B2b: Skalierung validieren (nur bei aktivem RoPE relevant). */
    if (rope_an && rope_skal != 0) {
        if (rope_faktor < 1.0) {
            moo_throw(moo_error("schicht_attention: der RoPE-Skalierungsfaktor "
                                "muss >= 1 sein"));
            return moo_none();
        }
        if (rope_skal == 2 && dh <= 2) {
            moo_throw(moo_error("schicht_attention: die NTK-RoPE-Skalierung "
                                "braucht eine Kopf-Dimension >= 4"));
            return moo_none();
        }
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
    dset(d, "rope", moo_number(rope_an ? 1.0 : 0.0));
    if (rope_an) {
        dset(d, "rope_basis", moo_number(rope_basis));
        /* KIP-B2b: Skalierungs-Felder NUR wenn aktiv => alte/unskalierte Layer
         * bit-identisch (keine neuen Keys). */
        if (rope_skal != 0) {
            dset(d, "rope_skalierung", moo_number((double)rope_skal));
            dset(d, "rope_faktor", moo_number(rope_faktor));
        }
    }
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

static MooValue fw_rmsnorm(MooValue schicht, MooValue x) {
    MooValue g = dget(schicht, "g");
    MooValue out = moo_none();
    if (g.tag == MOO_TENSOR) {
        /* letzte Achse (keepdims), analog LayerNorm-Muster */
        MooValue achse = moo_number((double)(T(x)->ndim - 1));
        MooValue x2  = moo_tensor_mul(x, x);              /* x^2 */
        MooValue ms  = moo_tensor_mittel(x2, achse);      /* mean(x^2) [.,1] */
        MooValue mse = moo_tensor_adds(ms, moo_number(1e-5));
        MooValue s   = moo_tensor_sqrt(mse);              /* sqrt(mean(x^2)+eps) */
        MooValue n   = moo_tensor_div(x, s);              /* x * rsqrt(...) (broadcast) */
        out = moo_tensor_mul(n, g);                       /* * g (broadcast [1,dim]) */
        moo_release(x2); moo_release(ms); moo_release(mse);
        moo_release(s);  moo_release(n);
    } else {
        moo_throw(moo_error("vorwaerts: kaputte RMSNorm-Schicht (g fehlt)"));
    }
    moo_release(g);
    return out;
}

static MooValue fw_ffn_gated(MooValue schicht, MooValue x) {
    MooValue w1 = dget(schicht, "w1");
    MooValue w2 = dget(schicht, "w2");
    MooValue w3 = dget(schicht, "w3");
    MooValue av = dget(schicht, "art");
    MooValue out = moo_none();
    if (w1.tag == MOO_TENSOR && w2.tag == MOO_TENSOR && w3.tag == MOO_TENSOR) {
        const char* a = (av.tag == MOO_STRING) ? MV_STR(av)->chars : "swiglu";
        MooValue h1 = moo_tensor_matmul(x, w1);          /* [n, versteckt] */
        MooValue gate;
        if (strcmp(a, "geglu") == 0) {
            gate = moo_tensor_gelu(h1);
        } else {                                         /* swiglu: silu=z*sigmoid(z) */
            MooValue sg = moo_tensor_sigmoid(h1);
            gate = moo_tensor_mul(h1, sg);
            moo_release(sg);
        }
        MooValue h3 = moo_tensor_matmul(x, w3);          /* [n, versteckt] */
        MooValue g  = moo_tensor_mul(gate, h3);          /* elementweise */
        out = moo_tensor_matmul(g, w2);                  /* [n, dim] */
        moo_release(h1); moo_release(gate); moo_release(h3); moo_release(g);
    } else {
        moo_throw(moo_error("vorwaerts: kaputte FFN-Gated-Schicht (w1/w2/w3 fehlen)"));
    }
    moo_release(w1); moo_release(w2); moo_release(w3); moo_release(av);
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
    /* KIP-T1: Produktionspfad = gather(W, indizes). Backward = scatter-add
     * nach W, mathematisch identisch zu one-hot@W, aber ohne [batch, vokab]-
     * Materialisierung. one-hot bleibt Referenzpfad unter
     * MOO_KI_EINBETTUNG_ONEHOT=1 (Differential-/LM-Kurven-Gate). */
    {
        const char* onehot_env = getenv("MOO_KI_EINBETTUNG_ONEHOT");
        if (!(onehot_env && onehot_env[0] == '1')) {
            MooValue out = moo_tensor_gather(w, x);   /* Range-Check in gather */
            moo_release(w);
            return out;
        }
    }
    int32_t batch = xt->shape[0];
    int32_t vokab = wt->shape[0];
    /* --- Referenzpfad: one-hot [batch, vokab] OHNE grad; matmul (hat
     * backward) traegt den Gradienten in W — identisch zum Gather-Scatter. */
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

/* Kausale (+optional Sliding-) Maske fuer den Cache-Pfad [t_neu, t_ges]
 * (KI-M2c): Zeile r steht fuer Gesamt-Position t_alt + r und darf
 * j <= t_alt + r sehen (Sliding: zusaetzlich hoechstens W zurueck).
 * Grad-lose Konstante; bei t_alt == 0 identisch zur normalen Maske. */
static MooValue cache_maske(int32_t t_neu, int32_t t_ges, bool sliding,
                            int32_t fenster) {
    int32_t t_alt = t_ges - t_neu;
    int32_t shape[2] = { t_neu, t_ges };
    MooTensor* m = moo_tensor_raw(2, shape);
    if (!m) return moo_none();
    for (int32_t r = 0; r < t_neu; r++) {
        int32_t pos = t_alt + r;
        for (int32_t j = 0; j < t_ges; j++)
            m->data[(int64_t)r * t_ges + j] =
                (j > pos || (sliding && pos - j >= fenster)) ? -1e9f : 0.0f;
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, m);
    return v;
}

/* ============================================================
 * KIP-B2: RoPE (Rotary Position Embedding) — reine Op-Komposition
 * ============================================================
 * Standard-RoPE (interleaved): rope(t, p0) = t (*) cos + (t @ R) (*) sin,
 * mit t [rows, dh], cos/sin [rows, dh] und R [dh, dh] als GRAD-LOSE
 * Konstanten (wie causal_maske). R[2i,2i+1]=+1, R[2i+1,2i]=-1 =>
 * (t@R)[.,2i]=-t[.,2i+1], (t@R)[.,2i+1]=t[.,2i]. Damit:
 *   roped[.,2i]   = t[.,2i]*cos   - t[.,2i+1]*sin
 *   roped[.,2i+1] = t[.,2i+1]*cos + t[.,2i]*sin
 * cos/sin haengen NUR von der absoluten Position p0+r und dem Paar-Index
 * ab (row-unabhaengig) => chunked Prefill == Voll, Cache-Decode == Nicht-
 * Cache bit-kompatibel. mul/matmul/add tragen backward => Grad fliesst in
 * wq/wk. KEIN neuer Registry-Op (B2-Vertrag: reine Komposition). */
static MooValue rope_matrix(int32_t dh) {
    int32_t shape[2] = { dh, dh };
    MooTensor* R = moo_tensor_raw(2, shape);
    if (!R) return moo_none();
    for (int32_t i = 0; i + 1 < dh; i += 2) {
        R->data[(int64_t)(i) * dh + (i + 1)] = 1.0f;    /* R[2p,2p+1] = +1 */
        R->data[(int64_t)(i + 1) * dh + (i)] = -1.0f;   /* R[2p+1,2p] = -1 */
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, R);
    return v;
}

/* cos/sin [rows, dh] fuer absolute Positionen p0..p0+rows-1 (grad-los).
 * angle(p,i) = p * basis^(-(2i)/dh); beide Paar-Komponenten teilen cos/sin. */
/* pos != NULL: absolute Position von Zeile r = pos[r] (Per-Doc-Reset beim
 * Packing, KIP-B4a); pos == NULL: kontiguierlich p0+r (Normal/Cache-Pfad). */
static bool rope_cossin(int32_t rows, int32_t dh, int32_t p0, const float* pos,
                        double basis, int32_t skal, double faktor,
                        MooValue* cos_out, MooValue* sin_out) {
    int32_t shape[2] = { rows, dh };
    MooTensor* c = moo_tensor_raw(2, shape);
    MooTensor* s = moo_tensor_raw(2, shape);
    if (!c || !s) {
        if (c) { MooValue cv; cv.tag = MOO_TENSOR; moo_val_set_ptr(&cv, c); moo_release(cv); }
        if (s) { MooValue sv; sv.tag = MOO_TENSOR; moo_val_set_ptr(&sv, s); moo_release(sv); }
        return false;
    }
    /* KIP-B2b: Kontext-Skalierung. NTK-aware (skal==2) skaliert die Basis
     * (theta' = basis * faktor^(dh/(dh-2)), bloc97) — Positionen unveraendert;
     * Linear/PI (skal==1) staucht die Position (p' = p/faktor, Chen 2023).
     * skal==0: unveraendert (bit-identisch zu Standard-RoPE, KIP-B2). */
    double basis_eff = basis;
    if (skal == 2 && dh > 2 && faktor > 0.0)
        basis_eff = basis * pow(faktor, (double)dh / ((double)dh - 2.0));
    for (int32_t r = 0; r < rows; r++) {
        double praw = pos ? (double)pos[r] : (double)(p0 + r);
        double p = (skal == 1 && faktor > 0.0) ? praw / faktor : praw;
        for (int32_t i = 0; i < dh / 2; i++) {
            double freq = pow(basis_eff, -((double)(2 * i)) / (double)dh);
            double ang = p * freq;
            float cf = (float)cos(ang);
            float sf = (float)sin(ang);
            c->data[(int64_t)r * dh + (2 * i)]     = cf;
            c->data[(int64_t)r * dh + (2 * i + 1)] = cf;
            s->data[(int64_t)r * dh + (2 * i)]     = sf;
            s->data[(int64_t)r * dh + (2 * i + 1)] = sf;
        }
    }
    cos_out->tag = MOO_TENSOR; moo_val_set_ptr(cos_out, c);
    sin_out->tag = MOO_TENSOR; moo_val_set_ptr(sin_out, s);
    return true;
}

/* rope(t) = t (*) cos + (t @ R) (*) sin. Gibt +1-Tensor (oder none). */
static MooValue rope_anwenden(MooValue t, MooValue cosv, MooValue sinv,
                              MooValue R) {
    MooValue tc = moo_tensor_mul(t, cosv);
    MooValue tR = moo_tensor_matmul(t, R);
    MooValue ts = (tR.tag == MOO_TENSOR) ? moo_tensor_mul(tR, sinv) : moo_none();
    MooValue out = (tc.tag == MOO_TENSOR && ts.tag == MOO_TENSOR)
                   ? moo_tensor_add(tc, ts) : moo_none();
    moo_release(tc); moo_release(tR); moo_release(ts);
    return out;
}

/* ============================================================
 * KIP-B2: KV-Cache-Zugriff ueber EINEN Indirektionspunkt (X2 §2.4)
 * ============================================================
 * Alle Lese-/Schreib-/Laengen-Zugriffe auf cache_k{g}/cache_v{g} laufen
 * hier durch => Paged-KV ist spaeter ein lokaler Umbau. */
static void cache_kv_lesen(MooValue schicht, int32_t g, MooValue* k, MooValue* v) {
    char name[24];
    snprintf(name, sizeof(name), "cache_k%d", g);
    *k = dget(schicht, name);            /* +1 oder none */
    snprintf(name, sizeof(name), "cache_v%d", g);
    *v = dget(schicht, name);
}
static void cache_kv_schreiben(MooValue schicht, int32_t g, MooValue k, MooValue v) {
    char name[24];
    snprintf(name, sizeof(name), "cache_k%d", g);
    dset(schicht, name, k);              /* Transfer ins Dict */
    snprintf(name, sizeof(name), "cache_v%d", g);
    dset(schicht, name, v);
}
/* t_alt = Cache-Laenge VOR dem Append = EINZIGE Positionsquelle (X2 §2.2),
 * identisch zum t_alt der cache_maske (t_ges - t_neu). 0 = frischer Cache. */
static int32_t cache_laenge(MooValue schicht) {
    MooValue k0 = dget(schicht, "cache_k0");
    int32_t n = (k0.tag == MOO_TENSOR) ? T(k0)->shape[0] : 0;
    moo_release(k0);
    return n;
}

/* KI-M2c: Attention-Forward MIT KV-Cache — NUR Inferenz (der Dispatcher in
 * fw_attention prueft autograd). x = NEUER Token-Block [t_neu, dim]; K/V
 * des Blocks werden pro KV-Gruppe an cache_k{g}/cache_v{g} angehaengt
 * (verbinden, Achse 0), Q laeuft nur ueber die neuen Zeilen gegen den
 * Gesamt-Cache. Der Voll-Pfad (fw_attention) bleibt unveraendert.
 * BIT-IDENTITAETS-ARGUMENT: matmul/softmax arbeiten zeilenweise unabhaengig,
 * die K/V-Zeilen entstehen aus denselben Eingabezeilen mit denselben Ops wie
 * im Voll-Forward — das Gate beweist tokenweise == Voll-Forward bit-identisch
 * (Stopp-Regel: wenn nicht erreichbar, M2c stoppen + dokumentieren). */
static MooValue fw_attention_cache(MooValue schicht, MooValue x, int32_t nd,
                                   int32_t nk, int32_t kv, int32_t rep,
                                   bool sliding, int32_t fenster) {
    MooTensor* xt = T(x);
    int32_t t_neu = xt->shape[0];
    int32_t dh = nd / nk;
    char name[24];
    MooValue kg[16], vg[16];   /* kv <= koepfe <= 16 (Konstruktor-Invariante) */
    for (int32_t g = 0; g < kv; g++) { kg[g] = moo_none(); vg[g] = moo_none(); }
    bool fehler = false;
    /* KIP-B2: RoPE — t_alt = Cache-Laenge VOR dem Append (EINZIGE Positions-
     * quelle, X2 §2.2). cos/sin/R einmal fuer die neuen Zeilen (Positionen
     * t_alt .. t_alt+t_neu-1). */
    bool rope_an = (dnum(schicht, "rope", 0.0) == 1.0);
    double rope_basis = dnum(schicht, "rope_basis", 10000.0);
    int32_t rope_skal = (int32_t)dnum(schicht, "rope_skalierung", 0.0);
    double rope_faktor = dnum(schicht, "rope_faktor", 1.0);
    int32_t t_alt = cache_laenge(schicht);
    MooValue rcos = moo_none(), rsin = moo_none(), rmat = moo_none();
    if (rope_an) {
        if (!rope_cossin(t_neu, dh, t_alt, NULL, rope_basis, rope_skal, rope_faktor, &rcos, &rsin))
            return moo_none();
        rmat = rope_matrix(dh);
        if (rmat.tag != MOO_TENSOR) {
            moo_release(rcos); moo_release(rsin);
            return moo_none();
        }
    }
    for (int32_t g = 0; g < kv && !fehler; g++) {
        snprintf(name, sizeof(name), "wk%d", g);
        MooValue wk = dget(schicht, name);
        snprintf(name, sizeof(name), "wv%d", g);
        MooValue wv = dget(schicht, name);
        MooValue kn = moo_tensor_matmul(x, wk);
        MooValue vn = moo_tensor_matmul(x, wv);
        moo_release(wk); moo_release(wv);
        if (kn.tag != MOO_TENSOR || vn.tag != MOO_TENSOR) {
            moo_release(kn); moo_release(vn);
            fehler = true;
            break;
        }
        /* KIP-B2: neues K bei Positionen t_alt.. rotieren -> der Cache speichert
         * ROTIERTE K (X2 §2.1). V bleibt unrotiert. */
        if (rope_an) {
            MooValue kr = rope_anwenden(kn, rcos, rsin, rmat);
            moo_release(kn);
            if (kr.tag != MOO_TENSOR) { moo_release(vn); fehler = true; break; }
            kn = kr;
        }
        MooValue ka, va;
        cache_kv_lesen(schicht, g, &ka, &va);   /* EIN Indirektionspunkt */
        if (ka.tag == MOO_TENSOR) {
            kg[g] = moo_tensor_verbinden(ka, kn);
            moo_release(ka); moo_release(kn);
        } else {
            moo_release(ka);
            kg[g] = kn;
        }
        if (va.tag == MOO_TENSOR) {
            vg[g] = moo_tensor_verbinden(va, vn);
            moo_release(va); moo_release(vn);
        } else {
            moo_release(va);
            vg[g] = vn;
        }
        if (kg[g].tag != MOO_TENSOR || vg[g].tag != MOO_TENSOR) fehler = true;
    }
    if (fehler) {
        for (int32_t g = 0; g < kv; g++) { moo_release(kg[g]); moo_release(vg[g]); }
        moo_release(rcos); moo_release(rsin); moo_release(rmat);
        return moo_none();
    }
    int32_t t_ges = T(kg[0])->shape[0];
    MooValue maske = cache_maske(t_neu, t_ges, sliding, fenster);
    if (maske.tag != MOO_TENSOR) {
        for (int32_t g = 0; g < kv; g++) { moo_release(kg[g]); moo_release(vg[g]); }
        moo_release(rcos); moo_release(rsin); moo_release(rmat);
        return moo_none();
    }
    MooValue skal = moo_number(1.0 / sqrt((double)dh));
    MooValue acc = moo_none();
    for (int32_t h = 0; h < nk && !fehler; h++) {
        int32_t g = h / rep;
        snprintf(name, sizeof(name), "wq%d", h);
        MooValue wq = dget(schicht, name);
        MooValue q = moo_tensor_matmul(x, wq);
        moo_release(wq);
        if (rope_an && q.tag == MOO_TENSOR) {   /* KIP-B2: Q bei t_alt.. rotieren */
            MooValue qr = rope_anwenden(q, rcos, rsin, rmat);
            moo_release(q);
            q = qr;
        }
        MooValue kt = moo_tensor_transponieren(kg[g]);
        MooValue s = (q.tag == MOO_TENSOR && kt.tag == MOO_TENSOR)
                     ? moo_tensor_matmul(q, kt) : moo_none();
        MooValue ss = (s.tag == MOO_TENSOR) ? moo_tensor_muls(s, skal) : moo_none();
        MooValue sm = (ss.tag == MOO_TENSOR) ? moo_tensor_add(ss, maske) : moo_none();
        MooValue a = (sm.tag == MOO_TENSOR) ? moo_tensor_softmax(sm) : moo_none();
        MooValue oh = (a.tag == MOO_TENSOR) ? moo_tensor_matmul(a, vg[g]) : moo_none();
        MooValue oht = (oh.tag == MOO_TENSOR) ? moo_tensor_transponieren(oh)
                                              : moo_none();
        moo_release(q); moo_release(kt); moo_release(s); moo_release(ss);
        moo_release(sm); moo_release(a); moo_release(oh);
        if (oht.tag != MOO_TENSOR) { moo_release(oht); fehler = true; break; }
        if (acc.tag == MOO_NONE) {
            acc = oht;
        } else {
            MooValue neu = moo_tensor_verbinden(acc, oht);
            moo_release(acc); moo_release(oht);
            if (neu.tag != MOO_TENSOR) { moo_release(neu); fehler = true; break; }
            acc = neu;
        }
    }
    moo_release(maske);
    moo_release(rcos); moo_release(rsin); moo_release(rmat);
    /* Cache aktualisieren: rotierte Gesamt-K + V per Transfer ins Dict ueber
     * EINEN Indirektionspunkt (X2 §2.4) — auch im Fehlerfall konsistent, kein
     * Doppel-Release. */
    for (int32_t g = 0; g < kv; g++)
        cache_kv_schreiben(schicht, g, kg[g], vg[g]);
    if (fehler || acc.tag != MOO_TENSOR) {
        moo_release(acc);
        return moo_none();
    }
    MooValue zusammen = moo_tensor_transponieren(acc);
    moo_release(acc);
    MooValue wo = dget(schicht, "wo");
    MooValue out = moo_tensor_matmul(zusammen, wo);
    moo_release(zusammen); moo_release(wo);
    return out;
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
    /* KI-M2c: KV-Cache — expliziter Inferenzzustand (att["cache"] = 1).
     * NUR bei autograd_aus; das Flag wird von cache_leeren NICHT angefasst. */
    if (dnum(schicht, "cache", 0.0) == 1.0) {
        if (moo_ag_ist_an()) {
            moo_throw(moo_error("attention: der KV-Cache geht nur beim "
                                "Generieren (autograd_aus) — beim Training "
                                "att[\"cache\"] = 0 setzen"));
            return moo_none();
        }
        return fw_attention_cache(schicht, x, nd, nk, kv, rep, sliding,
                                  (int32_t)dnum(schicht, "fenster", 1.0));
    }
    /* KIP-B4a (X3 §2): externe Maske (Packing = block-diagonal) ueberschreibt
     * die interne causal/sliding-Maske. Muss [seq, seq] passen. */
    MooValue pmaske = dget(schicht, "pack_maske");
    MooValue maske;
    if (pmaske.tag == MOO_TENSOR) {
        if (T(pmaske)->ndim != 2 || T(pmaske)->shape[0] != seq ||
            T(pmaske)->shape[1] != seq) {
            moo_release(pmaske);
            moo_throw(moo_error("attention: pack_maske hat nicht die Form "
                                "[seq, seq] der Eingabe"));
            return moo_none();
        }
        maske = pmaske;   /* +1 owning; unten released */
    } else {
        moo_release(pmaske);
        maske = sliding
            ? sliding_maske(seq, (int32_t)dnum(schicht, "fenster", 1.0))
            : causal_maske(seq);
    }
    if (maske.tag != MOO_TENSOR) return moo_none();
    MooValue skal = moo_number(1.0 / sqrt((double)dh));
    /* KIP-B2: RoPE fuer den Voll-Pfad — Positionen 0..seq-1 (p0=0). Selbe
     * cos/sin/R fuer alle Koepfe/Gruppen (haengen nur von Position+dh ab). */
    bool rope_an = (dnum(schicht, "rope", 0.0) == 1.0);
    MooValue rcos = moo_none(), rsin = moo_none(), rmat = moo_none();
    if (rope_an) {
        double rope_basis = dnum(schicht, "rope_basis", 10000.0);
        int32_t rope_skal = (int32_t)dnum(schicht, "rope_skalierung", 0.0);
        double rope_faktor = dnum(schicht, "rope_faktor", 1.0);
        /* KIP-B4a: externe Positionen (Per-Doc-Reset beim Packing) via pack_pos
         * [seq] — noetig fuer BIT-Identitaet gepackt==ungepackt (Rotation bei
         * absoluter vs doc-lokaler Position rundet in float unterschiedlich). */
        MooValue ppos = dget(schicht, "pack_pos");
        const float* posdat = NULL;
        if (ppos.tag == MOO_TENSOR) {
            if (T(ppos)->size != (int64_t)seq) {
                moo_release(ppos); moo_release(maske);
                moo_throw(moo_error("attention: pack_pos hat nicht die Laenge "
                                    "der Sequenz"));
                return moo_none();
            }
            moo_tensor_f32_sichern(T(ppos));
            posdat = T(ppos)->data;
        }
        bool ok = rope_cossin(seq, dh, 0, posdat, rope_basis, rope_skal, rope_faktor, &rcos, &rsin);
        moo_release(ppos);   /* posdat wurde in cos/sin kopiert */
        if (!ok) { moo_release(maske); return moo_none(); }
        rmat = rope_matrix(dh);
        if (rmat.tag != MOO_TENSOR) {
            moo_release(maske); moo_release(rcos); moo_release(rsin);
            return moo_none();
        }
    }

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
        if (rope_an) {   /* KIP-B2: Q + K bei Positionen 0..seq-1 rotieren */
            MooValue qr = (q.tag == MOO_TENSOR)
                          ? rope_anwenden(q, rcos, rsin, rmat) : moo_none();
            MooValue kr = (k.tag == MOO_TENSOR)
                          ? rope_anwenden(k, rcos, rsin, rmat) : moo_none();
            moo_release(q); moo_release(k);
            q = qr; k = kr;
        }
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
    moo_release(rcos); moo_release(rsin); moo_release(rmat);
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
    if (x.tag == MOO_TENSOR) moo_tensor_f32_sichern(MV_TENSOR(x));   /* KIP-D1 Eintrittspunkt: bf16-Input -> f32 */
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
    /* KIP-B2: Doppel-Positions-Guard (X2 §2) — eine additive position-Schicht
     * UND eine attention-Schicht mit RoPE im selben Netz waere doppelte
     * Positionscodierung. Einmaliger Scan vor dem Forward. */
    {
        bool hat_pos = false, hat_rope = false;
        for (int32_t i = 0; i < l->length; i++) {
            MooValue si = l->items[i];
            if (nn_ist(si, "position")) hat_pos = true;
            else if (nn_ist(si, "attention") && dnum(si, "rope", 0.0) == 1.0)
                hat_rope = true;
        }
        if (hat_pos && hat_rope) {
            moo_throw(moo_error("vorwaerts: additive Positions-Schicht UND "
                                "Attention mit RoPE zusammen ergeben doppelte "
                                "Positionscodierung — nutze nur eins von beidem"));
            return moo_none();
        }
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
static void params_rmsnorm(MooValue schicht, MooValue liste) {
    moo_list_append(liste, dget(schicht, "g"));
}
static void params_ffn_gated(MooValue schicht, MooValue liste) {
    moo_list_append(liste, dget(schicht, "w1"));
    moo_list_append(liste, dget(schicht, "w2"));
    moo_list_append(liste, dget(schicht, "w3"));
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
    { "rmsnorm",   fw_rmsnorm,   params_rmsnorm   },
    { "ffn_gated", fw_ffn_gated, params_ffn_gated },
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

/* cache_leeren(netz): entfernt die KV-Cache-Tensoren ALLER attention-
 * Schichten (KI-M2c). Reiner Zustands-Reset — das "cache"-Flag am Layer
 * bleibt UNVERAENDERT (Aktivierung/Deaktivierung nur via att["cache"]).
 * Idempotent; wirft nur bei Nicht-Netz. Rueckgabe none. */
static void cache_leeren_schicht(MooValue schicht) {
    if (!nn_ist(schicht, "attention")) return;
    int32_t nk = (int32_t)dnum(schicht, "koepfe", 1.0);
    int32_t kvn = (int32_t)dnum(schicht, "kv_koepfe", (double)nk);
    char name[24];
    for (int32_t g = 0; g < kvn; g++) {
        /* moo_dict_remove KONSUMIERT den frischen Key (Transfer-Konvention
         * wie dget/dset) — KEIN eigenes Release, sonst Doppel-Release. */
        snprintf(name, sizeof(name), "cache_k%d", g);
        moo_dict_remove(schicht, moo_string_new(name));
        snprintf(name, sizeof(name), "cache_v%d", g);
        moo_dict_remove(schicht, moo_string_new(name));
    }
}
MooValue moo_nn_cache_leeren(MooValue netz) {
    if (nn_ist(netz, "netz")) {   /* D1: Kinderleicht-Netz delegiert */
        MooValue schichten = dget(netz, "schichten");
        MooValue r = moo_nn_cache_leeren(schichten);
        moo_release(schichten);
        return r;
    }
    if (netz.tag == MOO_DICT && nn_ist_schicht(netz)) {
        cache_leeren_schicht(netz);
        return moo_none();
    }
    if (netz.tag == MOO_LIST) {
        MooList* l = MV_LIST(netz);
        for (int32_t i = 0; i < l->length; i++)
            cache_leeren_schicht(l->items[i]);
        return moo_none();
    }
    moo_throw(moo_error("cache_leeren: erwarte eine Schicht, eine Liste von "
                        "Schichten oder ein ki_netz"));
    return moo_none();
}

/* ============================================================
 * KIP-B4a: Sequence Packing (Block-Maske + Loss-Maske + Positions-Reset)
 * ============================================================ */

/* sequence_packen(docs, block_len): docs = Liste von 1D-Token-ID-Tensoren.
 * Packt sie sequenziell in EINEN Block der Laenge block_len (Rest 0-gepadded).
 * Liefert Dict { ids[block], attn_maske[block,block] (BLOCKDIAGONAL + kausal
 * pro Doc), loss_maske[block] (0 an der letzten Doc-Position + Padding),
 * positionen[block] (Per-Doc-Reset 0..len-1), doc_offsets[n+1] }. Alle Masken
 * sind grad-lose Konstanten aus den Doc-Grenzen. X3 §2: SFT-Loss-Masken
 * kommen separat von aussen — hier nur die Packing-Grenzen. */
MooValue moo_nn_sequence_packen(MooValue docs, MooValue block_len) {
    if (docs.tag != MOO_LIST) {
        moo_throw(moo_error("sequence_packen: erwarte eine Liste von Token-Tensoren"));
        return moo_none();
    }
    int32_t bl = ganze_zahl(block_len, "sequence_packen (block_len)", 1);
    if (bl < 0) return moo_none();
    MooList* dl = MV_LIST(docs);
    int32_t nd = dl->length;
    if (nd <= 0) {
        moo_throw(moo_error("sequence_packen: die Doc-Liste ist leer"));
        return moo_none();
    }
    int32_t* len = (int32_t*)calloc((size_t)nd, sizeof(int32_t));
    int32_t* docof = (int32_t*)calloc((size_t)(nd + 1), sizeof(int32_t));
    if (!len || !docof) { free(len); free(docof); return moo_none(); }
    int32_t sum = 0;
    for (int32_t d = 0; d < nd; d++) {
        MooValue di = dl->items[d];
        if (di.tag != MOO_TENSOR) {
            free(len); free(docof);
            moo_throw(moo_error("sequence_packen: jedes Dokument muss ein Token-Tensor sein"));
            return moo_none();
        }
        MooTensor* dt = T(di);
        moo_tensor_f32_sichern(dt);
        int32_t l = (int32_t)dt->size;
        if (l <= 0) {
            free(len); free(docof);
            moo_throw(moo_error("sequence_packen: ein Dokument ist leer"));
            return moo_none();
        }
        len[d] = l; sum += l;
    }
    if (sum > bl) {
        free(len); free(docof);
        moo_throw(moo_error("sequence_packen: die Summe der Doc-Laengen "
                            "uebersteigt block_len"));
        return moo_none();
    }
    for (int32_t d = 0; d < nd; d++) docof[d + 1] = docof[d] + len[d];

    int32_t sh1[1] = { bl };
    int32_t sh2[2] = { bl, bl };
    int32_t sho[1] = { nd + 1 };
    MooTensor* ids  = moo_tensor_raw(1, sh1);
    MooTensor* mask = moo_tensor_raw(2, sh2);
    MooTensor* lm   = moo_tensor_raw(1, sh1);
    MooTensor* pos  = moo_tensor_raw(1, sh1);
    MooTensor* off  = moo_tensor_raw(1, sho);
    if (!ids || !mask || !lm || !pos || !off) {
        free(len); free(docof);
#define B4A_RELT(t) do { if (t) { MooValue _v; _v.tag = MOO_TENSOR; moo_val_set_ptr(&_v, t); moo_release(_v); } } while (0)
        B4A_RELT(ids); B4A_RELT(mask); B4A_RELT(lm); B4A_RELT(pos); B4A_RELT(off);
#undef B4A_RELT
        return moo_none();
    }
    /* ids + Per-Doc-Positionen + Loss-Maske; Padding bleibt 0 (raw nullt). */
    for (int32_t d = 0; d < nd; d++) {
        MooTensor* dt = T(dl->items[d]);
        int32_t s = docof[d];
        for (int32_t i = 0; i < len[d]; i++) {
            ids->data[s + i] = dt->data[i];
            pos->data[s + i] = (float)i;
            lm->data[s + i]  = (i < len[d] - 1) ? 1.0f : 0.0f;
        }
        off->data[d] = (float)docof[d];
    }
    off->data[nd] = (float)docof[nd];
    /* Block-diagonale + kausale Maske: 0 nur wenn selber Doc UND j<=i. */
    for (int32_t i = 0; i < bl; i++) {
        int32_t di = -1;
        if (i < sum)
            for (int32_t d = 0; d < nd; d++)
                if (i >= docof[d] && i < docof[d + 1]) { di = d; break; }
        for (int32_t j = 0; j < bl; j++) {
            int32_t dj = -1;
            if (j < sum)
                for (int32_t d = 0; d < nd; d++)
                    if (j >= docof[d] && j < docof[d + 1]) { dj = d; break; }
            bool ok = (di >= 0 && di == dj && j <= i);
            mask->data[(int64_t)i * bl + j] = ok ? 0.0f : -1e9f;
        }
    }
    free(len); free(docof);
    MooValue r = moo_dict_new();
#define B4A_PUT(name, t) do { MooValue _v; _v.tag = MOO_TENSOR; moo_val_set_ptr(&_v, t); dset(r, name, _v); } while (0)
    B4A_PUT("ids", ids); B4A_PUT("attn_maske", mask); B4A_PUT("loss_maske", lm);
    B4A_PUT("positionen", pos); B4A_PUT("doc_offsets", off);
#undef B4A_PUT
    return r;
}

/* Setzt/entfernt die transienten Packing-Felder (pack_maske/pack_pos) an ALLEN
 * attention-Schichten des Netzes — die Attention-Forward liest sie (X3 §2,
 * Muster wie das cache-Flag). Dieselbe Maske/Positionen gelten fuer alle Layer. */
static void packung_setzen_schicht(MooValue schicht, MooValue maske, MooValue pos) {
    if (!nn_ist(schicht, "attention")) return;
    moo_retain(maske); dset(schicht, "pack_maske", maske);
    moo_retain(pos);   dset(schicht, "pack_pos", pos);
}
static void packung_leeren_schicht(MooValue schicht) {
    if (!nn_ist(schicht, "attention")) return;
    moo_dict_remove(schicht, moo_string_new("pack_maske"));
    moo_dict_remove(schicht, moo_string_new("pack_pos"));
}
MooValue moo_nn_packung_setzen(MooValue netz, MooValue maske, MooValue positionen) {
    if (maske.tag != MOO_TENSOR || positionen.tag != MOO_TENSOR) {
        moo_throw(moo_error("packung_setzen: erwarte (netz, attn_maske, positionen)"));
        return moo_none();
    }
    if (nn_ist(netz, "netz")) {
        MooValue s = dget(netz, "schichten");
        MooValue r = moo_nn_packung_setzen(s, maske, positionen);
        moo_release(s); return r;
    }
    if (netz.tag == MOO_DICT && nn_ist_schicht(netz)) {
        packung_setzen_schicht(netz, maske, positionen); return moo_none();
    }
    if (netz.tag == MOO_LIST) {
        MooList* l = MV_LIST(netz);
        for (int32_t i = 0; i < l->length; i++)
            packung_setzen_schicht(l->items[i], maske, positionen);
        return moo_none();
    }
    moo_throw(moo_error("packung_setzen: erwarte eine Schicht, Liste oder ki_netz"));
    return moo_none();
}
MooValue moo_nn_packung_leeren(MooValue netz) {
    if (nn_ist(netz, "netz")) {
        MooValue s = dget(netz, "schichten");
        MooValue r = moo_nn_packung_leeren(s);
        moo_release(s); return r;
    }
    if (netz.tag == MOO_DICT && nn_ist_schicht(netz)) {
        packung_leeren_schicht(netz); return moo_none();
    }
    if (netz.tag == MOO_LIST) {
        MooList* l = MV_LIST(netz);
        for (int32_t i = 0; i < l->length; i++)
            packung_leeren_schicht(l->items[i]);
        return moo_none();
    }
    moo_throw(moo_error("packung_leeren: erwarte eine Schicht, Liste oder ki_netz"));
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
static MooValue ce_kern(MooValue logits, MooValue ziele, MooValue maske) {
    if (logits.tag != MOO_TENSOR || ziele.tag != MOO_TENSOR) {
        moo_throw(moo_error("kreuzentropie: erwarte zwei Tensoren (Logits, Ziele)"));
        return moo_none();
    }
    MooTensor* lt = T(logits);
    MooTensor* zt = T(ziele);
    moo_tensor_f32_sichern(zt);   // KIP-D1 Eintrittspunkt: zt->data wird direkt gelesen (logits geht ueber logsoftmax/expect_t)
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
    MooValue loss = moo_none();
    if (maske.tag == MOO_TENSOR) {
        /* KIP-B4a (X3 §2): maskierte CE — Summe nur ueber maske==1, normiert
         * ueber die ANZAHL maskierter Tokens (nicht batch). maske [batch] oder
         * [batch,1]; grad-lose Kopie [batch,1] fuer den Broadcast-mul. */
        MooTensor* mt = T(maske);
        moo_tensor_f32_sichern(mt);
        bool form_ok = (mt->ndim == 1 && mt->shape[0] == batch) ||
                       (mt->ndim == 2 && mt->shape[0] == batch && mt->shape[1] == 1);
        if (!form_ok) {
            moo_release(ls); moo_release(p); moo_release(onehot);
            moo_throw(moo_error("kreuzentropie: die Maske muss die Form [batch] "
                                "oder [batch, 1] haben"));
            return moo_none();
        }
        int32_t m1_shape[2] = { batch, 1 };
        MooTensor* m1 = moo_tensor_raw(2, m1_shape);   /* grad-los */
        if (!m1) { moo_release(ls); moo_release(p); moo_release(onehot); return moo_none(); }
        double count = 0.0;
        for (int32_t r = 0; r < batch; r++) {
            float v = mt->data[r];
            m1->data[r] = v;
            count += (double)v;
        }
        MooValue mv; mv.tag = MOO_TENSOR; moo_val_set_ptr(&mv, m1);
        if (count <= 0.0) {
            moo_release(mv); moo_release(ls); moo_release(p); moo_release(onehot);
            moo_throw(moo_error("kreuzentropie: die Maske hat keine aktiven "
                                "Positionen (Summe 0) — kein normierbarer Verlust"));
            return moo_none();
        }
        MooValue pm = (p.tag == MOO_TENSOR) ? moo_tensor_mul(p, mv) : moo_none();
        MooValue s  = (pm.tag == MOO_TENSOR) ? moo_tensor_summe(pm, moo_number(-1.0)) : moo_none();
        loss = (s.tag == MOO_TENSOR) ? moo_tensor_muls(s, moo_number(-1.0 / count)) : moo_none();
        moo_release(mv); moo_release(pm); moo_release(s);
    } else {
        MooValue s = (p.tag == MOO_TENSOR) ? moo_tensor_summe(p, moo_number(-1.0)) : moo_none();
        loss = (s.tag == MOO_TENSOR)
            ? moo_tensor_muls(s, moo_number(-1.0 / (double)batch)) : moo_none();
        moo_release(s);
    }
    moo_release(ls); moo_release(p); moo_release(onehot);
    return loss;
}

/* kreuzentropie(logits, ziele): unmaskiert, ueber batch normiert. */
MooValue moo_nn_kreuzentropie(MooValue logits, MooValue ziele) {
    return ce_kern(logits, ziele, moo_none());
}
/* kreuzentropie(logits, ziele, maske): KIP-B4a — Next-Token-Loss nur auf
 * maske==1 (Doc-Grenzen/SFT-Loss-Maske kommen von aussen, X3 §2). */
MooValue moo_nn_kreuzentropie_maskiert(MooValue logits, MooValue ziele,
                                       MooValue maske) {
    return ce_kern(logits, ziele, maske);
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
            p->valid = MOO_V_DATA;   /* KIP-D1 Mutations-Invalidierung (D0 §4.2): Optimizer schrieb p->data */
            if (p->grad) memset(p->grad, 0, (size_t)p->size * sizeof(float));
        }
    }
    moo_release(artv); moo_release(params); moo_release(ml); moo_release(vl);
    /* Tape leeren — die aufgezeichneten Nodes dieser Iteration freigeben. */
    moo_ag_reset();
    return moo_none();
}
