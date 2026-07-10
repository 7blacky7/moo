#ifndef MOO_RUNTIME_H
#define MOO_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>

// === Tag-Definitionen ===
typedef enum {
    MOO_NUMBER  = 0,
    MOO_STRING  = 1,
    MOO_BOOL    = 2,
    MOO_NONE    = 3,
    MOO_LIST    = 4,
    MOO_DICT    = 5,
    MOO_FUNC    = 6,
    MOO_OBJECT  = 7,
    MOO_ERROR   = 8,
    MOO_THREAD  = 9,
    MOO_CHANNEL = 10,
    MOO_DATABASE = 11,
    MOO_WINDOW   = 12,
    MOO_WINDOW3D = 13,
    MOO_REGEX    = 14,
    MOO_SOCKET   = 15,
    MOO_WEBSERVER = 16,
    MOO_DB_STMT   = 17,
    MOO_WINDOW_HYBRID = 18,
    MOO_VOXELWORLD = 19,
    MOO_FRAME = 20,
    MOO_GIF = 21,
    MOO_VIDEO = 22,
    MOO_TENSOR = 23,
} MooTag;

// === Forward Declarations ===
typedef struct MooString MooString;
typedef struct MooList MooList;
typedef struct MooDict MooDict;
typedef struct MooObject MooObject;
typedef struct MooFunc MooFunc;
typedef struct MooThread MooThread;
typedef struct MooChannel MooChannel;
typedef struct MooValue MooValue;

// === MooValue: Der universelle Wert ===
// Layout: { uint64_t tag, uint64_t data } = 16 Bytes
// data wird als uint64_t gespeichert und bei Bedarf zu double/pointer gecastet.
// Das garantiert ABI-Kompatibilitaet mit LLVM { i64, i64 }.
struct MooValue {
    uint64_t tag;
    uint64_t data;
};

// Zugriff-Makros
static inline double moo_val_as_double(MooValue v) {
    double d;
    memcpy(&d, &v.data, sizeof(double));
    return d;
}
static inline void moo_val_set_double(MooValue* v, double d) {
    memcpy(&v->data, &d, sizeof(double));
}
static inline void* moo_val_as_ptr(MooValue v) {
    return (void*)(uintptr_t)v.data;
}
static inline void moo_val_set_ptr(MooValue* v, void* p) {
    v->data = (uint64_t)(uintptr_t)p;
}
static inline bool moo_val_as_bool(MooValue v) { return (bool)v.data; }
static inline void moo_val_set_bool(MooValue* v, bool b) { v->data = (uint64_t)b; }

// Typ-spezifische Getter (Kurzformen)
#define MV_NUM(v)    moo_val_as_double(v)
#define MV_STR(v)    ((MooString*)moo_val_as_ptr(v))
#define MV_LIST(v)   ((MooList*)moo_val_as_ptr(v))
#define MV_DICT(v)   ((MooDict*)moo_val_as_ptr(v))
#define MV_OBJ(v)    ((MooObject*)moo_val_as_ptr(v))
#define MV_FUNC(v)   ((MooFunc*)moo_val_as_ptr(v))
#define MV_BOOL(v)   moo_val_as_bool(v)
#define MV_ERR(v)    ((char*)moo_val_as_ptr(v))

// === Reference Counting ===
// Erstes Feld in jedem Heap-Objekt. Startet bei 1 bei Erstellung.
// moo_retain() erhoeht, moo_release() verringert und gibt bei 0 frei.
void moo_retain(MooValue v);
void moo_release(MooValue v);
void moo_thread_free(void* ptr);
void moo_channel_free(void* ptr);
void moo_db_free(void* ptr);
void moo_db_stmt_free(void* ptr);
void moo_window_free(void* ptr);
void moo_web_free(void* ptr);
void moo_voxel_free(void* ptr);
void moo_frame_free(void* ptr);
void moo_gif_handle_free(void* ptr);
void moo_tensor_free(void* ptr);

// === Frame (opaker Pixel-Frame-Heap-Typ, Plan-008 A3A) ===
// Pixeldaten NIE als moo-Liste (MooValue=16B; ultrawide ~20MB/Frame). Opaker
// refcounteter Heap-Typ. STANDARDISIERT: format 0 = RGBA8, top-left origin
// (Y-Flip backend-uebergreifend einheitlich beim Grab erledigt). stride in Bytes.
#define MOO_FRAME_FMT_RGBA8 0

// === Tensor (Plan-014 A1) ===
// KI-Kern-Datentyp: n-dimensionales f32-Array, row-major, contiguous.
// IMMER gebaut (keine externen Deps, kein Feature-Gate — Lehre MOO_HAS_3D).
//
// TENSOR-KONVENTION (Refcount, bewusst EINE Regel fuer die ganze Domaene):
//   * ALLE moo_tensor_*-Funktionen behandeln ALLE Argumente als GELIEHEN
//     (borrowed): kein moo_release, kein moo_retain auf Args.
//   * Rueckgabewerte sind +1 owning (frische Objekte).
//   * Die Codegen-Arms machen Post-Call-Release aller Heap-Args
//     (pure-Reader-Muster, Commit 072834f) — leak-messbar, kein Mix.
#define MOO_TENSOR_MAX_DIMS 8

// === DType-/Device-/Valid-Konstanten (KIP-STRUCT f2cbebc7, D0 §2 + G1 §1) ===
// dtype: Storage-Repraesentation. Der COMPUTE-Pfad bleibt f32 (D0-Vertrag).
#define MOO_DT_F32   0   // Default: data (float*) ist autoritativ
#define MOO_DT_BF16  1   // erster Zusatz-Storage-DType (D1) — nur `store`, kein Compute
// valid/grad_valid: EINE geteilte Bitmaske ueber alle Repraesentationen eines
// Tensors (D0 §2, mit G1 §1 vereinheitlicht). Schreiben macht genau eine Repr.
// autoritativ und loescht die anderen Bits. Invariante: valid != 0.
#define MOO_V_DATA   0x1 // f32-`data` gueltig
#define MOO_V_STORE  0x2 // dtype-`store` gueltig (D1)
#define MOO_V_DEV    0x4 // GPU-`gpu_buf` gueltig (G1)
// device: BEVORZUGTER Compute-Ort (nie die einzige Kopie).
#define MOO_DEV_CPU  0
#define MOO_DEV_GPU  1

