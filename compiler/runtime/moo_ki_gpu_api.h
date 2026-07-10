/**
 * moo_ki_gpu_api.h — KIP-G1: GPU-residente Buffer-API + Telemetrie.
 *
 * Erweitert die GPU2-Compute-Schicht (moo_ki_gpu.c) um PERSISTENTE
 * VRAM-Buffers: ein Tensor haelt seinen Vulkan-Buffer ueber mehrere Ops,
 * ohne zwischen den Ops Host<->Device zu kopieren. Damit ist GPU-Residenz
 * messbar (Submit-/Transfer-Zaehler) statt behauptet — die Grundlage fuer
 * das G4-Residenz-Gate (docs/kip/G1-device-modell-design.md §5).
 *
 * Die bestehenden nicht-residenten Ops (moo_ki_gpu_matmul/_ew/_reduce_sum)
 * sind in moo_runtime.h deklariert und bleiben unveraendert (Host-in/out).
 *
 * OHNE MOO_HAS_VULKAN (Default-Build) sind ALLE Funktionen hier Stubs:
 * belegen -> NULL, alle bool-Ops -> false, Telemetrie -> 0. Ein Aufrufer
 * (Tensor-Schicht) sieht damit "keine GPU" und bleibt CPU-resident —
 * Stub-Vertrag identisch zu den bestehenden GPU2-Ops, kein #ifdef noetig.
 */
#ifndef MOO_KI_GPU_API_H
#define MOO_KI_GPU_API_H

#include <stdbool.h>
#include <stdint.h>

/* === KIP-G3d-a: Op-Codes fuer unaere/Skalar/Aktivierungs-Ops ===
 * Von unary_fwd.comp UND unary_bw.comp geteilt (gleiche Codes). Die CPU-
 * Referenzsemantik steht in moo_tensor_ops.c (u_x / ews_op / pow) bzw.
 * moo_autograd.c (bw_x). */
enum {
    MOO_KI_U_ADDS = 0, MOO_KI_U_SUBS = 1, MOO_KI_U_MULS = 2, MOO_KI_U_DIVS = 3,
    MOO_KI_U_EXP = 4,  MOO_KI_U_LOG = 5,  MOO_KI_U_SQRT = 6, MOO_KI_U_NEG = 7,
    MOO_KI_U_POW = 8,  MOO_KI_U_RELU = 9, MOO_KI_U_SIGMOID = 10,
    MOO_KI_U_TANH = 11, MOO_KI_U_GELU = 12
};

/* === KIP-G3a: Softmax/LogSoftmax-Variante (Forward + Backward teilen Codes) === */
enum { MOO_KI_SM_SOFTMAX = 0, MOO_KI_SM_LOGSOFTMAX = 1 };

/* === KIP-G3b: Normalisierungs-Variante (Forward + Backward teilen Codes) === */
enum { MOO_KI_NORM_LAYER = 0, MOO_KI_NORM_RMS = 1 };

/* === KIP-G3d-b: Achsen-Reduktions-Art (sum/mean/max) === */
enum { MOO_KI_RED_SUM = 0, MOO_KI_RED_MEAN = 1, MOO_KI_RED_MAX = 2 };

/* === Residente Buffer-Handles (opaque) ===
 * Ein Handle kapselt ein pool-verwaltetes VRAM/Staging-Paar (GPU3-A/B).
 * Der Tensor besitzt genau EINEN Handle (MooTensor.gpu_buf, opaque void*),
 * gibt ihn in moo_tensor_free an den Pool zurueck (KEIN vkDestroy). */

/* Buffer fuer >= bytes belegen (GPU3-B-Pool). Rueckgabe NULL = keine GPU
 * / Init- oder Allok-Fehler -> Aufrufer bleibt CPU-resident. */
void* moo_ki_gpu_buf_belegen(int64_t bytes);

/* Buffer freigeben (Pool-Rueckgabe). Der synchrone Dispatch garantiert,
 * dass die GPU bei Rueckgabe idle ist (Fence-Wait vor jedem Op-Return),
 * daher ist kein separater In-flight-Wait noetig. NULL ist erlaubt. */
void  moo_ki_gpu_buf_freigeben(void* handle);

