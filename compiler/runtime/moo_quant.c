/**
 * moo_quant.c - KI-Q1: rotationsbasierte, kalibrierungsfreie Quant-Primitiven.
 * ============================================================================
 * QUELLEN (Primaerpaper; Zeilenanker aus
 * docs/research/ki-papers/analysis/paper_verify_04_quant_matrix.md):
 *   - QJL (arXiv 2406.03482) Def 3.1, Z.27 / 177-183 / 257-267:
 *     "JL transform followed by sign-bit quantization"; die JL-Matrix ist eine
 *     random GAUSSIAN projection. Der Schaetzer ist ASYMMETRISCH: der Key wird
 *     quantisiert, die Query wird mit DERSELBEN JL-Transform projiziert aber
 *     NICHT quantisiert -> unbiased.
 *   - PolarQuant (arXiv 2502.02617) Z.85-86: random Hadamard matrices als
 *     Preconditioner sind etablierte Praxis (QuaRot/QuIP#-Linie).
 *
 * ABGRENZUNG (verbindlich aus VERIFY-04, Gesamt-Entscheid Punkt 2):
 * Walsh-Hadamard und Gauss-JL sind ZWEI GETRENNTE Primitiven und werden hier
 * bewusst nicht vermischt:
 *   1. hadamard  -> Registry-Op, linear + orthogonal, voll differenzierbar,
 *                   backward in moo_autograd.c, Gradcheck automatisch (B2).
 *   2. QJL       -> KEIN Registry-Op. sign() hat keinen sinnvollen Gradienten;
 *                   ein Registry-Eintrag ohne echtes backward wuerde den
 *                   B2-Vertrag aushoehlen (bzw. einen nicht verifizierbaren
 *                   Fake-STE-Gradienten erzwingen). QJL ist inferenz-only und
 *                   hat stattdessen eigene numerische Gates in
 *                   tests/test_quant_asan.c (Determinismus, Unbiasedness,
 *                   Fehlerschranke).
 *
 * EHRLICHE EINSCHRAENKUNG ZUR BIT-PACKUNG:
 * Die Sign-Bits werden in einem f32-Traeger gehalten (16 Bit pro float; 16-Bit-
 * Werte sind in der f32-Mantisse exakt darstellbar, also verlustfrei). Das ist
 * eine LOGISCHE 1-Bit-Repraesentation und spart in Q1 noch KEINEN echten
 * Speicher - der Traeger ist breiter als die Nutzlast. Die reale Byte-Packung
 * gehoert in den KV-Cache-Store (KI-Q2), wo sie tatsaechlich Speicher spart.
 * Deshalb wird in Q1 bewusst KEINE Speicherreduktions-Zahl behauptet. Die
 * Paper-Zahlen (QJL >5x, PolarQuant >x4.2) gelten fuer grosse LLMs auf einer
 * A100 und sind KEIN Zielwert fuer moo-Toy-Modelle.
 *
 * UB-POLICY (Memory ub-arithmetik-policy): jede Hash-/Seed-Arithmetik laeuft
 * unsigned mit u/ULL-Suffixen; kein signed overflow, keine impliziten Casts an
 * unkontrollierten Raendern.
 * ============================================================================
 */
#include "moo_runtime.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Hergeleitete Estimator-Konstante: fuer u ~ N(0,1) ist E[|u|] = sqrt(2/pi),
 * also <q,k> = sqrt(pi/2) * ||k|| * E[sign(<s,k>) * <s,q>]. NICHT gefittet. */
#define QJL_SKALIERUNG 1.2533141373155003  /* sqrt(pi/2) */

/* Bits pro f32-Traegerwort. 16 Bit -> Werte 0..65535, in der f32-Mantisse
 * (24 Bit) exakt darstellbar, also verlustfrei speicherbar. */
#define QJL_BITS_PRO_WORT 16

/* ============================================================
 * Deterministische, index-adressierbare Zufallsquelle
 * ------------------------------------------------------------
 * Bewusst KEIN laufender Zustand, sondern eine reine Funktion von
 * (seed, index): dadurch ist die erzeugte JL-Matrix unabhaengig von der
 * Schleifenreihenfolge und in Quantisierer und Projektion garantiert
 * identisch - das ist die zentrale Determinismus-Invariante des Verfahrens.
 * ============================================================ */