// LAYOUT-HINWEIS (KIP-STRUCT): refcount MUSS erstes Feld bleiben (moo_release
// liest es via generischem Header). Neue D0/G1-Felder wurden ANS ENDE angehaengt;
// sizeof(MooTensor) waechst um dtype/valid/grad_valid/device (4x uint8_t, gepackt)
// + store/gpu_buf/gpu_grad (3x void*). MooValue transportiert nur einen Zeiger
// auf dieses Struct — die 16-Byte-MooValue-ABI ist UNBERUEHRT.
typedef struct MooTensor {
    int32_t  refcount;                       // MUSS erstes Feld sein
    int32_t  ndim;                           // 1..MOO_TENSOR_MAX_DIMS
    int64_t  size;                           // Produkt aller shape-Eintraege
    int32_t  shape[MOO_TENSOR_MAX_DIMS];
    int64_t  strides[MOO_TENSOR_MAX_DIMS];   // in Elementen, row-major
    float*   data;                           // f32-COMPUTE-Buffer, owned; ab D1 NULLABLE wenn store autoritativ
    float*   grad;                           // NULL oder size Elemente, owned; bleibt IMMER f32 (Autograd B1)
    bool     requires_grad;
    // --- D0/G1-Erweiterung (KIP-STRUCT f2cbebc7) — F32/CPU-Default, keine Verhaltensaenderung ---
    uint8_t  dtype;                          // MOO_DT_F32 (Default) | MOO_DT_BF16 — Storage-DType (D1)
    uint8_t  valid;                          // geteilte Repr.-Bitmaske MOO_V_DATA|MOO_V_STORE|MOO_V_DEV; Invariante != 0
    uint8_t  grad_valid;                     // eigene Grad-Maske MOO_V_DATA|MOO_V_DEV (grad hat kein bf16-store); 0 = kein Grad
    uint8_t  device;                         // MOO_DEV_CPU (Default) | MOO_DEV_GPU — bevorzugter Compute-Ort (G1)
    void*    store;                          // dtype-Storage-Buffer (bf16, ...), NULL bei reinem f32 (D1)
    void*    gpu_buf;                         // opaque MooKiGpuBuf*, Pool-verwaltet, NULL bis G1-PoC
    void*    gpu_grad;                        // opaque GPU-Gradient-Handle, NULL bis G3c
} MooTensor;
#define MV_TENSOR(v) ((MooTensor*)moo_val_as_ptr(v))

MooValue moo_tensor_neu(MooValue shape_list, MooValue fill);
MooValue moo_tensor_nullen(MooValue shape_list);
MooValue moo_tensor_einsen(MooValue shape_list);
MooValue moo_tensor_zufall(MooValue shape_list, MooValue seed);
MooValue moo_tensor_aus_liste(MooValue list);
MooValue moo_tensor_holen(MooValue t, MooValue idx_list);
MooValue moo_tensor_setzen(MooValue t, MooValue idx_list, MooValue val);
MooValue moo_tensor_form(MooValue t);
MooValue moo_tensor_groesse(MooValue t);
MooValue moo_tensor_zu_liste(MooValue t);
MooValue moo_tensor_to_string(MooValue t);
MooValue moo_tensor_als_dtype(MooValue t, MooValue dtype_str);   // D1: f32<->bf16 Storage-Konvertierung (in-place, gibt denselben Tensor +1 owning zurueck)

// Intern (P014-A2): Roh-Konstruktor fuer die Ops-Schicht (moo_tensor_ops.c).
// Liefert refcount=1, data calloc'd (0.0f). NICHT fuer Codegen/Bindings.
MooTensor* moo_tensor_raw(int32_t ndim, const int32_t* shape);

// === Repraesentations-Sicherung (KIP-STRUCT f2cbebc7, D0 §2 / G1 §3) ===
// Stellen sicher, dass die jeweilige Repraesentation gueltig ist (valid-Bit
// gesetzt), indem sie ggf. aus der autoritativen Repr. konvertieren. Im reinen
// F32/CPU-Skelett ist `data` stets autoritativ -> alle drei sind no-op.
// Volle Konvertierungs-Semantik: D1 (store) bzw. G1-PoC (gpu_buf/Download).
void moo_tensor_f32_sichern(MooTensor* t);    // sichert f32-`data`    (MOO_V_DATA)
void moo_tensor_store_sichern(MooTensor* t);  // sichert dtype-`store`  (MOO_V_STORE) — D1
void moo_tensor_host_sichern(MooTensor* t);   // sichert Host-Sicht (Download GPU->CPU) — G1
void moo_tensor_nach_gpu(MooTensor* t);       // GPU-resident machen (Upload) — G1, idempotent
void moo_tensor_nach_cpu(MooTensor* t);       // Host-resident machen (Download) — G1, idempotent
void moo_tensor_bf16_runden(MooTensor* t);    // KIP-D2: f32-`data` in-place auf bf16-Praezision runden (Aktivierungs-Storage-Numerik)

// === Tensor-Ops + Registry (Plan-014 A2) ===
// Alle Ops: Tensor-Konvention (Args borrowed, Rueckgabe +1 owning).
// Elementwise-Ops broadcasten nach NumPy-Regeln. Div folgt IEEE-754
// (x/0 = inf/nan, KEIN throw) — ML-ueblich und ehrlich dokumentiert.
// Reduktionen mit Achse behalten die Dimension (keepdims), damit das
// Ergebnis direkt zurueck-broadcastet (Softmax/Norm-Muster).
MooValue moo_tensor_add(MooValue a, MooValue b);
MooValue moo_tensor_sub(MooValue a, MooValue b);
MooValue moo_tensor_mul(MooValue a, MooValue b);
MooValue moo_tensor_div(MooValue a, MooValue b);
MooValue moo_tensor_adds(MooValue a, MooValue zahl);
MooValue moo_tensor_subs(MooValue a, MooValue zahl);
MooValue moo_tensor_muls(MooValue a, MooValue zahl);
MooValue moo_tensor_divs(MooValue a, MooValue zahl);
MooValue moo_tensor_matmul(MooValue a, MooValue b);
MooValue moo_tensor_transponieren(MooValue a);
MooValue moo_tensor_umformen(MooValue a, MooValue shape_list);
MooValue moo_tensor_zeilen(MooValue a, MooValue start, MooValue ende);
MooValue moo_tensor_verbinden(MooValue a, MooValue b);
MooValue moo_tensor_summe(MooValue a, MooValue achse);
MooValue moo_tensor_mittel(MooValue a, MooValue achse);
MooValue moo_tensor_maximum(MooValue a, MooValue achse);
MooValue moo_tensor_exp(MooValue a);
MooValue moo_tensor_log(MooValue a);
MooValue moo_tensor_sqrt(MooValue a);
MooValue moo_tensor_neg(MooValue a);
MooValue moo_tensor_pow(MooValue a, MooValue zahl);
MooValue moo_tensor_relu(MooValue a);
MooValue moo_tensor_sigmoid(MooValue a);
MooValue moo_tensor_tanh(MooValue a);
MooValue moo_tensor_gelu(MooValue a);
MooValue moo_tensor_softmax(MooValue a);
MooValue moo_tensor_logsoftmax(MooValue a);
// gather(W, indizes): Zeilen-Lookup W[indizes]; Backward = scatter-add nach W.
// Indizes: f32-Tensor, ganzzahlig in [0, vokab). NICHT differenzierbar (KIP-T1).
MooValue moo_tensor_gather(MooValue w, MooValue indizes);

// Op-Registry: neuer Op = 1 Funktion + 1 Tabelleneintrag (Erweiterbarkeits-
// Vertrag). backward registriert moo_autograd.c beim Init via
// moo_tensor_op_set_bw; B2 lehnt Ops ohne Gradient-Check ab.
// Iteration via count/at ist fuer B2 gedacht.
typedef enum {
    MOO_OP_UNARY = 1,          // fw1(a)
    MOO_OP_BINARY = 2,         // fw2(a, b)  — broadcastet
    MOO_OP_BINARY_SCALAR = 3,  // fw2(a, zahl)
} MooTensorOpArt;

