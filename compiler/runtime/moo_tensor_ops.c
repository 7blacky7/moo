/**
 * moo_tensor_ops.c — Tensor-Ops + Op-Registry (Plan-014 A2).
 * ============================================================================
 * ERWEITERBARKEITS-VERTRAG: Neuer Op = 1 Funktion + 1 Registry-Eintrag,
 * KEIN Umbau. B2-Regel: Ops mit backward brauchen eine Gradient-Check-Zeile
 * im Harness, sonst wird der Registry-Eintrag im Review abgelehnt.
 *
 * TENSOR-KONVENTION (moo_runtime.h): Args borrowed, Rueckgabe +1 owning.
 * BROADCASTING: NumPy-Regeln (rechts ausgerichtet, Dim kompatibel wenn
 *   gleich oder 1). Fastpaths: gleiche Form (flacher Loop, auto-vektorisiert
 *   via MOO_OPTIMIZE_O3 wie moo_ops.c) und Zeilen-Broadcast [1,c] (Bias!).
 * DIV: IEEE-754 (x/0 = ±inf, 0/0 = nan) — KEIN throw. ML-ueblich; wer es
 *   prueft, nutzt die Werte (dokumentierter Vertrag, kein Versehen).
 * REDUKTIONEN mit Achse: keepdims — [r,c] --achse 0--> [1,c],
 *   --achse 1--> [r,1]; achse -1 = alles --> [1]. Damit broadcastet das
 *   Ergebnis direkt zurueck (Softmax-/Normierungs-Muster).
 * UB-POLICY: Groessen int64_t (Shapes sind einzeln <= INT32_MAX, Produkte
 *   wurden bereits in moo_tensor_raw checked multipliziert).
 * ============================================================================
 */
#include "moo_runtime.h"
#include "moo_ki_gpu_api.h"   // KIP-G4c Stufe 1: residenter matmul_res-Pfad

#if defined(_MSC_VER)
#define MOO_OPTIMIZE_O3
#else
#define MOO_OPTIMIZE_O3 __attribute__((optimize("O3")))
#endif

// ============================================================
// Gemeinsame Helfer
// ============================================================

static MooTensor* expect_t(MooValue v, const char* wo) {
    if (v.tag != MOO_TENSOR) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: das ist kein Tensor", wo);
        moo_throw(moo_error(msg));
        return NULL;
    }
    MooTensor* t = MV_TENSOR(v);
    moo_tensor_f32_sichern(t);   // KIP-D1 Eintrittspunkt (D0 §4.1): DER Trichter aller 26 Registry-Ops -> f32-data garantiert
    return t;
}

static MooValue wrap_t(MooTensor* t) {
    MooValue v;
    if (!t) return moo_none();
    v.tag = MOO_TENSOR;
    moo_val_set_ptr(&v, t);
    return v;
}

static void shape_str(const MooTensor* t, char* buf, size_t n) {
    int off = snprintf(buf, n, "[");
    for (int32_t d = 0; d < t->ndim; d++)
        off += snprintf(buf + off, n - (size_t)off, "%s%d", d ? "x" : "", t->shape[d]);
    snprintf(buf + off, n - (size_t)off, "]");
}

// ============================================================
// Elementwise binaer mit Broadcasting
// ============================================================

typedef float (*FOp)(float, float);
static inline float f_add(float a, float b) { return a + b; }
static inline float f_sub(float a, float b) { return a - b; }
static inline float f_mul(float a, float b) { return a * b; }
static inline float f_div(float a, float b) { return a / b; }  // IEEE: /0 -> inf/nan

// NumPy-Broadcast: Shapes rechts ausrichten; Dim ok wenn gleich oder 1.
// true + out_shape/out_ndim bei Erfolg; wirft + false sonst.
static bool broadcast_shape(const MooTensor* a, const MooTensor* b,
                            int32_t* out_shape, int32_t* out_ndim,
                            const char* wo) {
    int32_t nd = a->ndim > b->ndim ? a->ndim : b->ndim;
    for (int32_t d = 0; d < nd; d++) {
        int32_t ad = (d >= nd - a->ndim) ? a->shape[d - (nd - a->ndim)] : 1;
        int32_t bd = (d >= nd - b->ndim) ? b->shape[d - (nd - b->ndim)] : 1;
        if (ad != bd && ad != 1 && bd != 1) {
            char sa[64], sb[64], msg[256];
            shape_str(a, sa, sizeof(sa)); shape_str(b, sb, sizeof(sb));
            snprintf(msg, sizeof(msg),
                     "%s: Formen %s und %s passen nicht zusammen — gleiche Form "
                     "oder Broadcast (1er-Dimensionen) noetig", wo, sa, sb);
            moo_throw(moo_error(msg));
            return false;
        }
        out_shape[d] = ad > bd ? ad : bd;
    }
    *out_ndim = nd;
    return true;
}

