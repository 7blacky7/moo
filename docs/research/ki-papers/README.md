# KI-Papers fuer moo KI-Architektur

Stand: 2026-07-05

Diese Dateien wurden lokal im Projekt abgelegt, damit spaetere Agenten sie gezielt analysieren koennen.

## PDFs
- `docs/research/ki-papers/pdf/ChatGLM-GLM4_2406.12793.pdf`
- `docs/research/ki-papers/pdf/DeepSeek-R1_2501.12948.pdf`
- `docs/research/ki-papers/pdf/DeepSeek-V2_2405.04434.pdf`
- `docs/research/ki-papers/pdf/DeepSeek-V3_2412.19437.pdf`
- `docs/research/ki-papers/pdf/DeepSeekMath_2402.03300.pdf`
- `docs/research/ki-papers/pdf/DeepSeekMoE_2401.06066.pdf`
- `docs/research/ki-papers/pdf/GLM-130B_2210.02414.pdf`
- `docs/research/ki-papers/pdf/GLM-4.5_2508.06471.pdf`
- `docs/research/ki-papers/pdf/GLM_2103.10360.pdf`

## Text-Extrakte
- `docs/research/ki-papers/text/ChatGLM-GLM4_2406.12793.txt`
- `docs/research/ki-papers/text/DeepSeek-R1_2501.12948.txt`
- `docs/research/ki-papers/text/DeepSeek-V2_2405.04434.txt`
- `docs/research/ki-papers/text/DeepSeek-V3_2412.19437.txt`
- `docs/research/ki-papers/text/DeepSeekMath_2402.03300.txt`
- `docs/research/ki-papers/text/DeepSeekMoE_2401.06066.txt`
- `docs/research/ki-papers/text/GLM-130B_2210.02414.txt`
- `docs/research/ki-papers/text/GLM-4.5_2508.06471.txt`
- `docs/research/ki-papers/text/GLM_2103.10360.txt`

## Analyse

- `docs/research/ki-papers/analysis/moo_ki_paper_erstanalyse.md`

## Hinweis

Die PDFs sind Arbeitsmaterial. Nicht automatisch in den aktiven Modell-Kontext laden; erst gezielt einzelne Abschnitte lesen.

## Neuere Modelle 2025/2026

- `docs/research/ki-papers/analysis/neuere_modelle_berechnungsmoeglichkeiten_2025_2026.md`

## GLM-5.x / GLM-5.3-Recherche

- `docs/research/ki-papers/glm-new/analysis/glm5x_recherche_und_moo_berechnungsideen.md`

## Quantisierung / Extreme Compression (TurboQuant-Linie, hinzugefügt 2026-07-19)

## Residual-Architektur (AttnRes, hinzugefügt 2026-07-19)

PDFs:
- `docs/research/ki-papers/pdf/AttnRes_2603.15031.pdf` (Kimi Team / MoonshotAI, 11.604 Wörter Extrakt)

Text:
- `docs/research/ki-papers/text/AttnRes_2603.15031.txt`

Analyse:
- `docs/research/ki-papers/analysis/paper_verify_05_attnres_matrix.md` — Verify-Matrix + Erstanalyse, GO für KI-R1 (Block-AttnRes als opt-in Residual-Modus). Kern: Softmax-Attention über Tiefe statt fester Einheits-Residuals, phi = exp(w^T RMSNorm(v)), w null-initialisiert, Block-Variante O(Nd).


PDFs:
- `docs/research/ki-papers/pdf/TurboQuant_2504.19874.pdf`
- `docs/research/ki-papers/pdf/PolarQuant_2502.02617.pdf`
- `docs/research/ki-papers/pdf/QJL_2406.03482.pdf`

Text-Extrakte:
- `docs/research/ki-papers/text/TurboQuant_2504.19874.txt`
- `docs/research/ki-papers/text/PolarQuant_2502.02617.txt`
- `docs/research/ki-papers/text/QJL_2406.03482.txt`

## Ideen / Zukunfts-Kandidaten

- `docs/research/ki-papers/analysis/turboquant_kv_quant_moo_idee.md` — wissenschaftliche Analyse der TurboQuant/PolarQuant/QJL-Linie (aus den PDFs, nicht nur dem Blog) + codebelegte moo-Anknüpfung (KV-Cache im Generierungs-Loop + rotationsbasierte, kalibrierungsfreie Quant für `moo_tensor`). Status: Idee, nicht geplant.
