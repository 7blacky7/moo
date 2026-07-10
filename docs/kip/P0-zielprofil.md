# KIP-P0 — Zielprofil + Skalierungs-Meilensteine (moo LLM-Runtime)

Stand: 2026-07-10. Task 2967dfb5. Status: Entwurf, GPT-Review pending.
Dieses Dokument blockiert per Task-Graph v2.1 den Implementierungsstart aller D/G/E-Tasks.

## 1. Live-Hardware-Inventur (2026-07-10, Job 8ab1121e)

| Ressource | Wert | Quelle |
|---|---|---|
| GPU (dieser PC) | 1x NVIDIA RTX 4070 Ti, 12282 MiB VRAM | nvidia-smi |
| Vulkan | apiVersion 1.4.329, Gerät erkannt | vulkaninfo |
| CPU | 32 Threads (Ryzen 9 3950X, 16C/32T) | nproc |
| RAM | 31 GiB gesamt, ~13 GiB verfügbar (Desktop-Last) | free |
| Disk | 1,3 TiB frei auf /home (NVMe) | df |

WICHTIG (X1-relevant): Die RTX 2070 (8 GB) ist auf DIESEM PC NICHT vorhanden — sie
steckt im Unraid-Server. Lokales Multi-GPU existiert Stand heute NICHT. Jede
Multi-GPU-Strategie wäre Multi-NODE über LAN — das verschiebt X1 deutlich Richtung
"kein Multi-GPU, Gradient Accumulation" als Default-Empfehlung. Entscheid bleibt
bei KIP-X1 mit erneuter Inventur.

## 2. Meilensteine

Prinzip: Jeder Meilenstein hat ein hartes, deterministisches Exit-Gate. Kein
Meilenstein wird übersprungen. Parameterzahlen sind Zielkorridore, keine Fetische.

### M-A: 10M-Klasse ("Beweis der Pipeline")
- Modell: Dense-Transformer, ~10M Parameter. dim 256, 6 Layer, 8 Köpfe (GQA 4:1
  optional), Kontext 1024, Vokabular 8k BPE (KIP-T2).
- Bausteine: RMSNorm (B1), RoPE (B2), SwiGLU (B3), Gather-Embedding (T1),
  KV-Cache (vorhanden).
- Training: CPU-Referenz + GPU-resident (G4-PoC-Modell IST dieses Modell),
  f32 zuerst, bf16-Storage als Vergleichslauf (D2).
- Daten: 50–200M Token aus Shard-Format (E1); Korpus-Wahl separat (öffentlich,
  lizenzklar — z.B. Wikipedia-DE/EN-Auszug; Entscheid bei E1).
- Budget: < 24h Wanduhr auf der 4070 Ti für einen vollen Lauf (ZIELWERT,
  kein Exit-Gate — Tokenmenge/Laufbudget werden nach der G4-tokens/s-Messung
  kalibriert; GPT-Batch-Review 49573fb4).
- EXIT-GATE: Val-Loss-Kurve deterministisch reproduzierbar (2 Läufe identisch
  auf CPU, GPU innerhalb Toleranz); Resume-Beweis (E2) mitten im Lauf;
  Sampling erzeugt kohärente Wortfolgen (protokolliert, kein hartes Gate);
  Perplexity-Messung auf Held-out-Split etabliert.

### M-B: 100M-Klasse ("ernsthaftes kleines LM")
- Modell: Dense ~100–125M (GPT-2-small-Klasse). dim 768, 12 Layer, Kontext 2048,
  Vokabular 32k, WEIGHT-TYING Embedding = LM-Head (ohne Tying kämen 32k×768
  ≈ 24,6M Parameter doppelt hinzu — Korridor risse; Parameterformel wird im
  M-B-Start ausgewiesen). Optional MoE-Variante (vorhandene moe-Schicht) als Vergleich.
- VRAM-Rechnung (f32, Adam): Gewichte 0,5 GB + Grads 0,5 GB + Adam m/v 1,0 GB
  = 2,0 GB statisch + Aktivierungen. Mit bf16-Storage (D2) + Packing (B4a)
  bequem in 12 GB. Activation Checkpointing (B4b) hier noch NICHT nötig.