// Fastpath: identische Form — flacher Loop, auto-vektorisierbar.
MOO_OPTIMIZE_O3
static void ew_same(const float* a, const float* b, float* o, int64_t n, FOp f) {
    for (int64_t i = 0; i < n; i++) o[i] = f(a[i], b[i]);
}

// Fastpath: b ist Zeilenvektor [1,c] (oder [c]) gegen a [r,c] — Bias-Muster.
MOO_OPTIMIZE_O3
static void ew_row(const float* a, const float* b, float* o,
                   int64_t rows, int64_t cols, FOp f) {
    for (int64_t r = 0; r < rows; r++) {
        const float* ar = a + r * cols;
        float* orow = o + r * cols;
        for (int64_t c = 0; c < cols; c++) orow[c] = f(ar[c], b[c]);
    }
}

// Generischer Broadcast-Pfad: Iteration ueber Ergebnis-Indizes; Input-
// Strides sind 0 in gebroadcasteten Dimensionen.
static void ew_generic(const MooTensor* a, const MooTensor* b, MooTensor* out, FOp f) {
    int32_t nd = out->ndim;
    int64_t sa[MOO_TENSOR_MAX_DIMS], sb[MOO_TENSOR_MAX_DIMS];
    for (int32_t d = 0; d < nd; d++) {
        int32_t ad = (d >= nd - a->ndim) ? a->shape[d - (nd - a->ndim)] : 1;
        int32_t bd = (d >= nd - b->ndim) ? b->shape[d - (nd - b->ndim)] : 1;
        sa[d] = (ad == 1) ? 0 : a->strides[d - (nd - a->ndim)];
        sb[d] = (bd == 1) ? 0 : b->strides[d - (nd - b->ndim)];
    }
    int32_t idx[MOO_TENSOR_MAX_DIMS] = {0};
    int64_t oa = 0, ob = 0;
    for (int64_t i = 0; i < out->size; i++) {
        out->data[i] = f(a->data[oa], b->data[ob]);
        // Indizes inkrementieren (rechts nach links)
        for (int32_t d = nd - 1; d >= 0; d--) {
            idx[d]++;
            oa += sa[d]; ob += sb[d];
            if (idx[d] < out->shape[d]) break;
            oa -= (int64_t)idx[d] * sa[d]; ob -= (int64_t)idx[d] * sb[d];
            idx[d] = 0;
        }
    }
}

static MooValue ew_op(MooValue av, MooValue bv, FOp f, const char* wo, const char* ag) {
    MooTensor* a = expect_t(av, wo); if (!a) return moo_none();
    MooTensor* b = expect_t(bv, wo); if (!b) return moo_none();
    int32_t oshape[MOO_TENSOR_MAX_DIMS]; int32_t ond;
    if (!broadcast_shape(a, b, oshape, &ond, wo)) return moo_none();
    MooTensor* out = moo_tensor_raw(ond, oshape);
    if (!out) return moo_none();

    if (a->ndim == b->ndim && memcmp(a->shape, b->shape, sizeof(int32_t) * (size_t)a->ndim) == 0) {
        /* GPU2: elementweise gleiche Shapes — Vulkan versucht, CPU faengt
         * auf (Schwellen + Verfuegbarkeit entscheidet moo_ki_gpu.c). */
        int32_t gop = (f == f_add) ? 0 : (f == f_sub) ? 1
                    : (f == f_mul) ? 2 : (f == f_div) ? 3 : -1;
        if (gop < 0 || !moo_ki_gpu_ew(gop, a->data, b->data, out->data, out->size))
            ew_same(a->data, b->data, out->data, out->size, f);
    } else if (out->ndim == 2 && b->size == out->shape[1] &&
               a->ndim == 2 && a->shape[0] == out->shape[0] && a->shape[1] == out->shape[1]) {
        ew_row(a->data, b->data, out->data, out->shape[0], out->shape[1], f);  // Bias
    } else {
        ew_generic(a, b, out, f);
    }
    MooValue ret = wrap_t(out);
    moo_ag_record(ag, av, bv, moo_none(), ret);   // B1: Tape
    return ret;
}

MooValue moo_tensor_add(MooValue a, MooValue b) { return ew_op(a, b, f_add, "tensor_plus", "add"); }
MooValue moo_tensor_sub(MooValue a, MooValue b) { return ew_op(a, b, f_sub, "tensor_minus", "sub"); }
MooValue moo_tensor_mul(MooValue a, MooValue b) { return ew_op(a, b, f_mul, "tensor_mal", "mul"); }
MooValue moo_tensor_div(MooValue a, MooValue b) { return ew_op(a, b, f_div, "tensor_geteilt", "div"); }

