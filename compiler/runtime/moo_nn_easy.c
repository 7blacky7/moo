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
        if (!(ist_schicht_typ(s, "dicht") || ist_schicht_typ(s, "dropout") ||
              ist_schicht_typ(s, "layernorm") || ist_schicht_typ(s, "embedding"))) {
            char msg[160];
            snprintf(msg, sizeof(msg), "ki_netz: Eintrag %d ist keine Schicht "
                     "(erwarte schicht_dicht/dropout/layernorm/embedding)", i);
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

/* Batch-Tensor [b, sp] aus Zeilen von src (via Permutation) — ohne grad. */
static MooValue batch_zeilen(MooTensor* src, const int32_t* perm,
                             int64_t start, int32_t b, int32_t sp) {
    int32_t shape[2] = { b, sp };
    MooTensor* t = moo_tensor_raw(2, shape);
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
    if (xt->ndim != 2) {
        moo_release(schichten);
        moo_throw(moo_error("trainiere: die Eingaben muessen 2D sein "
                            "[Beispiele, Werte] — eine Zeile pro Beispiel"));
        return moo_none();
    }
    int64_t n = xt->shape[0];
    int32_t din = xt->shape[1];
    int32_t erwartet = erste_dicht_ein(schichten);
    if (erwartet > 0 && din != erwartet) {
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

    for (int64_t ep = 0; ep < epochen && !fehler; ep++) {
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

/* Architektur-JSON einer Schicht anhaengen (kontrollierte Werte, die
 * Aktivierungs-Strings kommen aus akt_anwenden-Whitelist -> kein Escaping). */
static bool arch_schicht(Buf* b, MooValue s) {
    if (ist_schicht_typ(s, "dicht")) {
        MooValue w = eget(s, "w");
        MooValue a = eget(s, "aktivierung");
        if (w.tag != MOO_TENSOR) { moo_release(w); moo_release(a); return false; }
        buf_add(b, "{\"typ\":\"dicht\",\"ein\":%d,\"aus\":%d,\"aktivierung\":\"%s\"}",
                T(w)->shape[0], T(w)->shape[1],
                (a.tag == MOO_STRING) ? MV_STR(a)->chars : "keine");
        moo_release(w); moo_release(a);
        return true;
    }
    if (ist_schicht_typ(s, "dropout")) {
        buf_add(b, "{\"typ\":\"dropout\",\"rate\":%g}",
                enum_(s, "rate", 0.0));
        return true;
    }
    if (ist_schicht_typ(s, "layernorm")) {
        MooValue g = eget(s, "gamma");
        if (g.tag != MOO_TENSOR) { moo_release(g); return false; }
        buf_add(b, "{\"typ\":\"layernorm\",\"dim\":%d}", T(g)->shape[1]);
        moo_release(g);
        return true;
    }
    if (ist_schicht_typ(s, "embedding")) {
        MooValue w = eget(s, "w");
        if (w.tag != MOO_TENSOR) { moo_release(w); return false; }
        buf_add(b, "{\"typ\":\"embedding\",\"vokab\":%d,\"dim\":%d}",
                T(w)->shape[0], T(w)->shape[1]);
        moo_release(w);
        return true;
    }
    return false;
}

/* speichern(netz, pfad): safetensors-Datei schreiben. Rueckgabe none. */
MooValue moo_nn_speichern(MooValue netz, MooValue pfad) {
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
    uint64_t off = 0;
    for (int32_t i = 0; i < pl->length; i++) {
        MooTensor* p = T(pl->items[i]);
        uint64_t bytes = (uint64_t)p->size * 4u;
        buf_add(&h, ",\"p%d\":{\"dtype\":\"F32\",\"shape\":[", i);
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
        ok = fwrite(p->data, sizeof(float), (size_t)p->size, f)
             == (size_t)p->size;
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
 * Mini-Modelle. Nur dtype F32 (moo-Tensoren sind f32); andere dtypes
 * werfen erklaerend. __metadata__ wird uebersprungen. */
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
        if (!(dt.tag == MOO_STRING && strcmp(MV_STR(dt)->chars, "F32") == 0)) {
            snprintf(fmsg, sizeof(fmsg), "safetensors_laden: Tensor \"%s\" "
                     "hat dtype %s — moo kann bisher nur F32 (fp16/bf16-"
                     "Konvertierung = Backlog)", MV_STR(k)->chars,
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
            e_ok = (b - a) == (double)elems * 4.0;
            if (e_ok) {
                t = moo_tensor_raw(nd, shape);
                e_ok = t && fseek(f, daten_start + (long)a, SEEK_SET) == 0 &&
                       fread(t->data, sizeof(float), (size_t)elems, f)
                       == (size_t)elems;
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
        MooValue s = moo_none();
        if (strcmp(typ, "dicht") == 0) {
            MooValue akt = eget(e, "aktivierung");
            s = moo_nn_schicht_dicht(moo_number(enum_(e, "ein", 0)),
                                     moo_number(enum_(e, "aus", 0)),
                                     akt, moo_none());
            moo_release(akt);
        } else if (strcmp(typ, "dropout") == 0) {
            s = moo_nn_schicht_dropout(moo_number(enum_(e, "rate", 0.0)));
        } else if (strcmp(typ, "layernorm") == 0) {
            s = moo_nn_schicht_layernorm(moo_number(enum_(e, "dim", 0)));
        } else if (strcmp(typ, "embedding") == 0) {
            s = moo_nn_schicht_embedding(moo_number(enum_(e, "vokab", 0)),
                                         moo_number(enum_(e, "dim", 0)),
                                         moo_none());
        }
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