static inline uint64_t quant_hash(uint64_t seed, uint64_t idx) {
    /* Absichtliches Wrap-around (unsigned = definiert, UB-Policy Regel 1). */
    uint64_t z = seed * 0x9E3779B97F4A7C15ULL
               + idx * 0xBF58476D1CE4E5B9ULL
               + 0x165667B19E3779F9ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Gleichverteilt in (0,1] - die 0 wird ausgeschlossen, weil Box-Muller
 * log(u) zieht und log(0) = -inf waere. */
static inline double quant_uniform(uint64_t h) {
    return ((double)(uint32_t)(h >> 40) + 1.0) / 16777216.0;
}

/* Standardnormale Zufallszahl via Box-Muller aus zwei Hash-Ziehungen.
 * Deterministisch aus (seed, idx) - dieselbe Position liefert immer denselben
 * Wert, unabhaengig davon wann/wie oft sie gezogen wird. */
static double quant_gauss(uint64_t seed, uint64_t idx) {
    double u1 = quant_uniform(quant_hash(seed, 2u * idx));
    double u2 = quant_uniform(quant_hash(seed, 2u * idx + 1u));
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

/* Walsh-Hadamard-Kern: lebt in moo_tensor_ops.c (moo_quant_ist_zweierpotenz /
 * moo_quant_wht_zeile / moo_quant_vorzeichen + Registry-Op moo_tensor_hadamard).
 * Grund: moo_autograd.c (bw_hadamard) und die Op-Registry referenzieren die
 * Symbole - laegen sie hier, muesste JEDER Harness, der moo_autograd.c oder
 * moo_tensor_ops.c linkt, zusaetzlich moo_quant.c mitziehen (Link-Matrix,
 * QA1-Lehre in tests/run_sanitize.sh). moo_quant.c bleibt dadurch QJL-only. */

/* ============================================================
 * Lokale Helfer
 * ============================================================ */

static MooTensor* quant_tensor(MooValue v, const char* wo) {
    if (v.tag != MOO_TENSOR) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: erwartet einen Tensor", wo);
        moo_throw(moo_error(msg));
        return NULL;
    }
    MooTensor* t = MV_TENSOR(v);
    moo_tensor_f32_sichern(t);
    return t;
}

static MooValue quant_wrap(MooTensor* t) {
    MooValue v;
    v.tag = MOO_TENSOR;
    moo_val_set_ptr(&v, t);
    return v;
}

/* KI-M2c-Vertragskonsistenz: QJL ist Inferenz-Mechanik. Beim Training wuerde
 * der Tape-lose Pfad Gradienten STILL kappen - deshalb hart werfen, exakt wie
 * der KV-Cache (moo_nn.c fw_attention, att["cache"]). */
static bool quant_nur_inferenz(const char* wo) {
    if (moo_ag_ist_an()) {
        char msg[200];
        snprintf(msg, sizeof(msg), "%s: QJL geht nur beim Generieren "
                 "(autograd_aus) - beim Training den f32-Pfad benutzen", wo);
        moo_throw(moo_error(msg));
        return false;
    }
    return true;
}

/* Ganzzahliges, nichtnegatives Zahl-Argument pruefen (UB-Policy Regel 2:
 * erst validieren, dann casten). */
static bool quant_ganzzahl(MooValue v, const char* wo, const char* name,
                           double max, int64_t* aus) {
    if (v.tag != MOO_NUMBER) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: %s muss eine Zahl sein", wo, name);
        moo_throw(moo_error(msg));
        return false;
    }
    double d = MV_NUM(v);
    if (!(d >= 0.0) || d > max || floor(d) != d) {
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "%s: %s muss eine ganze Zahl zwischen 0 und %.0f sein",
                 wo, name, max);
        moo_throw(moo_error(msg));
        return false;
    }
    *aus = (int64_t)d;
    return true;
}

/* Registry-Op "hadamard": Implementierung in moo_tensor_ops.c (siehe
 * Umzugs-Begruendung oben). Herleitung der Orthogonalitaet und der
 * Incoherence-Processing-Zweck sind dort dokumentiert. */

