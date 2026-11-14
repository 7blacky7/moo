# Erste Paper-Analyse fuer moo-KI-Architektur

Stand: 2026-07-05

Diese Datei ist bewusst kompakt. Sie soll spaeteren Agenten zeigen, welche mathematischen Bausteine aus den Papers fuer `moo` interessant sind, ohne direkt die PDFs komplett in den Kontext zu laden.

## Abgelegte Paper

- `DeepSeek-R1_2501.12948`
- `DeepSeek-V2_2405.04434`
- `DeepSeek-V3_2412.19437`
- `DeepSeekMath_2402.03300`
- `DeepSeekMoE_2401.06066`
- `GLM-130B_2210.02414`
- `GLM_2103.10360`

## Begriffliche Treffer pro Paper

| Paper | MoE | Attention | Training | Precision/Perf | Reasoning |
|---|---:|---:|---:|---:|---:|
| `DeepSeek-R1_2501.12948` | 31 | 7 | 382 | 17 | 514 |
| `DeepSeek-V2_2405.04434` | 193 | 178 | 164 | 53 | 93 |
| `DeepSeek-V3_2412.19437` | 281 | 57 | 246 | 182 | 168 |
| `DeepSeekMath_2402.03300` | 0 | 2 | 225 | 3 | 520 |
| `DeepSeekMoE_2401.06066` | 781 | 19 | 88 | 3 | 30 |
| `GLM-130B_2210.02414` | 7 | 33 | 106 | 100 | 24 |
| `GLM_2103.10360` | 0 | 51 | 24 | 4 | 4 |

## Relevante Bausteine fuer moo

### 1. Transformer-Grundlage

- Decoder-only Transformer als naechstes kleines Gate.
- Zuerst Char-/Byte-LM, nicht direkt grosser Tokenizer.
- Minimal benoetigt: Embedding, Attention, FFN, LayerNorm/RMSNorm, Softmax/CE, Sampling.

### 2. Attention-Varianten

- Standard Multi-Head Attention als Baseline.
- Danach GQA/MQA oder MLA-artiges Experiment als eigenes Profil.
- Wichtig fuer `moo`: Attention als austauschbarer Layer, nicht fest in Transformer verdrahten.

### 3. MoE / Experten

- Mini-MoE mit 2-4 Experten als didaktisches Gate.
- Router zuerst top-1 oder top-2, mit einfacher Load-Balancing-Metrik.
- Spaeter shared experts / spezialisierte Experts nach DeepSeekMoE-Idee pruefen.

### 4. Trainingsverfahren

- SFT/klassisches CE bleibt Baseline.
- Distillation als kleines Folge-Gate.
- RL/GRPO-artige Verfahren erst als Toy-Environment, z. B. einfache Mathe-/Regelaufgaben.
- Reward-Schnittstelle als API vorbereiten, aber nicht sofort grosses RL-System bauen.

### 5. Performance-/Precision-Schicht

- Aktuell hat `moo` f32 Tensoren. Papers nutzen oft bf16/fp8/quantisierte Pfade.
- Fuer `moo`: erst GPU2 verbessern, dann optional bf16/f16/fp16 und Quantisierung planen.
- KV-Cache und Batch/Sequence-Optimierung werden fuer LM-Inferenz wichtig.

## Vorschlag fuer spaetere moo-Tasks

```text
P014-G1: Mini-Transformer + Char-LM
P014-G2: Attention-Profile: MHA, GQA/MQA, spaeter MLA-like
P014-G3: Mini-MoE: Router + 2-4 Experten + Load-Balancing-Gate
P014-G4: Sampling/KV-Cache fuer Inferenz
P014-H1: Trainingsprofile trennen: CE/SFT/Distillation/RL-Toy
P014-H2: Reward-API + GRPO-aehnliches Toy-Gate
P014-I1: KI-Profil-Dateien fuer Paper-inspirierte Architekturen
```

## Profil-Idee

Spaeter sollte `moo` KI-Architekturen nicht hart codieren, sondern als Profile konfigurieren koennen:

```moo
setze profil auf ki_profil("deepseek_v3_like")
setze modell auf ki_modell(profil)
```

Beispielhafte Profile:

- `transformer_dense_baseline`
- `deepseek_v2_like_mla_moe`
- `deepseek_v3_like_moe_mtp`
- `deepseek_r1_like_rl_reasoning_toy`
- `glm_like_blank_infilling`
- `glm_130b_like_bilingual_transformer`

## Snippets / erste Hinweise

### DeepSeek-R1_2501.12948

- **MoE / Experts:** to the creation of R1-Zero, and their work laid the foundation for R1. Daya Guo also contributed to the RL training stability of MOE models. Junxiao Song proposed the GRPO algorithm, implemented the initial version, and introduced rule-based rewards for math
- **Attention:** uning and reinforcement learning to enhance its abilities across various domains. The model incorporates innovative features like Multi-head Latent Attention (MLA) (DeepSeek-AI, 2024a) for efficient inference, an auxiliary- loss-free load-balancing strategy, a
- **Training:** DeepSeek-R1: Incentivizing Reasoning Capability in LLMs via Reinforcement Learning DeepSeek-AI
- **Precision / Performance:** aset. The maximum sequence length during training is set to 8192 tokens, whereas no explicit limit is imposed during reward model inference. Safety Reward Model To assess and improve model safety, we curated a dataset of 106,000 prompts with model-generated
- **Math / Reasoning:** DeepSeek-R1: Incentivizing Reasoning Capability in LLMs via Reinforcement Learning

### DeepSeek-V2_2405.04434

- **MoE / Experts:** DeepSeek-V2: A Strong, Economical, and Efficient Mixture-of-Experts Language Model
- **Attention:** s a context length of 128K tokens. DeepSeek-V2 adopts innovative architectures including Multi-head Latent Attention (MLA) and DeepSeekMoE. MLA guarantees efficient inference thr
- **Training:** ce. It comprises 236B total parameters, of which 21B are activated for each token, and supports a context length of 128K tokens. DeepSeek-V2 adopts innovative architectures inclu
- **Precision / Performance:** xture-of-Experts (MoE) language model characterized by economical training and efficient inference. It comprises 236B total parameters, of which 21B are activated for each token,
- **Math / Reasoning:** E Discussion About Pre-Training Data Debiasing 32 F Additional Evaluations on Math and Code 32 G Evaluation Formats

### DeepSeek-V3_2412.19437

- **MoE / Experts:** Abstract arXiv:2412.19437v2 [cs.CL] 18 Feb 2025 We present DeepSeek-V3, a strong Mixture-of-Experts (MoE) language model with 671B total parameters with 37B activated
- **Attention:** token. To achieve efficient inference and cost-effective training, DeepSeek-V3 adopts Multi-head Latent Attention (MLA) and DeepSeekMoE architec- tures, which were thorough
- **Training:** e, DeepSeek-V3 pioneers an auxiliary-loss-free strategy for load balancing and sets a multi-token prediction training objective for stronger performance. We pre-train DeepS
- **Precision / Performance:** el with 671B total parameters with 37B activated for each token. To achieve efficient inference and cost-effective training, DeepSeek-V3 adopts Multi-head Latent Attention
- **Math / Reasoning:** MMLU-Pro GPQA-Diamond MATH 500 AIME 2024 Codeforces SWE-bench Verified

### DeepSeekMath_2402.03300

- **Attention:** automation is beneficial to ensure the accuracy and relia- bility of mathematical proofs and enhance efficiency, with increasing attention in recent years. We evaluate DeepSeekMath-Base 7B on the task of informal-to-formal proving from (Jiang et al., 2022) wh
- **Training:** neered data selection pipeline. Second, we introduce Group Relative Policy Optimization (GRPO), a variant of Proximal Policy Optimization (PPO), that enhances mathematical reasoning
- **Precision / Performance:** ion with advanced sampling (decoding) strategies, like those based on tree-search methods (Yao et al., 2023). Also, the efficient inference techniques (Kwon et al., 2023; Leviathan et al., 2023; Xia et al., 2023, 2024), which determines
- **Math / Reasoning:** DeepSeekMath: Pushing the Limits of Mathematical Reasoning in Open Language Models

