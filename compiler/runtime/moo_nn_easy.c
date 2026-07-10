/**
 * moo_nn_easy.c — Kinderleicht-API (Plan-014 D1): ki_netz / trainiere /
 * vorhersage / genauigkeit / speichern / laden.
 * ============================================================================
 * DER USP: ein neuronales Netz in 5 Zeilen (DE/EN). Diese Schicht ist NUR
 * Zucker ueber moo_nn.c — sie erfindet keine neue Mathematik:
 *   * ki_netz(schichten) haelt die Schicht-Liste in einem "__nn"="netz"-Dict.
 *   * trainiere(netz, x, y, optionen) macht Shuffle/Batching/Forward/Loss/
 *     Backward/Schritt intern und druckt kindgerechten Fortschritt.
 *     Optionen als Dict (KEINE Named-Args: der Call-Parser kennt kein
 *     `epochen: 10` — Entscheid D1, dokumentiert): {"epochen": 10,
 *     "rate": 0.01, "batch": 32, "optimierer": "adam", "ausgabe": 1,
 *     "seed": 42, "verlust": "auto"}.
 *   * LOSS-AUTO-WAHL: endet das Netz auf softmax -> Kreuzentropie auf den
 *     WAHRSCHEINLICHKEITEN via -mean(sum(y*log(p+1e-7))) (Keras-Muster,
 *     Epsilon gegen log(0)); sonst MSE. Das fused-CE auf Logits
 *     (moo_nn_kreuzentropie) bleibt der Profi-Pfad.
 *   * .mook-Format (speichern/laden) = SAFETENSORS-Layout: 8-byte u64 LE
 *     Header-Laenge + JSON-Header {name:{dtype,shape,data_offsets}} +
 *     rohe f32-LE-Daten. Architektur steckt als JSON-String unter
 *     __metadata__.moo_arch (safetensors verlangt String-Values dort).
 *     Damit ist .mook direkt safetensors-lesbar. Fremd-Import-Gate = F1.
 * Fehlermeldungen DEUTSCH und ERKLAEREND (D1-Vorgabe).
 * Tensor-Konvention: Args borrowed, Rueckgaben +1. UB-Policy: Indizes
 * int64_t, RNG unsigned splitmix64, Byte-Groessen size_t.
 * ============================================================================
 */
#include "moo_runtime.h"
#include <math.h>
#include <stdarg.h>   /* Buf: vsnprintf-Header-Builder (speichern) */

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

/* Dict-Helfer (gleiche Konventionen wie moo_nn.c) */
static void eset(MooValue d, const char* k, MooValue v) {
    moo_dict_set(d, moo_string_new(k), v);
}
static MooValue eget(MooValue d, const char* k) {
    return moo_dict_get(d, moo_string_new(k));
}
static double enum_(MooValue d, const char* k, double fallback) {
    if (d.tag != MOO_DICT) return fallback;
    MooValue v = eget(d, k);
    double r = (v.tag == MOO_NUMBER) ? MV_NUM(v) : fallback;
    moo_release(v);
    return r;
}
/* String-Option in Puffer (Fallback wenn fehlt). */
static void estr(MooValue d, const char* k, const char* fallback,
                 char* out, size_t out_len) {
    snprintf(out, out_len, "%s", fallback);
    if (d.tag != MOO_DICT) return;
    MooValue v = eget(d, k);
    if (v.tag == MOO_STRING) snprintf(out, out_len, "%s", MV_STR(v)->chars);
    moo_release(v);
}
static bool ist_netz(MooValue d) {
    if (d.tag != MOO_DICT) return false;
    MooValue t = eget(d, "__nn");
    bool ok = (t.tag == MOO_STRING) && strcmp(MV_STR(t)->chars, "netz") == 0;
    moo_release(t);
    return ok;
}
static bool ist_schicht_typ(MooValue d, const char* typ) {
    if (d.tag != MOO_DICT) return false;
    MooValue t = eget(d, "__nn");
    bool ok = (t.tag == MOO_STRING) && strcmp(MV_STR(t)->chars, typ) == 0;
    moo_release(t);
    return ok;
}

static uint64_t esm64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ============================================================
 * ki_netz(schichten)
 * ============================================================ */
MooValue moo_nn_ki_netz(MooValue schichten) {
    if (schichten.tag != MOO_LIST || MV_LIST(schichten)->length == 0) {
        moo_throw(moo_error("ki_netz: erwarte eine Liste mit mindestens einer "
                            "Schicht, z.B. ki_netz([schicht_dicht(2, 1)])"));
        return moo_none();
    }
    MooList* l = MV_LIST(schichten);
    for (int32_t i = 0; i < l->length; i++) {
        MooValue s = l->items[i];
        /* Registry-Validierung (Phase 1a): moo_nn.c ist die einzige
         * Wahrheit ueber bekannte Schicht-Typen. */
        MooValue t = (s.tag == MOO_DICT) ? eget(s, "__nn") : moo_none();
        bool ok = (t.tag == MOO_STRING) &&
                  moo_nn_layer_bekannt(MV_STR(t)->chars);
        moo_release(t);
        if (!ok) {
            char namen[160];
            moo_nn_layer_namen(namen, sizeof(namen));
            char msg[256];
            snprintf(msg, sizeof(msg), "ki_netz: Eintrag %d ist keine Schicht "
                     "(erwarte schicht_%s)", i, namen);
            moo_throw(moo_error(msg));
            return moo_none();
        }
    }
    MooValue d = moo_dict_new();
    eset(d, "__nn", moo_string_new("netz"));
    moo_retain(schichten);
    eset(d, "schichten", schichten);
    return d;
}

/* ============================================================
 * Trainings-Hilfen
 * ============================================================ */

/* Endet das Netz auf einer softmax-dicht-Schicht? */
static bool endet_auf_softmax(MooValue schichten) {
    MooList* l = MV_LIST(schichten);
    for (int32_t i = l->length - 1; i >= 0; i--) {
        MooValue s = l->items[i];
        if (ist_schicht_typ(s, "dropout")) continue;   /* zaehlt nicht */
        if (!ist_schicht_typ(s, "dicht")) return false;
        MooValue a = eget(s, "aktivierung");
        bool sm = (a.tag == MOO_STRING) &&
                  strcmp(MV_STR(a)->chars, "softmax") == 0;
        moo_release(a);
        return sm;
    }
    return false;
}

/* Eingabe-Breite der ersten dichten Schicht (-1 wenn keine). */
static int32_t erste_dicht_ein(MooValue schichten) {
    MooList* l = MV_LIST(schichten);
    for (int32_t i = 0; i < l->length; i++) {
        if (ist_schicht_typ(l->items[i], "dicht")) {
            MooValue w = eget(l->items[i], "w");
            int32_t ein = (w.tag == MOO_TENSOR) ? T(w)->shape[0] : -1;
            moo_release(w);
            return ein;
        }
    }
    return -1;
}

