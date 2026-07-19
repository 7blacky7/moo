# TurboQuant / PolarQuant / QJL — wissenschaftliche Analyse + moo-Anknüpfung

Stand: 2026-07-19
Status: **Zukunfts-Idee / noch nicht geplant** (Kandidat für eine spätere KI-Erweiterung).
Methode: Primärquellen (arXiv-PDFs + Text-Extrakte) über den ki-browser/arxiv geladen und
gelesen — nicht nur der Google-Blog. Blog-Aussagen sind unten ausdrücklich als solche markiert
und teils vom Paper abweichend.

## Abgelegte Primärquellen (in diesem Corpus)

| Paper | arXiv | PDF | Text-Extrakt |
|---|---|---|---|
| TurboQuant: Online Vector Quantization with Near-optimal Distortion Rate | 2504.19874 | `pdf/TurboQuant_2504.19874.pdf` | `text/TurboQuant_2504.19874.txt` |
| PolarQuant: Quantizing KV Caches with Polar Transformation | 2502.02617 | `pdf/PolarQuant_2502.02617.pdf` | `text/PolarQuant_2502.02617.txt` |
| QJL: 1-Bit Quantized JL Transform for KV Cache Quantization with Zero Overhead | 2406.03482 | `pdf/QJL_2406.03482.pdf` | `text/QJL_2406.03482.txt` |
| Blog | — | Google Research: "TurboQuant: Redefining AI efficiency with extreme compression" | — |

## Die drei Paper — eine Linie, drei Ebenen

Es ist **eine zusammenhängende Forschungslinie** (QJL → PolarQuant → TurboQuant), nicht drei
unabhängige Tricks:

### QJL (2406.03482) — der 1-Bit-Baustein
- Problem: klassische Quantisierung muss pro Datenblock **Zero-Point + Scale in voller
  Präzision** speichern → **+1 bis +2 Bit pro Zahl** Overhead.
- Methode: **Johnson–Lindenstrauss-Transform (Zufallsprojektion) + Vorzeichen-Quantisierung.**
  `Q_qjl : R^d → {−1,+1}^m`, jede Koordinate wird auf **1 Vorzeichen-Bit** reduziert.
  Datenunabhängig (data-oblivious), **unverzerrter (unbiased) Schätzer** für innere Produkte,
  minimale Distortion. **Kein** Quant-Konstanten-Speicher → **Zero Overhead**.
- Ergebnis (Paper): KV-Cache auf **3 Bit → >5× Speicherreduktion ohne Accuracy-Verlust**.

### PolarQuant (2502.02617) — die Polar-Idee für den KV-Cache
- Methode: **Random Preconditioning + Polar-Transformation** der KV-Embeddings über einen
  **effizienten rekursiven Algorithmus**; quantisiert werden die **Winkel**.
- Kern-Insight: Nach dem Random-Preconditioning haben die Winkel eine **eng begrenzte, stark
  konzentrierte, analytisch berechenbare Verteilung** → die Normalisierung (Scale/Zero-Point
  pro Block) entfällt komplett.
- Ergebnis (Paper): KV-Cache **> ×4.2 komprimiert** bei besten Qualitätswerten (Long-Context).

### TurboQuant (2504.19874) — die allgemeine Theorie
- **Nicht nur KV-Cache**, sondern allgemeine **Vektor-Quantisierung (VQ)** für **beide**
  Distortion-Maße: **MSE** und **inneres Produkt**.
- Methode: **Zwei-Stufen.**
  1. **MSE-optimaler Quantizer**: Eingabevektoren werden **zufällig rotiert** → induziert eine
     **konzentrierte Beta-Verteilung** → optimaler Codebuch-Entwurf via **kontinuierlichem
     k-means**. Liefert optimale MSE-Distortion.
  2. Da MSE-optimale Quantizer das **innere Produkt verzerren**, wird auf den **Residualfehler**
     ein **1-Bit-QJL** angewandt → **unverzerrter** Inner-Product-Quantizer.
- Theorie: **informationstheoretische untere Schranke** bewiesen; TurboQuant erreicht sie bis
  auf kleinen konstanten Faktor über **alle Bit-Breiten und Dimensionen** — **exponentielle
  Verbesserung** in der Bit-Breiten-Abhängigkeit gegenüber bestehenden Methoden.
  Data-oblivious → **online-tauglich, kalibrierungsfrei** (kein Training/Fine-Tuning).

## Zahlen — Paper vs. Blog (Rigor!)

- **Paper-Experimente (TurboQuant):** alle auf **einer einzelnen NVIDIA A100 GPU**. Metriken:
  Recall im **Needle-In-A-Haystack** (Llama-3.1-8B-Instruct, 4k–104k Tokens) und **End-to-End
  auf LongBench** (Llama-3.1-8B-Instruct + Ministral-7B-Instruct). Baselines: **PolarQuant,
  SnapKV, PyramidKV, KIVI**. Ergebnis: schlägt Baselines in Recall bei geringerem Speicher;
  "perfect long-context retrieval" im NIAH-Test.