// Skalar-Varianten: kein Alloc fuer die Zahl, flacher Loop.
MOO_OPTIMIZE_O3
static MooValue ews_op(MooValue av, MooValue zahl, FOp f, const char* wo, const char* ag) {
    MooTensor* a = expect_t(av, wo); if (!a) return moo_none();
    if (zahl.tag != MOO_NUMBER) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: erwarte eine Zahl als zweites Argument", wo);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    MooTensor* out = moo_tensor_raw(a->ndim, a->shape);
    if (!out) return moo_none();
    float s = (float)MV_NUM(zahl);
    for (int64_t i = 0; i < a->size; i++) out->data[i] = f(a->data[i], s);
    MooValue ret = wrap_t(out);
    moo_ag_record(ag, av, moo_none(), zahl, ret);   // B1: Tape (skalar im Node)
    return ret;
}

MooValue moo_tensor_adds(MooValue a, MooValue z) { return ews_op(a, z, f_add, "tensor_plus", "adds"); }
MooValue moo_tensor_subs(MooValue a, MooValue z) { return ews_op(a, z, f_sub, "tensor_minus", "subs"); }
MooValue moo_tensor_muls(MooValue a, MooValue z) { return ews_op(a, z, f_mul, "tensor_mal", "muls"); }
MooValue moo_tensor_divs(MooValue a, MooValue z) { return ews_op(a, z, f_div, "tensor_geteilt", "divs"); }

// ============================================================
// MatMul — 2D @ 2D, i-k-j-Reihenfolge (cache-freundlich: innerste Schleife
// laeuft ueber zusammenhaengende b- und out-Zeilen -> auto-vektorisierbar).
// ============================================================
MOO_OPTIMIZE_O3
static void matmul_ikj(const float* a, const float* b, float* o,
                       int64_t m, int64_t k, int64_t n) {
    // o ist calloc'd (0.0f) — akkumulieren erlaubt.
    for (int64_t i = 0; i < m; i++) {
        const float* ai = a + i * k;
        float* oi = o + i * n;
        for (int64_t kk = 0; kk < k; kk++) {
            float av = ai[kk];
            const float* bk = b + kk * n;
            for (int64_t j = 0; j < n; j++) oi[j] += av * bk[j];
        }
    }
}