// === Autograd (Plan-014 B1): dynamischer Tape, reverse-mode ===
// Node haelt RETAINED Referenzen auf Tensor-Inputs + Output; tape_reset
// released alles (die Refcount-Minenzone — ASan-Gate in test_autograd).
typedef struct MooAgNode {
    const struct MooTensorOp* op;  // Registry-Eintrag (op->bw)
    MooValue inputs[2];            // retained Tensor-Inputs
    int32_t n_in;
    MooValue output;               // retained
    double skalar;                 // Zahl-Arg (adds/muls/pow/summe-achse ...)
    bool hat_skalar;
} MooAgNode;
typedef void (*MooAgBw)(const MooAgNode* n);

typedef struct MooTensorOp {
    const char* name;          // z.B. "add", "matmul", "relu"
    MooTensorOpArt art;
    MooValue (*fw1)(MooValue a);
    MooValue (*fw2)(MooValue a, MooValue b);
    MooAgBw bw;                // backward — traegt moo_autograd.c ein
    uint8_t nichtdiff_maske;   // KIP-T1: Bit i gesetzt = Eingang i NICHT
                               // differenzierbar (z.B. gather-Indizes). Default
                               // 0 = alle Eingaenge diff (bestehende Ops).
} MooTensorOp;
const MooTensorOp* moo_tensor_op_lookup(const char* name);
int moo_tensor_op_count(void);
const MooTensorOp* moo_tensor_op_at(int i);
bool moo_tensor_op_set_bw(const char* name, MooAgBw bw);

// Autograd-API. Tensor-Konvention gilt: Args borrowed, Rueckgaben +1.
void moo_ag_record(const char* op_name, MooValue a, MooValue b,
                   MooValue skalar, MooValue out);   // intern (Ops-Schicht)
MooValue moo_tensor_mit_gradient(MooValue t);        // requires_grad=true, t retained zurueck
MooValue moo_tensor_rueckwaerts(MooValue loss);      // backward ab Skalar-Loss
MooValue moo_tensor_gradient(MooValue t);            // grad als Kopie (+1)
MooValue moo_tensor_gradient_loeschen(MooValue t);   // grad-Buffer nullen
MooValue moo_ag_reset(void);                         // Tape leeren (Nodes releasen)
MooValue moo_ag_an(void);                            // Aufzeichnung an
MooValue moo_ag_aus(void);                            // Aufzeichnung aus (Inferenz)
bool moo_ag_ist_an(void);                             // Zustand (D1: vorhersage)
// KIP-D2 Mixed-Precision-Training an/aus. AUS = Default (keine Verhaltens-
// aenderung -> Basisgates unveraendert). AN = Op-Output-Aktivierungen werden auf
// bf16-Praezision gerundet; Parameter-Master/Gradienten/Optimizer bleiben f32.
// Opt-in per Setter ODER Umgebungsvariable MOO_KI_BF16=1 (einmalig lazy gelesen).
void moo_ag_bf16_setzen(bool an);
bool moo_ag_bf16_an(void);

// === NN-Schichten + Loss + Optimizer (Plan-014 C1, moo_nn.c) ===
// Schichten/Optimizer sind DICTS (Marker-Key "__nn"), Parameter sind
// Tensoren mit requires_grad. Vorwaerts/Loss sind reine KOMPOSITIONEN der
// Registry-Ops (das B2-Gradcheck-Gate deckt damit jeden Gradienten).
// opt_schritt: in-place Update (kein Tape-Record) + Grads nullen + Tape
// leeren = eine komplette Trainings-Iteration. Tensor-Konvention gilt.
MooValue moo_nn_schicht_dicht(MooValue ein, MooValue aus, MooValue aktivierung, MooValue seed);
MooValue moo_nn_schicht_dropout(MooValue rate);
MooValue moo_nn_schicht_layernorm(MooValue dim);
MooValue moo_nn_schicht_rmsnorm(MooValue dim);                                   // KIP-B1: y = x*rsqrt(mean(x^2)+eps)*g
MooValue moo_nn_schicht_ffn_gated(MooValue dim, MooValue versteckt, MooValue art);  // KIP-B3: SwiGLU/Gated-FFN
MooValue moo_nn_schicht_embedding(MooValue vokabular, MooValue dim, MooValue seed);
MooValue moo_nn_schicht_attention(MooValue dim, MooValue koepfe, MooValue seed, MooValue kv_koepfe, MooValue maske, MooValue fenster, MooValue rope);      // G1 + KI-M2a (GQA) + KI-M2b (Sliding) + KIP-B2 (RoPE)
MooValue moo_nn_schicht_position(MooValue max_laenge, MooValue dim, MooValue art, MooValue seed);  // G1
MooValue moo_nn_schicht_moe(MooValue dim, MooValue versteckt, MooValue n_experten, MooValue k, MooValue seed);  // KI-M1
MooValue moo_nn_moe_balance(MooValue netz);   // KI-M1: Balance-Verlust Gl. 12 (nach vorwaerts)
MooValue moo_nn_cache_leeren(MooValue netz);  // KI-M2c: KV-Cache-Zustand leeren (Flag bleibt)
MooValue moo_nn_sequence_packen(MooValue docs, MooValue block_len);  // KIP-B4a
MooValue moo_nn_packung_setzen(MooValue netz, MooValue maske, MooValue positionen);  // KIP-B4a
MooValue moo_nn_packung_leeren(MooValue netz);  // KIP-B4a
MooValue moo_nn_vorwaerts(MooValue netz, MooValue x);
MooValue moo_nn_parameter(MooValue netz);
MooValue moo_nn_mse(MooValue a, MooValue b);
MooValue moo_nn_kreuzentropie(MooValue logits, MooValue ziele);
MooValue moo_nn_kreuzentropie_maskiert(MooValue logits, MooValue ziele, MooValue maske);  // KIP-B4a
MooValue moo_nn_opt_sgd(MooValue params, MooValue rate, MooValue momentum);
MooValue moo_nn_opt_adam(MooValue params, MooValue rate);
MooValue moo_nn_opt_adamw(MooValue params, MooValue rate, MooValue decay);
MooValue moo_nn_opt_schritt(MooValue opt);
MooValue moo_nn_grad_clip(MooValue params, MooValue max_norm);   // E2: Rueckgabe = Norm vor Kappen
// Kinderleicht-API (Plan-014 D1, moo_nn_easy.c) — Zucker ueber moo_nn.c.
// trainiere: Optionen-DICT ({"epochen","rate","batch","optimierer",
// "ausgabe","seed","verlust","momentum"}), Rueckgabe = Fehler-Historie
// (Liste, eine Zahl pro Epoche). speichern/laden: .mook = safetensors-
// Layout (u64-LE-Headerlaenge + JSON + f32-LE), Arch unter
// __metadata__.moo_arch. Fremd-safetensors-Import folgt in F1.
MooValue moo_nn_ki_netz(MooValue schichten);
// Schicht-Typ-Registry (Typ-Zentralisierung Phase 1a, moo_nn.c) — INTERNAL:
// zentrale Wahrheit ueber bekannte Schicht-Typen. easy/Fehlermeldungen
// fragen hier statt eigene Typ-Listen zu pflegen.
bool moo_nn_layer_bekannt(const char* name);
void moo_nn_layer_namen(char* out, size_t out_len);
int32_t moo_nn_layer_anzahl(void);            // Registry-Iterator (Phase 1b)
const char* moo_nn_layer_name(int32_t i);     // borrowed static, NULL ausserhalb
MooValue moo_nn_trainiere(MooValue netz, MooValue x, MooValue y, MooValue optionen);
MooValue moo_nn_vorhersage(MooValue netz, MooValue x);
MooValue moo_nn_genauigkeit(MooValue netz, MooValue x, MooValue y);
MooValue moo_nn_speichern(MooValue netz, MooValue pfad);
MooValue moo_nn_laden(MooValue pfad);
MooValue moo_nn_safetensors(MooValue pfad);   // F1: Fremd-Import -> Dict {name: Tensor}
// KIP-E2: CPU-Voll-Checkpoint v2 (moo_nn_easy.c) — atomischer .mook-Superset:
// Modell-Gewichte + Optimizer (m/v/t) + Dropout-Zaehler + Dataloader-Position
// + Schritt + Tokenizer-/Arch-Version. Resume bit-identisch (f32; bf16 via D3).
MooValue moo_nn_ckpt_speichern(MooValue zustand, MooValue pfad);            // atomisch (tmp+rename)
MooValue moo_nn_ckpt_laden(MooValue pfad, MooValue erwartungen);           // erwartungen=none|Dict -> Versions-Check wirft bei Mismatch
MooValue moo_nn_ckpt_rotieren(MooValue verzeichnis, MooValue praefix, MooValue behalte);  // letzte N + best behalten
// GPU2 (Plan-014, moo_ki_gpu.c): Vulkan-Compute fuer matmul/elementwise/
// Voll-Reduktion. false = nicht ausgefuehrt (kein Vulkan-Build, unter
// Schwelle, Init-/Laufzeitfehler) -> Aufrufer nimmt den CPU-Pfad.
// ENV: MOO_KI_GPU=0 (aus), MOO_KI_GPU_ERZWINGEN=1 (Schwellen ignorieren).
bool moo_ki_gpu_matmul(const float* a, const float* b, float* o, int32_t m, int32_t k, int32_t n);
bool moo_ki_gpu_ew(int32_t op, const float* a, const float* b, float* o, int64_t n);
bool moo_ki_gpu_reduce_sum(const float* a, int64_t n, double* out_summe);
// Daten-Pipeline (Plan-014 E1, moo_dataset.c): MNIST-IDX (entpackt, siehe
// skripte/mnist_download.sh), Zahlen-CSV, PGM/PPM-Bilder (eigener Reader —
// Entscheid E1 statt stb/SDL_image), seed-deterministisches Mischen,
// Normalisieren (minmax/standard, ohne Tape).
MooValue moo_ds_mnist(MooValue prefix);
MooValue moo_ds_csv(MooValue pfad);
MooValue moo_ds_bild(MooValue pfad);
MooValue moo_ds_mischen(MooValue x, MooValue y, MooValue seed);
MooValue moo_ds_normalisieren(MooValue t, MooValue art);
MooValue moo_ds_tokenizer(MooValue text);

