# KI-MULTI-V3 — Multimodale Fusion: Vision/Audio-Encoder × LLM (Design)

Stand: 2026-07-16 · Task: 8e771d35 · Epic-Phase 1 (Design VOR Code)

REVIEW-POLICY (User-Entscheid 2026-07-16): KEIN GPT-Gegenreview in diesem Epic. Eine Fable-Instanz liest das Doc einmal kritisch gegen und verifiziert die Gates technisch. Selbstreview-Checkliste in §9.

## 1. Ziel und Scope

Vision×LLM und Audio×LLM komplett in Moo: Encoder-Ausgänge werden als Embedding-Vektoren in die LLM-Sequenz eingespeist, gemeinsames Training in Stufen (Projektion → joint). Erfolgsdefinition dieses Epics sind die deterministischen Toy-Gates V3a–V3d — KEIN Anspruch auf web-skalierte Daten oder produktionsreife Chat-Qualität (Scope-Lektion aus KIP-GRUNDSTOCK).

## 2. Bausteine-Inventur (nichts doppelt bauen)

| Baustein | Herkunft | Verwendung in V3 |
|---|---|---|
| Frame→Tensor `tensor_aus_frame` | V1 (1512c85) | Bildeingang (auch Live via C1/C2) |
| Conv2D/Pooling + Backward, `schicht_faltung/pooling/flach` | V2 (d035d19, e701d38) | Vision- und Audio-Encoder |
| STFT/Spektrogramm `|STFT|` [frames,bins] | A1 (72d9e66) | Audio-Frontend vor Conv |
| Contrastive/Kosinus-Loss | L1 (102bbf4) | Stufe A Encoder-Pretraining |
| Transformer+RoPE(+GQA), Schichten-API | B2 / Plan-014 (ki_sprachmodell.moos) | LLM-Backbone |
| BPE + Chat-Template + Spezial-Tokens + Loss-Maske | T2/T3 | Modalitäts-Tokens, Loss nur auf Text |
| Shards | E1 | erst ab M-A-Stufe relevant, Toy-Gates brauchen keine |
| GPU-residentes Training | G4/G4b | V3c |
| Kamera/Mikro-Capture | C1/C2 | V3d Live-Demo |

## 3. Architektur (LLaVA-Muster auf Moo-Größe)

```
Bild:  frame → tensor_aus_frame → Conv-Encoder → z_bild [K_b, dim_enc]
Audio: samples → |STFT| → Conv-Encoder → z_audio [K_a, dim_enc]
                z → Projektion P → e_inj [K, dim_llm]
Sequenz: concat(e_inj, E_text) → Transformer → Head → CE nur auf Text-Positionen
```

### 3.1 Encoder
- Vision (V3a-FINAL nach Gate-Iteration): ZWEI-PFAD-Encoder mit kompositionaler induktiver Verzerrung — Form-Pfad: V2-Conv-Stack (2× [faltung→pooling]) → GLOBALES Max-Pooling (translationsinvariant) → dicht; Ort-Pfad: flacher Conv-Zweig, frueh grob gepoolt (4×4-Grid — Position, kaum Form) → dicht. Encoder-Output = SUMME beider Pfade (passt zur Anker-Struktur form_proto + ort_proto, §4). LEARNING: ein einzelner flach+dicht-Kopf verschränkt Form×Ort und generalisiert NICHT auf ungesehene Kombis (Gate 1b war 0). Eingang Toy: 32×32×1 (grau, V1-Modus "grau"), Formen groß genug rendern (r 6–8 px — nach 2× Maxpool sind kleine Formen ununterscheidbar). Farbe kommt als 32×32×4 in einem späteren Toy-Level (§5).
- Audio (V3b-FINAL nach Gate-Iteration): A1-|STFT| → [frames,bins,1] → DREI Köpfe mit HARTER Subraum-Injektion (matmul mit konstanten 0/1-Matrizen): Band-Kopf (Conv-Stack + globales Max-Pool, dims 0..11), Dauer- und Muster-Kopf (gemeinsamer flacher Zeit-Trunk mit MITTEL-Pooling — Max-Pool ist modulationsblind, Amplitudenmuster steckt im Energie-Mittel — dims 12..21 bzw. 22..31). Encoder-Output = Summe der drei Injektionen. LEARNINGS: (a) der V3a-Zwei-Pfad-Trick mit Summen-Ankern skaliert NICHT auf 3 Faktoren (Kombi-Holdout schwankte 0.5–0.9 je Jitter-Draw); (b) Sinus-Frequenzen nicht auf FFT-Bin-Grenzen legen (2000 Hz = exakt bin 16 bei Fenster 64 → bimodales Leakage unter Jitter).
- ENTSCHEID: Encoder liefert in v1 genau EINEN Vektor pro Eingabe (K=1 nach flach+dicht), NICHT ein Patch-Gitter. Begründung: Toy-Gates brauchen keine räumliche Auflösung im LLM; K=1 hält Injection, Positionslogik und Masken minimal. K>1 (Patch-Slots) ist dokumentierte Ausbaustufe — der Injection-Vertrag (§3.3) ist bereits K-fähig spezifiziert.