- Daten: 1–3B Token, Streaming-Shards zwingend (E1), Sequence Packing (B4a).
- Budget: Tage, nicht Wochen. Tokens/s-Ziel wird nach G4-Messung festgelegt
  (kein Blindwert vor dem PoC — G4 liefert die Baseline-Zahl).
- EXIT-GATE: Perplexity schlägt M-A klar auf gleichem Held-out; Voll-Checkpoint-
  Rotation über mehrtägigen Lauf beweist Robustheit; SFT-Mini-Lauf (X3-Vertrag)
  auf Chat-Template funktioniert mechanisch.

### M-C: 1B-Klasse ("Stretch, nur nach M-B-Review")
- f32-Training scheitert an 12 GB (4+4+8 GB nur Zustände). Nur denkbar mit:
  bf16-Storage + f32-Master im RAM (Offload), Activation Checkpointing (B4b),
  Gradient Accumulation.  Realistisch ist M-C zunächst INFERENZ-Meilenstein
  (Fremdmodell-Import via D3 — NUR für Architekturen/Tokenizer-Konfigurationen,
  die moo abbildet; D3 importiert GEWICHTE, keine fremden Architekturen —
  + eigenes Serving via X2-Verträge).
- KEIN Task wird jetzt für M-C-Training angelegt. Entscheid nach M-B-Review.

## 3. Grundsatzentscheide

| Frage | Entscheid | Begründung |
|---|---|---|
| Dense oder MoE zuerst | DENSE | MoE existiert als Toy; Dense-Baseline ist der Referenzpfad, MoE ab M-B als Vergleichslauf |
| Vokabular | 8k (M-A), 32k (M-B), byte-level BPE | KIP-T2; klein starten hält Embedding/Head-Anteil ehrlich |
| DType-Pfad | f32 Referenz immer; bf16-Storage ab M-A-Vergleich; fp8/int8/int4 NICHT im KIP-Scope | KIP-D0/D1/D2; M6-Plan |
| Kontext | 1024 (M-A), 2048 (M-B); Skalierung (B2b) erst danach | ehrliche Gates vor Extrapolation |
| Positionsart | RoPE (B2) als neuer Default für LM-Profile; sinus/gelernt bleiben | Paper-Standard, Cache-kompatibel |
| Multi-GPU | Default NEIN (nur 1 lokale GPU); X1 entscheidet formal | Live-Inventur Abschnitt 1 |
| Serving-API | Moo-nativ zuerst, OpenAI-kompatibel optional | X2 |

## 4. Konsequenzen für den Task-Graph
- G4-PoC-Modell = M-A-Architektur (kein separates Spielzeug).
  AMENDMENT (2026-07-10, Scope-Entscheidung Channel 12766 + docs/kip/G4-
  residentes-training-poc-design.md): Das G4-PoC läuft mit drei dokumentierten
  Vereinfachungen (Single-Head statt GQA, additive Position statt RoPE,
  affine-freie RMSNorm), weil strided Kernels sonst G4-Scope-Creep wären.
  Der Residenz-/Trainings-Beweis bleibt vollgültig; die M-A-TREUE auf GPU
  wird durch KIP-G4b (strided Kernels: RoPE+GQA) und KIP-G4c (Production-
  Wiring) hergestellt — das M-A-GPU-Exit-Gate gilt erst mit G4b als erfüllt.
- Tokens/s-Zielwerte werden NACH G4 nachgetragen (Messung vor Behauptung).
- E1-Shard-Format muss von Anfang an M-B-Datenmengen (Multi-GB) tragen.
- X1 erbt Abschnitt 1 als Eingangsbefund.

## 5. Exit-Gate dieses Dokuments
Claude-Review (erledigt, Autor), GPT-Review: GO (49573fb4, Klarstellungen
eingearbeitet). Jeder KIP-Task referenziert bei Implementierungsstart seinen
Meilenstein (M-A/M-B).