typedef struct {
    int32_t  refcount;   // MUSS erstes Feld sein (Refcount-Konvention)
    int32_t  width;
    int32_t  height;
    int      format;     // MOO_FRAME_FMT_*
    int      stride;     // Bytes pro Zeile
    uint8_t* pixels;     // width*height*4 Bytes (RGBA8), top-left origin
} MooFrame;
#define MV_FRAME(v)  ((MooFrame*)moo_val_as_ptr(v))

// Frame-API (moo_frame.c). new uebernimmt den uebergebenen Pixelbuffer (take
// ownership). save_bmp/pixel sind backend-agnostisch und immer verfuegbar.
MooValue moo_frame_new_take(int width, int height, uint8_t* rgba_pixels_top_left);
// Liest einen Pixel aus einem MOO_FRAME (NUR Frame, kein Fenster). Out-of-bounds
// / Nicht-Frame -> moo_throw. Liefert Dict {rot,gruen,blau,alpha} 0..255.
MooValue moo_frame_read_pixel(MooValue frame, MooValue x, MooValue y);
// Pixel-Dict { rot,gruen,blau,alpha } direkt aus Frame-Koordinaten (keine
// Bounds-/Typpruefung — Aufrufer garantiert gueltige x/y). Fuer den Fenster-
// Pfad in moo_test_api.c (moo_test_pixel).
MooValue moo_frame_pixel_dict(const MooFrame* f, int x, int y);
MooValue moo_test_frame_save_bmp(MooValue frame, MooValue pfad);
// Folgende sind im test_*-Layer (moo_test_api.c, nur 3D-Build) implementiert:
//   moo_test_frame_grab(win) -> MOO_FRAME (Backend-Grab, RGBA top-left)
//   moo_test_pixel(frame_oder_win, x, y) -> Farbe (Frame direkt ODER Fenster grabben)
MooValue moo_test_frame_grab(MooValue win);
MooValue moo_test_pixel(MooValue frame_oder_win, MooValue x, MooValue y);

// === GIF-Recorder (opaker Heap-Typ MOO_GIF, Plan-008 A3B) ===
// Wrappt den isolierten Encoder-Kern (moo_gif.c / MooGifWriter*, frame-bounded:
// streamt direkt in die Datei, NIE eine Frame-Sequenz im RAM). Der Handle ist
// ein refcounteter moo-Heap-Wert; refcount MUSS erstes Feld sein. Wird ueber
// moo_release()/MOO_GIF -> moo_gif_handle_free() freigegeben (schliesst dort
// einen ggf. noch offenen Writer = sicherer Trailer + fclose, kein Leak).
typedef struct MooGifWriter MooGifWriter; // Vorwaerts-Decl (Definition: moo_gif.c)
typedef struct {
    int32_t       refcount;  // MUSS erstes Feld sein (Refcount-Konvention)
    MooGifWriter* writer;    // offener Encoder oder NULL nach test_gif_ende
} MooGifHandle;
#define MV_GIF(v)  ((MooGifHandle*)moo_val_as_ptr(v))

// test_gif_*-Builtins (moo_test_api.c, nur 3D-Build — brauchen Fenster-Grab):
//   test_gif_start(win_oder_frame, pfad, fps) -> MOO_GIF (Dimensionen aus erstem
//                                                Grab/Frame; oeffnet die Datei)
//   test_gif_frame(gif, frame_oder_win)        -> Bool (grabbt bzw. nutzt MOO_FRAME,
//                                                streamt 1 Frame in die Datei)
//   test_gif_ende(gif)                         -> Bool (Trailer + close, Writer frei)
MooValue moo_test_gif_start(MooValue win_oder_frame, MooValue pfad, MooValue fps);
MooValue moo_test_gif_frame(MooValue gif, MooValue frame_oder_win);
MooValue moo_test_gif_ende(MooValue gif);

// === MP4-Video-Recorder (opaker Heap-Typ MOO_VIDEO, Plan-009 V0) ===
// Wrappt den isolierten ffmpeg-Pipe-Kern (moo_video.c / MooVideoWriter*,
// frame-bounded: piped jedes Frame direkt nach ffmpeg-stdin, NIE eine Frame-
// Sequenz im RAM). Der Handle ist ein refcounteter moo-Heap-Wert; refcount MUSS
// erstes Feld sein. Wird ueber moo_release()/MOO_VIDEO -> moo_video_handle_free()
// freigegeben (schliesst dort einen ggf. noch offenen Writer = stdin close +
// waitpid, kein Leak/Zombie). ffmpeg laeuft als fork/execvp-Kindprozess (kein
// popen, keine Shell). Definition des Writers: moo_video.c (P009-V0).
typedef struct MooVideoWriter MooVideoWriter; // Vorwaerts-Decl (Def: moo_video.c)
typedef struct {
    int32_t         refcount;  // MUSS erstes Feld sein (Refcount-Konvention)
    MooVideoWriter* writer;    // offener Encoder oder NULL nach test_video_ende
} MooVideoHandle;
#define MV_VIDEO(v)  ((MooVideoHandle*)moo_val_as_ptr(v))