### 3.2 Projektion
- ENTSCHEID: Linear `dicht(dim_enc, dim_llm, "keine")` als v1. Ein 2-Layer-GELU-MLP ist Option NACH Messung (erst wenn Stufe B am Gate scheitert — nicht präventiv).
- Gradcheck-Pflicht: FD-Gradcheck der Projektion im eingebauten Zustand (Loss → rueckwaerts → FD-Vergleich an gesampelten Koordinaten), Muster aus Kern-Gradchecks.

### 3.3 Embedding-Injection-Vertrag (Kern des Designs)
- ENTSCHEID v1: **Präfix-Injection via concat.** Die K Modalitäts-Vektoren stehen IMMER am Sequenzanfang: `E_seq = verketten(e_inj [K,dim], E_text [T,dim]) → [K+T, dim]` (Tensor-Concat heißt `verketten`/`concat` — `verbinden` ist der Listen-Join!). Kein scatter, keine Mid-Sequence-Löcher. RoPE-Positionen laufen fortlaufend 0..K+T-1. Kausale Maske unverändert: jedes Text-Token sieht alle Modalitäts-Slots (steht links).
- Chat-Template: `<|bild|>` / `<|audio|>` bleiben T3-Spezial-Tokens — `encode()` kann sie NIE erzeugen (Injection-Schutz gratis). Das Template rendert sie in v1 NICHT in die Token-Sequenz, sondern sie markieren im Trainings-/Inferenz-Programm, DASS ein Präfix injiziert wird (Buchführung im Sample-Dict: `{"modalitaet": "bild", "frame": ..., "text_ids": ...}`). Mid-Sequence-Injection (Token-Position → Slot-Ersetzung per gather/scatter) ist dokumentierte Ausbaustufe mit eigenem Gate — die Ops existieren (gather + scatter_add, G3d), v1 braucht sie nicht.
- Loss-Maske: CE nur auf Text-Positionen; die ersten K Positionen sind hart maskiert (Mechanik = T3/X3-Loss-Maske, hier trivial: Maske [0]*K + [1]*T beim CE-Aufruf über die bestehende maskierte CE-Komposition).
- Gradient-Fluss: durch concat fließt der Gradient sauber in e_inj → Projektion → (Stufe C: weiter in den Encoder). Bei Stufe A/B wird der Encoder mit autograd_aus()-Vorlauf ausgewertet und sein Output als konstanter Tensor eingespeist (frozen = kein Tape-Eintrag), Stufe C hängt ihn ans Tape.

### 3.4 LLM-Backbone-Größe
- Toy (V3a/V3b): ki_sprachmodell-Klasse — dim 64, 2 Blöcke, Zeichen-Vokabular aus dem Toy-Textkorpus (klein, deterministisch). Training CPU in Minuten; jede Iteration am Design bleibt billig.
- M-A-Stufe (nach V3c, optional im Epic): 10M-Klasse nach P0, BPE 8k, Shards E1. NICHT Gate-relevant für V3a–V3c.

