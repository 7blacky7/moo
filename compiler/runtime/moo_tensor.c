/**
 * moo_tensor.c — KI-Tensor-Kern (Plan-014 A1).
 * ============================================================================
 * n-dimensionales f32-Array, row-major, contiguous. IMMER gebaut (keine
 * externen Deps, kein Feature-Gate — Lehre aus dem MOO_HAS_3D-Linkerfall).
 *
 * TENSOR-KONVENTION (siehe moo_runtime.h):
 *   ALLE Args GELIEHEN (borrowed) — diese Datei released/retained KEINE
 *   Argumente. Rueckgabewerte sind +1 owning. Die Codegen-Arms machen
 *   Post-Call-Release aller Heap-Args (Muster Commit 072834f).
 *
 * UB-POLICY (Memory ub-arithmetik-policy):
 *   Groessen via moo_checked_mul_i32/moo_checked_add_i32, Indizes int64_t,
 *   RNG rein uint64_t (splitmix64, absichtliches Wrap — kommentiert).
 *
 * FEHLERMELDUNGEN: deutsch + erklaerend (Kinderleicht-Ziel D1) — sie sagen
 *   was falsch ist UND was erwartet wurde.
 *
 * moo_throw kehrt im try-Kontext ZURUECK (kein noreturn) — nach jedem Wurf
 * wird defensiv ein harmloser Wert returnt (Muster moo_memory.c-Kommentar).
 * ============================================================================
 */
#include "moo_runtime.h"

// ============================================================
// Interne Helfer
// ============================================================

// Baut einen leeren Tensor mit gegebenem Shape. data wird via calloc
// angelegt (moo_alloc nullt NICHT — Airbag-Gotcha) und ist damit bereits
// mit 0.0f gefuellt. Gibt NULL zurueck wenn Shape ungueltig (nach moo_throw).
// NON-STATIC (P014-A2): interner Roh-Konstruktor fuer moo_tensor_ops.c —
// NICHT fuer Bindings gedacht (dort immer die moo_tensor_*-MooValue-API).
MooTensor* moo_tensor_raw(int32_t ndim, const int32_t* shape) {
    if (ndim < 1 || ndim > MOO_TENSOR_MAX_DIMS) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "tensor: %d Dimensionen sind nicht erlaubt (moeglich: 1 bis %d)",
                 ndim, MOO_TENSOR_MAX_DIMS);
        moo_throw(moo_error(msg));
        return NULL;
    }
    int64_t size = 1;
    for (int32_t d = 0; d < ndim; d++) {
        if (shape[d] <= 0) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "tensor: Dimension %d hat Groesse %d — jede Dimension braucht mindestens 1",
                     d, shape[d]);
            moo_throw(moo_error(msg));
            return NULL;
        }
        size = moo_checked_mul_i32(size, (int64_t)shape[d], "tensor");
        if (moo_error_flag) return NULL;
    }
    // Byte-Groesse gegen Limit pruefen (size * 4).
    int64_t bytes = moo_checked_mul_i32(size, (int64_t)sizeof(float), "tensor");
    if (moo_error_flag) return NULL;
    (void)bytes;

    MooTensor* t = (MooTensor*)moo_alloc(sizeof(MooTensor));
    t->refcount = 1;
    t->ndim = ndim;
    t->size = size;
    memset(t->shape, 0, sizeof(t->shape));
    memset(t->strides, 0, sizeof(t->strides));
    int64_t stride = 1;
    for (int32_t d = ndim - 1; d >= 0; d--) {
        t->shape[d] = shape[d];
        t->strides[d] = stride;
        stride *= shape[d];              // gegen Limit bereits geprueft
    }
    t->data = (float*)calloc((size_t)size, sizeof(float));
    if (!t->data) {
        free(t);
        moo_throw(moo_error("tensor: Speicher voll"));
        return NULL;
    }
    t->grad = NULL;
    t->requires_grad = false;
    return t;
}

static MooValue tensor_wrap(MooTensor* t) {
    MooValue v;
    if (!t) return moo_none();           // Fehlerfall (throw bereits erfolgt)
    v.tag = MOO_TENSOR;
    moo_val_set_ptr(&v, t);
    return v;
}