MooValue moo_tensor_matmul(MooValue av, MooValue bv) {
    MooTensor* a = expect_t(av, "tensor_matmul"); if (!a) return moo_none();
    MooTensor* b = expect_t(bv, "tensor_matmul"); if (!b) return moo_none();
    if (a->ndim != 2 || b->ndim != 2) {
        moo_throw(moo_error("tensor_matmul: beide Tensoren muessen 2-dimensional sein "
                            "(Matrix @ Matrix) — nutze umformen() fuer Vektoren"));
        return moo_none();
    }
    if (a->shape[1] != b->shape[0]) {
        char sa[64], sb[64], msg[256];
        shape_str(a, sa, sizeof(sa)); shape_str(b, sb, sizeof(sb));
        snprintf(msg, sizeof(msg),
                 "tensor_matmul: %s @ %s geht nicht — die innere Dimension muss gleich "
                 "sein (%d != %d)", sa, sb, a->shape[1], b->shape[0]);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int32_t oshape[2] = { a->shape[0], b->shape[1] };
    MooTensor* out = moo_tensor_raw(2, oshape);
    if (!out) return moo_none();
    int64_t mm = a->shape[0], kk = a->shape[1], nn = b->shape[1];
    bool done = false;
    /* KIP-G4c Stufe 1 (docs/kip/G4c-production-wiring-plan.md): residenter
     * Pfad zuerst versuchen (gleiche Groessen-Schwelle wie der bestehende
     * non-resident GPU2-Hook, M*K*N>=2^24). moo_tensor_nach_gpu ist bereits
     * idempotent + opportunistisch (silent no-op ohne Vulkan/GPU-Fehler,
     * Preflight-konform) — reiner Fallback-Pfad VOR dem bestehenden Hook,
     * kein bestehendes Verhalten geaendert. */
    if (mm * kk * nn >= (1LL << 24)) {
        moo_tensor_nach_gpu(a);
        moo_tensor_nach_gpu(b);
        if ((a->valid & MOO_V_DEV) && (b->valid & MOO_V_DEV)) {
            if (!out->gpu_buf)
                out->gpu_buf = moo_ki_gpu_buf_belegen((int64_t)out->size * (int64_t)sizeof(float));
            if (out->gpu_buf &&
                moo_ki_gpu_matmul_res(a->gpu_buf, b->gpu_buf, out->gpu_buf,
                                      (int32_t)mm, (int32_t)kk, (int32_t)nn)) {
                out->valid = MOO_V_DEV;    /* nur GPU-Seite gueltig; naechster ->data-Lesezugriff sichert via I1-Trichter */
                out->device = MOO_DEV_GPU;
                done = true;
            }
        }
    }
    /* GPU2: grosse MatMuls (non-resident) auf Vulkan, CPU-ikj als Fallback. */
    if (!done && !moo_ki_gpu_matmul(a->data, b->data, out->data,
                           a->shape[0], a->shape[1], b->shape[1]))
        matmul_ikj(a->data, b->data, out->data, a->shape[0], a->shape[1], b->shape[1]);
    MooValue ret = wrap_t(out);
    moo_ag_record("matmul", av, bv, moo_none(), ret);
    return ret;
}

// ============================================================
// Gather — Zeilen-Lookup out[i,:] = W[indizes[i],:] (Embedding-Kern, KIP-T1).
// W: [vokab, dim] (2D). indizes: Tensor [n] oder [n,1], f32 GANZZAHLIG in
// [0, vokab). Backward = scatter-add nach W (Duplikat-Indizes summieren,
// siehe bw_gather in moo_autograd.c) — mathematisch identisch zu one-hot@W,
// aber ohne [n, vokab]-Materialisierung.
// DESIGN (KIP-T1): Gather ist ein generischer BINARY-Op; die Indizes reiten
// als inputs[1] im Tape, sind aber NICHT differenzierbar (Registry-Feld
// nichtdiff_maske Bit1). Der Gradcheck perturbiert sie darum nie.
// INDEX-VERTRAG: endlich, ganzzahlig, in [0, vokab). f32 ist exakt-integer
// bis 2^24 -> gilt fuer alle praktischen Vokabulare (dokumentierte Grenze).
// ============================================================
MooValue moo_tensor_gather(MooValue wv, MooValue idxv) {
    MooTensor* w = expect_t(wv, "tensor_gather"); if (!w) return moo_none();
    MooTensor* idx = expect_t(idxv, "tensor_gather"); if (!idx) return moo_none();
    if (w->ndim != 2) {
        moo_throw(moo_error("tensor_gather: W muss 2-dimensional sein "
                            "[vokab, dim]"));
        return moo_none();
    }
    bool form_ok = (idx->ndim == 1) || (idx->ndim == 2 && idx->shape[1] == 1);
    if (!form_ok) {
        moo_throw(moo_error("tensor_gather: Indizes bitte als Tensor [n] "
                            "oder [n, 1]"));
        return moo_none();
    }
    int64_t n = idx->size;
    int32_t vokab = w->shape[0];
    int32_t dim = w->shape[1];
    // Index-Vertrag pruefen (endlich, ganzzahlig, in Range) — Tensorwerte f32!
    for (int64_t i = 0; i < n; i++) {
        double d = (double)idx->data[i];
        if (!(d == d) || d < 0.0 || d >= (double)vokab ||
            d != (double)(int64_t)d) {
            char msg[128];
            snprintf(msg, sizeof(msg), "tensor_gather: Index %g ist nicht "
                     "ganzzahlig in [0, %d)", d, vokab);
            moo_throw(moo_error(msg));
            return moo_none();
        }
    }
    int32_t oshape[2] = { (int32_t)n, dim };
    MooTensor* out = moo_tensor_raw(2, oshape);
    if (!out) return moo_none();
    for (int64_t i = 0; i < n; i++) {
        int64_t r = (int64_t)idx->data[i];
        memcpy(out->data + i * (int64_t)dim, w->data + r * (int64_t)dim,
               (size_t)dim * sizeof(float));
    }
    MooValue ret = wrap_t(out);
    // B1: Tape. idx = inputs[1] (nicht-diff, siehe nichtdiff_maske).
    moo_ag_record("gather", wv, idxv, moo_none(), ret);
    return ret;
}

// ============================================================
// Form-Ops
// ============================================================

MooValue moo_tensor_transponieren(MooValue av) {
    MooTensor* a = expect_t(av, "tensor_transponieren"); if (!a) return moo_none();
    if (a->ndim != 2) {
        moo_throw(moo_error("tensor_transponieren: nur fuer 2-dimensionale Tensoren"));
        return moo_none();
    }
    int32_t oshape[2] = { a->shape[1], a->shape[0] };
    MooTensor* out = moo_tensor_raw(2, oshape);
    if (!out) return moo_none();
    int64_t r = a->shape[0], c = a->shape[1];
    for (int64_t i = 0; i < r; i++)
        for (int64_t j = 0; j < c; j++)
            out->data[j * r + i] = a->data[i * c + j];
    MooValue ret = wrap_t(out);
    moo_ag_record("transpose", av, moo_none(), moo_none(), ret);
    return ret;
}

// Umformen: gleiche Elementzahl, Daten-Kopie (contiguous). View-Sharing ist
// bewusst Backlog — Kopie ist korrekt und einfach; matmul dominiert die Kosten.
MooValue moo_tensor_umformen(MooValue av, MooValue shape_list) {
    MooTensor* a = expect_t(av, "tensor_umformen"); if (!a) return moo_none();
    if (shape_list.tag != MOO_LIST) {
        moo_throw(moo_error("tensor_umformen: erwarte eine Form-Liste, z.B. [6] oder [2, 3]"));
        return moo_none();
    }
    MooList* l = MV_LIST(shape_list);
    if (l->length < 1 || l->length > MOO_TENSOR_MAX_DIMS) {
        moo_throw(moo_error("tensor_umformen: Form-Liste braucht 1 bis 8 Eintraege"));
        return moo_none();
    }
    int32_t shape[MOO_TENSOR_MAX_DIMS];
    int64_t total = 1;
    for (int32_t i = 0; i < l->length; i++) {
        if (l->items[i].tag != MOO_NUMBER || MV_NUM(l->items[i]) < 1) {
            moo_throw(moo_error("tensor_umformen: Form-Eintraege muessen ganze Zahlen >= 1 sein"));
            return moo_none();
        }
        shape[i] = (int32_t)MV_NUM(l->items[i]);
        total *= shape[i];
    }
    if (total != a->size) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "tensor_umformen: der Tensor hat %lld Werte, die neue Form braucht %lld "
                 "— die Anzahl muss gleich bleiben",
                 (long long)a->size, (long long)total);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    MooTensor* out = moo_tensor_raw(l->length, shape);
    if (!out) return moo_none();
    memcpy(out->data, a->data, (size_t)a->size * sizeof(float));
    MooValue ret = wrap_t(out);
    moo_ag_record("reshape", av, moo_none(), moo_none(), ret);
    return ret;
}