// MOO_VIDEO-Handle-Wrapper (moo_video_handle.c, immer gebaut). Nimmt einen
// offenen MooVideoWriter* (take ownership) und liefert einen MOO_VIDEO-Wert
// bzw. NONE bei NULL/Speichermangel (schliesst den Writer dann sicher).
MooValue moo_video_handle_new(MooVideoWriter* writer);
// Refcount-0-Destruktor, aus moo_release()/MOO_VIDEO aufgerufen.
void moo_video_handle_free(void* ptr);

// test_video_*-Builtins (moo_test_api.c, nur 3D-Build - brauchen Fenster-Grab,
// Plan-009 V1): test_video_start(win_oder_frame, pfad, fps) -> MOO_VIDEO;
// test_video_frame(video, frame_oder_win) -> Bool; test_video_ende(video) -> Bool.
// Symbole immer deklariert (wie test_gif_*); C-seitig nur im 3D-Build gelinkt.
MooValue moo_test_video_start(MooValue win_oder_frame, MooValue pfad, MooValue fps);
MooValue moo_test_video_frame(MooValue video, MooValue frame_oder_win);
MooValue moo_test_video_ende(MooValue video);

// === String ===
struct MooString {
    int32_t refcount;
    char* chars;
    int32_t length;
    int32_t capacity;
};

// === List ===
struct MooList {
    int32_t refcount;
    bool frozen;
    MooValue* items;
    int32_t length;
    int32_t capacity;
};

// === Dict (einfache Hash-Map) ===
typedef struct {
    MooString* key;
    MooValue value;
    bool occupied;
} MooDictEntry;

struct MooDict {
    int32_t refcount;
    bool frozen;
    MooDictEntry* entries;
    int32_t count;
    int32_t capacity;
};

// === Function ===
// Backward-compatible layout: bestehende Konsumenten (moo_thread, moo_object,
// moo_curry) nutzen weiter refcount/fn_ptr/arity/name. Neue Felder sind
// optional und steuern den Closure-Trampoline:
//   n_captured == 0 → fn_ptr ist eine direkte Function (klassisches Lambda /
//                     benannte Funktion). Call-Convention: fn_ptr(MooValue...).
//   n_captured >  0 → fn_ptr ist ein Trampoline. Call-Convention:
//                     fn_ptr(MooFunc* env, MooValue...).
//                     Der Trampoline liest env->captured[i] und ruft die
//                     eigentliche inner-Function mit (params..., captures...).
struct MooFunc {
    int32_t refcount;
    void* fn_ptr;
    int32_t arity;      // User-sichtbare Arity (ohne Captures)
    char* name;
    MooValue* captured; // Capture-Array (retain-t) oder NULL
    int32_t n_captured; // Anzahl gebundener Captures oder 0
};

// === Object ===
typedef struct {
    MooString* name;
    MooValue value;
} MooProperty;

struct MooObject {
    int32_t refcount;
    bool frozen;
    char* class_name;
    MooProperty* properties;
    int32_t prop_count;
    int32_t prop_capacity;
    MooObject* parent;
};

// === Thread ===
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

struct MooThread {
    int32_t refcount;
#ifdef _WIN32
    HANDLE thread;
    DWORD thread_id;
    MooValue* retval;
#else
    pthread_t thread;
#endif
    MooValue result;
    bool done;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
};

// === Channel (Go-Style, buffered) ===
struct MooChannel {
    int32_t refcount;
    MooValue* buffer;
    int32_t capacity;
    int32_t count;
    int32_t read_pos;
    int32_t write_pos;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE not_empty;
    CONDITION_VARIABLE not_full;
#else
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
#endif
    bool closed;
};

// === Thread-Funktionen ===
MooValue moo_thread_spawn(MooValue func, MooValue arg);
MooValue moo_thread_wait(MooValue thread);
MooValue moo_thread_done(MooValue thread);

// === Channel-Funktionen ===
MooValue moo_channel_new(MooValue capacity);
void moo_channel_send(MooValue channel, MooValue value);
MooValue moo_channel_recv(MooValue channel);
void moo_channel_close(MooValue channel);

// === Fehlerbehandlung ===
#define MOO_TRY_STACK_SIZE 64
extern jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
extern int moo_try_depth;
extern MooValue moo_last_error;

// === Konstruktoren ===
MooValue moo_number(double n);
MooValue moo_bool(bool b);
MooValue moo_none(void);
MooValue moo_error(const char* msg);

// === Typ-Pruefung ===
bool moo_is_number(MooValue v);
bool moo_is_string(MooValue v);
bool moo_is_bool(MooValue v);
bool moo_is_none(MooValue v);
bool moo_is_list(MooValue v);
bool moo_is_dict(MooValue v);
bool moo_is_truthy(MooValue v);
const char* moo_type_name(MooValue v);

// === Konvertierungen ===
double moo_as_number(MooValue v);
bool moo_as_bool(MooValue v);

// === String-Funktionen ===
MooValue moo_string_new(const char* chars);
/* Binary-safe: Laenge explizit, NUL-Bytes im Inhalt erlaubt (kein strlen). */
MooValue moo_string_new_len(const char* chars, int32_t len);
MooValue moo_string_concat(MooValue a, MooValue b);
MooValue moo_string_length(MooValue s);
MooValue moo_string_index(MooValue s, MooValue idx);
MooValue moo_string_compare(MooValue a, MooValue b);
MooValue moo_string_contains(MooValue haystack, MooValue needle);
MooValue moo_string_split(MooValue s, MooValue delim);
MooValue moo_string_replace(MooValue s, MooValue old_s, MooValue new_s);
MooValue moo_string_trim(MooValue s);
MooValue moo_string_upper(MooValue s);
MooValue moo_string_lower(MooValue s);
MooValue moo_string_slice(MooValue s, MooValue start, MooValue end);

// === Listen-Funktionen ===
MooValue moo_list_new(int32_t initial_capacity);
MooValue moo_list_from(MooValue* items, int32_t count);
void moo_list_append(MooValue list, MooValue item);
MooValue moo_list_get(MooValue list, MooValue index);
void moo_list_set(MooValue list, MooValue index, MooValue value);
MooValue moo_list_length(MooValue list);
MooValue moo_list_pop(MooValue list);
MooValue moo_list_contains(MooValue list, MooValue item);
MooValue moo_list_reverse(MooValue list);
MooValue moo_list_sort(MooValue list);
MooValue moo_list_join(MooValue list, MooValue delim);
int32_t moo_list_iter_len(MooValue list);
MooValue moo_list_iter_get(MooValue list, int32_t index);

// === Dict-Funktionen ===
MooValue moo_dict_new(void);
MooValue moo_dict_get(MooValue dict, MooValue key);
void moo_dict_set(MooValue dict, MooValue key, MooValue value);
MooValue moo_dict_has(MooValue dict, MooValue key);
MooValue moo_dict_keys(MooValue dict);
MooValue moo_dict_values(MooValue dict);
MooValue moo_dict_length(MooValue dict);
void moo_dict_remove(MooValue dict, MooValue key);