// Liest eine moo-Liste aus Zahlen in ein int32-Shape-Array. Liste GELIEHEN.
// true bei Erfolg; wirft + false bei Fehlern.
static bool shape_from_list(MooValue shape_list, int32_t* out, int32_t* out_ndim) {
    if (shape_list.tag != MOO_LIST) {
        moo_throw(moo_error("tensor: erwarte eine Liste als Form, z.B. tensor([2, 3])"));
        return false;
    }
    MooList* l = MV_LIST(shape_list);
    if (l->length < 1 || l->length > MOO_TENSOR_MAX_DIMS) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "tensor: Form-Liste hat %d Eintraege (moeglich: 1 bis %d)",
                 l->length, MOO_TENSOR_MAX_DIMS);
        moo_throw(moo_error(msg));
        return false;
    }
    for (int32_t i = 0; i < l->length; i++) {
        if (l->items[i].tag != MOO_NUMBER) {
            moo_throw(moo_error("tensor: die Form-Liste darf nur Zahlen enthalten"));
            return false;
        }
        double d = MV_NUM(l->items[i]);
        if (d < 1 || d > 2147483647.0 || d != (double)(int32_t)d) {
            moo_throw(moo_error("tensor: Form-Eintraege muessen ganze Zahlen >= 1 sein"));
            return false;
        }
        out[i] = (int32_t)d;
    }
    *out_ndim = l->length;
    return true;
}

// Prueft dass v ein Tensor ist; wirft sonst (mit Kontext-Name).
static MooTensor* expect_tensor(MooValue v, const char* wo) {
    if (v.tag != MOO_TENSOR) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: das ist kein Tensor", wo);
        moo_throw(moo_error(msg));
        return NULL;
    }
    return MV_TENSOR(v);
}

// Flachen Index aus einer Index-Liste berechnen (Bounds-Check pro Dimension).
// Rueckgabe -1 bei Fehler (throw bereits erfolgt). Liste GELIEHEN.
static int64_t flat_index(MooTensor* t, MooValue idx_list, const char* wo) {
    if (idx_list.tag != MOO_LIST) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s: erwarte eine Index-Liste mit %d Eintraegen, z.B. [0, 1]",
                 wo, t->ndim);
        moo_throw(moo_error(msg));
        return -1;
    }
    MooList* l = MV_LIST(idx_list);
    if (l->length != t->ndim) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s: der Tensor hat %d Dimensionen, deine Index-Liste hat %d Eintraege",
                 wo, t->ndim, l->length);
        moo_throw(moo_error(msg));
        return -1;
    }
    int64_t flat = 0;
    for (int32_t d = 0; d < t->ndim; d++) {
        if (l->items[d].tag != MOO_NUMBER) {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: Indizes muessen Zahlen sein", wo);
            moo_throw(moo_error(msg));
            return -1;
        }
        int64_t idx = (int64_t)MV_NUM(l->items[d]);
        if (idx < 0 || idx >= (int64_t)t->shape[d]) {
            char msg[192];
            snprintf(msg, sizeof(msg),
                     "%s: Index %lld liegt ausserhalb von Dimension %d (erlaubt: 0 bis %d)",
                     wo, (long long)idx, d, t->shape[d] - 1);
            moo_throw(moo_error(msg));
            return -1;
        }
        flat += idx * t->strides[d];
    }
    return flat;
}

// ============================================================
// Deterministischer RNG — splitmix64 (rein uint64_t, absichtliches Wrap:
// die Konstanten/Mults sind Teil des Algorithmus, Ueberlauf ist definierte
// unsigned-Semantik. Muster: seed-deterministisch wie moo_noise.c).
// ============================================================
static inline uint64_t splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Uniform in [-1, 1): 24 Zufallsbits -> [0,1) -> skaliert.
static inline float rand_uniform_pm1(uint64_t* state) {
    uint32_t bits = (uint32_t)(splitmix64(state) >> 40);   // obere 24 Bit
    float u01 = (float)bits / 16777216.0f;                 // / 2^24
    return u01 * 2.0f - 1.0f;
}

// ============================================================
// Free (Dispatch-Ziel aus moo_memory.c)
// ============================================================
void moo_tensor_free(void* ptr) {
    if (!ptr) return;
    MooTensor* t = (MooTensor*)ptr;
    if (t->data) free(t->data);
    if (t->grad) free(t->grad);
    free(t);
}