/* Host -> Device: memcpy Host->Staging, dann (diskrete GPU) Copy
 * Staging->VRAM mit Barrier fuer nachfolgende Shader-Reads. Zaehlt uploads++. */
bool  moo_ki_gpu_upload(void* handle, const float* src, int64_t bytes);

/* Device -> Host: (diskrete GPU) Copy VRAM->Staging mit Barrier gegen den
 * vorherigen Compute-Write, dann memcpy Staging->Host. Zaehlt downloads++.
 * Das ist der Materialisierungs-/Download-Punkt (G1 §3, host_sichern). */
bool  moo_ki_gpu_download(void* handle, float* dst, int64_t bytes);

/* === Residente Ops ===
 * Ein-/Ausgaben sind BEREITS residente Handles; es findet KEIN Host<->Device-
 * Transfer statt (nur ein Compute-Dispatch, zaehlt submits++). Ein Cross-
 * Submit-Memory-Barrier am Dispatch-Anfang macht Writes vorheriger Ops/Uploads
 * fuer diese Op sichtbar. Rueckgabe false = nicht ausgefuehrt (cpu_fallbacks++). */
bool moo_ki_gpu_matmul_res(void* a, void* b, void* o, int32_t m, int32_t k, int32_t n);
/* KIP-G2: naive Matmul-Variante — NUR fuer den A/B-Mikrobenchmark (alt vs neu),
 * nicht im Produktivpfad. Gleiche Signatur/Semantik wie matmul_res. */
bool moo_ki_gpu_matmul_naiv_res(void* a, void* b, void* o, int32_t m, int32_t k, int32_t n);
bool moo_ki_gpu_ew_res(int32_t op, void* a, void* b, void* o, int64_t n);
/* KIP-G3d-a: unaere/Skalar/Aktivierungs-Forward — o[i] = f(a[i], skalar).
 * skalar nur fuer adds/subs/muls/divs/pow genutzt (sonst ignoriert). op =
 * MOO_KI_U_*. Residente Handles, ein Compute-Dispatch (submits++). */
bool moo_ki_gpu_unary_res(int32_t op, void* a, void* o, int64_t n, float skalar);
/* KIP-G3d-a: zugehoeriger Backward — gin[i] = gout[i] * f'(src[i]). REINER
 * Gradient-Beitrag OHNE Akkumulation (das += ist G3c grad_accumulate). Der
 * Aufrufer MUSS als src den von der Ableitung benoetigten Buffer binden:
 * INPUT x fuer log/pow/relu/gelu, OUTPUT y fuer exp/sqrt/sigmoid/tanh; fuer
 * adds/subs/muls/divs/neg ist f' konstant und src wird ignoriert. */
bool moo_ki_gpu_unary_bw_res(int32_t op, void* src, void* gout, void* gin, int64_t n, float skalar);
/* KIP-G3d-c: 2D-Transponierung — o[C,R] = a[R,C] transponiert (rows=R, cols=C).
 * Backward = derselbe Kernel mit vertauschten Dims: transpose(gout[C,R]) liefert
 * gin[R,C]. Residente Handles, ein Compute-Dispatch. */
bool moo_ki_gpu_transpose_res(void* a, void* o, int32_t rows, int32_t cols);
/* KIP-G3d-c: Kopie o[dst_off + i] = a[src_off + i] (i in [0,n)). Basis fuer
 * reshape (Offsets 0), concat-Forward (dst_off) und concat-Split/Backward
 * (src_off). Offsets in ELEMENTEN. Residente Handles. */
bool moo_ki_gpu_copy_res(void* a, void* o, int64_t n, int64_t src_off, int64_t dst_off);
/* === KIP-G3c: GPU-Gradient-Akkumulation + Optimizer-Schritt ===
 * grad_accum: acc[i] += g[i] auf residenten Buffers. Loest das +=/Fan-out ein,
 * das die reinen Bwd-Beitraege (unary_bw_res u.a.) bewusst weglassen. */
bool moo_ki_gpu_grad_accum_res(void* acc, void* g, int64_t n);
/* SGD-mit-Momentum-Schritt (in-place): m := mu*m + grad; p := p - lr*m.
 * p/m read+write, grad read. Zustand = m (Groesse n). */
bool moo_ki_gpu_opt_sgd_res(void* p, void* m, void* grad, int64_t n,
                            float lr, float momentum);