// Zeilenbereich [start, ende) entlang der ersten Dimension — Batching-Baustein.
MooValue moo_tensor_zeilen(MooValue av, MooValue startv, MooValue endev) {
    MooTensor* a = expect_t(av, "tensor_zeilen"); if (!a) return moo_none();
    if (startv.tag != MOO_NUMBER || endev.tag != MOO_NUMBER) {
        moo_throw(moo_error("tensor_zeilen: erwarte Zahlen (start, ende)"));
        return moo_none();
    }
    int64_t s = (int64_t)MV_NUM(startv), e = (int64_t)MV_NUM(endev);
    if (s < 0 || e > (int64_t)a->shape[0] || s >= e) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "tensor_zeilen: Bereich [%lld, %lld) ist ungueltig — erlaubt: 0 bis %d, "
                 "start < ende", (long long)s, (long long)e, a->shape[0]);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int32_t oshape[MOO_TENSOR_MAX_DIMS];
    memcpy(oshape, a->shape, sizeof(oshape));
    oshape[0] = (int32_t)(e - s);
    MooTensor* out = moo_tensor_raw(a->ndim, oshape);
    if (!out) return moo_none();
    int64_t row = a->strides[0];
    memcpy(out->data, a->data + s * row, (size_t)((e - s) * row) * sizeof(float));
    return wrap_t(out);
}

