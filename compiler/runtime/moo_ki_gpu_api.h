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
bool moo_ki_gpu_ew_res(int32_t op, void* a, void* b, void* o, int64_t n);
/* Voll-Reduktion einer residenten Eingabe zu einem Host-Skalar. Die
 * Partial-Readback ist inhaerent (Reduktion verlaesst die GPU): submits++
 * (Compute) + downloads++ (Partials). */
bool moo_ki_gpu_reduce_sum_res(void* a, int64_t n, double* out_summe);

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
