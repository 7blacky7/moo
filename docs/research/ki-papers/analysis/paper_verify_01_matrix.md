# PAPER-VERIFY-01: Verifikations-Matrix (Profile gegen Quellen)

Stand: 2026-07-06. Prueft die Behauptungen aus `moo_profil_mapping.md` gegen die Text-Extrakte in `text/` (gezielte Fundstellen mit Zeilennummern, keine PDFs geladen). Spalten: Claim / Quelle (Datei:Zeilen) / Evidenz / moo-Implikation / Risiko / Go-No-Go. Interpretation vs. Paper-Fakt ist je Zeile explizit markiert.

## M1 — Mini-MoE

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| MoE = gewichtete Experten-Summe, Top-k als sparses Gate | DeepSeekMoE_2401.06066.txt:196-215 | Gl. (3)-(5): h = SUM(g_i * FFN_i(u)) **+ u** ; g_i = s_i wenn s_i in Topk sonst 0; s = Softmax_i(u^T e_i) | **PAPER-FAKT.** Grad-lose Top-k-Maske + softmax-gewichtete Summe = exakt Gl. (4)/(5); Op-Komposition traegt. **KORREKTUR: Residual +u ist Teil der Formel — muss in fw_moe** (fehlte im Mapping). | keins | GO |
| Router = dicht(dim→n)+softmax | dito :205-212 | s = Softmax(u^T e_i), e_i = Experten-ZENTROID (Linear **ohne Bias**) | **INTERPRETATION mit Abweichung:** dicht hat Bias. Papertreu = bias-freie Projektion. Empfehlung: Router als bias-freie matmul bauen (Op vorhanden), nicht dicht. | gering (funktional aehnlich), aber vermeidbar | GO mit Anpassung |
| Top-1 oder Top-2 als Startpunkt | dito :193-194, :702 | "assigned to one (Fedus) or two (Lepikhin) experts"; V3-Familie: top-K aus routed experts | PAPER-FAKT. top-1/top-2 sind die kanonischen Basisformen. | keins | GO |
| Load-Balancing: "Metrik zuerst, aux-loss spaeter" | dito :335-348, :1153 | Paper-Verfahren IST der Expert-Level Balance **Loss** L=alpha*SUM(f_i*P_i) (Gl. 12); expliziter Zweck "**prevent routing collapse**" (Faktor 0.003 selbst bei 145B) | **INTERPRETATION, RISIKOBEHAFTET:** Nur-Metrik ist KEIN Paper-Verfahren. Ohne Gegenmassnahme ist Routing-Collapse real → das eigene Gate "Auslastung nicht-degeneriert" wuerde ehrlich ROT. **KORREKTUR: einfacher Expert-Level-Balance-Loss (Gl. 12) gehoert in den M1-Scope**, Metrik nur als Messgroesse. | mittel: Toy-Gate koennte ohne aux-loss nicht bestehen | GO mit Scope-Anpassung |
| Aux-loss-free Alternative (V3) | DeepSeek-V3_2412.19437.txt:19, 158-160 | "pioneers an auxiliary-loss-free strategy (Wang 2024a) for load balancing" (Bias-Steuerung) | Fuer moo-Toy NICHT noetig; als Folge-Option notiert. | keins | spaeter |
| Shared Experts optional | DeepSeekMoE_2401.06066.txt:807-809 | "2 shared experts and 64 routed experts ... routed to these 2 shared + 6 of 64" | Folge-Option nach Basis-MoE, nicht M1-Scope. | keins | spaeter |

## M2 — Attention-Varianten + KV-Cache

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| GQA/MQA = ARCHITEKTUR zur KV-Reduktion; KV-Cache = Inferenzzustand | DeepSeek-V2_2405.04434.txt:141-146, 261-264 | "In order to reduce the KV cache, MQA and GQA are proposed"; KV-Cache als Inferenz-Bottleneck/Speicher | PAPER-FAKT. Trennung im Mapping korrekt: kv_koepfe = Layer-Architektur; Cache = Laufzeit-Dict bei autograd_aus. | V2 warnt: GQA/MQA "compromise performance" vs MHA — fuers Mechanik-Toy irrelevant, im Gate NICHT Qualitaet vergleichen, nur Korrektheit (kv==koepfe → bit-identisch MHA) | GO |
| Sliding+Global traegt auf Masken-Muster | MiMo-V2-Flash_2601.02780.txt:16-17, 140-144 | "hybrid attention ... **interleaves** SWA with global attention, 128-token window, **5:1 ratio**" + "learnable attention sink bias" | **PRAEZISIERUNG:** Hybrid = LAYER-Interleaving (manche Layer SWA, manche global), NICHT Misch-Maske in einem Layer. moo: Sliding-MASKE pro Layer traegt auf dem causal_maske-Muster ✓; das 5:1-Hybrid ist NETZBAU (Layer-Liste mischt attention(maske=sliding|causal)), kein Layer-Feature. Sink-Bias fuers Toy weglassen (kleine Kontexte). | gering | GO mit Praezisierung |
| MLA erst nach GQA | DeepSeek-V2_2405.04434.txt:144-146, 265-267 | MLA = low-rank KV-joint-compression, eigene Architektur | Bestaetigt: separater, groesserer Schnitt; nach GQA evaluieren. | — | spaeter |