// Verbinden entlang Achse 0 (uebrige Dimensionen muessen gleich sein).
MooValue moo_tensor_verbinden(MooValue av, MooValue bv) {
    MooTensor* a = expect_t(av, "tensor_verbinden"); if (!a) return moo_none();
    MooTensor* b = expect_t(bv, "tensor_verbinden"); if (!b) return moo_none();
    if (a->ndim != b->ndim ||
        memcmp(a->shape + 1, b->shape + 1, sizeof(int32_t) * (size_t)(a->ndim - 1)) != 0) {
        char sa[64], sb[64], msg[256];
        shape_str(a, sa, sizeof(sa)); shape_str(b, sb, sizeof(sb));
        snprintf(msg, sizeof(msg),
                 "tensor_verbinden: %s und %s passen nicht — bis auf die erste Dimension "
                 "muessen alle gleich sein", sa, sb);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int32_t oshape[MOO_TENSOR_MAX_DIMS];
    memcpy(oshape, a->shape, sizeof(oshape));
    oshape[0] = a->shape[0] + b->shape[0];
    MooTensor* out = moo_tensor_raw(a->ndim, oshape);
    if (!out) return moo_none();
    memcpy(out->data, a->data, (size_t)a->size * sizeof(float));
    memcpy(out->data + a->size, b->data, (size_t)b->size * sizeof(float));
    MooValue ret = wrap_t(out);
    moo_ag_record("concat", av, bv, moo_none(), ret);
    return ret;
}

// ============================================================
// Reduktionen — achse -1 = alles -> [1]; 2D: achse 0 -> [1,c], 1 -> [r,1]
// (keepdims). f64-Akkumulation gegen Ausloeschung bei grossen Tensoren.
// ============================================================

typedef enum { RED_SUM, RED_MEAN, RED_MAX } RedArt;

static MooValue reduce_op(MooValue av, MooValue achsev, RedArt art, const char* wo) {
    static const char* ag_namen[] = { "sum", "mean", "max" };
    const char* ag = ag_namen[art];
    MooTensor* a = expect_t(av, wo); if (!a) return moo_none();
    int32_t achse = (achsev.tag == MOO_NUMBER) ? (int32_t)MV_NUM(achsev) : -1;

    if (achse == -1) {
        int32_t oshape[1] = { 1 };
        MooTensor* out = moo_tensor_raw(1, oshape);
        if (!out) return moo_none();
        if (art == RED_MAX) {
            float m = a->data[0];
            for (int64_t i = 1; i < a->size; i++) if (a->data[i] > m) m = a->data[i];
            out->data[0] = m;
        } else {
            double acc = 0.0;
            /* GPU2: Voll-Summe/-Mittel gross -> Vulkan-Partials + Host-Finish */
            if (!moo_ki_gpu_reduce_sum(a->data, a->size, &acc)) {
                acc = 0.0;
                for (int64_t i = 0; i < a->size; i++) acc += (double)a->data[i];
            }
            out->data[0] = (float)(art == RED_MEAN ? acc / (double)a->size : acc);
        }
        MooValue ret0 = wrap_t(out);
        moo_ag_record(ag, av, moo_none(), achsev, ret0);
        return ret0;
    }
    if (a->ndim != 2 || (achse != 0 && achse != 1)) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "%s: Achse %d gibt es hier nicht — erlaubt: -1 (alles) oder bei "
                 "2D-Tensoren 0 (Spalten) / 1 (Zeilen)", wo, achse);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int64_t r = a->shape[0], c = a->shape[1];
    int32_t oshape[2] = { achse == 0 ? 1 : (int32_t)r, achse == 0 ? (int32_t)c : 1 };
    MooTensor* out = moo_tensor_raw(2, oshape);
    if (!out) return moo_none();
    if (achse == 0) {           // ueber Zeilen reduzieren -> [1,c]
        for (int64_t j = 0; j < c; j++) {
            if (art == RED_MAX) {
                float m = a->data[j];
                for (int64_t i = 1; i < r; i++)
                    if (a->data[i * c + j] > m) m = a->data[i * c + j];
                out->data[j] = m;
            } else {
                double acc = 0.0;
                for (int64_t i = 0; i < r; i++) acc += (double)a->data[i * c + j];
                out->data[j] = (float)(art == RED_MEAN ? acc / (double)r : acc);
            }
        }
    } else {                    // ueber Spalten reduzieren -> [r,1]
        for (int64_t i = 0; i < r; i++) {
            const float* row = a->data + i * c;
            if (art == RED_MAX) {
                float m = row[0];
                for (int64_t j = 1; j < c; j++) if (row[j] > m) m = row[j];
                out->data[i] = m;
            } else {
                double acc = 0.0;
                for (int64_t j = 0; j < c; j++) acc += (double)row[j];
                out->data[i] = (float)(art == RED_MEAN ? acc / (double)c : acc);
            }
        }
    }
    MooValue ret = wrap_t(out);
    moo_ag_record(ag, av, moo_none(), achsev, ret);
    return ret;
}

MooValue moo_tensor_summe(MooValue a, MooValue achse)   { return reduce_op(a, achse, RED_SUM,  "tensor_summe"); }
MooValue moo_tensor_mittel(MooValue a, MooValue achse)  { return reduce_op(a, achse, RED_MEAN, "tensor_mittel"); }
MooValue moo_tensor_maximum(MooValue a, MooValue achse) { return reduce_op(a, achse, RED_MAX,  "tensor_maximum"); }

// ============================================================
// Unaere Ops + Aktivierungen
// ============================================================