// ============================================================
// Konstruktoren — alle: Args geliehen, Rueckgabe +1 owning.
// ============================================================

MooValue moo_tensor_neu(MooValue shape_list, MooValue fill) {
    int32_t shape[MOO_TENSOR_MAX_DIMS]; int32_t ndim;
    if (!shape_from_list(shape_list, shape, &ndim)) return moo_none();
    MooTensor* t = moo_tensor_raw(ndim, shape);
    if (!t) return moo_none();
    double f = (fill.tag == MOO_NUMBER) ? MV_NUM(fill) : 0.0;
    if (f != 0.0) {                      // calloc hat bereits 0.0f geliefert
        float ff = (float)f;
        for (int64_t i = 0; i < t->size; i++) t->data[i] = ff;
    }
    return tensor_wrap(t);
}

MooValue moo_tensor_nullen(MooValue shape_list) {
    return moo_tensor_neu(shape_list, moo_number(0.0));
}

MooValue moo_tensor_einsen(MooValue shape_list) {
    return moo_tensor_neu(shape_list, moo_number(1.0));
}

MooValue moo_tensor_zufall(MooValue shape_list, MooValue seed) {
    int32_t shape[MOO_TENSOR_MAX_DIMS]; int32_t ndim;
    if (!shape_from_list(shape_list, shape, &ndim)) return moo_none();
    MooTensor* t = moo_tensor_raw(ndim, shape);
    if (!t) return moo_none();
    uint64_t s = (seed.tag == MOO_NUMBER) ? (uint64_t)(int64_t)MV_NUM(seed) : 42ULL;
    // Leerer Seed-Zustand waere degeneriert -> festes Salt dazu (Wrap ok).
    uint64_t state = s * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    for (int64_t i = 0; i < t->size; i++)
        t->data[i] = rand_uniform_pm1(&state);
    return tensor_wrap(t);
}

// 1D-Liste [1,2,3] -> Tensor[3]; verschachtelte Liste [[1,2],[3,4]] ->
// Tensor[2x2] (alle Zeilen muessen gleich lang sein). Liste GELIEHEN.
MooValue moo_tensor_aus_liste(MooValue list) {
    if (list.tag != MOO_LIST) {
        moo_throw(moo_error("tensor_aus_liste: erwarte eine Liste, z.B. [1, 2, 3] oder [[1,2],[3,4]]"));
        return moo_none();
    }
    MooList* l = MV_LIST(list);
    if (l->length == 0) {
        moo_throw(moo_error("tensor_aus_liste: die Liste ist leer"));
        return moo_none();
    }
    if (l->items[0].tag == MOO_LIST) {
        // 2D-Fall
        int32_t rows = l->length;
        int32_t cols = MV_LIST(l->items[0])->length;
        if (cols == 0) {
            moo_throw(moo_error("tensor_aus_liste: die erste Zeile ist leer"));
            return moo_none();
        }
        for (int32_t r = 0; r < rows; r++) {
            if (l->items[r].tag != MOO_LIST || MV_LIST(l->items[r])->length != cols) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "tensor_aus_liste: Zeile %d passt nicht — alle Zeilen brauchen %d Eintraege",
                         r, cols);
                moo_throw(moo_error(msg));
                return moo_none();
            }
        }
        int32_t shape[2] = { rows, cols };
        MooTensor* t = moo_tensor_raw(2, shape);
        if (!t) return moo_none();
        for (int32_t r = 0; r < rows; r++) {
            MooList* row = MV_LIST(l->items[r]);
            for (int32_t c = 0; c < cols; c++) {
                if (row->items[c].tag != MOO_NUMBER) {
                    moo_tensor_free(t);
                    moo_throw(moo_error("tensor_aus_liste: alle Eintraege muessen Zahlen sein"));
                    return moo_none();
                }
                t->data[(int64_t)r * cols + c] = (float)MV_NUM(row->items[c]);
            }
        }
        return tensor_wrap(t);
    }
    // 1D-Fall
    int32_t shape[1] = { l->length };
    MooTensor* t = moo_tensor_raw(1, shape);
    if (!t) return moo_none();
    for (int32_t i = 0; i < l->length; i++) {
        if (l->items[i].tag != MOO_NUMBER) {
            moo_tensor_free(t);
            moo_throw(moo_error("tensor_aus_liste: alle Eintraege muessen Zahlen sein"));
            return moo_none();
        }
        t->data[i] = (float)MV_NUM(l->items[i]);
    }
    return tensor_wrap(t);
}

