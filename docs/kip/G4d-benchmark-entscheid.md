# KIP-G4d-EVAL: Benchmark- und Entscheidungsnachweis (Task 291fb2da)

Autor: claude-web (Koordinator) · Datum: 2026-07-10 · Harness: `tests/ki_gpu_g4d_bench.c` · Hardware: RTX 4070 Ti

## Kontext

Task 291fb2da wurde von koordinator2 (12:46) auf EVAL-Scope reduziert: dedizierte
LayerNorm/RMSNorm-Tape-Ops (WEG 2) nur bei messbarem Nutzen gegenüber der
WEG-1-Komposition. Durch ein Auftrags-Race hatte kip-ops WEG 2 zu dem Zeitpunkt
bereits implementiert und committet (d0ed5f5, transparent gemeldet in Msg 12905,
technisch verifiziert: Gradcheck 560/29, Vulkan-Differentialtest GPU==CPU).
Dieser Nachweis liefert den ursprünglich geforderten Benchmark **nachträglich**
und begründet die Koordinator-Entscheidung.

## Messergebnisse (Fwd+Bwd RMSNorm affine-frei, realistisches Routing ohne STRIKT)

| Größe | Regime | fused (WEG 2) | Komposition (alt) | Speedup | Submits/iter | grad-maxdiff |
|---|---|---|---|---|---|---|
| 8×1024 (n=8k) | CPU (unter Schwelle) | 0.047 ms | 0.109 ms | **2.29×** | 0 / 0 | 3.6e-07 |
| 1024×1024 (n=2^20) | Schwelle | 50.6 ms | 32.7 ms | **0.65×** | 3 / 6 | 8.7e-03 |
| 4096×1024 (n=2^22) | resident | 178.9 ms | 221.5 ms | **1.24×** | 3 / 6 | 2.0e-06 |

Korrektheit: CPU-Kreuzprobe 3.6e-07 beweist mathematische Äquivalenz der
analytischen Backward-Formel (d0ed5f5) mit der komponierten Backward-Kette.

## Nebenbefund: echter G4e-Folgebug (gefixt in 4a00ab1)

Der erste Benchmark-Lauf zeigte grad-maxdiff ≈ 2×iters — Signatur stiller
Akkumulation. Ursache: `moo_tensor_gradient_loeschen` nullte nur den Host-Grad,
ohne `grad_valid` zurückzusetzen. Bei GPU-autoritativem Grad (residentes bw_mul
seit af5dbf3) übersprang `grad_materialisieren_gpu` den Upload und akkumulierte
auf den stalen VRAM-Stand. Fix: Host nach dem Nullen als autoritativ markieren
(I9-Muster, analog I11). Von kip-gpu als vertragskonform bestätigt (Msg 12928);
Lehre: **jede** Stelle, die `->grad` direkt schreibt/nullt, braucht seit af5dbf3
grad_valid-Disziplin.

## Entscheidung (a): Commit d0ed5f5 bleibt

1. **Korrektheit bewiesen** (Gradcheck, Differentialtest, CPU-Kreuzprobe hier).
2. **Messbarer Nutzen in zwei von drei Regimes**: 2.29× im CPU-/Kleintensor-Fall
   (Tape-Overhead: 1 statt 5–6 Nodes — relevant für jede Schicht in jedem
   Trainingsschritt) und 1.24× + halbierte Submits im klar residenten Fall.
3. Der Implementierungsstand war im Channel vor der Task-Umdefinition inhaltlich
   gutgeheißen (12888); ein Rollback hätte verifizierte Arbeit wegen eines
   Task-Text-Races vernichtet.

## Offene Folge-Punkte (kein Blocker, Kandidaten für Folge-KIP)

- **Schwellen-Tuning**: Bei n=2^20 ist der fused GPU-Pfad 0.65× (langsamer als
  der Misch-Pfad der Komposition). Kandidaten: norm_res-Kernel-Auslastung bei
  mittleren Zeilenzahlen prüfen, Residenz-Schwelle für norm_kern ggf. anheben
  (z. B. n≥2^22) oder rows-abhängig wählen.
- **Numerik an der Schwelle**: grad-maxdiff 8.7e-03 bei 1024×1024 stammt aus
  f32-GPU-Statistiken vs. double-CPU-Akkumulation der Vergleichs-Ops — für
  Training unkritisch (Loss-Parität in Kriterium H bewiesen), aber beim
  Schwellen-Tuning mit betrachten.
- Zusammen mit Doc §5 P4 (bw_matmul/bw_gather-Residenz) und der Perf-Beobachtung
  aus dem Epic-Review (unbedingtes f32_sichern pro bw-Node) in einem
  GPU-Perf-Folge-KIP bündeln.
