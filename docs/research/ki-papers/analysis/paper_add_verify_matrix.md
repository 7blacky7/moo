# PAPER-VERIFY-03: Zusatz-Batch — Verify-Matrix (14 Paper)

Stand: 2026-07-06. Muster wie paper_verify_01/02. Quellen: text/-Extrakte Gemma_2403.08295, Gemma-2_2408.00118, Gemma-3_2503.19786, MobileLLM_2402.14905, MobileLLM-Pro_2511.06719, Mixtral_2401.04088, Mistral-7B_2310.06825, Llama-3-Herd_2407.21783, OLMo-2_2501.00656, Tulu-3_2411.15124, Phi-3_2404.14219, Phi-4_2412.08905, Gemini_2312.11805, Gemini-1.5_2403.05530. Keine PDFs geladen.

**Vorab-Hinweis (Moo-Direktive): Gemma 4 wird NICHT als offizielles Architektur-Paper behandelt — es liegt kein offizieller Gemma-4-Technical-Report vor. Alle Gemma-Aussagen hier beziehen sich ausschliesslich auf Gemma 1/2/3.**

## M1-Bezug: MoE

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Mixtral-SMoE: 8 Experten, Router waehlt 2, additive Kombination | Mixtral:18-22, 39-43, 46-48, 76-77 | "router network selects two experts ... combine their outputs"; "output is the weighted sum of the outputs of the two selected experts"; Tabelle num_experts=8, top_k=2 | ZWEITBELEG fuer die M1-Grundmechanik (gewichtete Summe + Top-k). | keins | stuetzt M1 |
| **Mixtral-Gating: Softmax NACH TopK** — G(x)=Softmax(TopK(x·Wg)), TopK setzt Rest auf −∞ | Mixtral:88-92 | woertliche Formel | **WICHTIGE DIFFERENZIERUNG zur M1-Basis:** DeepSeekMoE (VERIFY-01, Gl. 4/5) macht Softmax ueber ALLE, dann Auswahl (nicht renormalisiert); Mixtral renormalisiert ueber die Top-K (additive −∞-Maske VOR Softmax). Beide paper-belegt. M1 bleibt DeepSeekMoE-Variante (Moo-Go + Balance-Loss-Kopplung an Gl. 12-P_i); Mixtral-Reihenfolge waere mit dem additiven causal_maske-Muster (−1e9 vor softmax) sogar NATUERLICH abbildbar → dokumentierte Folge-Option "moe_mixtral_gating". | Verwechslungsgefahr der beiden Reihenfolgen in Doku/Gates — explizit benennen | GO als Folge-Option |
| Mixtral: Load Balancing | Mixtral:111 | nur als Infrastruktur-/Deployment-Thema erwaehnt (expert parallelism) | Kein aux-loss im Extrakt sichtbar → Mixtral liefert KEINEN Gegenbeleg zur M1-Entscheidung (Balance-Loss aus DeepSeekMoE Gl. 12 im Scope). | — | neutral |
| Gemini 1.5 Pro = sparse MoE-Transformer | Gemini-1.5:273-276 | "sparse mixture-of-expert (MoE) Transformer-based model"; verweist auf Google-MoE-Linie (Shazeer/GShard/Switch) | Bestaetigt MoE als Industrie-Hauptpfad; keine Architektur-Details im Extrakt → keine weiteren Ableitungen. | — | Kontext |

## M2-Bezug: Attention-Varianten + KV-Cache

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Mistral 7B: SWA + GQA | Mistral-7B:20-22, 41-45, 63-66 | "GQA for faster inference, coupled with SWA"; "each token can attend to at most W tokens from the previous layer" | ZWEITBELEG SWA als Fenster-MASKE (traegt exakt auf dem grad-losen Masken-Muster); DRITTER GQA-Beleg (Inferenz-Speed + Decode-Memory). | keins | stuetzt M2 |
| Gemma 2: Interleaving local-global + GQA + Distillation | Gemma-2:13-15, 40-43, 94 | "interleaving local-global attentions ... and group-query attention"; "Local Sliding Window and Global Attention" | ZWEITBELEG Layer-Interleaving (nach MiMo). | keins | stuetzt M2 |
| **Gemma 3: 1 global je 5 local, local span 1024, explizit gegen KV-Cache-Explosion** | Gemma-3:12-14, 27-30, 55-58 | "reduce the KV-cache memory ... by increasing the ratio of local to global attention layers, and keeping the span on local attention short"; "1 global for every 5 local layers", span 1024 | **DRITTBELEG Interleaving mit identischem 5:1-Verhaeltnis wie MiMo** — das 5:1-Muster ist damit doppelt unabhaengig belegt; KV-Cache-Motivation woertlich. M2-Toy-Empfehlung: Beispiel-Netz im 5:1-Muster bauen. | keins | stuetzt M2 stark |
| Llama 3: GQA mit 8 KV-Heads; Dokument-Grenz-Maske | Llama-3-Herd:313-316 | "GQA with 8 key-value heads to improve inference speed and reduce KV cache size"; "attention mask that prevents self-attention between different documents" | VIERTER GQA-Beleg. Dokument-Masken-Detail = weiterer Beleg, dass Masken-Varianten der tragfaehige Mechanismus sind (unser Muster). | keins | stuetzt M2 |