/* Adam/AdamW-Schritt (in-place), Reihenfolge wie CPU-Referenz moo_nn.c:
 *   [adamw] p -= lr*wd*p;  m := b1*m+(1-b1)*g;  v := b2*v+(1-b2)*g*g;
 *   p -= lr*(m/bc1)/(sqrt(v/bc2)+eps),  bc = 1-beta^t (t 1-basiert, >=1).
 * m und v teilen EINEN residenten Buffer mv der Groesse 2n: m in [0,n),
 * v in [n,2n) (G1 2 — keine getrennte 4. Bindung). adamw!=0 = decoupled decay. */
bool moo_ki_gpu_opt_adam_res(void* p, void* grad, void* mv, int64_t n,
                             float lr, float beta1, float beta2, float eps,
                             float wd, int adamw, int64_t t);
/* === KIP-G3c -> KIP-E2b: Optimizer-Zustands-Download-Schnittstelle ===
 * Der Optimizer-Zustand lebt in AUFRUFER-eigenen residenten Buffers und wird
 * NICHT implizit heruntergeladen (Residenz-Vertrag). Fuer den Checkpoint
 * (E2b) laedt der Aufrufer die Zustands-Buffer mit dem bestehenden
 * moo_ki_gpu_download(handle, dst, bytes) herunter. Layout je Optimizer:
 *   SGD  -> m  : n   floats.
 *   Adam -> mv : 2n  floats (m = mv[0..n), v = mv[n..2n)).
 * Schritt-Zaehler t (Bias-Korrektur) haelt der Aufrufer host-seitig. Restore
 * = moo_ki_gpu_upload derselben Buffer + Weitergabe von t an opt_adam_res. */
/* Voll-Reduktion einer residenten Eingabe zu einem Host-Skalar. Die
 * Partial-Readback ist inhaerent (Reduktion verlaesst die GPU): submits++
 * (Compute) + downloads++ (Partials). */
bool moo_ki_gpu_reduce_sum_res(void* a, int64_t n, double* out_summe);
/* === KIP-G3a: Softmax/LogSoftmax + fused Cross-Entropy (Forward + Backward) ===
 * Alle zeilenweise (ein Thread pro Zeile [rows,cols]), max-shift-stabil wie die
 * CPU-Referenz (moo_tensor_ops.c softmax_impl / moo_nn.c kreuzentropie). */
/* Forward: o = softmax(a) (op=MOO_KI_SM_SOFTMAX) bzw. logsoftmax (LOGSOFTMAX). */
bool moo_ki_gpu_softmax_res(int32_t op, void* a, void* o, int32_t rows, int32_t cols);
/* Backward: op SOFTMAX -> da = y*(g-sum(g*y)); op LOGSOFTMAX -> da = g-exp(y)*sum(g).
 * y = Forward-Ausgang, g = grad_out. REINER Beitrag OHNE += (das += ist G3c). */
bool moo_ki_gpu_softmax_bw_res(int32_t op, void* y, void* g, void* gin,
                               int32_t rows, int32_t cols);
/* Fused CE Forward: loss = mean_i(-sum_j target_ij*logsoftmax(logits)_ij). target
 * = one-hot/soft [rows,cols]. Partial-Readback inhaerent: submits++ + downloads++. */
bool moo_ki_gpu_ce_fwd_res(void* logits, void* target, int32_t rows, int32_t cols,
                           double* out_loss);
/* Fused CE Backward: grad = (softmax(logits) - target) * scale (scale = 1/batch
 * fuer loss=mean). REINER Beitrag OHNE += (das += ist G3c). */
bool moo_ki_gpu_ce_bw_res(void* logits, void* target, void* grad,
                          int32_t rows, int32_t cols, float scale);
/* === KIP-G3b: LayerNorm/RMSNorm-KERN (Forward + Backward, ohne Affine) ===
 * Zeilenweise ueber [rows,cols]. Affine (*gamma [+beta]) ist ew-Komposition
 * (wie CPU-Ref moo_nn.c fw_layernorm/fw_rmsnorm). op = MOO_KI_NORM_LAYER|RMS. */