// === Objekt-Funktionen ===
MooValue moo_object_new(const char* class_name);
MooValue moo_object_get(MooValue obj, const char* prop);
void moo_object_set(MooValue obj, const char* prop, MooValue value);
void moo_object_set_parent(MooValue obj, MooValue parent);

// === Event-System ===
void moo_event_on(MooValue obj, MooValue event_name, MooValue callback);
void moo_event_emit(MooValue obj, MooValue event_name);

// === Immutable/Freeze ===
MooValue moo_freeze(MooValue v);
MooValue moo_is_frozen(MooValue v);

// === Currying ===
MooValue moo_curry(MooValue func, MooValue arg);
MooValue moo_func_new(void* fn_ptr, int32_t arity, const char* name);
MooValue moo_func_with_captures(void* tramp_ptr, int32_t arity,
                                const char* name,
                                MooValue* caps, int32_t n);
MooValue moo_func_captured_at(MooFunc* fn, int32_t i);
MooValue moo_func_call_0(MooValue func);
MooValue moo_func_call_1(MooValue func, MooValue a0);
MooValue moo_func_call_2(MooValue func, MooValue a0, MooValue a1);
MooValue moo_func_call_3(MooValue func, MooValue a0, MooValue a1, MooValue a2);
MooValue moo_func_call_4(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3);
MooValue moo_func_call_5(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4);
MooValue moo_func_call_6(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5);
MooValue moo_func_call_7(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5, MooValue a6);
MooValue moo_func_call_8(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5, MooValue a6, MooValue a7);

// === Arithmetik & Vergleiche ===
MooValue moo_add(MooValue a, MooValue b);
MooValue moo_sub(MooValue a, MooValue b);
MooValue moo_mul(MooValue a, MooValue b);
MooValue moo_div(MooValue a, MooValue b);
MooValue moo_mod(MooValue a, MooValue b);
MooValue moo_pow(MooValue a, MooValue b);
MooValue moo_neg(MooValue v);
MooValue moo_eq(MooValue a, MooValue b);
MooValue moo_neq(MooValue a, MooValue b);
MooValue moo_lt(MooValue a, MooValue b);
MooValue moo_gt(MooValue a, MooValue b);
MooValue moo_lte(MooValue a, MooValue b);
MooValue moo_gte(MooValue a, MooValue b);
MooValue moo_and(MooValue a, MooValue b);
MooValue moo_or(MooValue a, MooValue b);
MooValue moo_not(MooValue v);

// === Bitwise ===
MooValue moo_bitand(MooValue a, MooValue b);
MooValue moo_bitor(MooValue a, MooValue b);
MooValue moo_bitxor(MooValue a, MooValue b);
MooValue moo_bitnot(MooValue v);
MooValue moo_lshift(MooValue a, MooValue b);
MooValue moo_rshift(MooValue a, MooValue b);

// === Raw Memory (GEFAEHRLICH) ===
MooValue moo_mem_read(MooValue addr, MooValue size);
void moo_mem_write(MooValue addr, MooValue value, MooValue size);

// === CPU / Hardware Builtins ===
void moo_cpu_halt(void);
void moo_cpu_cli(void);
void moo_cpu_sti(void);
MooValue moo_io_inb(MooValue port);
void moo_io_outb(MooValue port, MooValue data);

// === Ausgabe ===
void moo_print(MooValue v);
MooValue moo_to_string(MooValue v);

// === Fehlerbehandlung ===
void moo_throw(MooValue error);
void moo_try_enter(void);       // enter try block
int moo_try_check(void);        // 1 = error occurred
void moo_try_leave(void);       // leave try/catch
MooValue moo_get_error(void);   // get the caught error
extern int moo_error_flag;

// === Speicher ===
void* moo_alloc(size_t size);
void* moo_realloc(void* ptr, size_t size);
void moo_free(void* ptr);

// === Checked-Arithmetik fuer Allokationsgroessen (Plan-007 P007-U3) ===
// Container-Laengen/Kapazitaeten sind int32_t (moo_runtime.h). Additive bzw.
// multiplikative Groessenrechnungen koennen bei User-/Datengetriebenen Werten
// signed int32 ueberlaufen (UB) und zu viel zu kleinen Allokationen + Heap-
// Overflow fuehren. Diese Helfer rechnen im breiteren int64_t-Zwischentyp,
// pruefen gegen ein hartes Limit und werfen bei Ueberlauf einen sauberen
// moo-Fehler (moo_throw, kehrt nie zurueck). Rueckgabe ist garantiert ein
// nicht-negatives Ergebnis, das in size_t/int32_t passt.
// Hard-Limit: Ergebnisse muessen <= INT32_MAX bleiben, damit sie weiterhin in
// die int32_t-Laengen/Kapazitaeten der Container passen (kein stiller Trunc).
#define MOO_MAX_ALLOC_SIZE 0x7FFFFFFF  /* INT32_MAX */
int64_t moo_checked_add_i32(int64_t a, int64_t b, const char* ctx);
int64_t moo_checked_mul_i32(int64_t a, int64_t b, const char* ctx);

// === Debugger ===
void moo_breakpoint(MooValue line_num);

// === Stdlib ===
MooValue moo_abs(MooValue v);
MooValue moo_sqrt(MooValue v);
MooValue moo_sin(MooValue v);
MooValue moo_cos(MooValue v);
MooValue moo_tan(MooValue v);
MooValue moo_atan2(MooValue y, MooValue x);
MooValue moo_round(MooValue v);
MooValue moo_floor(MooValue v);
MooValue moo_ceil(MooValue v);
MooValue moo_min(MooValue a, MooValue b);
MooValue moo_max(MooValue a, MooValue b);
MooValue moo_random(void);
MooValue moo_type_of(MooValue v);
MooValue moo_input(MooValue prompt);
MooValue moo_length(MooValue v);
MooValue moo_range(MooValue start, MooValue end);
MooValue moo_time(void);
MooValue moo_syscall(MooValue nr, MooValue arg1, MooValue arg2, MooValue arg3);
// Kern-Builtins (moo_core.c)
void moo_sleep(MooValue duration);
MooValue moo_env(MooValue name);
void moo_exit(MooValue code);
MooValue moo_to_number(MooValue v);
void moo_args_init(int argc, char** argv);
MooValue moo_args(void);
MooValue moo_pid(void);

// === System-Tray ===
MooValue moo_tray_create(MooValue titel, MooValue icon_name);
MooValue moo_tray_menu_add(MooValue tray, MooValue label, MooValue callback);
MooValue moo_tray_menu_clear(MooValue tray);
MooValue moo_tray_timer_add(MooValue interval_ms, MooValue callback);

MooValue moo_gui_fenster(MooValue titel, MooValue breite, MooValue hoehe);
MooValue moo_gui_label(MooValue fenster, MooValue text);
MooValue moo_gui_button(MooValue fenster, MooValue label, MooValue callback);
MooValue moo_gui_label_setze(MooValue label, MooValue text);
MooValue moo_gui_icon_setze(MooValue fenster, MooValue icon_name);
MooValue moo_gui_zeige(MooValue fenster);
MooValue moo_tray_run(void);