- **Blog-Aussagen (separat zu behandeln):** "6× KV-Reduktion", "4-Bit TurboQuant bis **8×**
  Speedup vs. 32-Bit auf **H100**", "3-Bit ohne Accuracy-Verlust". → Der **H100/8×**-Wert steht
  so **nicht** im Paper-Text (Paper = A100, Recall/LongBench). Blog-Marketingzahlen daher nicht
  als Paper-Beleg zitieren.
- **QJL-Paper:** 3-Bit → **>5×** KV-Reduktion. **PolarQuant-Paper:** **>×4.2**.

## Warum das zu moo passt (codebelegt 2026-07-19)

moo ist nachweislich eine Plattform, um Transformer/LLMs **in der Sprache** zu bauen:

- **GPT-artiger Transformer**: `beispiele/ki_sprachmodell.moos` — 2 Blöcke, dim 64, ~120k Params,
  4 Köpfe, `schicht_embedding` + `schicht_position("gelernt")` + `schicht_layernorm` +
  `schicht_attention` (Pre-LayerNorm, GPT-Stil) + **autoregressiver Loop**
  `generiere(kontext_start, anzahl, temperatur)`.
- **Attention-Primitive mit GQA**: `moo_nn_schicht_attention(dim, koepfe, seed, kv_koepfe)`
  (`compiler/runtime/moo_nn.c`), causal, mit Grad-Fluss; Test `tests/test_nn_asan.c` (Plan-014 G1).
- **Moderne Techniken in Moo**: MoE (`ki_moe.moos`), MTP (`ki_mtp.moos`), GRPO/RL
  (`ki_grpo.moos`), Embeddings (`ki_embeddings.moos`), Mini-LM (`kip/d2_mini_lm.moos`).
- **Externe Gewichte**: `beispiele/ki_safetensors_import.moos`.
- Tensor-Stack: f32-Compute/Grad, bf16-Storage; residente Vulkan-GPU-API
  (`compiler/runtime/moo_ki_gpu_api.h`). int8-Quant bisher **EVAL-only / Produkt-NO-GO**.

## Bausteine → moo-Abbildung (falls später umgesetzt)

| Paper-Baustein | Abbildung in moo |
|---|---|
| Zufallsrotation (Hadamard/orthogonal) vor Quant | neue `moo_tensor`-Op: schnelle (Walsh-)Hadamard-Transform + fester Seed; deterministisch |
| Kontinuierliches k-means Codebuch (Beta-Verteilung) | offline berechenbare Codebuch-Tabelle, in Runtime nur Lookup |
| Polar-Rekursion + Winkelquant (PolarQuant) | `moo_tensor`-Kernel; Winkelverteilung analytisch → keine Scale/Zero-Point-Speicherung |
| 1-Bit-Sign-JL (QJL) auf Residual | Bit-gepackter `sign()`-Kernel `{−1,+1}` + Dekodier-Schätzer; unbiased inner product |
| KV-Cache | **fehlt aktuell** — expliziter KV-Cache im `generiere(...)`-Loop wäre Voraussetzung |

## Anknüpfungspunkte (Priorität)

1. **Expliziter KV-Cache** im Generierungs-Loop (heute wird pro Schritt vermutlich die volle
   Attention neu gerechnet). Ohne Cache hat TurboQuant nichts zu komprimieren.
2. **Rotationsbasierte, kalibrierungsfreie Quant-Primitiven für `moo_tensor`**
   (Incoherence-Processing; verwandt mit QuIP#/QuaRot/SpinQuant). Das ist der Hebel, der moos
   festgefahrene int8-Quantisierung auf brauchbare Qualität heben könnte — **unabhängig vom
   KV-Cache** und daher der risikoärmste erste Schritt.
3. **QJL/JL für Vektor-Suche/RAG**, falls moo eine Retrieval-/Embedding-Suche bekommt
   (TurboQuant zielt explizit auch auf Vektordatenbanken/NN-Search).

## Zielgruppe / Nutzen

- Nutzer, die **moo als KI-Entwicklungssprache** verwenden: Modelle in wenig Speicher lauffähig,
  ohne Kalibrierdaten.
- **Zukünftiges moos OS**: effiziente lokale Inferenz auf begrenzter/Edge-Hardware.
- Ausdrücklich **eine zusätzliche Möglichkeit** — die Transformer-/LLM-Grundlagen sind vorhanden.

## Offene Punkte (ehrlich)

- KV-Cache + rotationsbasierte Quant-Primitiven sind **neue Runtime-Arbeit** (natürliche
  Erweiterungen, keine Neubauten).
- Determinismus-/Qualitätsnachweis auf **eigener** (kleiner) moo-Modell-Hardware separat führen —
  die Paper-Zahlen gelten für große LLMs auf A100.
- Blog-Marketingzahlen (H100/8×) nicht als Beleg übernehmen; die belastbaren Paper-Zahlen sind
  QJL >5× (3-Bit), PolarQuant >×4.2, TurboQuant near-optimal Distortion + Recall-Gewinn.