## 4. Trainings-Stufen
- **Stufe A — Encoder-Pretraining (frozen danach):** Contrastive (L1, Kosinus) auf Toy-Paaren: Encoder-Embedding des Bildes vs. KOMPOSITIONALEM Klassen-Anker form_proto[fi] + ort_proto[oi] (eingefrorene Zufalls-Tensoren, deterministisch per Seed) — unabhängige Anker pro Klasse verhindern Kombi-Generalisierung (V3a-Learning: Combo-Retrieval war 0, kompositional 1.0). Gate: accuracy@1 ≥ 0.95 Retrieval auf ungesehenen Kombinationen (L1-Muster ki_embeddings). AUDIO-VARIANTE (V3b-FINAL, 3 Faktoren): Stufe A ist ANKER-REGRESSION statt In-Batch-Contrastive — MSE gegen fixe Anker in disjunkten Subraeumen (kein In-Batch-False-Negatives-Problem bei duplizierten Faktorwerten, kein Kollaps möglich, da Ziele fix; Anker-Skala 0.7 im tanh-Wertebereich; Loss-Normierung NUR über Batchgröße — Division durch dim macht die Gradienten ~32× zu klein).
- **Stufe B0 — LLM-Slot-Pretraining (Fable-Review-Auflage "Stufe 0", FINAL nach V3a-Iteration):** Das Toy-LLM wird KONDITIONAL vortrainiert, BEVOR es in Stufe B eingefroren wird: Präfix = kompositionaler Lehr-Slot form_emb[fi] + ort_emb[oi] in DISJUNKTEN Subräumen (Form dims 0..dim/2-1, Ort dims dim/2..dim-1) + kleines deterministisches Slot-Rauschen (0.05, Seed pro Schritt). DREI V3a-LEARNINGS stecken darin: (1) NULL-Slot-Pretraining lehrt das LLM, den Slot zu IGNORIEREN — die Projektion kann danach nichts injizieren (Caption-Accuracy blieb bei 0.2); (2) volle Zufalls-Slots pro Klasse werden MEMORIERT statt komponiert (ungesehene Kombi-Slots → Buchstabensalat); (3) erst disjunkte Subräume + Rauschen machen lineares Faktor-Auslesen zur einfachsten Lösung — danach liest das LLM UNGESEHENE Slot-Kombis korrekt. Präfix-Geometrie (K=1 an Position 0) identisch zu Stufe B. Die Contrastive-Anker in Stufe A bleiben davon unabhängig (dim_enc-Raum; A → B0 → B datenflussfrei). Gate: Toy-LM-Loss konvergiert deterministisch. Der Kern der Review-Auflage (dekodierfähiges LLM vor dem Einfrieren) ist erfüllt — konditional statt text-only.
- **Stufe B — Projektion only:** Encoder frozen, LLM frozen, nur P trainiert. Loss = Caption-CE + MSE-Anker (Gewicht 1.0/dim) auf den Lehr-Slot — Toy-Supervision, die P in den vom LLM lesbaren Slot-Raum zieht (reines CE ließ P bei einer memorierenden Lösung stehen: Gate 1b = 0 trotz CE-Loss 0.001). Moderate LR (0.02; bei 0.1 oszilliert die MSE-Landschaft). Gate: Toy-Caption-Accuracy (§7) durch das EINGEFRORENE LLM erreicht Schwelle — beweist, dass die Schnittstelle trägt, mit minimal beweglichen Teilen.
- **Stufe C — Joint Finetune:** P + LLM (+ optional Encoder) trainierbar. Gate: Accuracy ≥ Stufe B UND kein Vergessen (Text-only-Kontrollbatch verliert < 10% Loss-Verschlechterung gegen Stufe-B-Checkpoint).
- Optimizer: SGD+Momentum wie ki_sprachmodell (ADAM-B1-Befund gilt: batch=1-Pfade meiden Adam; Batches im Toy-Training ≥ 8).

## 5. Toy-Datensätze (deterministisch, KEINE Web-Scrapes)
- **Vision-Toy "Formen":** Moo rendert selbst (Framebuffer/MOO_FRAME, V1-Weg): 1 Form (kreis|quadrat|dreieck) × 4 Positionen (oben links|oben rechts|unten links|unten rechts) auf 32×32 grau → Zieltext z.B. "kreis oben links". 12 Klassen, LCG-Seed generiert Größen-/Helligkeits-Jitter. Level 2 (nach Gate): + 4 Farben auf 32×32×4 → "rotes quadrat unten rechts" (48 Klassen).
- **Audio-Toy "Kommandos":** synthetische Signale (A1-Sinus-Generatorweg): 3 Frequenzbänder (tief|mittel|hoch) × 2 Dauern (kurz|lang) × Amplitudenmuster (konstant|pulsierend) → "hoher ton lang pulsierend" (12 Klassen), Jitter per Seed.
- **Holdout-Konstruktion:** Test = ungesehene KOMBINATIONEN (z.B. "dreieck unten rechts" nie im Training, alle Einzelfaktoren schon) → beweist Komposition statt Memorierung. Splits deterministisch aus Seed.
- Generator INLINE im jeweiligen Adapter-Beispiel (V3a: `beispiele/ki_multimodal_vision.moos` rendert selbst via Framebuffer/MOO_FRAME; deterministischer Prüfwert wird im Gate-Log ausgegeben — kein separates Generator-Programm nötig).