## M3 — Reasoning-Modus / Thinking-Budget

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Modi direkt/denken in EINEM Modell; Budget = Inferenz-Steuerung | Qwen3_2505.09388.txt:17-18, 66-73, 110 | "integration of thinking mode and non-thinking mode into a single model"; "thinking budgets ... fine-grained control over the level of reasoning effort ... during task execution"; "increasing the thinking budget" | PAPER-FAKT: Modi = Trainings-+Inferenzverhalten eines Modells; Budget = Inferenz-/Sampling-Steuerung. M3-Einordnung (Trainingsdaten-Schema + Sampling-Parameter, KEINE Modell-Architektur) korrekt. | keins | GO |
| Denk-Bloecke als Token-Struktur | Phi-4-reasoning_2504.21318.txt:203, 218 | reale `<think> ... </think>`-Bloecke in den Beispielen | Mode-/Denk-TOKENS im Vokabular sind das belegte Muster → Char-LM-Spezialtokens tragen. | Budget-Durchsetzung (Kappen + Antwort erzwingen) ist INTERPRETATION der Mechanik — plausibel, aber Detail nicht im Extrakt belegt; als Design-Freiheit dokumentiert | GO |

## M4 — MTP / Speculative Decoding

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| MTP = TRAININGS-Objective; Spec-Decoding = optionale Inferenz-Nutzung | DeepSeek-V3_2412.19437.txt:19, 161-162, 221-222, 571-574, 601-602 | "multi-token prediction TRAINING objective"; "EAGLE ... primary objective is speculative decoding, whereas WE utilize MTP to improve TRAINING"; "during inference we can directly DISCARD the MTP modules ... can also REPURPOSE these MTP modules" (fuer spec decoding) | PAPER-FAKT. M4-Trennung (Head+Loss=Training; Spec-Toy=Inferenz) exakt belegt. | — | GO |
| "Zweiter dicht-Head" als Bauform | dito :571-572 + MiMo :404-407 | V3 nutzt SEQUENTIELLE MTP-Module mit geteiltem Output-Head + kausaler Vorhersagekette; MiMo hat explizit "Lightweight MTP Design" | **INTERPRETATION/VEREINFACHUNG:** der simple Parallel-Head ist NICHT das V3-Design (kausale Kette entfaellt) — er entspricht der leichtgewichtigen Linie. Fuers Toy zulaessig, im Gate/Doku als bewusste Vereinfachung markieren. | gering (Toy-Anspruch) | GO als Toy |

## M5 — Reward/GRPO

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| GRPO = Gruppen-Sampling, Baseline aus Gruppen-Scores, KEIN Value-Model | DeepSeekMath_2402.03300.txt:76-79, 118-121, 626-630 | "GRPO foregoes the critic model, instead estimating the baseline from group scores"; Motivation: Value-Function pro Token schwierig, Reward nur am Ende | PAPER-FAKT. M5-Kern korrekt gemappt; advantage-gewichtete CE mit grad-loser Advantage-Konstante ist die passende Komposition. PPO-Clipping (eps) ist Teil des vollen GRPO — fuers Toy pruefen ob noetig oder erst vanilla policy gradient. | B2-STOPP-Klausel bleibt: falls Komposition nicht reicht → Design-Review | GO (nach M1-M4) |

## Gesamt-Entscheid

**KI-M1: GO — mit drei verbindlichen Anpassungen aus diesem Verify:**
1. fw_moe enthaelt das **Residual +u** (Gl. 3).
2. Router **bias-frei** (matmul + softmax, papertreu Gl. 5) statt dicht-Layer.
3. **Expert-Level-Balance-Loss (Gl. 12) in den M1-Scope** — nicht nur Metrik; sonst riskiert das eigene Nicht-Degeneriert-Gate ehrlichen Rot-Fall durch Routing-Collapse.

M2 GO mit Praezisierung (Hybrid = Netzbau-Interleaving). M3/M4 GO wie gemappt (Vereinfachungen markiert). M5 GO nach Reihenfolge. M6 unveraendert (nur Plan).