// ============================================================
// Zugriff
// ============================================================

MooValue moo_tensor_holen(MooValue tv, MooValue idx_list) {
    MooTensor* t = expect_tensor(tv, "tensor_holen");
    if (!t) return moo_number(0);
    int64_t flat = flat_index(t, idx_list, "tensor_holen");
    if (flat < 0) return moo_number(0);
    return moo_number((double)t->data[flat]);
}

MooValue moo_tensor_setzen(MooValue tv, MooValue idx_list, MooValue val) {
    MooTensor* t = expect_tensor(tv, "tensor_setzen");
    if (!t) return moo_none();
    if (val.tag != MOO_NUMBER) {
        moo_throw(moo_error("tensor_setzen: der neue Wert muss eine Zahl sein"));
        return moo_none();
    }
    int64_t flat = flat_index(t, idx_list, "tensor_setzen");
    if (flat < 0) return moo_none();
    t->data[flat] = (float)MV_NUM(val);
    return moo_none();
}

MooValue moo_tensor_form(MooValue tv) {
    MooTensor* t = expect_tensor(tv, "tensor_form");
    if (!t) return moo_list_new(0);
    MooValue list = moo_list_new(t->ndim);
    for (int32_t d = 0; d < t->ndim; d++)
        moo_list_append(list, moo_number((double)t->shape[d]));
    return list;
}

MooValue moo_tensor_groesse(MooValue tv) {
    MooTensor* t = expect_tensor(tv, "tensor_groesse");
    if (!t) return moo_number(0);
    return moo_number((double)t->size);
}

MooValue moo_tensor_zu_liste(MooValue tv) {
    MooTensor* t = expect_tensor(tv, "tensor_zu_liste");
    if (!t) return moo_list_new(0);
    if (t->size > 1000000) {
        moo_throw(moo_error("tensor_zu_liste: Tensor ist zu gross fuer eine Liste (max 1 Million Werte)"));
        return moo_list_new(0);
    }
    MooValue list = moo_list_new((int32_t)t->size);
    for (int64_t i = 0; i < t->size; i++)
        moo_list_append(list, moo_number((double)t->data[i]));
    return list;
}

// "Tensor[2x3]" — kompakte Anzeige fuer zeige(). Bei 1D/2D bis 6x6 werden
// die Werte mit angezeigt (kinderleicht: kleine Tensoren sind sichtbar).
MooValue moo_tensor_to_string(MooValue tv) {
    if (tv.tag != MOO_TENSOR) return moo_string_new("<kein Tensor>");
    MooTensor* t = MV_TENSOR(tv);
    char head[96];
    int off = snprintf(head, sizeof(head), "Tensor[");
    for (int32_t d = 0; d < t->ndim; d++)
        off += snprintf(head + off, sizeof(head) - (size_t)off, "%s%d",
                        d ? "x" : "", t->shape[d]);
    snprintf(head + off, sizeof(head) - (size_t)off, "]");

    bool klein = (t->ndim == 1 && t->size <= 8) ||
                 (t->ndim == 2 && t->shape[0] <= 6 && t->shape[1] <= 6);
    if (!klein) return moo_string_new(head);

    // Werte anhaengen: "Tensor[2x2] [[1, 2], [3, 4]]"
    char buf[1024];
    int o = snprintf(buf, sizeof(buf), "%s ", head);
    int32_t rows = (t->ndim == 2) ? t->shape[0] : 1;
    int32_t cols = (t->ndim == 2) ? t->shape[1] : t->shape[0];
    if (t->ndim == 2) o += snprintf(buf + o, sizeof(buf) - (size_t)o, "[");
    for (int32_t r = 0; r < rows && o < (int)sizeof(buf) - 32; r++) {
        o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s[", r ? ", " : "");
        for (int32_t c = 0; c < cols && o < (int)sizeof(buf) - 32; c++)
            o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s%g",
                          c ? ", " : "", (double)t->data[(int64_t)r * cols + c]);
        o += snprintf(buf + o, sizeof(buf) - (size_t)o, "]");
    }
    if (t->ndim == 2) snprintf(buf + o, sizeof(buf) - (size_t)o, "]");
    return moo_string_new(buf);
}