/* ============================================================
 * QJL: 1-Bit Sign-JL (QJL Def 3.1) - INFERENZ-ONLY, kein Registry-Op
 * ============================================================ */

/* Erzeugt die JL-Matrix S (m x d) deterministisch aus dem Seed.
 * Sie wird bewusst NICHT gespeichert, sondern bei jedem Aufruf neu erzeugt:
 * das ist der data-oblivious-Charakter des Verfahrens (QJL:158, 193) und der
 * Grund, warum es ohne Kalibrierdatensatz auskommt. */
static float* quant_jl_matrix(uint64_t seed, int64_t m, int64_t d) {
    float* S = (float*)malloc((size_t)(m * d) * sizeof(float));
    if (!S) return NULL;
    for (int64_t i = 0; i < m; i++)
        for (int64_t j = 0; j < d; j++)
            S[i * d + j] = (float)quant_gauss(seed, (uint64_t)(i * d + j));
    return S;
}

/* Gemeinsame Form-/Argumentpruefung fuer beide QJL-Richtungen. */
static bool quant_jl_args(MooValue tv, MooValue mv, MooValue seedv,
                          const char* wo, MooTensor** t_aus,
                          int64_t* m_aus, int64_t* seed_aus,
                          int64_t* zeilen_aus, int64_t* d_aus) {
    MooTensor* t = quant_tensor(tv, wo);
    if (!t) return false;
    if (!quant_nur_inferenz(wo)) return false;
    if (t->ndim != 2) {
        char msg[180];
        snprintf(msg, sizeof(msg),
                 "%s: erwartet einen 2D-Tensor [vektoren, dimension]", wo);
        moo_throw(moo_error(msg));
        return false;
    }
    if (!quant_ganzzahl(mv, wo, "die Projektionsbreite m", 65536.0, m_aus))
        return false;
    if (*m_aus < 1) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: die Projektionsbreite m muss >= 1 sein", wo);
        moo_throw(moo_error(msg));
        return false;
    }
    if (!quant_ganzzahl(seedv, wo, "der Seed", 9007199254740992.0, seed_aus))
        return false;
    *t_aus = t;
    *zeilen_aus = t->shape[0];
    *d_aus = t->shape[1];
    return true;
}

/**
 * moo_quant_sign_jl(keys, m, seed) - die QUANTISIERENDE Seite.
 * Q(k) = sign(S*k) in {-1,+1}^m, plus die Norm ||k|| je Vektor.
 *
 * Warum die Norm mitgespeichert wird: der Schaetzer rekonstruiert
 * <q,k> = sqrt(pi/2) * ||k|| * E[...]; die Sign-Bits allein tragen nur die
 * RICHTUNG von k, nicht seine Laenge. Das ist EIN f32 pro Vektor - und damit
 * immer noch der Kern des QJL-Arguments (QJL:191-193): es gibt keinen
 * Zero-Point und keine Scale PRO BLOCK, wie sie klassische Quantisierung
 * braucht und die dort +1 bis +2 Bit pro Zahl kosten.
 *
 * Rueckgabe: Dictionary mit "bits", "normen", "m", "d", "seed".
 */
