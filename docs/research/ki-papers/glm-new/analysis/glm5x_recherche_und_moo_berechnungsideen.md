# Neue GLM/Z.ai-Modelle: Suche nach GLM-5.x / GLM-5.3

Stand: 2026-07-05

## Ergebnis kurz

- Offiziell sichtbar sind in den abgefragten Quellen: `GLM-5`, `GLM-5.1`, `GLM-5.2` sowie FP8-Varianten.
- Ein sauber belegbares `GLM-5.3` wurde in HuggingFace/Z.ai/GitHub/arXiv-Suche **nicht gefunden**.
- Falls Moritz „5.3“ meinte, ist aktuell wahrscheinlich `GLM-5.2` oder eine interne/API-Version gemeint.

## Gefundene offizielle Modellkarten

### GLM-5-FP8

Gefundene Stichworte: FP8/Quant, long context, agent/coding, sparse attention, reasoning, domestic chips

Auszuege/Stichpunkte aus der Modellkarte:
- # GLM-5-FP8
- We are launching GLM-5, targeting complex systems engineering and long-horizon agentic tasks. Scaling is still one of the most important ways to improve the intelligence efficiency of Artificial General Intelligence (AGI). Compared to GLM-4
- Reinforcement learning aims to bridge the gap between competence and excellence in pre-trained models. However, deploying it at scale for LLMs is a challenge due to the RL training inefficiency. To this end, we developed [slime](https://git
- | SWE-bench Verified               | 77.8                   | 73.8      | 73.1          | 76.8      | 80.9            | 76.2         | 80.0            |
- | SWE-bench Multilingual           | 73.3                   | 66.7      | 70.2          | 73.0      | 77.5            | 65.0         | 72.0            |
- | BrowseComp (w/ Context Manage)   | 75.9                   | 67.5      | 67.6          | 74.9      | 67.8            | 59.2         | 65.8            |
- * **Humanity’s Last Exam (HLE) & other reasoning tasks**: We evaluate with a maximum generation length of 131,072 tokens (`temperature=1.0, top_p=0.95, max_new_tokens=131072`). By default, we report the text-only subset; results marked with
- * **SWE-bench & SWE-bench Multilingual**: We run the SWE-bench suite with OpenHands using a tailored instruction prompt. Settings: `temperature=0.7, top_p=0.95, max_new_tokens=16384`, with a 200K context window.
- * **BrowserComp**: Without context management, we retain details from the most recent 5 turns. With context management, we use the same discard-all strategy as DeepSeek-v3.2 and Kimi K2.5.
- * **Terminal-Bench 2.0 (Terminus 2)**: We evaluate with the Terminus framework using `timeout=2h, temperature=0.7, top_p=1.0, max_new_tokens=8192`, with a 128K context window. Resource limits are capped at 16 CPUs and 32 GB RAM.

### GLM-5.1-FP8

Gefundene Stichworte: MoE, FP8/Quant, long context, agent/coding, sparse attention, reasoning, domestic chips

Auszuege/Stichpunkte aus der Modellkarte:
- # GLM-5.1-FP8
- GLM-5.1 is our next-generation flagship model for agentic engineering, with significantly stronger coding capabilities than its predecessor. It achieves state-of-the-art performance on SWE-Bench Pro and leads GLM-5 by a wide margin on NL2Re
- GLM-5.1, by contrast, is built to stay effective on agentic tasks over much longer horizons. We've found that the model handles ambiguous problems with better judgment and stays productive over longer sessions. It breaks complex problems do
- | SWE-Bench Pro                              | **58.4**           | 55.1                | 56.6         | 56.2                 | -                    | 53.8       | 57.3            | 54.2           | 57.7             |
- | BrowseComp (w/ Context Manage)             | 79.3               | 75.9                | -            | -                    | 67.6                 | 74.9       | 84.0            | **85.9**       | 82.7             |
- - [Transformers](https://github.com/huggingface/transformers) (v0.5.3+) — see [transformers docs](https://github.com/huggingface/transformers/blob/main/docs/source/en/model_doc/glm_moe_dsa.md)
- @misc{glm5team2026glm5vibecodingagentic,
- title={GLM-5: from Vibe Coding to Agentic Engineering},

### GLM-5.1

Gefundene Stichworte: MoE, long context, agent/coding, sparse attention, reasoning, domestic chips

Auszuege/Stichpunkte aus der Modellkarte:
- GLM-5.1 is our next-generation flagship model for agentic engineering, with significantly stronger coding capabilities than its predecessor. It achieves state-of-the-art performance on SWE-Bench Pro and leads GLM-5 by a wide margin on NL2Re
- GLM-5.1, by contrast, is built to stay effective on agentic tasks over much longer horizons. We've found that the model handles ambiguous problems with better judgment and stays productive over longer sessions. It breaks complex problems do
- | SWE-Bench Pro                              | **58.4**           | 55.1                | 56.6         | 56.2                 | -                    | 53.8       | 57.3            | 54.2          | 57.7             |
- | BrowseComp (w/ Context Manage)             | 79.3               | 75.9                | -            | -                    | 67.6                 | 74.9       | 84.0            | **85.9**      | 82.7             |
- - [Transformers](https://github.com/huggingface/transformers) (v0.5.3+) — see [transformers docs](https://github.com/huggingface/transformers/blob/main/docs/source/en/model_doc/glm_moe_dsa.md)
- @misc{glm5team2026glm5vibecodingagentic,
- title={GLM-5: from Vibe Coding to Agentic Engineering},

### GLM-5.2-FP8

Gefundene Stichworte: MoE, FP8/Quant, long context, agent/coding, sparse attention, reasoning, domestic chips

Auszuege/Stichpunkte aus der Modellkarte:
- # GLM-5.2-FP8
- We're introducing GLM-5.2, our latest flagship model for long-horizon tasks. It marks a substantial leap in long-horizon task capability over its predecessor GLM-5.1 and, for the first time, delivers that capability on a **solid 1M-token co
- - **Solid 1M Context:** A solid 1M-token context that stably sustains long-horizon work
- - **Improved Architecture**: We propose [IndexShare](https://arxiv.org/abs/2603.12201), which reuses the same indexer across every four sparse attention layers, reducing per-token FLOPs by 2.9× at a 1M context length. We also improve GLM-5.
- |SWE-bench Pro|62.1|58.4|60.6|59|55.4|69.2|58.6|54.2|
- |Agentic|||||||||||
- - [Transformers](https://github.com/huggingface/transformers) (v0.5.12+) — see [transformers docs](https://github.com/huggingface/transformers/blob/main/docs/source/en/model_doc/glm_moe_dsa.md)
- * **Humanity’s Last Exam (HLE) & other reasoning tasks**: We use sampling parameters of `temperature=1.0`, `top_p=0.95` for evaluation. We evaluate with a maximum generation length of `163,840` tokens. By default, we report the text-only su
- * **SWE-Bench Pro**: We run the SWE-Bench Pro suite with OpenHands using a tailored instruction prompt. Settings: `temperature=1`, `top_p=1`, `max_new_tokens=32k`, with a 400K context window.
- * **NL2Repo**: We evaluated NL2Repo with `temperature=1.0`, `top_p=1.0`, and `max_new_tokens=48k` under 400k context. To prevent hacking, we use rule-based and a LLM-based judgement to prevent malicious behaviors (e.g., unauthorized pip or 

### GLM-5.2

Gefundene Stichworte: MoE, long context, agent/coding, sparse attention, reasoning, domestic chips

Auszuege/Stichpunkte aus der Modellkarte:
- We're introducing GLM-5.2, our latest flagship model for long-horizon tasks. It marks a substantial leap in long-horizon task capability over its predecessor GLM-5.1 and, for the first time, delivers that capability on a **solid 1M-token co
- - **Solid 1M Context:** A solid 1M-token context that stably sustains long-horizon work
- - **Improved Architecture**: We propose [IndexShare](https://arxiv.org/abs/2603.12201), which reuses the same indexer across every four sparse attention layers, reducing per-token FLOPs by 2.9× at a 1M context length. We also improve GLM-5.
- |SWE-bench Pro|62.1|58.4|60.6|59|55.4|69.2|58.6|54.2|
- |Agentic|||||||||||
- - [Transformers](https://github.com/huggingface/transformers) (v0.5.12+) — see [transformers docs](https://github.com/huggingface/transformers/blob/main/docs/source/en/model_doc/glm_moe_dsa.md)
- * **Humanity’s Last Exam (HLE) & other reasoning tasks**: We use sampling parameters of `temperature=1.0`, `top_p=0.95` for evaluation. We evaluate with a maximum generation length of `163,840` tokens. By default, we report the text-only su
- * **SWE-Bench Pro**: We run the SWE-Bench Pro suite with OpenHands using a tailored instruction prompt. Settings: `temperature=1`, `top_p=1`, `max_new_tokens=32k`, with a 400K context window.
- * **NL2Repo**: We evaluated NL2Repo with `temperature=1.0`, `top_p=1.0`, and `max_new_tokens=48k` under 400k context. To prevent hacking, we use rule-based and a LLM-based judgement to prevent malicious behaviors (e.g., unauthorized pip or 
- * **DeepSWE**: We run DeepSWE with the official pier evaluation framework and the mini-swe-agent harness (`temperature=1.0`, `top_p=1.0`, `timeout=2h`, 400K context). Each task is solved in an isolated container with 2 CPUs, 8 GB RAM, and n

### GLM-5

Gefundene Stichworte: long context, agent/coding, sparse attention, reasoning, domestic chips

Auszuege/Stichpunkte aus der Modellkarte:
- We are launching GLM-5, targeting complex systems engineering and long-horizon agentic tasks. Scaling is still one of the most important ways to improve the intelligence efficiency of Artificial General Intelligence (AGI). Compared to GLM-4
- Reinforcement learning aims to bridge the gap between competence and excellence in pre-trained models. However, deploying it at scale for LLMs is a challenge due to the RL training inefficiency. To this end, we developed [slime](https://git
- | SWE-bench Verified               | 77.8                   | 73.8      | 73.1          | 76.8      | 80.9            | 76.2         | 80.0            |
- | SWE-bench Multilingual           | 73.3                   | 66.7      | 70.2          | 73.0      | 77.5            | 65.0         | 72.0            |
- | BrowseComp (w/ Context Manage)   | 75.9                   | 67.5      | 67.6          | 74.9      | 67.8            | 59.2         | 65.8            |
- * **Humanity’s Last Exam (HLE) & other reasoning tasks**: We evaluate with a maximum generation length of 131,072 tokens (`temperature=1.0, top_p=0.95, max_new_tokens=131072`). By default, we report the text-only subset; results marked with
- * **SWE-bench & SWE-bench Multilingual**: We run the SWE-bench suite with OpenHands using a tailored instruction prompt. Settings: `temperature=0.7, top_p=0.95, max_new_tokens=16384`, with a 200K context window.
- * **BrowserComp**: Without context management, we retain details from the most recent 5 turns. With context management, we use the same discard-all strategy as DeepSeek-v3.2 and Kimi K2.5.
- * **Terminal-Bench 2.0 (Terminus 2)**: We evaluate with the Terminus framework using `timeout=2h, temperature=0.7, top_p=1.0, max_new_tokens=8192`, with a 128K context window. Resource limits are capped at 16 CPUs and 32 GB RAM.
- @misc{glm5team2026glm5vibecodingagentic,

## Rechen-/Architekturideen fuer moo aus GLM-5.x

1. **Agentic-Coding-Profil**
   - GLM-5.x ist stark auf lange Coding-/Agent-Aufgaben ausgerichtet. Fuer `moo`: eigenes Trainingsprofil mit verifizierbaren Coding-/Terminal-Aufgaben, passend zu Synapse.

2. **Long-Horizon-/Long-Context-Pfad**
   - Fuer `moo`: KV-Cache, Sliding-/Sparse-Attention, Kontext-Segmentierung und Speicher-/Resume-Mechanik als getrennte Systeme planen.

3. **FP8-/Quantisierungsvarianten**
   - Offizielle FP8-Modelle zeigen: DType/Quantisierung muss als eigene Tensor-Schicht kommen, nicht nur als Modell-Option.

4. **MoE bleibt zentral**
   - GLM-5.x ist in der gleichen Richtung wie GLM-4.5/DeepSeek/Kimi/Qwen: wenige aktive Parameter, viele Gesamtparameter. Fuer `moo`: Mini-MoE als Pflicht-Gate.

5. **Hardware-/Serving-Optimierung**
   - Z.ai optimiert sichtbar fuer verschiedene Inferenz-Hardware. Fuer `moo`: CPU/GPU-Backend, Device-Local, Staging, DType und Batch/Cache als getrennte Backends halten.

## Empfohlene Ablage/Naechster Schritt

- `GLM-5.2` und `GLM-5.1` sollten als neue Profile in die Paper-/Backlog-Auswertung aufgenommen werden.
- Kein `GLM-5.3` als Task anlegen, solange keine offizielle Quelle gefunden wurde.
- Stattdessen Task-Titel: `GLM-5.x-Agentic-Long-Horizon-Profile`.
