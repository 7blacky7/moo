# Paper→moo Profil-Mapping (Architektur-Inventur)

Stand: 2026-07-06. Baut auf `moo_ki_paper_erstanalyse.md` + `neuere_modelle_berechnungsmoeglichkeiten_2025_2026.md` auf und mappt deren Bausteine auf den **realen moo-Ist-Stand nach der Typ-Zentralisierung** (Commits 8f57d1c/58a1e8e/fc8c383/a32997c). Reihenfolge = Moo-Priorisierung 2026-07-06.

## Ist-Stand (was die Profile tragen muss)

| Baustein | Stand | Relevanz fuers Mapping |
|---|---|---|
| Schicht-Registry | `MooNNLayerDesc` + Save/Load-Hooks, Vollstaendigkeit doppelt erzwungen | **Neuer Layer = 1 Registry- + 1 Hook-Zeile + fw/params/aw/rb** |
| Ops | 26 Registry-Ops, alle mit backward + Gradcheck (B2-Vertrag) | Kompositionen erben den Gradcheck; neuer Op NUR mit bw + Gradcheck-Zeile |
| Layer | dicht, dropout, layernorm, embedding, attention (MHA causal, pro-Kopf wq/wk/wv+wo), position (gelernt/sinus) | MHA = Baseline fuer Attention-Profile |
| Aktivierungen | Registry (relu/sigmoid/tanh/gelu/softmax) inkl. he_init-Flag | neue Aktivierung = 1 Zeile |
| Training | mse, kreuzentropie (fused logsoftmax), sgd/adam/adamw, grad_clip, trainiere() | CE/SFT-Baseline steht |
| Persistenz | .mook = safetensors (arch im __metadata__), Fremdimport | neue Layer-Felder muessen .mook-Param-Reihenfolge deterministisch halten (Vertrag!) |
| Gates | ir/tag/leak in run_all; MNIST bit-identisch; LM-Kurve byte-identisch (ki_sprachmodell) | jedes Profil braucht ein eigenes kleines deterministisches Gate |
| Grenzen | f32-only; GPU = Vulkan-PoC (Default CPU); kein Inferenz-Zustand (Cache) | DType/Serving bewusst spaeter |

Maskierungs-Muster als grad-lose Konstante existiert (causal_maske) — traegt Top-k-Routing und Sliding/Global.

## Profil-Mapping (Prioritaet = Moo-Reihenfolge)

### M1 — Mini-MoE (`moe_top1` / `moe_top2`)
Papers: DeepSeekMoE, DeepSeek-V2/V3, Qwen3, Kimi-K2, GLM-4.5, MiMo.
Bausteine auf moo gemappt:
- Neuer Layer `moe`: n Experten (je 2 dicht-Schichten als FFN), Router = dicht(dim→n)+softmax.
- Top-k als **grad-lose Maske** (causal_maske-Muster); Gradient fliesst ueber die softmax-**gewichtete Summe** der aktiven Experten-Outputs — reine Op-Komposition (matmul/mul/add/softmax), KEIN neuer Registry-Op, Gradcheck-Vertrag bleibt.
- Load-Balancing-METRIK zuerst (Auslastungs-Zaehler pro Experte im Layer-Dict, dropout-"zaehler"-Muster), aux-loss erst als Folge-Option.
- Persistenz: Experten-Params deterministisch (e0_w1,e0_b1,... + router_w — attention-wq%d-Muster).
Gate: Toy-Task, Verlust sinkt, Auslastung protokolliert nicht-degeneriert (kein Experte 0%/100%), MoE-vs-dense-Vergleich, .mook-Roundtrip bit-identisch.
Aufwand: 1 Registry-Zeile + Hooks + fw_moe/params_moe; mittelklein.

