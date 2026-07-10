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
#include "moo_ki_gpu_api.h"   // KIP-G1: residente GPU-Buffer-API (Stub ohne Vulkan)

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
    // D0/G1-Skelett (KIP-STRUCT f2cbebc7): F32/CPU-Default, `data` autoritativ.
    // moo_alloc nullt NICHT -> alle neuen Felder explizit setzen.
    t->dtype = MOO_DT_F32;
    t->valid = MOO_V_DATA;     // Invariante: mindestens ein Repr.-Bit gesetzt
    t->grad_valid = 0;         // kein Grad -> Maske leer (erlaubt)
    t->device = MOO_DEV_CPU;
    t->store = NULL;
    t->gpu_buf = NULL;
    t->gpu_grad = NULL;
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
    MooTensor* t = MV_TENSOR(v);
    moo_tensor_f32_sichern(t);   // KIP-D1 Eintrittspunkt (D0 §4.1): f32-data garantieren (bf16-store -> f32)
    return t;
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
    if (t->store) free(t->store);   // KIP-D1: bf16-Storage freigeben (nur != NULL nach als_dtype)
    // KIP-G1: GPU-Buffer an den Pool zurueckgeben (NICHT vkDestroy/free — Pool-Semantik).
    // Der synchrone Dispatch garantiert GPU-idle; Stub ohne Vulkan = no-op (stets NULL).
    if (t->gpu_buf) moo_ki_gpu_buf_freigeben(t->gpu_buf);
    if (t->gpu_grad) moo_ki_gpu_buf_freigeben(t->gpu_grad);   // G3c-Feld, heute NULL
    free(t);
}

// === bf16 <-> f32 Konvertierung (KIP-D1) ===
// bf16 = obere 16 Bit eines f32 (gleicher 8-Bit-Exponent, 7 Mantissen-Bit).
// f32->bf16: round-to-nearest-even; NaN bleibt NaN (entartet nicht zu Inf),
// Inf bleibt Inf; sehr kleine Werte koennen zu 0 flushen (bf16-Semantik).
static inline uint16_t moo_f32_zu_bf16(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    if (((x >> 23) & 0xFFu) == 0xFFu && (x & 0x7FFFFFu)) {
        return (uint16_t)((x >> 16) | 0x0040u);   // NaN -> quiet bf16-NaN
    }
    uint32_t bias = 0x7FFFu + ((x >> 16) & 1u);   // round-to-nearest-even
    x += bias;                                    // uint32: definiertes Wrap (UB-Policy)
    return (uint16_t)(x >> 16);
}
static inline float moo_bf16_zu_f32(uint16_t h) {
    uint32_t x = (uint32_t)h << 16;
    float f;
    memcpy(&f, &x, sizeof(f));
    return f;
}

// === KIP-D2 Mixed-Precision: Aktivierung auf bf16-Praezision runden ===
// Reduziert die (gueltige) f32-`data` IN-PLACE auf bf16-Praezision:
// round-trip f32->bf16->f32 (round-to-nearest-even, identisch zum Storage-Pfad).
// Numerisch IDENTISCH zu als_dtype("bf16")+f32_sichern, aber OHNE store-Alloc /
// data-Free -> `data` bleibt autoritativ (valid unveraendert = MOO_V_DATA),
// KEIN Sicherungs-Vertrag/ASan-Risiko fuer Direkt-Leser (Backward liest weiter
// gueltiges f32). Aufrufer: moo_ag_record fuer Op-Output-Aktivierungen (D2 an).
void moo_tensor_bf16_runden(MooTensor* t) {
    if (!t || !t->data) return;
    if (!(t->valid & MOO_V_DATA)) return;   // nur gueltige f32-Aktivierung runden
    for (int64_t i = 0; i < t->size; i++)
        t->data[i] = moo_bf16_zu_f32(moo_f32_zu_bf16(t->data[i]));
}

// === Repraesentations-Sicherung (KIP-D1, Valid-Masken-Vertrag D0 §2) ===
// f32_sichern: garantiert gueltiges f32-`data` (materialisiert ggf. aus bf16-store).
//   Schneller Pfad: MOO_V_DATA schon gesetzt -> no-op (der F32-Normalfall).
void moo_tensor_f32_sichern(MooTensor* t) {
    if (!t) return;
    if (t->valid & MOO_V_DATA) return;                 // bereits gueltig (F32-Fastpath)
    if (t->valid & MOO_V_STORE) {                      // aus bf16-store rematerialisieren
        if (!t->data) {
            t->data = (float*)calloc((size_t)t->size, sizeof(float));
            if (!t->data) { moo_throw(moo_error("tensor: Speicher voll bei f32-Sicherung")); return; }
        }
        const uint16_t* s = (const uint16_t*)t->store;
        for (int64_t i = 0; i < t->size; i++) t->data[i] = moo_bf16_zu_f32(s[i]);
        t->valid |= MOO_V_DATA;
        return;
    }
    if (t->valid & MOO_V_DEV) {                        // KIP-G1: aus GPU-Buffer laden
        if (!t->data) {
            t->data = (float*)calloc((size_t)t->size, sizeof(float));
            if (!t->data) { moo_throw(moo_error("tensor: Speicher voll bei GPU-Download")); return; }
        }
        if (!moo_ki_gpu_download(t->gpu_buf, t->data,
                                 (int64_t)t->size * (int64_t)sizeof(float))) {
            moo_throw(moo_error("tensor: GPU->Host-Download fehlgeschlagen")); return;
        }
        t->valid |= MOO_V_DATA;                        // DATA jetzt gueltig (DEV bleibt gueltig)
        return;
    }
    moo_throw(moo_error("tensor: keine gueltige CPU-Repraesentation zum Sichern"));
}
// store_sichern: garantiert gueltigen dtype-`store` (bf16) aus f32-`data`.
void moo_tensor_store_sichern(MooTensor* t) {
    if (!t) return;
    if (t->dtype == MOO_DT_F32) return;                // kein store bei reinem f32
    if (t->valid & MOO_V_STORE) return;                // bereits aktuell
    if (!(t->valid & MOO_V_DATA)) { moo_throw(moo_error("tensor: kein f32-data fuer store-Sicherung")); return; }
    if (!t->store) {
        t->store = malloc((size_t)t->size * sizeof(uint16_t));
        if (!t->store) { moo_throw(moo_error("tensor: Speicher voll bei store-Sicherung")); return; }
    }
    uint16_t* s = (uint16_t*)t->store;
    for (int64_t i = 0; i < t->size; i++) s[i] = moo_f32_zu_bf16(t->data[i]);
    t->valid |= MOO_V_STORE;
}
// host_sichern: Host-Sicht (f32-data) garantieren. Delegiert an f32_sichern,
// das seit KIP-G1 auch den GPU-Fall (MOO_V_DEV) per Download materialisiert —
// DER zentrale Grep-bare Download-Punkt (G1 §3: zeige/print/zu_liste/speichern).
void moo_tensor_host_sichern(MooTensor* t) { moo_tensor_f32_sichern(t); }