### DeepSeekMoE_2401.06066

- **MoE / Experts:** DeepSeekMoE: Towards Ultimate Expert Specialization in Mixture-of-Experts Language Model
- **Attention:** h𝑡𝑙 = FFN u𝑡𝑙 + u𝑡𝑙 , (2) where 𝑇 denotes the sequence length, Self-Att(·) denotes the self-attention module, FFN(·) denotes the Feed-Forward Network (FFN), u1: 𝑙 𝑇
- **Training:** rts, face challenges in ensuring expert specialization, i.e. each expert acquires non-overlapping and focused knowledge. In response, we propose the DeepSeekMoE architecture towa
- **Precision / Performance:** DeepSeekMoE 16B to the public. Notably, this model can be deployed on a single GPU with 40GB of memory without the need for quantization. 2. Preliminaries: Mixture-of-Experts for Transformers We first introduce a generic MoE architecture commonly used
- **Math / Reasoning:** . It is de- 7 rived from diverse sources, including web text, mathematical material, coding scripts, published literature, and various other textual materials. For the purpose of validation ex

### GLM-130B_2210.02414

- **MoE / Experts:** verse benchmark including 57 multi-choice question an- swering tasks concerning human knowledge ranging from high-school-level to expert-level. It is released after the crawling of Pile and serves as an ideal test-bed for LLMs’ few-shot learning. The GPT-3 res
- **Attention:** corrupted spans, their visibility to each other is decided by a randomly sampled permutation on their order. GLM’s bidirectional attention over unmasked (i.e., uncorrupted) contexts distinguishes GLM-130B from GPT-style LLMs in which the unidirectional attent
- **Training:** we face numer- ous unexpected technical and engineering challenges, particularly on loss spikes and divergence. In this paper, we introduce the training p
- **Precision / Performance:** e first among 100B-scale models and more importantly, allowing its effective inference on 4×RTX 3090 (24G) or 8×RTX 2080 Ti (11G) GPUs, the most affordabl
- **Math / Reasoning:** ON G AME B ENCHMARK (BIG- BENCH ) BIG-bench (Srivastava et al., 2022) benchmarks challenging tasks concerning models’ ability on reasoning, knowledge, and commonsense. Given evaluating on its 150 tasks is time-consuming for LLMs, we report the BIG-bench-lite—

### GLM_2103.10360

- **Attention:** hem autoregressively. (Some ditional generation. We propose a General attention edges are omitted; cf. Figure 2.) Language Model (GLM) based on autoreg
- **Training:** <latexit sha1_base64="cIlXHKTMHL8y94GI+KZXnlT1K7g=">AAAB7XicbVDLSgNBEOyNrxhfUY9ehgQhIoRdD+ox6MVjBPOAZAmzk9lkzOzMMjMrLjH/4EEPinj1f7zlb5w8DppY0FBUddPdFcScaeO6Yyezsrq2vpHdzG1t7+zu5fcP6lomitAakVyqZoA15UzQmmGG02asKI4CThvB4HriNx6o0kyKO5PG1I9wT7CQEWysV
- **Precision / Performance:** tion encodings before corruption. During inference, as generation tasks of blank infilling, following
- **Math / Reasoning:** ens Winter, Christopher Hesse, Mark Chen, and Paolini et al. (2020) convert structured predic- Eric Sigler, Mateusz Litwin, Scott Gray, Benjamin Chess, Jack Clark, Christopher Berner, Sam Mc- tion


## Nachtraeglich ergaenzte GLM-Paper

- `GLM-4.5_2508.06471`: relevant fuer MoE, hybrid thinking/direct-response, agentic/reasoning/coding Profil.
- `ChatGLM-GLM4_2406.12793`: relevant fuer GLM-Familie, Tool-Use, Engineering-Entwicklung von GLM-130B bis GLM-4.

Diese beiden Paper sollten vor einem `glm_like`-Profil gezielt gelesen werden.