typedef float (*UOp)(float);
static inline float u_exp(float x)  { return expf(x); }
static inline float u_log(float x)  { return logf(x); }   // IEEE: log(0)=-inf, log(<0)=nan
static inline float u_sqrt(float x) { return sqrtf(x); }
static inline float u_neg(float x)  { return -x; }
static inline float u_relu(float x) { return x > 0.0f ? x : 0.0f; }
static inline float u_sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }
static inline float u_tanh(float x) { return tanhf(x); }
// GELU (tanh-Approximation, wie GPT-2/BERT): 0.5x(1+tanh(sqrt(2/pi)(x+0.044715x^3)))
static inline float u_gelu(float x) {
    const float k = 0.7978845608f;  // sqrt(2/pi)
    float inner = k * (x + 0.044715f * x * x * x);
    return 0.5f * x * (1.0f + tanhf(inner));
}

MOO_OPTIMIZE_O3
static MooValue u_op(MooValue av, UOp f, const char* wo, const char* ag) {
    MooTensor* a = expect_t(av, wo); if (!a) return moo_none();
    MooTensor* out = moo_tensor_raw(a->ndim, a->shape);
    if (!out) return moo_none();
    for (int64_t i = 0; i < a->size; i++) out->data[i] = f(a->data[i]);
    MooValue ret = wrap_t(out);
    moo_ag_record(ag, av, moo_none(), moo_none(), ret);
    return ret;
}

MooValue moo_tensor_exp(MooValue a)     { return u_op(a, u_exp,     "tensor_exp", "exp"); }
MooValue moo_tensor_log(MooValue a)     { return u_op(a, u_log,     "tensor_log", "log"); }
MooValue moo_tensor_sqrt(MooValue a)    { return u_op(a, u_sqrt,    "tensor_wurzel", "sqrt"); }
MooValue moo_tensor_neg(MooValue a)     { return u_op(a, u_neg,     "tensor_neg", "neg"); }
MooValue moo_tensor_relu(MooValue a)    { return u_op(a, u_relu,    "tensor_relu", "relu"); }
MooValue moo_tensor_sigmoid(MooValue a) { return u_op(a, u_sigmoid, "tensor_sigmoid", "sigmoid"); }
MooValue moo_tensor_tanh(MooValue a)    { return u_op(a, u_tanh,    "tensor_tanh", "tanh"); }
MooValue moo_tensor_gelu(MooValue a)    { return u_op(a, u_gelu,    "tensor_gelu", "gelu"); }

MooValue moo_tensor_pow(MooValue av, MooValue zahl) {
    MooTensor* a = expect_t(av, "tensor_hoch"); if (!a) return moo_none();
    if (zahl.tag != MOO_NUMBER) {
        moo_throw(moo_error("tensor_hoch: erwarte eine Zahl als Exponent"));
        return moo_none();
    }
    MooTensor* out = moo_tensor_raw(a->ndim, a->shape);
    if (!out) return moo_none();
    float p = (float)MV_NUM(zahl);
    for (int64_t i = 0; i < a->size; i++) out->data[i] = powf(a->data[i], p);
    MooValue ret = wrap_t(out);
    moo_ag_record("pow", av, moo_none(), zahl, ret);
    return ret;
}

// ============================================================
// Softmax / LogSoftmax — letzte Achse, numerisch stabil (max-shift):
// grosse Logits (z.B. 1000) duerfen NICHT zu inf/nan fuehren.
// ============================================================

static MooValue softmax_impl(MooValue av, bool log_variante, const char* wo) {
    MooTensor* a = expect_t(av, wo); if (!a) return moo_none();
    if (a->ndim != 1 && a->ndim != 2) {
        moo_throw(moo_error("softmax: nur fuer 1- oder 2-dimensionale Tensoren"));
        return moo_none();
    }
    MooTensor* out = moo_tensor_raw(a->ndim, a->shape);
    if (!out) return moo_none();
    int64_t rows = (a->ndim == 2) ? a->shape[0] : 1;
    int64_t cols = (a->ndim == 2) ? a->shape[1] : a->shape[0];
    for (int64_t i = 0; i < rows; i++) {
        const float* x = a->data + i * cols;
        float* o = out->data + i * cols;
        float m = x[0];
        for (int64_t j = 1; j < cols; j++) if (x[j] > m) m = x[j];
        double sum = 0.0;
        for (int64_t j = 0; j < cols; j++) sum += exp((double)(x[j] - m));
        if (log_variante) {
            double lse = log(sum);   // log-sum-exp nach Shift
            for (int64_t j = 0; j < cols; j++)
                o[j] = (float)((double)(x[j] - m) - lse);
        } else {
            for (int64_t j = 0; j < cols; j++)
                o[j] = (float)(exp((double)(x[j] - m)) / sum);
        }
    }
    MooValue ret = wrap_t(out);
    moo_ag_record(log_variante ? "logsoftmax" : "softmax", av, moo_none(), moo_none(), ret);
    return ret;
}