/* Batch-Tensor mit erhaltener Beispiel-Geometrie aus src — ohne grad. */
static MooValue batch_zeilen(MooTensor* src, const int32_t* perm,
                             int64_t start, int32_t b, int32_t sp) {
    int32_t shape[8];
    if (src->ndim < 1 || src->ndim > 8) return moo_none();
    shape[0] = b;
    for (int32_t d = 1; d < src->ndim; d++) shape[d] = src->shape[d];
    MooTensor* t = moo_tensor_raw(src->ndim, shape);
    if (!t) return moo_none();
    for (int32_t r = 0; r < b; r++) {
        int64_t q = (int64_t)perm[start + r] * sp;
        memcpy(t->data + (int64_t)r * sp, src->data + q,
               (size_t)sp * sizeof(float));
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

/* Batch-Ziel als one-hot [b, klassen] aus Index-y ([n] oder [n,1]). */
static MooValue batch_onehot(MooTensor* y, const int32_t* perm,
                             int64_t start, int32_t b, int32_t klassen) {
    int32_t shape[2] = { b, klassen };
    MooTensor* t = moo_tensor_raw(2, shape);
    if (!t) return moo_none();
    for (int32_t r = 0; r < b; r++) {
        double d = (double)y->data[perm[start + r]];
        if (d < 0 || d >= (double)klassen || d != (double)(int64_t)d) {
            MooValue tv; tv.tag = MOO_TENSOR; moo_val_set_ptr(&tv, t);
            moo_release(tv);
            char msg[160];
            snprintf(msg, sizeof(msg), "trainiere: Label %g ist keine gueltige "
                     "Klasse (erlaubt: ganze Zahlen 0 bis %d)", d, klassen - 1);
            moo_throw(moo_error(msg));
            return moo_none();
        }
        t->data[(int64_t)r * klassen + (int64_t)d] = 1.0f;
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

/* CE auf Wahrscheinlichkeiten: -mean_batch(sum(ziel * log(p + 1e-7))).
 * Reine Op-Komposition (log/mul/summe/muls haben backward, B2-gedeckt). */
static MooValue loss_ce_p(MooValue p, MooValue ziel_onehot, int32_t b) {
    MooValue pe = moo_tensor_adds(p, moo_number(1e-7));
    MooValue lp = (pe.tag == MOO_TENSOR) ? moo_tensor_log(pe) : moo_none();
    MooValue m  = (lp.tag == MOO_TENSOR) ? moo_tensor_mul(ziel_onehot, lp) : moo_none();
    MooValue s  = (m.tag == MOO_TENSOR) ? moo_tensor_summe(m, moo_number(-1.0)) : moo_none();
    MooValue loss = (s.tag == MOO_TENSOR)
        ? moo_tensor_muls(s, moo_number(-1.0 / (double)b)) : moo_none();
    moo_release(pe); moo_release(lp); moo_release(m); moo_release(s);
    return loss;
}

/* ============================================================
 * trainiere(netz, x, y, optionen)
 * ============================================================ */
MooValue moo_nn_trainiere(MooValue netz, MooValue x, MooValue y, MooValue optionen) {
    /* --- Netz + Schichten --- */
    MooValue schichten;
    if (ist_netz(netz)) schichten = eget(netz, "schichten");
    else if (netz.tag == MOO_LIST) { moo_retain(netz); schichten = netz; }
    else {
        moo_throw(moo_error("trainiere: erwarte ein ki_netz(...) oder eine "
                            "Liste von Schichten"));
        return moo_none();
    }
    if (optionen.tag != MOO_NONE && optionen.tag != MOO_DICT) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: Optionen bitte als Dict, z.B. "
                            "{\"epochen\": 10, \"rate\": 0.01}"));
        return moo_none();
    }
    /* --- Daten pruefen (ERKLAEREND) --- */
    if (x.tag != MOO_TENSOR || y.tag != MOO_TENSOR) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: Daten und Ziele muessen Tensoren sein "
                            "(Tipp: tensor_aus_liste([...]))"));
        return moo_none();
    }
    MooTensor* xt = T(x);
    MooTensor* yt = T(y);
    if (xt->ndim < 2) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: Eingaben brauchen eine Beispiel-Achse "
                            "[Beispiele, ...]"));
        return moo_none();
    }
    int64_t n = xt->shape[0];
    int64_t din64 = 1;
    for (int32_t d = 1; d < xt->ndim; d++) din64 *= xt->shape[d];
    if (din64 < 1 || din64 > INT32_MAX) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: ein Beispiel ist zu gross"));
        return moo_none();
    }
    int32_t din = (int32_t)din64;
    int32_t erwartet = erste_dicht_ein(schichten);
    if (xt->ndim == 2 && erwartet > 0 && din != erwartet) {
        moo_release(schichten);
        char msg[200];
        snprintf(msg, sizeof(msg), "trainiere: deine Eingabe hat %d Werte pro "
                 "Zeile, das Netz erwartet %d (erste Schicht)", din, erwartet);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    if (yt->shape[0] != n) {
        moo_release(schichten);
        char msg[200];
        snprintf(msg, sizeof(msg), "trainiere: du hast %lld Eingaben, aber %d "
                 "Ziele — beides muss gleich viele Zeilen haben",
                 (long long)n, yt->shape[0]);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    if (n < 1) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: keine Trainingsbeispiele uebergeben"));
        return moo_none();
    }

    /* --- Optionen --- */
    int64_t epochen = (int64_t)enum_(optionen, "epochen", 10.0);
    double rate = enum_(optionen, "rate", 0.01);
    int64_t batch = (int64_t)enum_(optionen, "batch", 32.0);
    bool ausgabe = enum_(optionen, "ausgabe", 1.0) != 0.0;
    uint64_t seed = (uint64_t)(int64_t)enum_(optionen, "seed", 42.0);
    char opt_name[24], verlust[24];
    estr(optionen, "optimierer", "adam", opt_name, sizeof(opt_name));
    estr(optionen, "verlust", "auto", verlust, sizeof(verlust));
    /* E2: Trainings-Techniken (EN-Alias-Keys jeweils moeglich) */
    double clip = enum_(optionen, "clip", enum_(optionen, "clip_norm", 0.0));
    int64_t geduld = (int64_t)enum_(optionen, "geduld",
                                    enum_(optionen, "patience", 0.0));
    /* min_besserung (Keras min_delta): so viel muss der Loss sinken um als
     * Verbesserung zu zaehlen — faengt Float-Rundungsrauschen der Shuffle-
     * Permutation ab (mean-Reduktion ist permutationsabhaengig gerundet). */
    double min_besserung = enum_(optionen, "min_besserung",
                                 enum_(optionen, "min_delta", 0.0));
    if (min_besserung < 1e-12) min_besserung = 1e-12;
    double plan_faktor = enum_(optionen, "plan_faktor",
                               enum_(optionen, "schedule_factor", 0.1));
    int64_t plan_schritt = (int64_t)enum_(optionen, "plan_schritt",
                                          enum_(optionen, "schedule_step", 0.0));
    char lr_plan[24];
    estr(optionen, "lr_plan", "", lr_plan, sizeof(lr_plan));
    if (!lr_plan[0]) estr(optionen, "lr_schedule", "keiner", lr_plan, sizeof(lr_plan));
    char ckpt[512];
    estr(optionen, "checkpoint", "", ckpt, sizeof(ckpt));
    bool plan_step = strcmp(lr_plan, "step") == 0;
    bool plan_cos = strcmp(lr_plan, "cosine") == 0 || strcmp(lr_plan, "kosinus") == 0;
    bool plan_warm = strcmp(lr_plan, "warmup") == 0;
    if (!plan_step && !plan_cos && !plan_warm &&
        strcmp(lr_plan, "keiner") != 0 && strcmp(lr_plan, "none") != 0) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: \"lr_plan\" kann \"keiner\", \"step\", "
                            "\"cosine\" oder \"warmup\" sein"));
        return moo_none();
    }
    if (plan_schritt <= 0)
        plan_schritt = plan_step ? (epochen / 3 > 0 ? epochen / 3 : 1)
                                 : (epochen / 10 > 0 ? epochen / 10 : 1);
    if (epochen < 1 || epochen > 1000000) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: \"epochen\" muss zwischen 1 und "
                            "1000000 liegen"));
        return moo_none();
    }
    if (batch < 1) batch = 1;
    if (batch > n) batch = n;

    /* --- Loss-Wahl --- */
    bool ce;
    if (strcmp(verlust, "auto") == 0) ce = endet_auf_softmax(schichten);
    else if (strcmp(verlust, "kreuzentropie") == 0 ||
             strcmp(verlust, "cross_entropy") == 0) ce = true;
    else if (strcmp(verlust, "mse") == 0) ce = false;
    else {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: \"verlust\" kann \"auto\", \"mse\" "
                            "oder \"kreuzentropie\" sein"));
        return moo_none();
    }
    /* Ziel-Geometrie: bei CE brauchen wir one-hot [b, klassen] */
    int32_t klassen = 0;
    bool y_ist_index = false;
    if (ce) {
        MooList* sl = MV_LIST(schichten);
        MooValue letzte = sl->items[sl->length - 1];
        MooValue w = eget(letzte, "w");
        klassen = (w.tag == MOO_TENSOR) ? T(w)->shape[1] : 0;
        moo_release(w);
        if (yt->ndim == 1 || (yt->ndim == 2 && yt->shape[1] == 1)) {
            y_ist_index = true;
        } else if (!(yt->ndim == 2 && yt->shape[1] == klassen)) {
            moo_release(schichten);
            char msg[200];
            snprintf(msg, sizeof(msg), "trainiere: die Ziele passen nicht — "
                     "erwarte Klassen-Nummern [n] oder one-hot [n, %d]", klassen);
            moo_throw(moo_error(msg));
            return moo_none();
        }
    } else if (!(yt->ndim == 2)) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere (mse): die Ziele muessen 2D sein "
                            "[Beispiele, Werte] — wie die Eingaben"));
        return moo_none();
    }
    int32_t ysp = (yt->ndim == 2) ? yt->shape[1] : 1;

    /* --- Optimizer --- */
    MooValue params = moo_nn_parameter(schichten);
    if (params.tag != MOO_LIST) { moo_release(schichten); return moo_none(); }
    MooValue opt;
    MooValue raten = moo_number(rate);
    if (strcmp(opt_name, "adam") == 0)
        opt = moo_nn_opt_adam(params, raten);
    else if (strcmp(opt_name, "adamw") == 0)
        opt = moo_nn_opt_adamw(params, raten, moo_none());
    else if (strcmp(opt_name, "sgd") == 0)
        opt = moo_nn_opt_sgd(params, raten, moo_number(enum_(optionen, "momentum", 0.9)));
    else {
        moo_release(params); moo_release(schichten);
        moo_throw(moo_error("trainiere: \"optimierer\" kann \"adam\", "
                            "\"adamw\" oder \"sgd\" sein"));
        return moo_none();
    }
    if (opt.tag != MOO_DICT) {
        moo_release(params); moo_release(schichten);
        return moo_none();
    }

    /* --- Trainings-Loop --- */
    int32_t* perm = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    MooValue historie = moo_list_new((int32_t)epochen);
    if (!perm) {
        moo_release(opt); moo_release(params); moo_release(schichten);
        moo_release(historie);
        moo_throw(moo_error("trainiere: kein Speicher fuer die Misch-Tabelle"));
        return moo_none();
    }
    for (int64_t i = 0; i < n; i++) perm[i] = (int32_t)i;
    uint64_t rng = seed * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    bool fehler = false;
    double best = 1e300;
    int64_t ohne_verbesserung = 0;

    for (int64_t ep = 0; ep < epochen && !fehler; ep++) {
        /* E2: Lernraten-Plan — opt_schritt liest \"rate\" pro Aufruf aus dem
         * Dict, also wirkt das Setzen hier sofort. */
        double rate_e = rate;
        if (plan_step) {
            for (int64_t k = plan_schritt; k <= ep; k += plan_schritt)
                rate_e *= plan_faktor;
        } else if (plan_cos) {
            rate_e = rate * 0.5 * (1.0 + cos(3.14159265358979323846 *
                                             (double)ep / (double)epochen));
        } else if (plan_warm) {
            if (ep < plan_schritt)
                rate_e = rate * (double)(ep + 1) / (double)plan_schritt;
        }
        eset(opt, "rate", moo_number(rate_e));
        /* Fisher-Yates, seed-deterministisch */
        for (int64_t i = n - 1; i > 0; i--) {
            int64_t j = (int64_t)(esm64(&rng) % (uint64_t)(i + 1));
            int32_t tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
        }
        double summe = 0.0;
        int64_t start = 0;
        while (start < n && !fehler) {
            int32_t b = (int32_t)((n - start < batch) ? (n - start) : batch);
            MooValue xb = batch_zeilen(xt, perm, start, b, din);
            MooValue yb = ce && y_ist_index
                ? batch_onehot(yt, perm, start, b, klassen)
                : batch_zeilen(yt, perm, start, b, ysp);
            if (xb.tag != MOO_TENSOR || yb.tag != MOO_TENSOR) {
                moo_release(xb); moo_release(yb);
                fehler = true;
                break;
            }
            MooValue out = moo_nn_vorwaerts(schichten, xb);
            MooValue loss = moo_none();
            if (out.tag == MOO_TENSOR)
                loss = ce ? loss_ce_p(out, yb, b) : moo_nn_mse(out, yb);
            if (loss.tag == MOO_TENSOR) {
                summe += (double)T(loss)->data[0] * (double)b;
                moo_release(moo_tensor_rueckwaerts(loss));
                if (clip > 0.0)
                    moo_release(moo_nn_grad_clip(params, moo_number(clip)));
                moo_release(moo_nn_opt_schritt(opt));
            } else {
                fehler = true;
            }
            moo_release(loss); moo_release(out);
            moo_release(xb); moo_release(yb);
            start += b;
        }
        if (fehler) break;
        double avg = summe / (double)n;
        moo_list_append(historie, moo_number(avg));
        if (ausgabe)
            printf("Epoche %lld/%lld — Fehler: %.6f\n",
                   (long long)(ep + 1), (long long)epochen, avg);
        /* E2: Bestwert-Tracking -> Checkpoint + Early-Stopping */
        if (avg < best - min_besserung) {
            best = avg;
            ohne_verbesserung = 0;
            if (ckpt[0]) {
                MooValue cp = moo_string_new(ckpt);
                moo_release(moo_nn_speichern(schichten, cp));
                moo_release(cp);
            }
        } else {
            ohne_verbesserung++;
            if (geduld > 0 && ohne_verbesserung >= geduld) {
                if (ausgabe)
                    printf("Frueher Stopp nach Epoche %lld — keine "
                           "Verbesserung seit %lld Epochen.\n",
                           (long long)(ep + 1), (long long)geduld);
                break;
            }
        }
    }

    free(perm);
    moo_release(opt); moo_release(params); moo_release(schichten);
    if (fehler) { moo_release(historie); return moo_none(); }
    return historie;   /* Fehler-Verlauf pro Epoche — fuer eigene Plots */
}

/* ============================================================
 * vorhersage(netz, x) — Inferenz ohne Tape, ag-Zustand bleibt erhalten
 * ============================================================ */
MooValue moo_nn_vorhersage(MooValue netz, MooValue x) {
    bool war_an = moo_ag_ist_an();
    moo_ag_aus();
    MooValue out = moo_nn_vorwaerts(netz, x);
    if (war_an) moo_ag_an();
    return out;
}

/* ============================================================
 * genauigkeit(netz, x, y) — Anteil korrekter Vorhersagen [0..1]
 * ============================================================ */