MooValue moo_quant_sign_jl(MooValue kv, MooValue mv, MooValue seedv) {
    MooTensor* k;
    int64_t m, seed, zeilen, d;
    if (!quant_jl_args(kv, mv, seedv, "sign_jl", &k, &m, &seed, &zeilen, &d))
        return moo_none();

    int64_t worte = (m + QJL_BITS_PRO_WORT - 1) / QJL_BITS_PRO_WORT;
    int32_t bshape[2] = { (int32_t)zeilen, (int32_t)worte };
    int32_t nshape[1] = { (int32_t)zeilen };
    MooTensor* bits = moo_tensor_raw(2, bshape);
    MooTensor* normen = bits ? moo_tensor_raw(1, nshape) : NULL;
    float* S = normen ? quant_jl_matrix((uint64_t)seed, m, d) : NULL;
    if (!S) {
        if (bits) moo_release(quant_wrap(bits));
        if (normen) moo_release(quant_wrap(normen));
        moo_throw(moo_error("sign_jl: Speicher voll"));
        return moo_none();
    }

    for (int64_t r = 0; r < zeilen; r++) {
        const float* kr = k->data + r * d;

        double nq = 0.0;
        for (int64_t j = 0; j < d; j++) nq += (double)kr[j] * (double)kr[j];
        normen->data[r] = (float)sqrt(nq);

        for (int64_t i = 0; i < m; i++) {
            double p = 0.0;
            const float* Si = S + i * d;
            for (int64_t j = 0; j < d; j++) p += (double)Si[j] * (double)kr[j];
            /* Bit 1 = negatives Vorzeichen. Die Null wird zu +1 gezaehlt;
             * bei stetigen Eingaben ist <s,k> == 0 ein Nullmengen-Ereignis. */
            if (p < 0.0) {
                int64_t w = i / QJL_BITS_PRO_WORT;
                uint32_t b = (uint32_t)(i % QJL_BITS_PRO_WORT);
                uint32_t cur = (uint32_t)bits->data[r * worte + w];
                bits->data[r * worte + w] = (float)(cur | (1u << b));
            }
        }
    }
    free(S);

    MooValue dict = moo_dict_new();
    moo_dict_set(dict, moo_string_new("bits"), quant_wrap(bits));
    moo_dict_set(dict, moo_string_new("normen"), quant_wrap(normen));
    moo_dict_set(dict, moo_string_new("m"), moo_number((double)m));
    moo_dict_set(dict, moo_string_new("d"), moo_number((double)d));
    moo_dict_set(dict, moo_string_new("seed"), moo_number((double)seed));
    return dict;
}

/**
 * moo_quant_jl_projektion(queries, m, seed) - die NICHT quantisierende Seite.
 * Liefert S*q in voller Praezision.
 *
 * Das ist der ASYMMETRISCHE Kern von QJL (QJL:262-263, 284-285): wuerde man
 * auch die Query mit sign() quantisieren, waere der Schaetzer VERZERRT. Nur
 * weil eine Seite unquantisiert bleibt, ist das Produkt erwartungstreu.
 * Fuer den KV-Cache passt das genau: die Keys liegen dauerhaft im Speicher
 * (lohnt zu quantisieren), die Query existiert nur fuer einen Schritt.
 */
MooValue moo_quant_jl_projektion(MooValue qv, MooValue mv, MooValue seedv) {
    MooTensor* q;
    int64_t m, seed, zeilen, d;
    if (!quant_jl_args(qv, mv, seedv, "jl_projektion", &q, &m, &seed, &zeilen, &d))
        return moo_none();

    int32_t oshape[2] = { (int32_t)zeilen, (int32_t)m };
    MooTensor* out = moo_tensor_raw(2, oshape);
    float* S = out ? quant_jl_matrix((uint64_t)seed, m, d) : NULL;
    if (!S) {
        if (out) moo_release(quant_wrap(out));
        moo_throw(moo_error("jl_projektion: Speicher voll"));
        return moo_none();
    }

    for (int64_t r = 0; r < zeilen; r++) {
        const float* qr = q->data + r * d;
        for (int64_t i = 0; i < m; i++) {
            double p = 0.0;
            const float* Si = S + i * d;
            for (int64_t j = 0; j < d; j++) p += (double)Si[j] * (double)qr[j];
            out->data[r * m + i] = (float)p;
        }
    }
    free(S);
    return quant_wrap(out);
}

/**
 * moo_quant_sign_jl_skalarprodukt(q_projektion, paket) - der SCHAETZER.
 *
 *   <q,k> ~ sqrt(pi/2) * ||k|| * (1/m) * SUMME_i sign(<s_i,k>) * <s_i,q>
 *
 * HERLEITUNG (deshalb ist die Konstante pruefbar und nicht gefittet):
 * Sei s ~ N(0, I_d) und u = <s, k/||k||> ~ N(0,1). Zerlegt man <s,q> in den
 * Anteil entlang k und den dazu orthogonalen Rest, faellt der orthogonale
 * Anteil im Erwartungswert weg (unabhaengig von u, Mittel 0). Es bleibt
 *   E[sign(<s,k>) * <s,q>] = E[|u|] * <k/||k||, q> = sqrt(2/pi) * <q,k>/||k||.
 * Aufloesen nach <q,k> liefert exakt den Faktor sqrt(pi/2) * ||k||.
 *
 * Der Schaetzer ist damit erwartungstreu FUER JEDES m; groesseres m senkt
 * nur die Varianz (~1/sqrt(m)), nicht einen Bias. Genau das prueft das Gate
 * in test_quant_asan.c: Mittelung ueber viele Seeds muss gegen den echten
 * Wert konvergieren, nicht gegen einen daneben liegenden.
 *
 * Rueckgabe: Tensor [query_zeilen, key_zeilen].
 */