MooValue moo_tensor_softmax(MooValue a)    { return softmax_impl(a, false, "tensor_softmax"); }
MooValue moo_tensor_logsoftmax(MooValue a) { return softmax_impl(a, true,  "tensor_logsoftmax"); }

// ============================================================
// Op-Registry — DER Erweiterbarkeits-Vertrag.
// Neuer Op: Funktion oben implementieren + HIER eine Zeile. Fertig.
// bw (Autograd-backward) traegt B1 nach; B2 verlangt pro bw eine
// Gradient-Check-Zeile im Harness.
// ============================================================
static MooTensorOp op_tabelle[] = {
    { "add",        MOO_OP_BINARY,        NULL, moo_tensor_add,        NULL },
    { "sub",        MOO_OP_BINARY,        NULL, moo_tensor_sub,        NULL },
    { "mul",        MOO_OP_BINARY,        NULL, moo_tensor_mul,        NULL },
    { "div",        MOO_OP_BINARY,        NULL, moo_tensor_div,        NULL },
    { "adds",       MOO_OP_BINARY_SCALAR, NULL, moo_tensor_adds,       NULL },
    { "subs",       MOO_OP_BINARY_SCALAR, NULL, moo_tensor_subs,       NULL },
    { "muls",       MOO_OP_BINARY_SCALAR, NULL, moo_tensor_muls,       NULL },
    { "divs",       MOO_OP_BINARY_SCALAR, NULL, moo_tensor_divs,       NULL },
    { "matmul",     MOO_OP_BINARY,        NULL, moo_tensor_matmul,     NULL },
    // gather: BINARY, aber Eingang 1 (Indizes) ist NICHT differenzierbar
    // (nichtdiff_maske Bit1 = 0x2). Backward = scatter-add nach W.
    { "gather",     MOO_OP_BINARY,        NULL, moo_tensor_gather,     NULL, 0x2 },
    { "transpose",  MOO_OP_UNARY,         moo_tensor_transponieren, NULL, NULL },
    { "reshape",    MOO_OP_BINARY,        NULL, moo_tensor_umformen,   NULL },
    { "concat",     MOO_OP_BINARY,        NULL, moo_tensor_verbinden,  NULL },
    { "sum",        MOO_OP_BINARY_SCALAR, NULL, moo_tensor_summe,      NULL },
    { "mean",       MOO_OP_BINARY_SCALAR, NULL, moo_tensor_mittel,     NULL },
    { "max",        MOO_OP_BINARY_SCALAR, NULL, moo_tensor_maximum,    NULL },
    { "exp",        MOO_OP_UNARY,         moo_tensor_exp,        NULL, NULL },
    { "log",        MOO_OP_UNARY,         moo_tensor_log,        NULL, NULL },
    { "sqrt",       MOO_OP_UNARY,         moo_tensor_sqrt,       NULL, NULL },
    { "neg",        MOO_OP_UNARY,         moo_tensor_neg,        NULL, NULL },
    { "pow",        MOO_OP_BINARY_SCALAR, NULL, moo_tensor_pow,        NULL },
    { "relu",       MOO_OP_UNARY,         moo_tensor_relu,       NULL, NULL },
    { "sigmoid",    MOO_OP_UNARY,         moo_tensor_sigmoid,    NULL, NULL },
    { "tanh",       MOO_OP_UNARY,         moo_tensor_tanh,       NULL, NULL },
    { "gelu",       MOO_OP_UNARY,         moo_tensor_gelu,       NULL, NULL },
    { "softmax",    MOO_OP_UNARY,         moo_tensor_softmax,    NULL, NULL },
    { "logsoftmax", MOO_OP_UNARY,         moo_tensor_logsoftmax, NULL, NULL },
};
#define OP_COUNT ((int)(sizeof(op_tabelle) / sizeof(op_tabelle[0])))

const MooTensorOp* moo_tensor_op_lookup(const char* name) {
    for (int i = 0; i < OP_COUNT; i++)
        if (strcmp(op_tabelle[i].name, name) == 0) return &op_tabelle[i];
    return NULL;
}
int moo_tensor_op_count(void) { return OP_COUNT; }
const MooTensorOp* moo_tensor_op_at(int i) {
    return (i >= 0 && i < OP_COUNT) ? &op_tabelle[i] : NULL;
}

// B1: moo_autograd.c registriert hier seine backward-Funktionen.
// Doppel-Registrierung ueberschreibt (idempotenter Init).
bool moo_tensor_op_set_bw(const char* name, MooAgBw bw) {
    for (int i = 0; i < OP_COUNT; i++) {
        if (strcmp(op_tabelle[i].name, name) == 0) {
            op_tabelle[i].bw = bw;
            return true;
        }
    }
    return false;
}