// === Datei-I/O ===
MooValue moo_file_read(MooValue path);
MooValue moo_file_write(MooValue path, MooValue content);
MooValue moo_file_append(MooValue path, MooValue content);
MooValue moo_file_lines(MooValue path);
MooValue moo_file_exists(MooValue path);
MooValue moo_file_delete(MooValue path);
MooValue moo_file_mtime(MooValue path);
MooValue moo_file_is_dir(MooValue path);
MooValue moo_file_mkdir(MooValue path);
MooValue moo_dir_list(MooValue path);

// === Kryptografie & Sicherheit ===
MooValue moo_sha256(MooValue input);
MooValue moo_secure_random(MooValue length);
MooValue moo_base64_encode(MooValue input);
MooValue moo_base64_decode(MooValue input);
MooValue moo_sanitize_html(MooValue input);
MooValue moo_sanitize_sql(MooValue input);
MooValue moo_sha256_bytes(MooValue input);
MooValue moo_hmac_sha256(MooValue key, MooValue msg);
MooValue moo_pbkdf2_sha256(MooValue password, MooValue salt, MooValue iterations, MooValue dk_len);

// Universelle Index-Ops (dispatcht nach Container-Typ)
MooValue moo_string_repeat(MooValue s, MooValue count);
MooValue moo_index_get(MooValue container, MooValue index);
void moo_index_set(MooValue container, MooValue index, MooValue value);

// === JSON ===
MooValue moo_json_parse(MooValue json_string);
MooValue moo_json_string(MooValue value);

// === HTTP ===
MooValue moo_http_get(MooValue url);
MooValue moo_http_post(MooValue url, MooValue body);

// === Datenbank ===
MooValue moo_db_connect(MooValue url);
MooValue moo_db_execute(MooValue db, MooValue sql);
MooValue moo_db_query(MooValue db, MooValue sql);
void moo_db_close(MooValue db);

// === Grafik (SDL2) ===
MooValue moo_window_create(MooValue title, MooValue width, MooValue height);
MooValue moo_window_is_open(MooValue window);
void moo_window_clear(MooValue window, MooValue color);
void moo_window_update(MooValue window);
void moo_window_close(MooValue window);
void moo_draw_rect(MooValue win, MooValue x, MooValue y, MooValue w, MooValue h, MooValue color);
void moo_draw_circle(MooValue win, MooValue cx, MooValue cy, MooValue r, MooValue color);
void moo_draw_line(MooValue win, MooValue x1, MooValue y1, MooValue x2, MooValue y2, MooValue color);
void moo_draw_pixel(MooValue win, MooValue x, MooValue y, MooValue color);

// === Grafik Input (SDL2) ===
MooValue moo_key_pressed(MooValue key);
MooValue moo_mouse_x(MooValue window);
MooValue moo_mouse_y(MooValue window);
MooValue moo_mouse_pressed(MooValue window);
void moo_delay(MooValue ms);
void moo_pump_events(void);

// === Test-API (Screenshot, Input-Simulation) ===
MooValue moo_screenshot(MooValue window, MooValue path);
void moo_simulate_key(MooValue key, MooValue pressed);
void moo_simulate_mouse(MooValue x, MooValue y, MooValue click);

// === Sprites (SDL2_Image) ===
MooValue moo_sprite_load(MooValue win, MooValue path);
void moo_sprite_draw(MooValue win, MooValue id, MooValue x, MooValue y);
void moo_sprite_draw_scaled(MooValue win, MooValue id, MooValue x, MooValue y, MooValue w, MooValue h);
void moo_sprite_draw_region(MooValue win, MooValue id, MooValue sx, MooValue sy, MooValue sw, MooValue sh, MooValue dx, MooValue dy, MooValue dw, MooValue dh);
MooValue moo_sprite_width(MooValue id);
MooValue moo_sprite_height(MooValue id);
void moo_sprite_free(MooValue id);

// === 3D Grafik (OpenGL + GLFW) ===
MooValue moo_3d_create(MooValue title, MooValue w, MooValue h);
MooValue moo_3d_is_open(MooValue win);
void moo_3d_clear(MooValue win, MooValue r, MooValue g, MooValue b);
void moo_3d_update(MooValue win);
void moo_3d_close(MooValue win);
void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color);
void moo_3d_cube(MooValue win, MooValue x, MooValue y, MooValue z, MooValue size, MooValue color);
void moo_3d_sphere(MooValue win, MooValue x, MooValue y, MooValue z,
                   MooValue radius, MooValue color, MooValue detail);
void moo_3d_camera(MooValue win, MooValue eyeX, MooValue eyeY, MooValue eyeZ,
                   MooValue lookX, MooValue lookY, MooValue lookZ);
void moo_3d_perspective(MooValue win, MooValue fov, MooValue near_val, MooValue far_val);
void moo_3d_rotate(MooValue win, MooValue angle, MooValue ax, MooValue ay, MooValue az);
void moo_3d_translate(MooValue win, MooValue x, MooValue y, MooValue z);
void moo_3d_push(MooValue win);
void moo_3d_pop(MooValue win);

// === 3D Input ===
MooValue moo_3d_key_pressed(MooValue win, MooValue key);
void moo_3d_capture_mouse(MooValue win);
MooValue moo_3d_mouse_dx(MooValue win);
MooValue moo_3d_mouse_dy(MooValue win);
// === 3D Test-Sim (Plan-008 A1) ===
void moo_3d_simulate_key(MooValue win, MooValue key, MooValue pressed);   // Tri-State
void moo_3d_simulate_mouse_delta(MooValue win, MooValue dx, MooValue dy); // consume-on-read
void moo_3d_simulate_reset(MooValue win);                                 // alle Sim-States reset

// === Einheitlicher Test-API-Layer (Plan-008 A2, tag-dispatch 2D/3D/Hybrid) ===
// Dispatcht ueber window.tag auf 2D-SDL bzw. 3D-Vtable bzw. Hybrid.
// raum_sim_*/moo_simulate_* bleiben als Aliases erhalten (keine Breaking Changes).
void     moo_test_sim_taste(MooValue win, MooValue taste, MooValue gedrueckt);
void     moo_test_sim_maus_pos(MooValue win, MooValue x, MooValue y);
void     moo_test_sim_maus_taste(MooValue win, MooValue taste, MooValue gedrueckt);
void     moo_test_sim_maus_rad(MooValue win, MooValue dy);
void     moo_test_sim_maus_delta(MooValue win, MooValue dx, MooValue dy);
void     moo_test_sim_reset(MooValue win);
MooValue moo_test_screenshot(MooValue win, MooValue pfad);   // S5: wirft statt still false
MooValue moo_test_fenster_info(MooValue win);                // Dict{breite,hoehe,backend,offen}

// === Welt (Game-Dev Runtime) ===
MooValue moo_world_create(MooValue title, MooValue w, MooValue h);
MooValue moo_world_is_open(MooValue world);
void moo_world_update(MooValue world);
void moo_world_close(MooValue world);
void moo_world_seed(MooValue world, MooValue seed);
void moo_world_biome(MooValue world, MooValue name, MooValue h_min, MooValue h_max, MooValue color, MooValue trees);
void moo_world_trees(MooValue world, MooValue biom, MooValue chance);
void moo_world_sun(MooValue world, MooValue x, MooValue y, MooValue z);
void moo_world_fog(MooValue world, MooValue dist);
void moo_world_sea_level(MooValue world, MooValue level);
void moo_world_render_dist(MooValue world, MooValue dist);
void moo_world_time_of_day(MooValue world, MooValue t);