/* Forward: op LAYER -> (x-mean)/sqrt(var+eps); op RMS -> x/sqrt(mean(x^2)+eps). */
bool moo_ki_gpu_norm_res(int32_t op, void* x, void* o, int32_t rows, int32_t cols, float eps);
/* Backward w.r.t. x (Stats aus x rekonstruiert): op LAYER -> dx=(1/s)(g-mean(g)
 * -n*mean(g*n)); op RMS -> dx=(1/s)(g-n*mean(g*n)). REINER Beitrag OHNE +=. */
bool moo_ki_gpu_norm_bw_res(int32_t op, void* x, void* g, void* dx,
                            int32_t rows, int32_t cols, float eps);
/* === KIP-G3d-b: Achsen-Reduktionen + Broadcast (Fwd-Luecken + Backward) ===
 * Ueber [rows,cols], keepdims (CPU-Ref moo_tensor_ops.c reduce_op / autograd
 * bw_sum/bw_mean/bw_max). axis 0 -> [1,cols], axis 1 -> [rows,1]. */
/* Achsen-Reduktion: op = MOO_KI_RED_SUM|MEAN|MAX. o hat keepdims-Groesse
 * (cols bei axis 0, rows bei axis 1). */
bool moo_ki_gpu_reduce_axis_res(int32_t op, int32_t axis, void* a, void* o,
                                int32_t rows, int32_t cols);
/* Broadcast+Skalierung: out[i,j] = src[axis==0?j:i]*scale. Deckt sum/mean-
 * Reduktions-Backward (scale 1 bzw. 1/rows|1/cols) UND ew-Broadcast-Forward-
 * Baustein ab. REINER Beitrag OHNE +=. */
bool moo_ki_gpu_broadcast_res(int32_t axis, void* src, void* o,
                              int32_t rows, int32_t cols, float scale);
/* Max-Reduktions-Subgradient: g fliesst an die erste argmax-Position je Gruppe.
 * a = Original-Input (argmax), g = grad_out (keepdims), gin = [rows,cols].
 * REINER Beitrag OHNE +=. */
bool moo_ki_gpu_reduce_max_bw_res(int32_t axis, void* a, void* g, void* gin,
                                  int32_t rows, int32_t cols);
/* === KIP-G3d-d: gather (Embedding-Lookup) + deterministische scatter-add ===
 * idx = integer-wertige floats [rows]. CPU-Ref kip-ops T1 gather + moo_autograd
 * bw_gather. */
/* Forward: out[i,d] = W[idx[i], d]. W [vocab,dim], out [rows,dim]. */
bool moo_ki_gpu_gather_res(void* w, void* idx, void* o,
                           int32_t rows, int32_t dim, int32_t vocab);
/* Backward (scatter-add), DETERMINISTISCH als Segment-Reduktion (G0 §2):
 * gW[v,d] = sum_{i: uint(idx[i])==v} g[i,d], sequentiell in i -> 2 Laeufe
 * bit-identisch (KEIN atomicAdd). g [rows,dim], gW [vocab,dim]. Ordnung je
 * Segment identisch zu moo_autograd bw_gather -> bit-exakt vs CPU. REINER
 * Beitrag OHNE += (das += ist G3c). */
bool moo_ki_gpu_scatter_add_res(void* g, void* idx, void* gw,
                                int32_t rows, int32_t dim, int32_t vocab);

/* === Telemetrie (G1 §5 — G4-Beweismittel) ===
 * submits       = Compute-Dispatches (residente + nicht-residente Ops; genau
 *                 1 pro Op). uploads/downloads = EXPLIZITE Transfers ueber die
 *                 residente API (nicht-residente Legacy-Ops falten ihre
 *                 Transfers in den Compute-Submit und zaehlen hier NICHT).
 * cpu_fallbacks = residente Op nicht ausgefuehrt (Init-/Allok-Fehler). */
typedef struct {
    uint64_t submits;
    uint64_t uploads;
    uint64_t downloads;
    uint64_t cpu_fallbacks;
} MooKiGpuTelemetrie;

/* Aktuelle Zaehler in *out kopieren (out != NULL). */
void moo_ki_gpu_telemetrie(MooKiGpuTelemetrie* out);
/* Zaehler auf 0 setzen (Test-Isolation / Steady-State-Messung). */
void moo_ki_gpu_telemetrie_reset(void);

#endif /* MOO_KI_GPU_API_H */