MooValue moo_quant_sign_jl_skalarprodukt(MooValue qprojv, MooValue paketv) {
    MooTensor* qp = quant_tensor(qprojv, "sign_jl_skalarprodukt");
    if (!qp) return moo_none();
    if (!quant_nur_inferenz("sign_jl_skalarprodukt")) return moo_none();
    if (qp->ndim != 2) {
        moo_throw(moo_error("sign_jl_skalarprodukt: die projizierte Query muss "
                            "2D sein [queries, m]"));
        return moo_none();
    }
    if (paketv.tag != MOO_DICT) {
        moo_throw(moo_error("sign_jl_skalarprodukt: erwartet das Paket aus "
                            "sign_jl als zweites Argument"));
        return moo_none();
    }

    /* OWNERSHIP: moo_dict_get (moo_dict.c:101-116) konsumiert den Key UND
     * liefert eine eigene, retainte Referenz auf den Wert. Jede der drei
     * Referenzen muss also genau einmal wieder freigegeben werden - auch auf
     * jedem Fehlerpfad. (moo_release auf MOO_NUMBER ist ein no-op.) */
    MooValue bitsv = moo_dict_get(paketv, moo_string_new("bits"));
    MooValue normenv = moo_dict_get(paketv, moo_string_new("normen"));
    MooValue mv = moo_dict_get(paketv, moo_string_new("m"));
    if (bitsv.tag != MOO_TENSOR || normenv.tag != MOO_TENSOR ||
        mv.tag != MOO_NUMBER) {
        moo_release(bitsv); moo_release(normenv); moo_release(mv);
        moo_throw(moo_error("sign_jl_skalarprodukt: das Paket ist unvollstaendig "
                            "(bits/normen/m fehlen)"));
        return moo_none();
    }
    MooTensor* bits = MV_TENSOR(bitsv);
    MooTensor* normen = MV_TENSOR(normenv);
    moo_tensor_f32_sichern(bits);
    moo_tensor_f32_sichern(normen);

    int64_t m = (int64_t)MV_NUM(mv);
    if (qp->shape[1] != (int32_t)m) {
        char msg[220];
        snprintf(msg, sizeof(msg),
                 "sign_jl_skalarprodukt: die Query wurde mit m=%d projiziert, "
                 "die Keys aber mit m=%lld quantisiert - beide Seiten muessen "
                 "dieselbe JL-Transform benutzen",
                 (int)qp->shape[1], (long long)m);
        moo_release(bitsv); moo_release(normenv); moo_release(mv);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    int64_t qz = qp->shape[0];
    int64_t kz = bits->shape[0];
    int64_t worte = bits->shape[1];
    int32_t oshape[2] = { (int32_t)qz, (int32_t)kz };
    MooTensor* out = moo_tensor_raw(2, oshape);
    if (!out) {
        moo_release(bitsv); moo_release(normenv); moo_release(mv);
        return moo_none();
    }

    double vorfaktor = QJL_SKALIERUNG / (double)m;
    for (int64_t a = 0; a < qz; a++) {
        const float* qr = qp->data + a * m;
        for (int64_t b = 0; b < kz; b++) {
            const float* br = bits->data + b * worte;
            double s = 0.0;
            for (int64_t i = 0; i < m; i++) {
                uint32_t wort = (uint32_t)br[i / QJL_BITS_PRO_WORT];
                uint32_t bit = (wort >> (uint32_t)(i % QJL_BITS_PRO_WORT)) & 1u;
                s += bit ? -(double)qr[i] : (double)qr[i];
            }
            out->data[a * kz + b] = (float)(vorfaktor * (double)normen->data[b] * s);
        }
    }
    moo_release(bitsv); moo_release(normenv); moo_release(mv);
    return quant_wrap(out);
}