// === Result-Typ ===
MooValue moo_result_ok(MooValue value);
MooValue moo_result_err(MooValue msg);
MooValue moo_result_is_ok(MooValue result);
MooValue moo_result_is_err(MooValue result);
MooValue moo_result_unwrap(MooValue result);

// === Profiler ===
void moo_profile_enter(MooValue name);
void moo_profile_exit(MooValue name);
void moo_profile_report(void);

// === Netzwerk (TCP/UDP) ===
MooValue moo_tcp_server(MooValue port);
MooValue moo_tcp_connect(MooValue host, MooValue port);
MooValue moo_udp_socket(MooValue port);
MooValue moo_socket_accept(MooValue server);
MooValue moo_socket_read(MooValue sock, MooValue max_bytes);
void moo_socket_write(MooValue sock, MooValue data);
void moo_socket_close(MooValue sock);

// === Eval ===
MooValue moo_eval(MooValue code);

// === Hybrid 2D+3D Window (M5: ein Fenster, ein Z-Buffer) ===
// Welt-Einheiten fuer Z (z=0.0 = ground, z=1.0 = ein Tile hoch).
// Runtime mappt intern auf GL-Clip-Range via Ortho/Perspective-Shader.
MooValue moo_hybrid_create(MooValue title, MooValue w, MooValue h);
MooValue moo_hybrid_is_open(MooValue win);
void moo_hybrid_clear(MooValue win, MooValue r, MooValue g, MooValue b);
void moo_hybrid_update(MooValue win);
void moo_hybrid_close(MooValue win);
// 2D-Pass: Pixel-Koordinaten + Welt-Z (Origin links-oben).
void moo_hybrid_rect_z(MooValue win, MooValue x, MooValue y, MooValue z, MooValue w, MooValue h, MooValue color);
void moo_hybrid_line_z(MooValue win, MooValue x1, MooValue y1, MooValue x2, MooValue y2, MooValue z, MooValue color);
void moo_hybrid_circle_z(MooValue win, MooValue cx, MooValue cy, MooValue z, MooValue r, MooValue color);
void moo_hybrid_sprite_z(MooValue win, MooValue id, MooValue x, MooValue y, MooValue z, MooValue w, MooValue h);
MooValue moo_hybrid_sprite_load(MooValue win, MooValue path);

// === Voxel-Welt (Phase 1a: naiver uint16-Kern, opaker C-Heap-Typ) ===
// VoxelWorld lebt als opaker C-Heap-Typ (MOO_VOXELWORLD), niemals als moo-Liste.
// Koordinaten sind moo Numbers -> floor()-Cast auf int32_t (auch negative).
// Chunk-Lookup per floor-div/floor-mod, NICHT C-/ und % (die runden zur Null).
MooValue moo_voxel_welt_neu(MooValue seed);
MooValue moo_voxel_setzen(MooValue welt, MooValue x, MooValue y, MooValue z, MooValue block_id);
MooValue moo_voxel_holen(MooValue welt, MooValue x, MooValue y, MooValue z);
/* Worldgen (Plan-005 RT5): generiert die vertikale Chunk-Saeule fuer die
 * horizontale Chunk-Position (cx,cz) seed-deterministisch (Heightmap via
 * moo_noise_fbm). voxel_holen generiert noch nie beruehrte Chunks ausserdem
 * lazy beim ersten Lesezugriff. */
MooValue moo_voxel_generieren(MooValue welt, MooValue cx, MooValue cz);
MooValue moo_voxel_chunk_entladen(MooValue welt, MooValue x, MooValue y, MooValue z);
MooValue moo_voxel_ram_statistik(MooValue welt);
/* Plan-006 R3 (Mutation/Downgrade): expliziter Optimierungslauf. Stuft alle
 * PALETTE-Sections herab (PALETTE->SOLID/EMPTY wo wieder uniform) und kompaktiert
 * uebrige Paletten; komplett leere Chunks kollabieren auf NULL. Main-Thread-only
 * (K5: nie waehrend aktiver Mesh-Worker). -> Number geaenderter Chunks. Im
 * normalen Spielbetrieb laeuft der Downgrade ohnehin lazy in voxel_aktualisieren. */
MooValue moo_voxel_welt_optimieren(MooValue welt);
// Phase 1b (Mesher): baut/aktualisiert die GPU-Render-Geometrie eines Chunks
// bzw. aller als dirty markierten Chunks. Nur sichtbare Faces + Nachbar-Culling
// ueber Chunk-Grenzen (kein Greedy, kein AO). Render-Chunk-IDs werden NUR bei
// aktivem 3D-Backend angelegt; ohne Backend bleibt es CPU-konsistent (no-op auf GPU).
//   moo_voxel_mesh_bauen(w, cx, cy, cz) -> Number Render-Chunk-ID (-1 = kein Cache)
//   moo_voxel_aktualisieren(w)          -> Number Anzahl neu gemeshter Chunks
MooValue moo_voxel_mesh_bauen(MooValue welt, MooValue cx, MooValue cy, MooValue cz);
MooValue moo_voxel_aktualisieren(MooValue welt);
// Phase 1d (DDA-Raycast + AABB-Overlap, Amanatides-Woo).
//   moo_voxel_strahl(w, ox,oy,oz, dx,dy,dz, max_dist)
//     -> Dict {hit, x, y, z, nx, ny, nz, id, dist} (P0.5-Contract).
//        Ursprung+Richtung in Welt-Float-Koordinaten; Richtung wird intern
//        normalisiert. x/y/z = getroffene Voxel-Koordinate (signed), nx/ny/nz =
//        Face-Normale der Einstiegsseite, dist = Distanz bis Einstiegs-Face.
//        Invalider Handle ODER Null-/nicht-endliche Richtung = moo_throw.
//        Leerer Raum/ueber Reichweite = hit:false. Start-im-Block = hit, dist 0.
//   moo_voxel_aabb(w, minx,miny,minz, maxx,maxy,maxz)
//     -> Dict {hit, count, x, y, z}. Achsenparallele Box (Welt-Float) gegen
//        solide Bloecke. count = Anzahl ueberlappter solider Zellen, x/y/z =
//        erste Treffer-Zelle. min/max-Reihenfolge egal. Invalider Handle ODER
//        nicht-endliche Grenze = moo_throw.
MooValue moo_voxel_strahl(MooValue welt,
                          MooValue ox, MooValue oy, MooValue oz,
                          MooValue dx, MooValue dy, MooValue dz,
                          MooValue max_dist);
MooValue moo_voxel_aabb(MooValue welt,
                        MooValue minx, MooValue miny, MooValue minz,
                        MooValue maxx, MooValue maxy, MooValue maxz);
// Plan-005 1b: 1 = ein 3D-Backend ist aktiv (g_backend && g_ctx), sonst 0.
int moo_3d_backend_active(void);

// === Webserver ===
MooValue moo_web_server(MooValue port);
MooValue moo_web_accept(MooValue server);
MooValue moo_web_respond(MooValue request, MooValue body, MooValue status);
MooValue moo_web_json(MooValue request, MooValue data);
void moo_web_close(MooValue server);
MooValue moo_web_file(MooValue request, MooValue filepath);
MooValue moo_web_template(MooValue request, MooValue html, MooValue vars);

#endif // MOO_RUNTIME_H
