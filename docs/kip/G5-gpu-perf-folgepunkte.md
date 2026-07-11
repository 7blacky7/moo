# KIP-G5: GPU-Perf-Folgepunkte

Stand: 2026-07-11 · Referenzhardware: NVIDIA RTX 4070 Ti

## Entscheidungen

### Fused Norm

Der produktive LayerNorm-/RMSNorm-Dispatch verwendet GPU-Fusion erst ab
`2^22` Elementen. STRIKT ignoriert die Performance-Schwelle weiterhin.
Die Kalibrierung stammt aus `ki_gpu_g4d_bench.c`:

| Elemente | fused | Komposition | Speedup | Entscheidung |
|---:|---:|---:|---:|---|
| 2^20 | 50,6 ms | 32,7 ms | 0,65x | Komposition/CPU-Pfad |
| 2^22 | 178,9 ms | 221,5 ms | 1,24x | fused GPU |

Die gemeinsame Konstante `MOO_KI_NORM_FUSED_GPU_MIN_ELEMENTS` steuert
Forward und Backward, sodass beide Richtungen dieselbe Shape-Entscheidung
treffen.

### Matmul-Backward

`bw_matmul` benutzt den bereits vorhandenen residenten
`matmul_bw_res`-Beitrag und akkumuliert `da`/`db` jetzt direkt über
`grad_accum_res` in tensor-eigene `gpu_grad`-Buffer. Das gilt auch, wenn nur
ein Operand Gradient benötigt. Der zentrale Backward-Loop materialisiert
`matmul` nicht mehr vorbeugend auf dem Host.

### Gather-Backward

`bw_gather` verwendet den deterministischen residenten Scatter-Add-Kernel
und akkumuliert den reinen Embedding-Beitrag direkt in `gpu_grad`.
Duplikat-Indizes behalten die sequentielle, reproduzierbare Summenreihenfolge.

## Vorher/Nachher-Evidenz

| Produktpfad | Vorher | Nachher | Hardwaregate |
|---|---|---|---|
| bw_matmul symmetrisch | 1 Grad-Upload + 2 Beitrags-Downloads je Node | 3 einmalige Initial-Uploads, 0 Downloads; weitere Fan-out-Beiträge resident | `test_g4c_strikt`: isoliertes Backward-Delta |
| bw_matmul einseitig | CPU-Fallback; STRIKT-Lücke | resident, 0 Downloads, 0 Fallbacks | `test_g4c_strikt` |
| bw_gather | CPU-Scatter-Add | residenter Scatter-Add + Grad-Akkumulation, 0 Downloads/Fallbacks | `test_g4c_strikt`, inklusive Duplikat-Indizes |
| G4 LM | residenter Hand-PoC | Loss-Kurve unverändert grün; GPU 0,0088 s im kleinen Korrektheitsregime | `kip_g4_lm.sh` |
| G4b LM | residenter Hand-PoC | Loss-Kurve unverändert grün; GPU 0,0185 s im kleinen Korrektheitsregime | `kip_g4b_lm.sh` |

Die G4/G4b-Zeiten sind bewusst keine Speedup-Behauptung: ihre winzigen
Korrektheitsformen sind CPU-freundlich. Der G5-Gewinn ist die Entfernung
skalierender Hosttransfers im produktiven Autogradpfad.

## f32_sichern-Audit

Vor G5 rief der zentrale Backward-Loop für jeden Tape-Node
`moo_tensor_f32_sichern(out)` und für alle Inputs auf. `matmul` und `gather`
sind nun – wie die V2b-Bildops – explizite eigene GPU-Trichter. Sie sichern
Hostdaten erst im tatsächlich genommenen CPU-Fallback. Das Hardwaregate
setzt die Telemetrie unmittelbar vor `rueckwaerts` zurück und beweist für
beide Ops `downloads == 0` sowie `cpu_fallbacks == 0`.

## Gates

- `mise run test-compiler`: 60 bestanden, 0 fehlgeschlagen, 40 übersprungen.
- `bash skripte/kip_gpu_coverage.sh`: grün.
- `bash skripte/kip_g4_lm.sh`: GPU/CPU-Kurve und Determinismus grün.
- `bash skripte/kip_g4b_lm.sh`: GQA/RoPE-Kurve und Determinismus grün.
- `bash skripte/kip_g4c_gate.sh`: G5-Produktpfade 27 Checks grün; das Skript
  meldet weiterhin seinen vorbestehenden, G5-fremden Phase-2-Pending-Hinweis
  zum STRIKT-ohne-Vulkan-Moo-Programm.