// === Geraetetransfer (KIP-G1) — explizit, moo-sichtbar, idempotent ===
// nach_gpu: GPU-residente Repraesentation sichern (Upload). Transfer, KEIN Write
// => data UND dev bleiben/werden gueltig. Ohne Vulkan/GPU liefert belegen NULL
// => Tensor bleibt CPU-resident (device zurueck auf CPU), kein Fehler/Zwang.
void moo_tensor_nach_gpu(MooTensor* t) {
    if (!t) return;
    t->device = MOO_DEV_GPU;                       // bevorzugter Compute-Ort
    if (t->valid & MOO_V_DEV) return;              // idempotent (schon resident)
    moo_tensor_f32_sichern(t);                     // gueltiges f32-data garantieren
    int64_t bytes = (int64_t)t->size * (int64_t)sizeof(float);
    if (!t->gpu_buf) t->gpu_buf = moo_ki_gpu_buf_belegen(bytes);
    if (!t->gpu_buf || !moo_ki_gpu_upload(t->gpu_buf, t->data, bytes)) {
        t->device = MOO_DEV_CPU;                   // keine GPU -> CPU-resident bleiben
        return;
    }
    t->valid |= MOO_V_DEV;                          // data + dev beide gueltig
}
// nach_cpu: Host-Sicht garantieren (Download falls noetig) + bevorzugter Ort CPU.
void moo_tensor_nach_cpu(MooTensor* t) {
    if (!t) return;
    moo_tensor_host_sichern(t);                    // DEV -> DATA falls noetig
    t->device = MOO_DEV_CPU;
}

// === als_dtype: Storage-DType wechseln (KIP-D1) ===
// tensor.als_dtype("bf16"/"f32"): konvertiert IN-PLACE, gibt denselben (mutierten)
// Tensor +1 owning zurueck. "bf16" baut store aus data + gibt data frei; "f32"
// materialisiert data + gibt store frei.
MooValue moo_tensor_als_dtype(MooValue tv, MooValue dtype_str) {
    MooTensor* t = expect_tensor(tv, "als_dtype");     // sichert data (f32) vor
    if (!t) return moo_none();
    if (dtype_str.tag != MOO_STRING) {
        moo_throw(moo_error("als_dtype: erwarte einen Typ-Namen als Text (\"f32\" oder \"bf16\")"));
        return moo_none();
    }
    const char* name = MV_STR(dtype_str)->chars;
    uint8_t ziel;
    if (strcmp(name, "f32") == 0 || strcmp(name, "float32") == 0) ziel = MOO_DT_F32;
    else if (strcmp(name, "bf16") == 0 || strcmp(name, "bfloat16") == 0) ziel = MOO_DT_BF16;
    else {
        char msg[128];
        snprintf(msg, sizeof(msg), "als_dtype: unbekannter Typ \"%s\" (moeglich: \"f32\", \"bf16\")", name);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    if (ziel == MOO_DT_BF16) {
        t->dtype = MOO_DT_BF16;
        moo_tensor_store_sichern(t);                   // store aus (gueltigem) data
        if (moo_error_flag) return moo_none();
        if (t->data) { free(t->data); t->data = NULL; }
        t->valid = MOO_V_STORE;                        // nur noch bf16-Storage autoritativ
    } else {
        t->dtype = MOO_DT_F32;                          // data ist via expect_tensor bereits gesichert
        if (t->store) { free(t->store); t->store = NULL; }
        t->valid = MOO_V_DATA;
    }
    moo_retain(tv);                                    // Rueckgabe +1 owning (derselbe Tensor)
    return tv;
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
    t->valid = MOO_V_DATA;   // KIP-D1 Mutations-Invalidierung (D0 §4.2): data autoritativ, store/dev verworfen
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
    moo_tensor_f32_sichern(t);   // KIP-D1 Eintrittspunkt: Werte-Anzeige braucht gueltiges f32-data
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
