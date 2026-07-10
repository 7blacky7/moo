# KIP-G0 — GPU-Autograd-Vertrag + Op-Coverage-Inventur

Stand: 2026-07-10. Task de0dc515. Status: Entwurf, GPT-Review pending.
Hartes Start-Gate für KIP-G4. Baut auf docs/kip/G1-device-modell-design.md.

## 1. Gradient-Device-Vertrag
- Heute: backward akkumuliert ausschließlich in Host-`float* grad`
  (moo_autograd.c), 23 direkte grad[-Zugriffe (D0-Inventur).
- Vertrag: Der Tape/Autograd-KERN bleibt unverändert (Node-Aufzeichnung,
  Traversierung, Refcounts). Nur die backward-FUNKTIONEN der Ops werden
  device-fähig: eine bw-Funktion, deren Eingänge/Ausgang GPU-resident sind
  UND deren Gradient-Kernel existiert (G3a/b/c), schreibt in `gpu_grad`
  (G1-Feld); sonst CPU-Pfad in `grad` wie heute. Akkumulation (+=, Fan-out)
  braucht einen GPU-Kernel `grad_accumulate` (Teil von G3c).
- Mischverbot analog G1: Gradient-Gültigkeit läuft über die EIGENE Maske
  `grad_valid` (G1 §1, korrigiert 49573fb4) — NIE über die Daten-Maske;
  Optimizer-Schritt (G3c) konsumiert dort, wo grad_valid zeigt.
- MOO_KI_GPU_STRIKT=1 (G4): CPU-Gradient-Fallback im Hotpath = Fehler.

## 2. Op-Coverage-Inventur für das M-A-Zielmodell (P0)
M-A-Modell: Gather-Embedding (T1) → RoPE-Attention (B2, GQA) → RMSNorm (B1)
→ SwiGLU (B3) → Head → fused CE. Benötigte Registry-Ops + Status:

| Op | GPU-Fwd heute | GPU-Bwd heute | Plan |
|---|---|---|---|
| matmul | JA (naiv) | nein | Bwd = 2 matmuls (g@b^T, a^T@g) → fällt mit G2/G3c ab |
| add / sub / mul / div | JA (nur gleiche Shapes) | nein | Bwd elementwise; BROADCAST-Fälle fehlen auch im Fwd → G3d |
| adds/subs/muls/divs (Skalar) | nein | nein | G3d, trivial (elementwise-Shader-Variante) |
| sum / mean | JA (Voll-Reduktion) | nein | Achsen-Reduktion (keepdims) fehlt im Fwd → G3d; Bwd = Broadcast |
| softmax / logsoftmax | nein | nein | G3a (zeilenweise, F+B) |
| exp / log / sqrt / neg / pow | nein | nein | G3d elementwise (RMSNorm-Komposition braucht sqrt/pow) |
| relu / sigmoid / tanh / gelu | nein | nein | G3d elementwise (SwiGLU braucht sigmoid) |
| transpose | nein | nein | G3d (Kernel oder Stride-Trick; Attention braucht ihn) |
| reshape | nein | nein | GPU: Metadaten-Op OHNE Kopie falls contiguous — sonst Kopie-Kernel; Entscheid in G3d |
| concat | nein | nein | G3d (Head-Merge in Attention) |
| max / maximum | nein | nein | nur falls M-A-Pfad ihn nutzt — prüfen; sonst raus aus Pflichtliste |
| gather (T1, neu) | — | — | Fwd-Kernel + Bwd scatter-add. REFERENZ = deterministische SEGMENT-REDUKTION (sortiert nach Ziel-Index, sequentielle Akkumulation pro Segment — bit-reproduzierbar); Atomic-CAS-Float-Add nur als OPTIONALER schneller Pfad mit eigenem Toleranz-/Determinismusvertrag (GPT 49573fb4). Pflicht für G4 (Embedding-Pfad). |
| RMSNorm / CE (Kompositionen) | — | — | G3b/G3a als FUSED Kernel (schneller) ODER via Einzel-Ops — Entscheid nach G3d-Mikrobenchmark |

## 3. Maschinenprüfbare Coverage-Liste (das G4-Start-Gate)
Artefakt: skripte/kip_gpu_coverage.sh — führt das M-A-Modell mit
MOO_KI_GPU_STRIKT=1 + MOO_KI_GPU_ERZWINGEN=1 aus. ERFOLGSKRITERIEN
(erweitert, GPT 49573fb4 — cpu_fallbacks==0 allein reicht nicht, ein
abgebrochener Pfad wäre falsch grün):
1. Prozess-Exit 0.
2. MINDESTENS ZWEI vollständige Schritte Forward+Backward+Optimizer
   (Steady-State, nicht nur Prefill).
3. Loss und Parameter nach jedem Schritt finit (isfinite-Prüfung).
4. cpu_fallbacks == 0.
5. ERWARTETE Op-Namen tatsächlich in der Telemetrie beobachtet
   (Positivliste aus diesem Doc — beweist, dass der Pfad wirklich lief).
6. uploads/downloads nur im erlaubten Randbereich (Batch rein, Loss raus;
   Grenzwerte im Skript dokumentiert).
Der Report listet bei Rot die fehlenden/unerwarteten Ops. Die Tabelle in §2
bleibt ERKLÄRENDE Inventur — das Skript ist das harte Gate.

## 4. Konsequenz für den Task-Graph
- G3d wird als EPIC/PARENT mit VIER SLICES angelegt (GPT 49573fb4 — kein
  Großauftrag an einen Agenten): (a) elementwise + Skalar-Varianten +
  Aktivierungen; (b) Reduktionen mit Achse + Broadcast-Backward;
  (c) Layout-Ops transpose/reshape/concat; (d) gather/scatter-add
  (Segment-Reduktion-Referenz). Das Coverage-Skript (§3) schließt den Parent.
  Anlage NACH GPT-Gegenreview dieses korrigierten Docs.
- Reihenfolge Strang G bestätigt: G1-PoC → G2 (tiled matmul) → G3a/b/c +
  G3d-Slices parallelisierbar → Coverage-Skript grün → G4.

## 5. Exit-Gate
GPT-Erstreview 49573fb4: Coverage-per-Skript BESTÄTIGT; Korrekturen
eingearbeitet (grad_valid getrennt, erweiterte Skript-Kriterien,
Segment-Reduktion als gather-Referenz, G3d als Epic+4 Slices). Kurzes
GPT-GEGENREVIEW pending; danach G3d-Anlage. G4 bleibt gesperrt bis Skript grün.
