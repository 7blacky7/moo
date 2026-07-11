# KI-MULTI-V2b — GPU-Residenz- und Dispatch-Entscheid

Stand: 2026-07-11 · Referenzhardware: NVIDIA RTX 4070 Ti · API: Vulkan Compute

## Entscheidung

V2b verwendet GPU-residentes `im2col`/`col2im` plus den vorhandenen geteilten
Matmul-Kern. Pooling besitzt eigene Forward- und Backward-Kernel.

Ein direkter Conv-Kern wurde für diesen Schritt verworfen: Er hätte zusätzlich
zwei unabhängige Backward-Kerne für dX und dW benötigt und damit eine zweite
Conv-Mathematik neben dem bereits vollständig gradgecheckten
`im2col -> matmul`-Graphen geschaffen. Der gemessene Engpass des bisherigen
Pfads war nicht Matmul, sondern CPU-im2col samt Host-Transfers. V2b entfernt
genau diese Materialisierung vom Host. Ein späterer tiled Direct-Conv ist nur
dann gerechtfertigt, wenn ein Profil dieses nun GPU-interne Zwischenarray als
neuen VRAM-/Bandbreitenengpass zeigt.

## Mikrobenchmark

Runner: `bash skripte/kip_gpu_v2b_bench.sh`

Verglichen wurde der bisherige Pfad (CPU-im2col, Upload, GPU-Matmul, Download)
mit residentem GPU-im2col + GPU-Matmul. Form: 28x28, 16 Eingangskanäle,
3x3-Kernel, 32 Ausgangskanäle, 20 Iterationen.

| Batch | bisher | resident | Speedup | Transfers im Loop |
|---:|---:|---:|---:|---:|
| 1 | 0,988 ms | 0,157 ms | 6,31x | 20 Uploads + 20 Downloads → 0 + 0 |
| 8 | 4,184 ms | 0,229 ms | 18,29x | 20 + 20 → 0 + 0 |
| 32 | 17,113 ms | 0,211 ms | 80,93x | 20 + 20 → 0 + 0 |

Daraus folgt für CPU-Eingaben eine Anfangsschwelle von 2^16
im2col-Ausgabeelementen. Ist die Eingabe bereits GPU-resident oder STRIKT aktiv,
wird unabhängig von der Schwelle resident geroutet. Pooling bleibt nach einer
residenten Faltung ebenfalls resident. Das Hardware-Gate protokolliert
`cpu_fallbacks == 0`; STRIKT wirft bei jedem Routingfehler.

## Korrektheitsgates

- `kip_gpu_v2b.sh`: CPU/GPU-Differential, col2im-Adjungiertheit,
  Max-/Mittel-Pooling F/B und null Kerntransfers.
- `kip_gpu_v2b_e2e.sh`: Tensor-/Autograd-End-to-End unter STRIKT,
  exakte Conv-/Pooling-Gradienten und null CPU-Fallbacks.
- `mise run test-compiler`: 60/0/40.
- `tests/run_sanitize.sh ubsan`: vollständige Runtime-Matrix grün.
- ASan Tensor-E2E: grün mit deaktivierter Treiber-LSan-Auswertung; Address-Checks
  bleiben aktiv. Der globale ASan-Runner meldet weiterhin bekannte Leaks seines
  simulierten Throw-Modells in mehreren älteren Fehlerpfad-Harnesses.