MooValue moo_nn_genauigkeit(MooValue netz, MooValue x, MooValue y) {
    if (x.tag != MOO_TENSOR || y.tag != MOO_TENSOR) {
        moo_throw(moo_error("genauigkeit: Daten und Ziele muessen Tensoren sein"));
        return moo_none();
    }
    MooValue out = moo_nn_vorhersage(netz, x);
    if (out.tag != MOO_TENSOR) return moo_none();
    MooTensor* ot = T(out);
    MooTensor* yt = T(y);
    if (ot->ndim != 2 || ot->shape[0] != yt->shape[0]) {
        moo_release(out);
        moo_throw(moo_error("genauigkeit: Vorhersagen und Ziele haben "
                            "verschieden viele Zeilen"));
        return moo_none();
    }
    int64_t n = ot->shape[0];
    int32_t c = ot->shape[1];
    bool y_onehot = (yt->ndim == 2 && yt->shape[1] == c && c > 1);
    bool y_index = (yt->ndim == 1) || (yt->ndim == 2 && yt->shape[1] == 1);
    if (!y_onehot && !y_index) {
        moo_release(out);
        moo_throw(moo_error("genauigkeit: Ziele bitte als Klassen-Nummern [n] "
                            "oder one-hot [n, klassen]"));
        return moo_none();
    }
    int64_t korrekt = 0;
    for (int64_t r = 0; r < n; r++) {
        int32_t vorhergesagt, richtig;
        if (c > 1) {
            /* argmax je Zeile */
            int32_t am = 0;
            float best = ot->data[r * c];
            for (int32_t k = 1; k < c; k++)
                if (ot->data[r * c + k] > best) { best = ot->data[r * c + k]; am = k; }
            vorhergesagt = am;
            if (y_onehot) {
                int32_t ym = 0;
                float yb = yt->data[r * c];
                for (int32_t k = 1; k < c; k++)
                    if (yt->data[r * c + k] > yb) { yb = yt->data[r * c + k]; ym = k; }
                richtig = ym;
            } else {
                richtig = (int32_t)yt->data[r];
            }
        } else {
            /* 1 Ausgabe: runden (Binaer-Klassifikation / Regression-Nahfall) */
            vorhergesagt = (ot->data[r] >= 0.5f) ? 1 : 0;
            richtig = (yt->data[r] >= 0.5f) ? 1 : 0;
        }
        if (vorhergesagt == richtig) korrekt++;
    }
    moo_release(out);
    return moo_number((double)korrekt / (double)n);
}

/* ============================================================
 * .mook speichern/laden (safetensors-Layout)
 * ============================================================ */

/* Wachsender Text-Puffer fuer den JSON-Header. */
typedef struct { char* s; size_t len; size_t cap; bool oom; } Buf;
static void buf_add(Buf* b, const char* fmt, ...) {
    if (b->oom) return;
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { b->oom = true; va_end(ap2); return; }
    if (b->len + (size_t)need + 1 > b->cap) {
        size_t nc = (b->cap ? b->cap * 2 : 256);
        while (nc < b->len + (size_t)need + 1) nc *= 2;
        char* ns = (char*)realloc(b->s, nc);
        if (!ns) { b->oom = true; va_end(ap2); return; }
        b->s = ns; b->cap = nc;
    }
    vsnprintf(b->s + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)need;
}

/* ============================================================
 * Save/Load-Hook-Tabelle (Typ-Zentralisierung Phase 1b)
 * ============================================================
 * Buf ist bewusst LOKAL (Serialisierungsdetail dieser Datei) — die
 * Kopplung zur Schicht-Registry in moo_nn.c laeuft ueber die Iterator-
 * API (moo_nn_layer_anzahl/name): sl_hooks_vollstaendig() prueft beim
 * ersten speichern/laden, dass JEDER Registry-Layer hier einen Hook hat,
 * und wirft sonst ERKLAEREND. Ein neuer Layer ohne Hook ist damit ein
 * sofortiger benannter Fehler statt stillem Datenverlust (Layer wuerde
 * gespeichert-aber-nicht-geladen bzw. gar nicht serialisiert). */
typedef struct {
    const char* name;                          /* == Registry-Name          */
    bool (*arch_write)(Buf* b, MooValue s);    /* Arch-JSON-Objekt anhaengen */
    MooValue (*rebuild)(MooValue e);           /* Schicht aus Arch-Eintrag +1 */
} NNSaveLoadHook;

static bool aw_dicht(Buf* b, MooValue s) {
    MooValue w = eget(s, "w");
    MooValue a = eget(s, "aktivierung");
    if (w.tag != MOO_TENSOR) { moo_release(w); moo_release(a); return false; }
    buf_add(b, "{\"typ\":\"dicht\",\"ein\":%d,\"aus\":%d,\"aktivierung\":\"%s\"}",
            T(w)->shape[0], T(w)->shape[1],
            (a.tag == MOO_STRING) ? MV_STR(a)->chars : "keine");
    moo_release(w); moo_release(a);
    return true;
}
static bool aw_faltung(Buf* b, MooValue s) {
    MooValue a = eget(s, "aktivierung");
    buf_add(b, "{\"typ\":\"faltung\",\"cin\":%d,\"cout\":%d,\"kernel\":%d,\"schritt\":%d,\"polster\":%d,\"aktivierung\":\"%s\"}",
            (int32_t)enum_(s,"cin",0), (int32_t)enum_(s,"cout",0),
            (int32_t)enum_(s,"kernel",0), (int32_t)enum_(s,"schritt",1),
            (int32_t)enum_(s,"polster",0),
            a.tag==MOO_STRING ? MV_STR(a)->chars : "keine");
    moo_release(a); return true;
}
static bool aw_pooling(Buf* b, MooValue s) {
    buf_add(b, "{\"typ\":\"pooling\",\"art\":%d,\"groesse\":%d,\"schritt\":%d}",
            (int32_t)enum_(s,"art",0), (int32_t)enum_(s,"groesse",2),
            (int32_t)enum_(s,"schritt",2)); return true;
}
static bool aw_flach(Buf* b, MooValue s) { (void)s; buf_add(b, "{\"typ\":\"flach\"}"); return true; }
static bool aw_dropout(Buf* b, MooValue s) {
    buf_add(b, "{\"typ\":\"dropout\",\"rate\":%g}", enum_(s, "rate", 0.0));
    return true;
}
static bool aw_layernorm(Buf* b, MooValue s) {
    MooValue g = eget(s, "gamma");
    if (g.tag != MOO_TENSOR) { moo_release(g); return false; }
    buf_add(b, "{\"typ\":\"layernorm\",\"dim\":%d}", T(g)->shape[1]);
    moo_release(g);
    return true;
}
static bool aw_rmsnorm(Buf* b, MooValue s) {
    MooValue g = eget(s, "g");
    if (g.tag != MOO_TENSOR) { moo_release(g); return false; }
    buf_add(b, "{\"typ\":\"rmsnorm\",\"dim\":%d}", T(g)->shape[1]);
    moo_release(g);
    return true;
}
static bool aw_ffn_gated(Buf* b, MooValue s) {
    MooValue w2 = eget(s, "w2");
    MooValue a  = eget(s, "art");
    if (w2.tag != MOO_TENSOR) { moo_release(w2); moo_release(a); return false; }
    /* W2: [versteckt, dim] -> dim=shape[1], versteckt=shape[0] */
    buf_add(b, "{\"typ\":\"ffn_gated\",\"dim\":%d,\"versteckt\":%d,\"art\":\"%s\"}",
            T(w2)->shape[1], T(w2)->shape[0],
            (a.tag == MOO_STRING) ? MV_STR(a)->chars : "swiglu");
    moo_release(w2); moo_release(a);
    return true;
}
static bool aw_embedding(Buf* b, MooValue s) {
    MooValue w = eget(s, "w");
    if (w.tag != MOO_TENSOR) { moo_release(w); return false; }
    buf_add(b, "{\"typ\":\"embedding\",\"vokab\":%d,\"dim\":%d}",
            T(w)->shape[0], T(w)->shape[1]);
    moo_release(w);
    return true;
}
static bool aw_attention(Buf* b, MooValue s) {
    /* KI-M2a: kv_koepfe mitschreiben; Fallback = koepfe (alte Dicts).
     * KI-M2b: maske/fenster mitschreiben; Fallback causal/0 (alte Dicts). */
    int32_t nk = (int32_t)enum_(s, "koepfe", 1);
    MooValue m = eget(s, "maske");
    const char* mart = (m.tag == MOO_STRING) ? MV_STR(m)->chars : "causal";
    /* KIP-B2: rope/rope_basis mitschreiben; Fallback 0/10000 (alte Dicts). */
    buf_add(b, "{\"typ\":\"attention\",\"dim\":%d,\"koepfe\":%d,"
            "\"kv_koepfe\":%d,\"maske\":\"%s\",\"fenster\":%d,"
            "\"rope\":%d,\"rope_basis\":%g,"
            "\"rope_skalierung\":%d,\"rope_faktor\":%g}",
            (int32_t)enum_(s, "dim", 0), nk, (int32_t)enum_(s, "kv_koepfe", nk),
            mart, (int32_t)enum_(s, "fenster", 0),
            (int32_t)enum_(s, "rope", 0), enum_(s, "rope_basis", 10000.0),
            (int32_t)enum_(s, "rope_skalierung", 0), enum_(s, "rope_faktor", 1.0));
    moo_release(m);
    return true;
}
static bool aw_moe(Buf* b, MooValue s) {
    buf_add(b, "{\"typ\":\"moe\",\"dim\":%d,\"versteckt\":%d,\"n\":%d,\"k\":%d}",
            (int32_t)enum_(s, "dim", 0), (int32_t)enum_(s, "versteckt", 0),
            (int32_t)enum_(s, "n", 0), (int32_t)enum_(s, "k", 1));
    return true;
}
static bool aw_position(Buf* b, MooValue s) {
    MooValue p = eget(s, "pos");
    MooValue a = eget(s, "art");
    if (p.tag != MOO_TENSOR) { moo_release(p); moo_release(a); return false; }
    buf_add(b, "{\"typ\":\"position\",\"max\":%d,\"dim\":%d,\"art\":\"%s\"}",
            T(p)->shape[0], T(p)->shape[1],
            (a.tag == MOO_STRING) ? MV_STR(a)->chars : "gelernt");
    moo_release(p); moo_release(a);
    return true;
}