## 6. Meilensteine/Größen
Toy-Stufe komplett CPU-tauglich (< 5 min/Training auf Desktop). V3c hebt denselben Graph GPU-resident (G4-Muster). M-A-10M-Skalierung ist EXPLIZIT Folge-Milestone nach V3d (Synergie KIP-X1b verteiltes Training).

## 7. Phasen und Gates
- **V3a Vision-Adapter:** Stufen A+B0+B auf Vision-Toy. GATES: (1) Toy-Caption-Accuracy ≥ 0.9 getrennt auf Jitter-Holdout (1a) UND Kombi-Holdout (1b, ungesehene Form-Ort-Kombis — der Kompositions-Beweis; exakte Textfolge, Greedy); (2) FD-Gradcheck Projektion PASS; (3) Checkpoint-Roundtrip (Save→Load→Weiterrechnen) bit-identisch für P+Encoder+LLM (E2-Muster); (4) ASan-Harness für neue Pfade leak-clean; (5) run_all + run_sanitize regressionsfrei (GPU-frei).
- **V3b Audio-Adapter:** identische Gate-Struktur auf Audio-Toy (Band×Dauer×Muster = 12 Klassen, 2 Kombi-Holdouts). Kombi-Holdout mit 12 Samples (JITTER_COMBO 6) — STICHPROBEN-LEARNING: mit nur 8 Samples ist die 0.9-Schwelle ein Alles-oder-nichts-Münzwurf und ein 7/8 kann Kleine-Stichproben-Glück sein; erst die größere Stichprobe deckte auf, dass die Summen-Anker-Variante nicht robust generalisierte. 3-FAKTOREN-SHORTCUT: Captions mit 3 Faktoren machen den Text-Kontext im Training perfekt prädiktiv (Holdout!) — das LLM-Slot-Lesen braucht die kompositionalen Lehr-Slots in disjunkten Subraeumen + Rauschen besonders dringend (§4 B0).
- **V3c Joint + GPU-Residenz:** Stufe C; danach derselbe multimodale Trainingsschritt GPU-resident. GATES: cpu_fallbacks==0, uploads==0 im Loop (Telemetrie, G4-Kriterien), GPU-Loss vs CPU-double-Referenz rel < 2e-3, Determinismus 2 Läufe bit-identisch.
- **V3d Live-Demo + Kinderleicht-API:** Kamera ODER Mikro → Antwort-Text, 5-Zeilen-Beispiel (`ki_multimodal.moos`), headless-Selftest mit aufgezeichnetem Frame/Signal (Capture-Gates C1/C2 bleiben unberührt). Gate: Selftest PASS headless + manuelle Live-Sichtung durch User.
- Reihenfolge hart: V3a → V3b → V3c → V3d. Kein Phasenstart vor Gate-PASS der vorigen (Leitplanke KI-MULTI-01: Agentenmeldung allein zählt nicht, Gates sind Skripte/Marker).

## 8. Risiken / bewusste Nicht-Ziele
- Kein Patch-Gitter (K>1), keine Mid-Sequence-Injection, kein BPE-Toy (Zeichen-Vokab reicht), kein Web-Datensatz, kein Speedup-Anspruch in V3c (Residenz+Korrektheit, Perf ist G5-Thema).
- Risiko Stufe B scheitert (Projektion zu schwach): dokumentierte Eskalation = 2-Layer-MLP, DANN erst K>1.
- Risiko Vergessen in Stufe C: Kontrollbatch-Gate (§4) fängt es; Eskalation = kleinere LR/nur letzte Blöcke trainierbar.

## 9. Selbstreview-Checkliste (ersetzt GPT-Gate, Fable-Instanz)
[ ] Injection-Vertrag §3.3 widerspruchsfrei zu T3 (encode erzeugt nie Spezial-Tokens; Template-Buchführung klar)?
[ ] Gradient-Fluss Stufe A/B/C korrekt begründet (frozen = kein Tape-Eintrag; concat-Backward existiert und ist gradgecheckt)?
[ ] Gates maschinell prüfbar formuliert (Schwellen, bit-identisch, Telemetrie-Kriterien)?
[ ] Holdout beweist Komposition (ungesehene Kombis) statt Memorierung?
[x] Stufe B0 (konditionales Lehr-Slot-Pretraining, disjunkte Subräume + Rauschen) vor LLM-Freeze — Präfix-Geometrie identisch zwischen B0 und B.
[ ] Keine Doppel-Implementierung vorhandener Bausteine?
[ ] run_all bleibt GPU-frei; neue ASan-Harnesse eingetragen?
