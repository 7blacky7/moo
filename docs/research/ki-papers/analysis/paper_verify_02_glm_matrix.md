# PAPER-VERIFY-02: GLM-Familie gegen Quellen (Verify-Matrix)

Stand: 2026-07-06. Muster wie paper_verify_01_matrix.md. Quellen: text/-Extrakte GLM_2103.10360, GLM-130B_2210.02414, ChatGLM-GLM4_2406.12793, GLM-4.5_2508.06471. **Bestandsluecke vorab: GLM-5.1/5.2 liegen NICHT als Extrakte vor** (weder text/ noch pdf/ noch analysis/) — dazu sind KEINE quellengesicherten Aussagen moeglich (siehe Luecken-Block unten).

## GLM-4.5 (2508.06471)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Hybrid Thinking/Direct in einem Modell | GLM-4.5:18, 143-145, 369-371 | "hybrid reasoning method that supports both thinking and direct response modes"; "thinking mode for complex reasoning and agentic tasks, non-thinking mode for instant responses"; Integration via Self-Distillation mehrerer Experten-Modelle | PAPER-FAKT, bestaetigt M3 aus zweiter Familie (neben Qwen3/Phi-4). Zusatz-Detail: RL lernt, wann langes CoT unnoetig ist (:393-394) — fuer moo-Toy nicht noetig, notiert. | keins | stuetzt M3 |
| GLM-4.5-MoE nutzt loss-free balance routing + **SIGMOID-Gates** | GLM-4.5:174-176 | "We employ loss-free balance routing [40] and sigmoid gates for MoE layers [23]" | **WICHTIGE DIFFERENZIERUNG:** dritte Gating-Variante neben DeepSeekMoE-Softmax(+aux-loss) und V3-loss-free. M1 bleibt bewusst DeepSeekMoE-softmax+balance-loss (paper-belegt, einfachste Basis); sigmoid-gate + loss-free = dokumentierte Folge-Optionen fuer ein glm_45_like-Profil. | Verwechslungsgefahr der Varianten — in M1-Doku explizit abgrenzen | Folge-Option |
| "deeper not wider" Architektur-Entscheid | GLM-4.5:176-178 | "reduce the width ... increase its height (number of layers), as we found that deeper models exhibited better reasoning capacity" | Hyperparameter-Erkenntnis, kein Mechanismus — fuer Toy-Profile irrelevant, als Notiz fuers Netzbau-Beispiel. | keins | Notiz |
| Long-Horizon = RL-INFRASTRUKTUR, nicht Modell-Architektur | GLM-4.5:883-911 | "fully asynchronous and decoupled RL infrastructure that efficiently handles long-horizon agent rollouts"; high-concurrency Rollouts | **EINORDNUNG:** Long-Horizon bei GLM-4.5 ist Trainings-Infra (asynchrone Agent-Rollouts), KEIN Architektur-Baustein. Fuer moo weit ausserhalb Toy-Scope; gehoert (wenn ueberhaupt) zu M5-Folgearbeit "Agentic-Envs" = explizit-nicht-jetzt. | Fehleinordnung als Architektur-Feature vermeiden | kein moo-Task |
| Agentic Engineering | GLM-4.5:21, 148-149, 883ff | ARC-Framing (agentic/reasoning/coding), TAU-Bench/BFCL/SWE-bench; Agent-RL-Infra | Post-Training-/Benchmark-Ebene, kein uebertragbarer Mechanismus fuers Toy. | — | kein moo-Task |

## ChatGLM/GLM-4 (2406.12793)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| GQA ersetzt MHA zur KV-Cache-Reduktion | ChatGLM-GLM4:311-314 | "replaced MHA with GQA to cut down on the KV cache size during inference"; FFN-Kompensation dffn=10/3 hidden | **ZWEITE unabhaengige Quelle fuer den M2-GQA-Claim** (neben DeepSeek-V2). Detail: GQA spart Params → GLM kompensiert via FFN-Breite; fuers moo-Toy nicht noetig (Mechanik-, nicht Qualitaetsziel). | keins | stuetzt M2 |
| No-Bias-Except-QKV, RMSNorm, SwiGLU, 2D-RoPE | ChatGLM-GLM4:304-310 | woertlich belegt | Architektur-Zutaten fuer ein spaeteres glm_4_like-Profil; RMSNorm/SwiGLU waeren neue Bausteine (Aktivierungs-Registry traegt SwiGLU-Gate-Teil NICHT trivial — gated FFN ist Struktur, nicht Aktivierung!). RoPE fehlt moo komplett (position-Layer ist additiv gelernt/sinus). | RoPE/SwiGLU = eigene kleine Schnitte, NICHT in M1-M6 enthalten — bei Profil-Wunsch neue Tasks | Folge-Option |
| Tool-Use / All Tools | ChatGLM-GLM4:28-31, 227-231 | Alignment-Ebene (Intent + Tool-Wahl: Browser, Python, T2I, user-defined functions) | Post-Training-/Produkt-Ebene, kein Architektur-Baustein fuers Toy. | — | kein moo-Task |

## GLM-Basis + GLM-130B

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Autoregressive Blank Infilling + 2D-Positional | GLM:31-35, 72-75 | "autoregressive blank infilling ... 2D positional encodings ... arbitrary order to predict spans" | Basis fuer das in der Erstanalyse skizzierte glm_like_blank_infilling-Profil: es ist ein TRAININGS-OBJECTIVE (+ Attention-Sichtbarkeits-Schema), kein Layer — wuerde auf moo als Daten-/Masken-Schema aufsetzen. Kein aktueller M-Task. | mittel: braeuchte flexible Attention-Sichtbarkeit (GLM-130B: bidirektional ueber unmaskierte Kontexte) — mehr als causal_maske-Muster | Folge-Option, eigener Verify vor Bau |
| GLM-130B: DeepNorm, RoPE, GLU+GeLU | ChatGLM-GLM4:299-301 | Engineering-Historie | Kontext, keine Toy-Implikation. | — | Notiz |

## Luecken (explizit, quellenehrlich)

| Thema | Status |
|---|---|
| **GLM-5.1 / GLM-5.2** | KEINE Extrakte in text/pdf/analysis vorhanden → keine Aussagen moeglich. Falls GLM-5.x-Profile gewuenscht: erst Model-Cards/Reports beschaffen + PAPER-VERIFY-03. |
| **IndexShare** | Begriff kommt in KEINEM der 19 Extrakte vor (Volltext-grep ueber alle) → vermutlich GLM-5.x-/Model-Card-Terminologie ausserhalb dieser Sammlung. Keine quellengesicherte Aussage, keine moo-Ableitung. |
| Sparse-Attention bei GLM | In den GLM-Extrakten nicht als eigener Mechanismus belegt (Sparse-Referenz der Sammlung ist DeepSeek-V3.2 DSA) → GLM-Sparse-Aussagen unterlassen. |

## Gesamt-Einordnung

Kein Blocker fuer KI-M1 (basiert auf DeepSeekMoE, VERIFY-01). GLM-Beitraege: (a) M3-Zweitbeleg hybrid thinking/direct, (b) M2-Zweitbeleg GQA↔KV-Cache, (c) sigmoid-gate/loss-free als klar abgegrenzte MoE-Folge-Optionen, (d) Long-Horizon/Agentic/Tool-Use sauber als Trainings-Infra/Post-Training eingeordnet — KEINE Architektur-Tasks daraus. GLM-Profile (glm_45_like, glm_like_blank_infilling) bleiben Folge-Optionen mit eigenem Verify vor Bau; GLM-5.x/IndexShare sind ohne neue Quellen tabu.