### M2 — Attention-Varianten + KV-Cache (`gqa`, `sliding_global`, Cache)
Papers: DeepSeek-V2 (MLA), MiMo (SWA+global), DeepSeek-V3.2 (sparse), Qwen3.5 (hybrid).
- **GQA/MQA**: Option `kv_koepfe` am bestehenden attention-Layer (KV-Kopf-Sharing). ACHTUNG .mook-Vertrag: Param-Reihenfolge nur ANHAENGEN, alte Netze muessen ladbar bleiben (arch-JSON ohne kv_koepfe → Default = koepfe).
- **Sliding+Global**: zweite Masken-Variante neben causal_maske (grad-los) — kleinster Schnitt.
- **KV-Cache**: expliziter Inferenzzustand NUR bei autograd_aus; Cache-Dict am Layer (k/v pro Schritt anhaengen), API `netz.cache_leeren()`. Kein Autograd-Kontakt.
- MLA-like: erst NACH GQA evaluieren (eigenes Latent-Projektions-Paar; Op-Komposition moeglich, aber groesserer Schnitt).
Gate: Vorwaerts-Aequivalenz GQA(kv=koepfe)==MHA bit-identisch; Cache-Inferenz == Non-Cache-Inferenz bit-identisch auf Toy-Sequenz.

### M3 — Reasoning-Modus / Thinking-Budget (`direkt`/`denken`)
Papers: Qwen3 (thinking budget), Phi-4-reasoning (mode tokens), GLM-4.5 (hybrid).
- KEIN Agentensystem: Mode-TOKENS im Char-LM-Vokabular (<denk>/<antwort>-Spezialtokens) + Trainingsdaten-Schema; `thinking_budget` = Sampling-Parameter (max Denk-Schritte bis erzwungenes Antwort-Token).
- Traegt komplett auf ki_sprachmodell auf; Profil-/Trainingsmodus, kein neuer Layer.
Gate: deterministisches Toy-Korpus-Training, Budget-Erschoepfung erzwingt Antwort, Kurven-Gate wie LM.

### M4 — MTP / Speculative-Decoding-Toy (`mtp_head`)
Papers: DeepSeek-V3, MiMo-V2-Flash.
- Optionaler zweiter Head (dicht) auf denselben Hidden-States: Loss = CE(t+1) + lambda*CE(t+2) — reine Komposition.
- Spec-Decoding-Toy: MTP-Head als Draft, Haupt-Head verifiziert (Akzeptanzrate protokollieren). Keine Serving-Architektur.
Gate: MTP-Training senkt beide CE-Terme monoton; Spec-Toy misst Akzeptanzrate deterministisch.

### M5 — Reward-API + GRPO-Toy
Papers: DeepSeekMath (GRPO), DeepSeek-R1, Kimi-K2, Qwen3-Coder-Next, Phi-4.
- Reward-API: moo-Funktion (Sequenz→Zahl) als Schnittstelle; VERIFIZIERBARE Toy-Umgebung (z.B. Ziffern-Addition: Reward = exakt richtig).
- GRPO-Kern: Gruppen-Sampling + gruppenrelative Advantages + advantage-gewichtete CE — pruefen ob als Op-Komposition (mul mit grad-loser Advantage-Konstante) darstellbar; sonst STOPP + Design-Review (B2-Vertrag).
Gate: Toy-Env, Reward steigt ueber Gruppen, deterministisch geseedet.

### M6 — DType/Quantisierung (NUR PLAN, kein Code)
Papers: GLM-130B (INT4), DeepSeek-V3 (FP8), alle (BF16).
- f32 bleibt Hauptpfad. Ergebnis dieses Tasks = Design-Memo: Tensor-DType-Feld-Auswirkung auf Registry-Ops/Autograd/GPU-Pfad/safetensors-dtypes; Reihenfolge-Empfehlung (bf16 zuerst, fp8/int4 spaeter); KEINE Implementierung.

## Explizit NICHT jetzt
Multimodal (Bild/Audio/Video), MLA-Vollausbau, Sparse-Attention-Vollausbau, Serving/Offload, Agentic-Environments (Coding/Terminal), grosse Tokenizer. Profil-Zucker `ki_profil(name)` erst wenn >=2 Profile real existieren (sonst API ohne Substanz).

## Arbeitsregeln fuer alle M-Tasks
Kein neuer Registry-Op ohne backward+Gradcheck-Zeile (B2). .mook-Param-Reihenfolge ist Vertrag — nur anhaengen, Abwaertskompatibilitaet testen. Jedes Profil bekommt ein kleines deterministisches Gate (LM-Kurven-Muster). MNIST/LM-Basisgates muessen bit-/byte-identisch bleiben (neue Layer duerfen bestehende Pfade nicht beruehren). Keine Bootloader-/Windows-/P011-Dateien.