static MooValue rb_dicht(MooValue e) {
    MooValue akt = eget(e, "aktivierung");
    MooValue s = moo_nn_schicht_dicht(moo_number(enum_(e, "ein", 0)),
                                      moo_number(enum_(e, "aus", 0)),
                                      akt, moo_none());
    moo_release(akt);
    return s;
}
static MooValue rb_faltung(MooValue e) {
    MooValue a=eget(e,"aktivierung");
    MooValue r=moo_nn_schicht_faltung(moo_number(enum_(e,"cin",0)),moo_number(enum_(e,"cout",0)),
        moo_number(enum_(e,"kernel",0)),moo_number(enum_(e,"schritt",1)),
        moo_number(enum_(e,"polster",0)),a,moo_none());
    moo_release(a); return r;
}
static MooValue rb_pooling(MooValue e) {
    MooValue art=moo_string_new(enum_(e,"art",0)==0 ? "max" : "mittel");
    MooValue r=moo_nn_schicht_pooling(art,moo_number(enum_(e,"groesse",2)),moo_number(enum_(e,"schritt",2)));
    moo_release(art); return r;
}
static MooValue rb_flach(MooValue e) { (void)e; return moo_nn_schicht_flach(); }
static MooValue rb_dropout(MooValue e) {
    return moo_nn_schicht_dropout(moo_number(enum_(e, "rate", 0.0)));
}
static MooValue rb_layernorm(MooValue e) {
    return moo_nn_schicht_layernorm(moo_number(enum_(e, "dim", 0)));
}
static MooValue rb_rmsnorm(MooValue e) {
    return moo_nn_schicht_rmsnorm(moo_number(enum_(e, "dim", 0)));
}
static MooValue rb_ffn_gated(MooValue e) {
    MooValue art = eget(e, "art");
    MooValue s = moo_nn_schicht_ffn_gated(moo_number(enum_(e, "dim", 0)),
                                          moo_number(enum_(e, "versteckt", 0)),
                                          art);
    moo_release(art);
    return s;
}
static MooValue rb_embedding(MooValue e) {
    return moo_nn_schicht_embedding(moo_number(enum_(e, "vokab", 0)),
                                    moo_number(enum_(e, "dim", 0)),
                                    moo_none());
}
static MooValue rb_attention(MooValue e) {
    /* KI-M2a Abwaertskompat: arch-JSON ohne kv_koepfe => Default koepfe.
     * KI-M2b: ohne maske => causal (fenster nur bei sliding uebergeben). */
    double nk = enum_(e, "koepfe", 1);
    MooValue mart = eget(e, "maske");
    bool sliding = (mart.tag == MOO_STRING &&
                    strcmp(MV_STR(mart)->chars, "sliding") == 0);
    MooValue fw = sliding ? moo_number(enum_(e, "fenster", 1)) : moo_none();
    /* KIP-B2: RoPE-Konfig aus arch-JSON rekonstruieren (Fallback aus =
     * alte Dicts). rope==1 => Basis wieder anlegen (Cache-Zustand-Feld). */
    /* KIP-B2b: Skalierung roundtrippen — bei skalierung!=0 ein rope-Dict
     * {basis, skalierung, faktor} bauen; sonst Zahl (abwaertskompat). */
    MooValue rope;
    if (enum_(e, "rope", 0) != 1.0) {
        rope = moo_none();
    } else {
        int32_t rsk = (int32_t)enum_(e, "rope_skalierung", 0);
        if (rsk == 0) {
            rope = moo_number(enum_(e, "rope_basis", 10000.0));
        } else {
            rope = moo_dict_new();
            moo_dict_set(rope, moo_string_new("basis"),
                         moo_number(enum_(e, "rope_basis", 10000.0)));
            moo_dict_set(rope, moo_string_new("skalierung"),
                         moo_number((double)rsk));
            moo_dict_set(rope, moo_string_new("faktor"),
                         moo_number(enum_(e, "rope_faktor", 1.0)));
        }
    }
    MooValue s = moo_nn_schicht_attention(moo_number(enum_(e, "dim", 0)),
                                          moo_number(nk),
                                          moo_none(),
                                          moo_number(enum_(e, "kv_koepfe", nk)),
                                          mart,
                                          fw,
                                          rope);
    moo_release(mart);
    moo_release(rope);   /* KIP-B2b: rope-Dict freigeben (Zahl/none = no-op) */
    return s;
}
static MooValue rb_moe(MooValue e) {
    return moo_nn_schicht_moe(moo_number(enum_(e, "dim", 0)),
                              moo_number(enum_(e, "versteckt", 0)),
                              moo_number(enum_(e, "n", 0)),
                              moo_number(enum_(e, "k", 1)),
                              moo_none());
}
static MooValue rb_position(MooValue e) {
    /* sinus-pos rekonstruiert der Konstruktor; gelernt-pos wird
     * gleich durch den Param-Fill ueberschrieben. */
    MooValue art = eget(e, "art");
    MooValue s = moo_nn_schicht_position(moo_number(enum_(e, "max", 0)),
                                         moo_number(enum_(e, "dim", 0)),
                                         art, moo_none());
    moo_release(art);
    return s;
}

static const NNSaveLoadHook nn_sl_hooks[] = {
    { "dicht",     aw_dicht,     rb_dicht     },
    { "faltung",   aw_faltung,   rb_faltung   },
    { "pooling",   aw_pooling,   rb_pooling   },
    { "flach",     aw_flach,     rb_flach     },
    { "dropout",   aw_dropout,   rb_dropout   },
    { "layernorm", aw_layernorm, rb_layernorm },
    { "rmsnorm",   aw_rmsnorm,   rb_rmsnorm   },
    { "ffn_gated", aw_ffn_gated, rb_ffn_gated },
    { "embedding", aw_embedding, rb_embedding },
    { "attention", aw_attention, rb_attention },
    { "position",  aw_position,  rb_position  },
    { "moe",       aw_moe,       rb_moe       },
};

static const NNSaveLoadHook* sl_hook_lookup(const char* name) {
    for (size_t i = 0; i < sizeof(nn_sl_hooks) / sizeof(nn_sl_hooks[0]); i++)
        if (strcmp(nn_sl_hooks[i].name, name) == 0)
            return &nn_sl_hooks[i];
    return NULL;
}

/* Vollstaendigkeits-Check gegen die zentrale Registry (einmalig gecacht).
 * false = geworfen; Caller bricht ab. */
static bool sl_hooks_vollstaendig(void) {
    static bool geprueft = false;
    if (geprueft) return true;
    for (int32_t i = 0; i < moo_nn_layer_anzahl(); i++) {
        const char* n = moo_nn_layer_name(i);
        if (!n || !sl_hook_lookup(n)) {
            char msg[220];
            snprintf(msg, sizeof(msg), "speichern/laden: Schicht-Typ \"%s\" "
                     "steht in der Registry (moo_nn.c), hat aber keinen "
                     "Save/Load-Hook in moo_nn_easy.c — Hook-Tabelle nachziehen",
                     n ? n : "?");
            moo_throw(moo_error(msg));
            return false;
        }
    }
    geprueft = true;
    return true;
}

/* Architektur-JSON einer Schicht anhaengen (kontrollierte Werte, die
 * Aktivierungs-Strings kommen aus akt_anwenden-Whitelist -> kein Escaping). */
static bool arch_schicht(Buf* b, MooValue s) {
    if (s.tag != MOO_DICT) return false;
    MooValue t = eget(s, "__nn");
    const NNSaveLoadHook* h = (t.tag == MOO_STRING)
        ? sl_hook_lookup(MV_STR(t)->chars) : NULL;
    moo_release(t);
    return h ? h->arch_write(b, s) : false;
}

/* speichern(netz, pfad): safetensors-Datei schreiben. Rueckgabe none. */
/* ============================================================
 * KIP-D3: bf16/f16 <-> f32 Konvertierung (lokale static Helfer)
 * ============================================================
 * d3_f32_zu_bf16 / d3_bf16_zu_f32 sind BIT-EXAKTE Spiegel der D1-Kanonik
 * in moo_tensor.c (moo_f32_zu_bf16/moo_bf16_zu_f32, static inline dort).
 * Lokal kopiert, um moo_runtime.h NICHT anfassen zu muessen (parallele
 * B4a-Belegung durch kip-ops). Bei Aenderung der Kanonik hier mitziehen.
 * d3_f16_zu_f32: IEEE half (1s/5e/10m) -> f32; NUR Import (kein F16-Export). */