## M5-Bezug: RL/Reward

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| **Tulu 3: RLVR — RL mit VERIFIZIERBAREN Rewards als eigenes Post-Training-Verfahren** | Tulu-3:49-53, 100-104 | "a novel method we call Reinforcement Learning with Verifiable Rewards (RLVR)"; eigene Kapitel (RLVR Data/Recipe/Infrastructure) | **DIREKTER BELEG fuer den M5-Ansatz** (Toy-Env mit Reward = exakt richtig): verifizierbare Rewards sind ein etabliertes, benanntes Verfahren — M5-Design (Ziffern-Addition, exakter Check) ist RLVR-im-Kleinen. SFT→DPO→RLVR-Pipeline als Kontext notiert (DPO ist NICHT M5-Scope). | keins | stuetzt M5 |

## Architektur-Zutaten / Klein-Modelle (Folge-Optionen, kein M-Task)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| MobileLLM: deep-and-thin + embedding sharing + block-wise weight sharing + GQA fuer Sub-1B | MobileLLM:31-42, 94 | woertlich | Klein-Modell-Rezepte passen zur moo-Toy-Philosophie. Embedding-Sharing (Input-Emb = Output-Head-Gewichte, "tied weights") waere eine KLEINE, saubere Folge-Option fuers Char-LM (Param-Ersparnis, ein Flag). Deep-thin deckt sich mit GLM-4.5-Befund (VERIFY-02). | tied weights beruehrt .mook-Param-Liste → bei Bau eigener Mini-Verify | Folge-Option |
| MobileLLM-Pro: 4-bit QAT, Distillation, Specialist-Merging, 128k | MobileLLM-Pro:23-30 | woertlich | Quantisierung = M6-PLAN-Futter (QAT als Stichwort fuer das Design-Memo); Rest ausserhalb Scope. | — | M6-Input |
| OLMo 2: Trainings-Stabilitaets-Rezept + Daten-Curriculum (Dolmino) | OLMo-2:43-47, 184-188 | "techniques for achieving better training stability"; loss spikes als Motivation; late-stage curriculum | Trainings-Engineering-Ebene; fuer moo-Toys aktuell kein Mechanismus-Bedarf (deterministische Mini-Laeufe). Notiz fuer spaeter (falls groessere Trainingslaeufe). | — | Notiz |
| Phi-3/Phi-4: Daten-Qualitaet/synthetische Daten als Kernrezept | Phi-4:20-24, 32-33 | "training recipe centrally focused on data quality ... synthetic data throughout" | Daten-Ebene, kein Architektur-Baustein. Fuer M3-Toy-Korpora ist die Idee (kleine, saubere synthetische Daten) ohnehin unser Muster. | — | Kontext |
| Gemini 1.0/1.5: multimodal + long context | Gemini-1.5:273ff | Extrakt-Ebene | Multimodal bleibt explizit-nicht-jetzt (Mapping); keine neuen Ableitungen. | — | Kontext |

## Luecken / Negativ-Befunde

| Thema | Status |
|---|---|
| **Gemma 4** | KEIN offizieller Technical Report vorhanden (Moo-Direktive bestaetigt Bestand) — Gemma-Aussagen nur fuer 1/2/3; Gemma-4-Behauptungen sind tabu bis ein offizieller Report vorliegt. |
| OLMo-2 QK-Norm/Detail-Architektur | Im Extrakt nicht an den gefundenen Stellen benannt — Detail-Architektur-Aussagen zu OLMo 2 unterlassen bzw. vor Nutzung gezielt nachschlagen. |

## Gesamt-Einordnung

Der Zusatz-Batch aendert KEINEN M-Task-Scope, staerkt aber die Beweislage erheblich: M1-Grundmechanik doppelt belegt (+ Mixtral-Gating-Reihenfolge als klar benannte Alternative), M2-Interleaving nun DREIFACH belegt mit unabhaengig identischem 5:1-Muster (MiMo + Gemma 3) und viertem GQA-Beleg, M5 durch RLVR (Tulu 3) als benanntes Verfahren direkt gestuetzt, M6 bekommt QAT als Design-Memo-Input. Neue dokumentierte Folge-Optionen: moe_mixtral_gating, tied embeddings (Char-LM). KI-M1 startet unveraendert nach Thought-76f3a7ee-Rezept.
