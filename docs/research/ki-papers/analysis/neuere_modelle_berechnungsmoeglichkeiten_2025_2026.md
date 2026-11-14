# Neuere Modelle 2025/2026 — Rechen- und Architekturmoeglichkeiten fuer moo

Stand: 2026-07-05

Diese Datei ergaenzt die erste Paper-Sammlung um neuere Modellfamilien. Fokus: Welche **Berechnungspfade** und mathematischen/architektonischen Ideen koennte `moo` spaeter anbieten?

## Neue/aktuelle Paper in dieser Runde

- `Qwen3_2505.09388` — Qwen3 Technical Report: Dense+MoE, thinking/non-thinking, thinking budget, multilingual, distillation
- `Qwen3-Omni_2509.17765` — Qwen3-Omni Technical Report: Thinker-Talker MoE, multimodal, codec prediction, streaming speech
- `Qwen3-VL_2511.21631` — Qwen3-VL Technical Report: VL long context, interleaved MRoPE, DeepStack, multimodal MoE
- `Qwen3-Coder-Next_2603.00729` — Qwen3-Coder-Next Technical Report: 80B-A3B coding agent, verifiable coding tasks, environment-feedback RL
- `Qwen3.5-Omni_2604.15804` — Qwen3.5-Omni Technical Report: Hybrid Attention MoE, omni-modality, ARIA alignment, long audio/video
- `Kimi-K2_2507.20534` — Kimi K2: Open Agentic Intelligence: 1T MoE, MuonClip optimizer, QK-clip, agentic data synthesis, joint RL
- `DeepSeek-V3.2_2512.02556` — DeepSeek-V3.2: Pushing the Frontier of Open Large Language Models: DeepSeek Sparse Attention, scalable RL, agentic synthesis pipeline
- `MiMo-V2-Flash_2601.02780` — MiMo-V2-Flash Technical Report: 309B-A15B MoE, SWA+global hybrid attention, MTP speculative decoding, MOPD
- `Phi-4-reasoning_2504.21318` — Phi-4-reasoning Technical Report: SFT on teachable prompts, outcome RL, inference-time reasoning chains
- `Phi-4-reasoning-vision_2603.03975` — Phi-4-reasoning-vision-15B Technical Report: compact multimodal reasoning, dynamic-resolution encoders, mode tokens

## Treffer-Matrix aus den Text-Extrakten

| Modell/Paper | MoE | Attention | RL/Reward | MTP/SpecDecode | Precision/Cache | Agentic | Multimodal |
|---|---:|---:|---:|---:|---:|---:|---:|
| Qwen3 | 57 | 9 | 135 | 0 | 0 | 7 | 3 |
| Qwen3-Omni | 16 | 19 | 63 | 14 | 3 | 10 | 569 |
| Qwen3-VL | 18 | 31 | 202 | 0 | 0 | 85 | 468 |
| Qwen3-Coder-Next | 26 | 6 | 134 | 0 | 0 | 216 | 5 |
| Qwen3.5-Omni | 18 | 11 | 91 | 7 | 0 | 15 | 598 |
| Kimi-K2 | 52 | 56 | 168 | 0 | 4 | 224 | 8 |
| DeepSeek-V3.2 | 16 | 54 | 101 | 0 | 1 | 123 | 2 |
| MiMo-V2-Flash | 52 | 107 | 182 | 60 | 7 | 125 | 16 |
| Phi-4-reasoning | 4 | 9 | 153 | 0 | 0 | 4 | 30 |
| Phi-4-reasoning-vision | 1 | 4 | 72 | 0 | 2 | 6 | 221 |

## Konkrete Berechnungspfade, die fuer moo interessant sind

### A. Dense Transformer Baseline

Pflichtbasis. Ohne stabile Baseline kein sinnvoller Vergleich.

- Embedding
- MHA/GQA
- RMSNorm/LayerNorm
- SwiGLU/FFN
- RoPE/MRoPE optional
- CE-Training
- Sampling

### B. Sparse/MoE-Pfad

Von Qwen3, Kimi K2, MiMo, GLM-4.5 und DeepSeek-V3/V3.2 klar bestaetigt: MoE ist der zentrale Effizienzpfad.

moo-Aufgaben:

- Mini-MoE mit 2-4 Experten
- Router top-1/top-2
- Load-Balancing-Metrik
- Shared-Expert-Option
- Expert-Auslastung im Training protokollieren

### C. Attention-Varianten

Aktuelle Modelle gehen weg von nur klassischer voller Attention:

- GQA/MQA fuer weniger KV-Speicher
- MLA/Latent-Attention-artige Pfade
- Sparse Attention fuer lange Kontexte
- Sliding-Window + Global Attention Hybrid
- MRoPE/interleaved-MRoPE fuer multimodale/spatio-temporale Daten

moo sollte Attention als austauschbare Strategie implementieren, nicht fest verdrahten.

### D. Reasoning-/Mode-Pfade

Qwen3, Phi-4 und GLM-4.5 zeigen: ein Modell kann schnelle direkte Antworten und langsames Denken/Reasoning per Modus trennen.

moo-Idee:

- `modus: "direkt"`
- `modus: "denken"`
- `thinking_budget` / Tokenbudget
- Mode-Tokens fuer Training/Inferenz

### E. RL/Agentic Training

Kimi K2, DeepSeek-V3.2, Qwen3-Coder-Next und Phi-4 zeigen: Post-Training wird immer staerker ueber Environment-/Reward-Schleifen.

moo-Idee:

- Reward-API
- kleine Toy-Umgebungen
- verifizierbare Aufgaben
- Coding-/Terminal-Environment spaeter
- GRPO/PPO/Outcome-RL als getrennte Trainingsprofile

### F. Multi-Token Prediction / Speculative Decoding

DeepSeek-V3 und MiMo-V2-Flash zeigen MTP als Trainings- und Inferenzbeschleuniger.

moo-Idee:

- MTP-Head optional an LM
- mehrere naechste Tokens vorhersagen
- MTP als Draft-Modell fuer speculative decoding nutzen

### G. Precision/Cache/Serving

Aktuelle Modelle nutzen FP8/BF16/INT4/KV-/Latent-Cache massiv.

moo-Idee:

- erst f32 stabil lassen
- danach f16/bf16/fp8 als Tensor-DType planen
- KV-Cache als expliziter Inferenzzustand
- Sparse/Latent-Cache fuer lange Kontexte spaeter
- Offload-Strategien CPU/GPU als eigenes Systemthema

### H. Multimodal-Pfade

Qwen3-Omni, Qwen3.5-Omni, Qwen3-VL und Phi-4-reasoning-vision zeigen, dass multimodale Modelle zunehmend dieselben Reasoning- und MoE-Mechaniken nutzen.

Fuer `moo` nicht sofort bauen, aber Profil-Schnitt vorbereiten:

- Text zuerst
- Bild-Tensoren spaeter
- Audio/Video nur nach stabiler Engine
- MRoPE/Time-Alignment als spaeterer Spezialpfad

## Empfohlene moo-Roadmap daraus

```text
KI-N1: Transformer Baseline + Char/Byte-LM
KI-N2: Attention-Strategien: MHA, GQA/MQA, SlidingWindow+Global
KI-N3: Mini-MoE: Router + Experts + Load-Balancing
KI-N4: Reasoning-Modus: direkt/denken + thinking_budget
KI-N5: Reward-API + Toy-RL/GRPO-artiges Gate
KI-N6: MTP + Speculative-Decoding Toy-Gate
KI-N7: KV-Cache + Inferenzzustand
KI-N8: DType-Plan: f16/bf16/fp8/quantisiert
KI-N9: Agentic Training mit verifizierbaren Umgebungen
```

## Wichtige Designentscheidung fuer moo

`moo` sollte nicht versuchen, ein Modell hart nachzubauen. Stattdessen sollte es **KI-Profile** koennen:

```moo
setze profil auf ki_profil("qwen3_moe_thinking")
setze modell auf ki_modell(profil)
```

Profile koennen dann Bausteine kombinieren:

- `dense_baseline`
- `moe_router_top2`
- `deepseek_sparse_attention_like`
- `qwen_thinking_budget_like`
- `kimi_agentic_rl_like`
- `mimo_hybrid_attention_mtp_like`
- `phi_reasoning_sft_rl_like`

## Hinweis fuer spaetere Agenten

Nicht alle PDFs in den Kontext laden. Erst diese Datei lesen, dann gezielt ein Paper/Text-Extrakt nach Bedarf oeffnen.