static inline uint16_t d3_f32_zu_bf16(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    if (((x >> 23) & 0xFFu) == 0xFFu && (x & 0x7FFFFFu)) {
        return (uint16_t)((x >> 16) | 0x0040u);   /* NaN -> quiet bf16-NaN */
    }
    uint32_t bias = 0x7FFFu + ((x >> 16) & 1u);   /* round-to-nearest-even */
    x += bias;                                    /* uint32: definiertes Wrap (UB-Policy) */
    return (uint16_t)(x >> 16);
}
static inline float d3_bf16_zu_f32(uint16_t h) {
    uint32_t x = (uint32_t)h << 16;
    float f;
    memcpy(&f, &x, sizeof(f));
    return f;
}
static inline float d3_f16_zu_f32(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp  = (uint32_t)((h >> 10) & 0x1Fu);
    uint32_t mant = (uint32_t)(h & 0x3FFu);
    uint32_t out;
    if (exp == 0u) {
        if (mant == 0u) {
            out = sign;                           /* +/- 0 */
        } else {
            /* subnormal half -> normalisierter f32 */
            exp = 127u - 15u + 1u;
            while ((mant & 0x400u) == 0u) { mant <<= 1; exp--; }
            mant &= 0x3FFu;
            out = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 0x1Fu) {
        out = sign | 0x7F800000u | (mant << 13);  /* Inf / NaN */
    } else {
        out = sign | ((exp - 15u + 127u) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &out, sizeof(f));
    return f;
}
/* bf16-Export-Toggle: Default AUS => f32-Pfad byte-identisch (Gate). */
static bool d3_export_bf16(void) {
    const char* e = getenv("MOO_KI_SPEICHERN_BF16");
    return e && e[0] && e[0] != '0';
}

MooValue moo_nn_speichern(MooValue netz, MooValue pfad) {
    if (!sl_hooks_vollstaendig()) return moo_none();
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("speichern: erwarte einen Dateinamen als Text, "
                            "z.B. netz.speichern(\"mein_netz.mook\")"));
        return moo_none();
    }
    MooValue schichten;
    if (ist_netz(netz)) schichten = eget(netz, "schichten");
    else if (netz.tag == MOO_LIST) { moo_retain(netz); schichten = netz; }
    else {
        moo_throw(moo_error("speichern: das ist kein ki_netz"));
        return moo_none();
    }
    MooValue params = moo_nn_parameter(schichten);
    if (params.tag != MOO_LIST) { moo_release(schichten); return moo_none(); }
    MooList* pl = MV_LIST(params);

    /* Architektur-JSON (als String im __metadata__) */
    Buf arch = {0};
    buf_add(&arch, "[");
    MooList* sl = MV_LIST(schichten);
    bool arch_ok = true;
    for (int32_t i = 0; i < sl->length && arch_ok; i++) {
        if (i) buf_add(&arch, ",");
        arch_ok = arch_schicht(&arch, sl->items[i]);
    }
    buf_add(&arch, "]");

    /* Header: __metadata__ + Tensor-Eintraege p0..pN. moo_arch ist ein
     * JSON-STRING im JSON -> Anfuehrungszeichen escapen. */
    Buf h = {0};
    buf_add(&h, "{\"__metadata__\":{\"moo_arch\":\"");
    for (size_t i = 0; i < arch.len && !h.oom; i++) {
        char c = arch.s[i];
        if (c == '"' || c == '\\') buf_add(&h, "\\%c", c);
        else buf_add(&h, "%c", c);
    }
    buf_add(&h, "\"}");
    bool exp_bf16 = d3_export_bf16();
    uint32_t d3_esz = exp_bf16 ? 2u : 4u;
    const char* exp_dt = exp_bf16 ? "BF16" : "F32";
    uint64_t off = 0;
    for (int32_t i = 0; i < pl->length; i++) {
        MooTensor* p = T(pl->items[i]);
        uint64_t bytes = (uint64_t)p->size * d3_esz;
        buf_add(&h, ",\"p%d\":{\"dtype\":\"%s\",\"shape\":[", i, exp_dt);
        for (int32_t d = 0; d < p->ndim; d++)
            buf_add(&h, "%s%d", d ? "," : "", p->shape[d]);
        buf_add(&h, "],\"data_offsets\":[%llu,%llu]}",
                (unsigned long long)off, (unsigned long long)(off + bytes));
        off += bytes;
    }
    buf_add(&h, "}");

    if (!arch_ok || h.oom || arch.oom) {
        free(arch.s); free(h.s);
        moo_release(params); moo_release(schichten);
        moo_throw(moo_error("speichern: konnte den Datei-Kopf nicht bauen"));
        return moo_none();
    }

    FILE* f = fopen(MV_STR(pfad)->chars, "wb");
    if (!f) {
        free(arch.s); free(h.s);
        moo_release(params); moo_release(schichten);
        char msg[300];
        snprintf(msg, sizeof(msg), "speichern: kann \"%s\" nicht schreiben "
                 "(Ordner vorhanden? Schreibrechte?)", MV_STR(pfad)->chars);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    /* safetensors: u64 LE Header-Laenge. x86 ist LE; das Format ist bewusst
     * LE festgeschrieben (wie safetensors selbst). */
    uint64_t hlen = (uint64_t)h.len;
    bool ok = fwrite(&hlen, 8, 1, f) == 1 &&
              fwrite(h.s, 1, h.len, f) == h.len;
    for (int32_t i = 0; i < pl->length && ok; i++) {
        MooTensor* p = T(pl->items[i]);
        if (!exp_bf16) {
            ok = fwrite(p->data, sizeof(float), (size_t)p->size, f)
                 == (size_t)p->size;
        } else {
            uint16_t* b16 = (uint16_t*)malloc((size_t)p->size * sizeof(uint16_t));
            if (!b16) { ok = false; break; }
            for (int64_t j = 0; j < p->size; j++) b16[j] = d3_f32_zu_bf16(p->data[j]);
            ok = fwrite(b16, sizeof(uint16_t), (size_t)p->size, f)
                 == (size_t)p->size;
            free(b16);
        }
    }
    ok = (fclose(f) == 0) && ok;
    free(arch.s); free(h.s);
    moo_release(params); moo_release(schichten);
    if (!ok) {
        moo_throw(moo_error("speichern: Schreiben fehlgeschlagen (Platte voll?)"));
        return moo_none();
    }
    return moo_none();
}

/* ============================================================
 * safetensors_laden(pfad) — Fremd-Import (Plan-014 F1)
 * ============================================================
 * Liest ein BELIEBIGES safetensors-File (auch ohne moo_arch) und gibt
 * ein Dict {tensorname: Tensor} zurueck — der Weg fuer HuggingFace-
 * Mini-Modelle. dtype F32 direkt; BF16/F16 werden beim Import nach f32
 * konvertiert (KIP-D3, moo-Tensoren bleiben f32); andere dtypes werfen
 * erklaerend. __metadata__ wird uebersprungen. */
MooValue moo_nn_safetensors(MooValue pfad) {
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("safetensors_laden: erwarte einen Dateinamen als Text"));
        return moo_none();
    }
    FILE* f = fopen(MV_STR(pfad)->chars, "rb");
    if (!f) {
        char msg[300];
        snprintf(msg, sizeof(msg), "safetensors_laden: kann \"%s\" nicht "
                 "oeffnen", MV_STR(pfad)->chars);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    uint64_t hlen = 0;
    if (fread(&hlen, 8, 1, f) != 1 || hlen == 0 || hlen > 256u * 1024u * 1024u) {
        fclose(f);
        moo_throw(moo_error("safetensors_laden: das ist keine safetensors-"
                            "Datei (Kopf kaputt)"));
        return moo_none();
    }
    char* hjson = (char*)malloc((size_t)hlen + 1);
    if (!hjson || fread(hjson, 1, (size_t)hlen, f) != (size_t)hlen) {
        free(hjson); fclose(f);
        moo_throw(moo_error("safetensors_laden: Datei-Kopf unvollstaendig"));
        return moo_none();
    }
    hjson[hlen] = '\0';
    MooValue hs = moo_string_new(hjson);
    free(hjson);
    MooValue header = moo_json_parse(hs);
    moo_release(hs);
    if (header.tag != MOO_DICT) {
        moo_release(header); fclose(f);
        moo_throw(moo_error("safetensors_laden: Kopf ist kein gueltiges JSON"));
        return moo_none();
    }
    long daten_start = (long)(8 + hlen);
    MooValue keys = moo_dict_keys(header);
    MooValue ergebnis = moo_dict_new();
    bool ok = (keys.tag == MOO_LIST);
    char fmsg[240] = {0};
    for (int32_t i = 0; ok && i < MV_LIST(keys)->length; i++) {
        MooValue k = MV_LIST(keys)->items[i];
        if (k.tag != MOO_STRING) continue;
        if (strcmp(MV_STR(k)->chars, "__metadata__") == 0) continue;
        /* moo_dict_get VERBRAUCHT die Key-Referenz (Transfer-Semantik,
         * siehe moo_dict.c) — k ist aus der keys-Liste geliehen, also
         * fuer den Lookup eine eigene +1 mitgeben. */
        moo_retain(k);
        MooValue ent = moo_dict_get(header, k);
        if (ent.tag != MOO_DICT) { moo_release(ent); continue; }
        MooValue dt = eget(ent, "dtype");
        /* dtype -> Elementgroesse + Konvertierungs-Art (KIP-D3).
         * d3_kind: 0=F32 (direkt), 1=BF16, 2=F16 (beide -> f32). */
        int d3_esz = 0;
        int d3_kind = 0;
        if (dt.tag == MOO_STRING) {
            const char* dn = MV_STR(dt)->chars;
            if (strcmp(dn, "F32") == 0)  { d3_kind = 0; d3_esz = 4; }
            else if (strcmp(dn, "BF16") == 0) { d3_kind = 1; d3_esz = 2; }
            else if (strcmp(dn, "F16") == 0 || strcmp(dn, "FP16") == 0) { d3_kind = 2; d3_esz = 2; }
        }
        if (d3_esz == 0) {
            snprintf(fmsg, sizeof(fmsg), "safetensors_laden: Tensor \"%s\" "
                     "hat dtype %s — moo unterstuetzt F32, BF16, F16 "
                     "(BF16/F16 werden nach f32 konvertiert)", MV_STR(k)->chars,
                     (dt.tag == MOO_STRING) ? MV_STR(dt)->chars : "?");
            ok = false;
            moo_release(dt); moo_release(ent);
            break;
        }
        moo_release(dt);
        MooValue shp = eget(ent, "shape");
        MooValue offs = eget(ent, "data_offsets");
        int32_t shape[8];
        int32_t nd = 0;
        int64_t elems = 1;
        bool e_ok = (shp.tag == MOO_LIST) && MV_LIST(shp)->length >= 1 &&
                    MV_LIST(shp)->length <= 8 &&
                    (offs.tag == MOO_LIST) && MV_LIST(offs)->length == 2;
        if (e_ok) {
            nd = MV_LIST(shp)->length;
            for (int32_t d = 0; d < nd && e_ok; d++) {
                MooValue sv = MV_LIST(shp)->items[d];
                e_ok = (sv.tag == MOO_NUMBER) && MV_NUM(sv) >= 1;
                if (e_ok) {
                    shape[d] = (int32_t)MV_NUM(sv);
                    elems *= shape[d];
                }
            }
        }
        MooTensor* t = NULL;
        if (e_ok) {
            double a = MV_NUM(MV_LIST(offs)->items[0]);
            double b = MV_NUM(MV_LIST(offs)->items[1]);
            e_ok = (b - a) == (double)elems * (double)d3_esz;
            if (e_ok) {
                t = moo_tensor_raw(nd, shape);
                e_ok = t && fseek(f, daten_start + (long)a, SEEK_SET) == 0;
                if (e_ok && d3_kind == 0) {
                    e_ok = fread(t->data, sizeof(float), (size_t)elems, f)
                           == (size_t)elems;
                } else if (e_ok) {
                    /* BF16/F16: 2-Byte-LE einlesen, elementweise nach f32. */
                    uint16_t* buf16 = (uint16_t*)malloc((size_t)elems * sizeof(uint16_t));
                    e_ok = buf16 && fread(buf16, sizeof(uint16_t), (size_t)elems, f)
                           == (size_t)elems;
                    if (e_ok) {
                        if (d3_kind == 1)
                            for (int64_t j = 0; j < elems; j++) t->data[j] = d3_bf16_zu_f32(buf16[j]);
                        else
                            for (int64_t j = 0; j < elems; j++) t->data[j] = d3_f16_zu_f32(buf16[j]);
                    }
                    free(buf16);
                }
            }
        }
        moo_release(shp); moo_release(offs); moo_release(ent);
        if (!e_ok) {
            if (t) { MooValue tv; tv.tag = MOO_TENSOR; moo_val_set_ptr(&tv, t); moo_release(tv); }
            snprintf(fmsg, sizeof(fmsg), "safetensors_laden: Eintrag \"%s\" "
                     "ist kaputt (Shape/Offsets passen nicht)", MV_STR(k)->chars);
            ok = false;
            break;
        }
        MooValue tv; tv.tag = MOO_TENSOR; moo_val_set_ptr(&tv, t);
        /* Frischer Key: k ist aus der keys-Liste geliehen und stirbt mit
         * header/keys-Release — nie in ein langlebiges Dict uebernehmen. */
        moo_dict_set(ergebnis, moo_string_new(MV_STR(k)->chars), tv);
    }
    fclose(f);
    moo_release(keys); moo_release(header);
    if (!ok) {
        moo_release(ergebnis);
        moo_throw(moo_error(fmsg[0] ? fmsg : "safetensors_laden: Datei kaputt"));
        return moo_none();
    }
    return ergebnis;
}

/* laden(pfad): Netz aus .mook rekonstruieren (+1). */
MooValue moo_nn_laden(MooValue pfad) {
    if (!sl_hooks_vollstaendig()) return moo_none();
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("ki_laden: erwarte einen Dateinamen als Text"));
        return moo_none();
    }
    FILE* f = fopen(MV_STR(pfad)->chars, "rb");
    if (!f) {
        char msg[300];
        snprintf(msg, sizeof(msg), "ki_laden: kann \"%s\" nicht oeffnen — "
                 "gibt es die Datei?", MV_STR(pfad)->chars);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    uint64_t hlen = 0;
    if (fread(&hlen, 8, 1, f) != 1 || hlen == 0 || hlen > 64u * 1024u * 1024u) {
        fclose(f);
        moo_throw(moo_error("ki_laden: das ist keine .mook/safetensors-Datei "
                            "(Kopf kaputt)"));
        return moo_none();
    }
    char* hjson = (char*)malloc((size_t)hlen + 1);
    if (!hjson || fread(hjson, 1, (size_t)hlen, f) != (size_t)hlen) {
        free(hjson); fclose(f);
        moo_throw(moo_error("ki_laden: Datei-Kopf unvollstaendig"));
        return moo_none();
    }
    hjson[hlen] = '\0';

    MooValue hs = moo_string_new(hjson);
    free(hjson);
    MooValue header = moo_json_parse(hs);
    moo_release(hs);
    if (header.tag != MOO_DICT) {
        moo_release(header); fclose(f);
        moo_throw(moo_error("ki_laden: Datei-Kopf ist kein gueltiges JSON"));
        return moo_none();
    }
    /* Architektur aus __metadata__.moo_arch */
    MooValue meta = eget(header, "__metadata__");
    MooValue archs = (meta.tag == MOO_DICT) ? eget(meta, "moo_arch") : moo_none();
    MooValue arch = (archs.tag == MOO_STRING) ? moo_json_parse(archs) : moo_none();
    moo_release(archs); moo_release(meta);
    if (arch.tag != MOO_LIST) {
        moo_release(arch); moo_release(header); fclose(f);
        moo_throw(moo_error("ki_laden: der Datei fehlt die moo-Architektur "
                            "(__metadata__.moo_arch) — reine safetensors-"
                            "Fremddateien kommen mit Plan-014 F1)"));
        return moo_none();
    }

    /* Netz rekonstruieren */
    MooValue schichten = moo_list_new(MV_LIST(arch)->length);
    bool ok = true;
    for (int32_t i = 0; i < MV_LIST(arch)->length && ok; i++) {
        MooValue e = MV_LIST(arch)->items[i];
        if (e.tag != MOO_DICT) { ok = false; break; }
        MooValue tv = eget(e, "typ");
        const char* typ = (tv.tag == MOO_STRING) ? MV_STR(tv)->chars : "";
        /* Registry-gekoppelte Rekonstruktion (Phase 1b). */
        const NNSaveLoadHook* h = sl_hook_lookup(typ);
        MooValue s = h ? h->rebuild(e) : moo_none();
        moo_release(tv);
        if (s.tag != MOO_DICT) { ok = false; moo_release(s); break; }
        moo_list_append(schichten, s);
    }
    moo_release(arch);
    if (!ok) {
        moo_release(schichten); moo_release(header); fclose(f);
        moo_throw(moo_error("ki_laden: Architektur in der Datei ist kaputt"));
        return moo_none();
    }
    MooValue netz = moo_nn_ki_netz(schichten);
    moo_release(schichten);
    if (netz.tag != MOO_DICT) { moo_release(header); fclose(f); return moo_none(); }

    /* Gewichte einlesen: Reihenfolge p0..pN == parameter()-Reihenfolge. */
    MooValue params = moo_nn_parameter(netz);
    long daten_start = (long)(8 + hlen);
    ok = (params.tag == MOO_LIST);
    if (ok) {
        MooList* pl = MV_LIST(params);
        for (int32_t i = 0; i < pl->length && ok; i++) {
            MooTensor* p = T(pl->items[i]);
            char key[24];
            snprintf(key, sizeof(key), "p%d", i);
            MooValue ent = eget(header, key);
            if (ent.tag != MOO_DICT) { moo_release(ent); ok = false; break; }
            MooValue offs = eget(ent, "data_offsets");
            bool e_ok = (offs.tag == MOO_LIST) && MV_LIST(offs)->length == 2 &&
                        MV_LIST(offs)->items[0].tag == MOO_NUMBER &&
                        MV_LIST(offs)->items[1].tag == MOO_NUMBER;
            if (e_ok) {
                double a = MV_NUM(MV_LIST(offs)->items[0]);
                double b = MV_NUM(MV_LIST(offs)->items[1]);
                e_ok = (b - a) == (double)p->size * 4.0;
                if (e_ok) {
                    e_ok = fseek(f, daten_start + (long)a, SEEK_SET) == 0 &&
                           fread(p->data, sizeof(float), (size_t)p->size, f)
                           == (size_t)p->size;
                }
            }
            moo_release(offs); moo_release(ent);
            ok = e_ok;
        }
    }
    fclose(f);
    moo_release(params); moo_release(header);
    if (!ok) {
        moo_release(netz);
        moo_throw(moo_error("ki_laden: Gewichte passen nicht zur Architektur "
                            "(Datei beschaedigt oder falsche Version)"));
        return moo_none();
    }
    return netz;
}

/* ============================================================
 * KIP-E2: CPU-Voll-Checkpoint v2 (Resume-exakt)
 * ============================================================
 * Format v2 = SUPERSET des .mook (safetensors-Layout, bleibt safetensors-
 * lesbar): [u64-LE Header-Laenge][JSON-Header][f32-LE-Blob].
 *   __metadata__.moo_arch  : Netz-Architektur (wie v1 -> Rekonstruktion)
 *   __metadata__.moo_ckpt  : v2-Trainingszustand als JSON-String:
 *       format_version=2, global_schritt, epoche, tokenizer_version,
 *       arch_version, metrik, opt{art,t,rate,beta1,beta2,eps,decay,
 *       momentum}, dropout[{i,z}] (mutierender Layer-Zaehler!), dataloader{}
 *   Tensor-Eintraege: p0..pN (Gewichte) + om0..omN (Opt 1. Moment m) +
 *       ov0..ovN (Opt 2. Moment v, nur adam/adamw).
 * ATOMISCH: Schreiben nach <pfad>.tmp, dann rename (POSIX atomar,
 * MoveFileEx auf Windows). Abbruch mittendrin laesst den alten <pfad>
 * unberuehrt (die tmp-Datei wird verworfen).
 * BIT-IDENTISCHES RESUME: Gewichte + Opt(m/v/t) + Dropout-Zaehler +
 * Schritt + Dataloader-Position werden exakt (f32-bit) wiederhergestellt;
 * der Dropout-RNG (seed+zaehler) und die Reihenfolge (seed+pos) ziehen
 * danach dieselbe Maske / denselben Batch wie ununterbrochen.
 * f32 ZUERST — bf16-Gewichte folgen mit KIP-D3.
 * ============================================================ */
#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#endif

/* Skalar-MooValue (Zahl/Text/Bool/none) als JSON-Wert an Buf haengen. */
static void ckpt_json_val(Buf* b, MooValue v) {
    if (v.tag == MOO_STRING) {
        buf_add(b, "\"");
        const char* s = MV_STR(v)->chars;
        for (size_t i = 0; s[i]; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c == '"' || c == '\\') buf_add(b, "\\%c", (char)c);
            else if (c < 0x20)         buf_add(b, "\\u%04x", (unsigned)c);
            else                       buf_add(b, "%c", (char)c);
        }
        buf_add(b, "\"");
    } else if (v.tag == MOO_NUMBER) {
        buf_add(b, "%.17g", MV_NUM(v));        /* roundtrip-feste double */
    } else if (v.tag == MOO_BOOL) {
        buf_add(b, MV_BOOL(v) ? "true" : "false");
    } else {
        buf_add(b, "null");
    }
}

/* Ein ",\"key\":<wert>"-Paar aus dem zustand-Dict (fehlt -> null). */
static void ckpt_kv(Buf* b, MooValue d, const char* key) {
    MooValue v = eget(d, key);
    buf_add(b, ",\"%s\":", key);
    ckpt_json_val(b, v);
    moo_release(v);
}

/* Werte-Gleichheit fuer den Versions-Check (Text/Zahl/none, sonst false). */
static bool ckpt_val_gleich(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING && b.tag == MOO_STRING)
        return strcmp(MV_STR(a)->chars, MV_STR(b)->chars) == 0;
    if (a.tag == MOO_NUMBER && b.tag == MOO_NUMBER)
        return MV_NUM(a) == MV_NUM(b);
    if (a.tag == MOO_NONE && b.tag == MOO_NONE) return true;
    return false;
}

/* KIP-E2c: bf16-Gewichts-Checkpoint. bf16 ist eine LOSSY Storage-/Export-Option
 * NUR fuer Gewichte p* (halbe Groesse), KEIN bit-identisches Trainings-Resume.
 * Default AUS (env MOO_KI_CKPT_BF16) -> f32-Checkpoint byte-identisch zum
 * Standardpfad. Empfehlung: Rotation letzte-N f32 lassen, bf16 fuer best/Export. */
static bool ckpt_export_bf16(void) {
    const char* e = getenv("MOO_KI_CKPT_BF16");
    return e && e[0] && e[0] != '0';
}

/* Einen benannten Tensor-Eintrag (p%d/om%d/ov%d) in dst einlesen. dtype-getrieben:
 * F32 (4 B direkt) oder BF16 (2 B -> f32 via d3_bf16_zu_f32, nur Gewichte p*).
 * Rueckwaerts-kompatibel: alte f32-Checkpoints tragen dtype F32. */
static bool ckpt_lese_tensor(FILE* f, long daten_start, MooValue header,
                             const char* key, MooTensor* dst) {
    MooValue ent = eget(header, key);
    bool ok = false;
    if (ent.tag == MOO_DICT) {
        MooValue dtv  = eget(ent, "dtype");
        MooValue offs = eget(ent, "data_offsets");
        const char* dt = (dtv.tag == MOO_STRING) ? MV_STR(dtv)->chars : "F32";
        bool is_bf16 = (strcmp(dt, "BF16") == 0);
        uint32_t esz = is_bf16 ? 2u : 4u;
        if (offs.tag == MOO_LIST && MV_LIST(offs)->length == 2 &&
            MV_LIST(offs)->items[0].tag == MOO_NUMBER &&
            MV_LIST(offs)->items[1].tag == MOO_NUMBER) {
            double a = MV_NUM(MV_LIST(offs)->items[0]);
            double b = MV_NUM(MV_LIST(offs)->items[1]);
            if ((b - a) == (double)dst->size * (double)esz &&
                fseek(f, daten_start + (long)a, SEEK_SET) == 0) {
                if (!is_bf16) {
                    ok = fread(dst->data, sizeof(float), (size_t)dst->size, f)
                             == (size_t)dst->size;
                } else {
                    uint16_t* b16 = (uint16_t*)malloc((size_t)dst->size * sizeof(uint16_t));
                    if (b16) {
                        ok = fread(b16, sizeof(uint16_t), (size_t)dst->size, f)
                                 == (size_t)dst->size;
                        if (ok) for (int64_t j = 0; j < dst->size; j++)
                            dst->data[j] = d3_bf16_zu_f32(b16[j]);
                        free(b16);
                    }
                }
            }
        }
        moo_release(offs); moo_release(dtv);
    }
    moo_release(ent);
    return ok;
}

/* Header-Tensor-Eintrag <key>:{dtype,shape,data_offsets} anhaengen.
 * bf16=true NUR fuer Gewichte p* (2 B/Elem, dtype BF16); sonst F32 (4 B). */
static void ckpt_header_tensor(Buf* h, const char* key, MooTensor* p,
                               uint64_t* off, bool bf16) {
    uint32_t esz = bf16 ? 2u : 4u;
    uint64_t bytes = (uint64_t)p->size * esz;
    buf_add(h, ",\"%s\":{\"dtype\":\"%s\",\"shape\":[", key, bf16 ? "BF16" : "F32");
    for (int32_t d = 0; d < p->ndim; d++)
        buf_add(h, "%s%d", d ? "," : "", p->shape[d]);
    buf_add(h, "],\"data_offsets\":[%llu,%llu]}",
            (unsigned long long)*off, (unsigned long long)(*off + bytes));
    *off += bytes;
}

/* checkpoint_speichern(zustand, pfad): atomischer Voll-Checkpoint.
 * zustand = Dict { netz (Pflicht), opt?, schritt?, epoche?,
 *   tokenizer_version?, arch_version?, metrik?, dataloader? }. Rueckgabe none.
 * KIP-E2c: env MOO_KI_CKPT_BF16 speichert NUR Gewichte p* als bf16 (lossy
 *   Storage/Export, KEIN bit-identisches Resume); Optimizer m/v/t, Dropout-
 *   Zaehler, Dataloader-Position bleiben immer f32. Default AUS = f32. */
MooValue moo_nn_ckpt_speichern(MooValue zustand, MooValue pfad) {
    if (!sl_hooks_vollstaendig()) return moo_none();
    if (zustand.tag != MOO_DICT) {
        moo_throw(moo_error("checkpoint_speichern: erwarte einen Zustand "
                            "(Dict mit \"netz\", optional \"opt\"/\"schritt\"/...)"));
        return moo_none();
    }
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("checkpoint_speichern: erwarte einen Dateinamen "
                            "als Text, z.B. checkpoint_speichern(zustand, "
                            "\"lauf/ckpt_100.mook\")"));
        return moo_none();
    }
    MooValue netz = eget(zustand, "netz");
    MooValue schichten;
    if (ist_netz(netz)) schichten = eget(netz, "schichten");
    else if (netz.tag == MOO_LIST) { moo_retain(netz); schichten = netz; }
    else {
        moo_release(netz);
        moo_throw(moo_error("checkpoint_speichern: \"netz\" fehlt oder ist "
                            "kein ki_netz"));
        return moo_none();
    }
    MooValue params = moo_nn_parameter(schichten);
    if (params.tag != MOO_LIST) {
        moo_release(netz); moo_release(schichten); return moo_none();
    }
    MooList* pl = MV_LIST(params);
    MooList* sl = MV_LIST(schichten);

    /* Optimizer-Momente (geliehen). */
    MooValue optv = eget(zustand, "opt");
    bool hat_opt = (optv.tag == MOO_DICT);
    MooValue ml = hat_opt ? eget(optv, "m") : moo_none();
    MooValue vl = hat_opt ? eget(optv, "v") : moo_none();
    MooList* mlist = (ml.tag == MOO_LIST) ? MV_LIST(ml) : NULL;
    MooList* vlist = (vl.tag == MOO_LIST) ? MV_LIST(vl) : NULL;

    /* --- 1. Architektur-JSON (wie v1) --- */
    Buf arch = {0};
    buf_add(&arch, "[");
    bool arch_ok = true;
    for (int32_t i = 0; i < sl->length && arch_ok; i++) {
        if (i) buf_add(&arch, ",");
        arch_ok = arch_schicht(&arch, sl->items[i]);
    }
    buf_add(&arch, "]");

    /* --- 2. Checkpoint-Zustands-JSON (moo_ckpt) --- */
    Buf ck = {0};
    buf_add(&ck, "{\"format_version\":2");
    ckpt_kv(&ck, zustand, "global_schritt");
    ckpt_kv(&ck, zustand, "schritt");
    ckpt_kv(&ck, zustand, "epoche");
    ckpt_kv(&ck, zustand, "tokenizer_version");
    ckpt_kv(&ck, zustand, "arch_version");
    ckpt_kv(&ck, zustand, "metrik");
    /* Dropout-Zaehler je Schicht (mutierender Layer-State!). */
    buf_add(&ck, ",\"dropout\":[");
    { bool first = true;
      for (int32_t i = 0; i < sl->length; i++) {
          if (ist_schicht_typ(sl->items[i], "dropout")) {
              double z = enum_(sl->items[i], "zaehler", 0.0);
              buf_add(&ck, "%s{\"i\":%d,\"z\":%.17g}", first ? "" : ",", i, z);
              first = false;
          }
      }
    }
    buf_add(&ck, "]");
    /* Optimizer-Skalare. */
    if (hat_opt) {
        MooValue artv = eget(optv, "art");
        buf_add(&ck, ",\"opt\":{\"art\":");
        ckpt_json_val(&ck, artv);
        moo_release(artv);
        buf_add(&ck, ",\"t\":%.17g",        enum_(optv, "t", 0.0));
        buf_add(&ck, ",\"rate\":%.17g",     enum_(optv, "rate", 0.01));
        buf_add(&ck, ",\"beta1\":%.17g",    enum_(optv, "beta1", 0.9));
        buf_add(&ck, ",\"beta2\":%.17g",    enum_(optv, "beta2", 0.999));
        buf_add(&ck, ",\"eps\":%.17g",      enum_(optv, "eps", 1e-8));
        buf_add(&ck, ",\"decay\":%.17g",    enum_(optv, "decay", 0.0));
        buf_add(&ck, ",\"momentum\":%.17g", enum_(optv, "momentum", 0.0));
        buf_add(&ck, "}");
    }
    /* Dataloader-Position (generisches Skalar-Dict). */
    MooValue dl = eget(zustand, "dataloader");
    if (dl.tag == MOO_DICT) {
        buf_add(&ck, ",\"dataloader\":{");
        MooValue keys = moo_dict_keys(dl);
        if (keys.tag == MOO_LIST) {
            bool first = true;
            for (int32_t i = 0; i < MV_LIST(keys)->length; i++) {
                MooValue k = MV_LIST(keys)->items[i];
                if (k.tag != MOO_STRING) continue;
                moo_retain(k);
                MooValue val = moo_dict_get(dl, k);   /* verbraucht +1 */
                buf_add(&ck, "%s", first ? "" : ",");
                ckpt_json_val(&ck, k);                 /* Key als String */
                buf_add(&ck, ":");
                ckpt_json_val(&ck, val);
                moo_release(val);
                first = false;
            }
        }
        moo_release(keys);
        buf_add(&ck, "}");
    }
    moo_release(dl);
    buf_add(&ck, "}");

    /* --- 3. safetensors-Header: metadata + Tensor-Eintraege --- */
    Buf h = {0};
    buf_add(&h, "{\"__metadata__\":{\"moo_arch\":\"");
    for (size_t i = 0; i < arch.len && !h.oom; i++) {
        char c = arch.s[i];
        if (c == '"' || c == '\\') buf_add(&h, "\\%c", c);
        else buf_add(&h, "%c", c);
    }
    buf_add(&h, "\",\"moo_ckpt\":\"");
    for (size_t i = 0; i < ck.len && !h.oom; i++) {
        char c = ck.s[i];
        if (c == '"' || c == '\\') buf_add(&h, "\\%c", c);
        else buf_add(&h, "%c", c);
    }
    buf_add(&h, "\"}");
    bool wbf16 = ckpt_export_bf16();   /* KIP-E2c: nur Gewichte p* optional bf16 */
    uint64_t off = 0;
    for (int32_t i = 0; i < pl->length; i++) {
        char key[24]; snprintf(key, sizeof(key), "p%d", i);
        ckpt_header_tensor(&h, key, T(pl->items[i]), &off, wbf16);
    }
    if (mlist) for (int32_t i = 0; i < mlist->length; i++) {
        char key[24]; snprintf(key, sizeof(key), "om%d", i);
        ckpt_header_tensor(&h, key, T(mlist->items[i]), &off, false);
    }
    if (vlist) for (int32_t i = 0; i < vlist->length; i++) {
        char key[24]; snprintf(key, sizeof(key), "ov%d", i);
        ckpt_header_tensor(&h, key, T(vlist->items[i]), &off, false);
    }
    buf_add(&h, "}");

    if (!arch_ok || h.oom || arch.oom || ck.oom) {
        free(arch.s); free(ck.s); free(h.s);
        moo_release(params); moo_release(schichten); moo_release(netz);
        moo_release(optv); moo_release(ml); moo_release(vl);
        moo_throw(moo_error("checkpoint_speichern: konnte den Datei-Kopf "
                            "nicht bauen (Speicher?)"));
        return moo_none();
    }

    /* --- 4. Atomisch schreiben: tmp -> rename --- */
    size_t plen = strlen(MV_STR(pfad)->chars);
    char* tmp = (char*)malloc(plen + 5);
    bool ok = (tmp != NULL);
    if (ok) { memcpy(tmp, MV_STR(pfad)->chars, plen); memcpy(tmp + plen, ".tmp", 5); }

    FILE* f = ok ? fopen(tmp, "wb") : NULL;
    if (!f) {
        free(arch.s); free(ck.s); free(h.s); free(tmp);
        moo_release(params); moo_release(schichten); moo_release(netz);
        moo_release(optv); moo_release(ml); moo_release(vl);
        char msg[320];
        snprintf(msg, sizeof(msg), "checkpoint_speichern: kann \"%s.tmp\" nicht "
                 "schreiben (Ordner vorhanden? Schreibrechte?)", MV_STR(pfad)->chars);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    uint64_t hlen = (uint64_t)h.len;
    ok = fwrite(&hlen, 8, 1, f) == 1 && fwrite(h.s, 1, h.len, f) == h.len;
    for (int32_t i = 0; i < pl->length && ok; i++) {
        MooTensor* p = T(pl->items[i]);
        if (!wbf16) {
            ok = fwrite(p->data, sizeof(float), (size_t)p->size, f) == (size_t)p->size;
        } else {
            uint16_t* b16 = (uint16_t*)malloc((size_t)p->size * sizeof(uint16_t));
            if (!b16) { ok = false; break; }
            for (int64_t j = 0; j < p->size; j++) b16[j] = d3_f32_zu_bf16(p->data[j]);
            ok = fwrite(b16, sizeof(uint16_t), (size_t)p->size, f) == (size_t)p->size;
            free(b16);
        }
    }
    if (mlist) for (int32_t i = 0; i < mlist->length && ok; i++) {
        MooTensor* p = T(mlist->items[i]);
        ok = fwrite(p->data, sizeof(float), (size_t)p->size, f) == (size_t)p->size;
    }
    if (vlist) for (int32_t i = 0; i < vlist->length && ok; i++) {
        MooTensor* p = T(vlist->items[i]);
        ok = fwrite(p->data, sizeof(float), (size_t)p->size, f) == (size_t)p->size;
    }
    ok = (fclose(f) == 0) && ok;

    if (ok) {
        /* Atomarer Austausch. Alter <pfad> bleibt bis hier unberuehrt. */
#ifdef _WIN32
        ok = MoveFileExA(tmp, MV_STR(pfad)->chars, MOVEFILE_REPLACE_EXISTING) != 0;
#else
        ok = rename(tmp, MV_STR(pfad)->chars) == 0;
#endif
    }
    if (!ok) remove(tmp);   /* tmp verwerfen -> alter Checkpoint intakt */

    free(arch.s); free(ck.s); free(h.s); free(tmp);
    moo_release(params); moo_release(schichten); moo_release(netz);
    moo_release(optv); moo_release(ml); moo_release(vl);
    if (!ok) {
        moo_throw(moo_error("checkpoint_speichern: Schreiben/Umbenennen "
                            "fehlgeschlagen — der vorherige Checkpoint (falls "
                            "vorhanden) ist unveraendert"));
        return moo_none();
    }
    return moo_none();
}

/* checkpoint_laden(pfad, erwartungen): Voll-Checkpoint -> Zustand-Dict
 * { netz, opt?, schritt, epoche, tokenizer_version, arch_version, metrik,
 *   dataloader? }. erwartungen = none | Dict{tokenizer_version?, arch_version?}:
 * bei Mismatch wirft es ERKLAEREND (Token-IDs zeigen sonst auf andere Symbole). */
MooValue moo_nn_ckpt_laden(MooValue pfad, MooValue erwartungen) {
    if (!sl_hooks_vollstaendig()) return moo_none();
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("checkpoint_laden: erwarte einen Dateinamen als Text"));
        return moo_none();
    }
    FILE* f = fopen(MV_STR(pfad)->chars, "rb");
    if (!f) {
        char msg[320];
        snprintf(msg, sizeof(msg), "checkpoint_laden: kann \"%s\" nicht "
                 "oeffnen — gibt es die Datei?", MV_STR(pfad)->chars);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    uint64_t hlen = 0;
    if (fread(&hlen, 8, 1, f) != 1 || hlen == 0 || hlen > 64u * 1024u * 1024u) {
        fclose(f);
        moo_throw(moo_error("checkpoint_laden: das ist kein .mook/Checkpoint "
                            "(Kopf kaputt)"));
        return moo_none();
    }
    char* hjson = (char*)malloc((size_t)hlen + 1);
    if (!hjson || fread(hjson, 1, (size_t)hlen, f) != (size_t)hlen) {
        free(hjson); fclose(f);
        moo_throw(moo_error("checkpoint_laden: Datei-Kopf unvollstaendig"));
        return moo_none();
    }
    hjson[hlen] = '\0';
    MooValue hs = moo_string_new(hjson);
    free(hjson);
    MooValue header = moo_json_parse(hs);
    moo_release(hs);
    if (header.tag != MOO_DICT) {
        moo_release(header); fclose(f);
        moo_throw(moo_error("checkpoint_laden: Datei-Kopf ist kein gueltiges JSON"));
        return moo_none();
    }
    MooValue meta  = eget(header, "__metadata__");
    MooValue archs = (meta.tag == MOO_DICT) ? eget(meta, "moo_arch")  : moo_none();
    MooValue ckps  = (meta.tag == MOO_DICT) ? eget(meta, "moo_ckpt")  : moo_none();
    MooValue arch  = (archs.tag == MOO_STRING) ? moo_json_parse(archs) : moo_none();
    MooValue ckpt  = (ckps.tag  == MOO_STRING) ? moo_json_parse(ckps)  : moo_none();
    moo_release(archs); moo_release(ckps); moo_release(meta);
    if (arch.tag != MOO_LIST || ckpt.tag != MOO_DICT) {
        moo_release(arch); moo_release(ckpt); moo_release(header); fclose(f);
        moo_throw(moo_error("checkpoint_laden: der Datei fehlt der moo-"
                            "Checkpoint-Block (__metadata__.moo_arch/moo_ckpt)"));
        return moo_none();
    }

    /* --- Versions-Check (erklaerend werfen bei Mismatch) --- */
    if (erwartungen.tag == MOO_DICT) {
        const char* felder[2] = { "tokenizer_version", "arch_version" };
        for (int fi = 0; fi < 2; fi++) {
            MooValue soll = eget(erwartungen, felder[fi]);
            if (soll.tag == MOO_NONE) { moo_release(soll); continue; }
            MooValue ist = eget(ckpt, felder[fi]);
            bool gleich = ckpt_val_gleich(soll, ist);
            if (!gleich) {
                char sb[64], ib[64];
                if (soll.tag == MOO_STRING) snprintf(sb, sizeof(sb), "%s", MV_STR(soll)->chars);
                else if (soll.tag == MOO_NUMBER) snprintf(sb, sizeof(sb), "%.17g", MV_NUM(soll));
                else snprintf(sb, sizeof(sb), "(unbekannt)");
                if (ist.tag == MOO_STRING) snprintf(ib, sizeof(ib), "%s", MV_STR(ist)->chars);
                else if (ist.tag == MOO_NUMBER) snprintf(ib, sizeof(ib), "%.17g", MV_NUM(ist));
                else snprintf(ib, sizeof(ib), "(fehlt)");
                char msg[360];
                snprintf(msg, sizeof(msg), "checkpoint_laden: %s passt nicht — "
                         "der Checkpoint wurde mit \"%s\" erzeugt, aktuell ist "
                         "\"%s\". %s muss identisch sein, sonst zeigen die "
                         "Token-IDs/Gewichte auf andere Symbole.",
                         felder[fi], ib, sb, felder[fi]);
                moo_release(soll); moo_release(ist);
                moo_release(arch); moo_release(ckpt); moo_release(header); fclose(f);
                moo_throw(moo_error(msg));
                return moo_none();
            }
            moo_release(soll); moo_release(ist);
        }
    }

    /* --- Netz aus Architektur rekonstruieren --- */
    MooValue schichten = moo_list_new(MV_LIST(arch)->length);
    bool ok = true;
    for (int32_t i = 0; i < MV_LIST(arch)->length && ok; i++) {
        MooValue e = MV_LIST(arch)->items[i];
        if (e.tag != MOO_DICT) { ok = false; break; }
        MooValue tv = eget(e, "typ");
        const char* typ = (tv.tag == MOO_STRING) ? MV_STR(tv)->chars : "";
        const NNSaveLoadHook* hk = sl_hook_lookup(typ);
        MooValue s = hk ? hk->rebuild(e) : moo_none();
        moo_release(tv);
        if (s.tag != MOO_DICT) { ok = false; moo_release(s); break; }
        moo_list_append(schichten, s);
    }
    if (!ok) {
        moo_release(schichten); moo_release(arch); moo_release(ckpt);
        moo_release(header); fclose(f);
        moo_throw(moo_error("checkpoint_laden: Architektur in der Datei ist kaputt"));
        return moo_none();
    }
    MooValue netz = moo_nn_ki_netz(schichten);
    moo_release(schichten);
    if (netz.tag != MOO_DICT) {
        moo_release(arch); moo_release(ckpt); moo_release(header); fclose(f);
        return moo_none();
    }

    long daten_start = (long)(8 + hlen);

    /* --- Gewichte p0..pN --- */
    MooValue params = moo_nn_parameter(netz);
    ok = (params.tag == MOO_LIST);
    if (ok) {
        MooList* pl = MV_LIST(params);
        for (int32_t i = 0; i < pl->length && ok; i++) {
            char key[24]; snprintf(key, sizeof(key), "p%d", i);
            ok = ckpt_lese_tensor(f, daten_start, header, key, T(pl->items[i]));
        }
    }

    /* --- Optimizer rekonstruieren (m/v/t + Skalare) --- */
    MooValue opt = moo_none();
    MooValue copt = eget(ckpt, "opt");
    if (ok && copt.tag == MOO_DICT && params.tag == MOO_LIST) {
        MooValue artv = eget(copt, "art");
        const char* art = (artv.tag == MOO_STRING) ? MV_STR(artv)->chars : "adam";
        MooValue rn = moo_number(enum_(copt, "rate", 0.01));
        if (strcmp(art, "sgd") == 0) {
            MooValue mo = moo_number(enum_(copt, "momentum", 0.0));
            opt = moo_nn_opt_sgd(params, rn, mo);
            moo_release(mo);
        } else if (strcmp(art, "adamw") == 0) {
            MooValue de = moo_number(enum_(copt, "decay", 0.01));
            opt = moo_nn_opt_adamw(params, rn, de);
            moo_release(de);
        } else {
            opt = moo_nn_opt_adam(params, rn);
        }
        moo_release(rn); moo_release(artv);
        if (opt.tag == MOO_DICT) {
            /* Skalare exakt zuruecksetzen (Bias-Korrektur t ist kritisch!). */
            eset(opt, "t",        moo_number(enum_(copt, "t", 0.0)));
            eset(opt, "rate",     moo_number(enum_(copt, "rate", 0.01)));
            eset(opt, "beta1",    moo_number(enum_(copt, "beta1", 0.9)));
            eset(opt, "beta2",    moo_number(enum_(copt, "beta2", 0.999)));
            eset(opt, "eps",      moo_number(enum_(copt, "eps", 1e-8)));
            eset(opt, "decay",    moo_number(enum_(copt, "decay", 0.0)));
            eset(opt, "momentum", moo_number(enum_(copt, "momentum", 0.0)));
            /* Momente om*/ /*/ov* einlesen. */
            MooValue ml2 = eget(opt, "m");
            if (ml2.tag == MOO_LIST) {
                MooList* mm = MV_LIST(ml2);
                for (int32_t i = 0; i < mm->length && ok; i++) {
                    char key[24]; snprintf(key, sizeof(key), "om%d", i);
                    ok = ckpt_lese_tensor(f, daten_start, header, key, T(mm->items[i]));
                }
            }
            MooValue vl2 = eget(opt, "v");
            if (vl2.tag == MOO_LIST) {
                MooList* vv = MV_LIST(vl2);
                for (int32_t i = 0; i < vv->length && ok; i++) {
                    char key[24]; snprintf(key, sizeof(key), "ov%d", i);
                    ok = ckpt_lese_tensor(f, daten_start, header, key, T(vv->items[i]));
                }
            }
            moo_release(ml2); moo_release(vl2);
        }
    }
    moo_release(copt);
    moo_release(params);
    fclose(f);

    if (!ok) {
        moo_release(opt); moo_release(netz);
        moo_release(arch); moo_release(ckpt); moo_release(header);
        moo_throw(moo_error("checkpoint_laden: Gewichte/Optimizer passen nicht "
                            "zur Architektur (Datei beschaedigt oder Version)"));
        return moo_none();
    }

    /* --- Dropout-Zaehler auf den Schichten setzen (mutierender State) --- */
    MooValue netz_sch = eget(netz, "schichten");
    MooValue dropv = eget(ckpt, "dropout");
    if (netz_sch.tag == MOO_LIST && dropv.tag == MOO_LIST) {
        MooList* nsl = MV_LIST(netz_sch);
        MooList* dl2 = MV_LIST(dropv);
        for (int32_t j = 0; j < dl2->length; j++) {
            if (dl2->items[j].tag != MOO_DICT) continue;
            int idx = (int)enum_(dl2->items[j], "i", -1.0);
            double z = enum_(dl2->items[j], "z", 0.0);
            if (idx >= 0 && idx < nsl->length &&
                ist_schicht_typ(nsl->items[idx], "dropout"))
                eset(nsl->items[idx], "zaehler", moo_number(z));
        }
    }
    moo_release(netz_sch); moo_release(dropv);

    /* --- Zustand-Dict bauen --- */
    MooValue zustand = moo_dict_new();
    eset(zustand, "netz", netz);   /* Ownership -> Dict */
    if (opt.tag == MOO_DICT) eset(zustand, "opt", opt); else moo_release(opt);
    const char* skalar[6] = { "global_schritt", "schritt", "epoche",
                              "tokenizer_version", "arch_version", "metrik" };
    for (int i = 0; i < 6; i++) {
        MooValue v = eget(ckpt, skalar[i]);
        if (v.tag != MOO_NONE) eset(zustand, skalar[i], v); else moo_release(v);
    }
    MooValue dlv = eget(ckpt, "dataloader");
    if (dlv.tag == MOO_DICT) eset(zustand, "dataloader", dlv); else moo_release(dlv);

    moo_release(arch); moo_release(ckpt); moo_release(header);
    return zustand;
}

/* Trailing-Integer (Schritt) direkt vor ".mook"; -1 = nicht rotierbar. */
static long ckpt_step_aus_name(const char* name, const char* praefix) {
    size_t pl = strlen(praefix);
    if (strncmp(name, praefix, pl) != 0) return -1;
    size_t n = strlen(name);
    if (n < 5 || strcmp(name + n - 5, ".mook") != 0) return -1;
    if (strstr(name, "best") != NULL) return -1;   /* best nie loeschen */
    size_t end = n - 5, i = end;
    while (i > 0 && name[i-1] >= '0' && name[i-1] <= '9') i--;
    if (i == end) return -1;                        /* keine Ziffern */
    long step = 0;
    for (size_t j = i; j < end; j++) step = step * 10 + (name[j] - '0');
    return step;
}

/* checkpoint_rotieren(verzeichnis, praefix, behalte): loescht die aeltesten
 * <praefix>*<step>.mook (nach Schritt-Nr sortiert), behaelt die neuesten
 * `behalte`; "*best*"-Dateien bleiben immer. Rueckgabe = Anzahl geloeschter. */
MooValue moo_nn_ckpt_rotieren(MooValue verzeichnis, MooValue praefix, MooValue behalte) {
    if (verzeichnis.tag != MOO_STRING || praefix.tag != MOO_STRING ||
        behalte.tag != MOO_NUMBER) {
        moo_throw(moo_error("checkpoint_rotieren: erwarte (verzeichnis:Text, "
                            "praefix:Text, behalte:Zahl)"));
        return moo_none();
    }
    const char* dir = MV_STR(verzeichnis)->chars;
    const char* prx = MV_STR(praefix)->chars;
    int keep = (int)MV_NUM(behalte);
    if (keep < 0) keep = 0;

    typedef struct { char name[256]; long step; } Eintrag;
    Eintrag* arr = NULL; int count = 0, cap = 0;
    bool leseok = true;

#ifdef _WIN32
    char muster[512];
    snprintf(muster, sizeof(muster), "%s\\%s*", dir, prx);
    WIN32_FIND_DATAA fd;
    HANDLE hf = FindFirstFileA(muster, &fd);
    if (hf == INVALID_HANDLE_VALUE) { leseok = false; }
    else {
        do {
            long step = ckpt_step_aus_name(fd.cFileName, prx);
            if (step < 0) continue;
            if (count == cap) { cap = cap ? cap * 2 : 16;
                arr = (Eintrag*)realloc(arr, (size_t)cap * sizeof(Eintrag)); }
            snprintf(arr[count].name, sizeof(arr[count].name), "%s", fd.cFileName);
            arr[count].step = step; count++;
        } while (FindNextFileA(hf, &fd));
        FindClose(hf);
    }
#else
    DIR* d = opendir(dir);
    if (!d) { leseok = false; }
    else {
        struct dirent* de;
        while ((de = readdir(d)) != NULL) {
            long step = ckpt_step_aus_name(de->d_name, prx);
            if (step < 0) continue;
            if (count == cap) { cap = cap ? cap * 2 : 16;
                arr = (Eintrag*)realloc(arr, (size_t)cap * sizeof(Eintrag)); }
            snprintf(arr[count].name, sizeof(arr[count].name), "%s", de->d_name);
            arr[count].step = step; count++;
        }
        closedir(d);
    }
#endif
    if (!leseok) {
        free(arr);
        char msg[320];
        snprintf(msg, sizeof(msg), "checkpoint_rotieren: Verzeichnis \"%s\" "
                 "nicht lesbar", dir);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    /* Aeltesten (kleinster step) loeschen, bis nur `keep` uebrig sind. */
    int geloescht = 0;
    while (count - geloescht > keep) {
        int mi = -1; long best = 0;
        for (int i = 0; i < count; i++) {
            if (arr[i].step < 0) continue;              /* schon geloescht */
            if (mi < 0 || arr[i].step < best) { mi = i; best = arr[i].step; }
        }
        if (mi < 0) break;
        char full[600];
        snprintf(full, sizeof(full), "%s/%s", dir, arr[mi].name);
        remove(full);
        arr[mi].step = -1;   /* markieren */
        geloescht++;
    }
    free(arr);
    return moo_number((double)geloescht);
}
