# PAPER-VERIFY-05: Attention Residuals (AttnRes) — Verify-Matrix + Erstanalyse

Stand: 2026-07-19. Quelle: text/AttnRes_2603.15031.txt (11.604 Woerter, pdftotext aus
pdf/AttnRes_2603.15031.pdf), Kimi Team / MoonshotAI, arXiv 2603.15031 (cs.CL, 2026).
Repo: github.com/MoonshotAI/Attention-Residuals (via ki-browser gesichtet 2026-07-19).
Muster wie paper_verify_01..04. ERWEITERT den Stack (opt-in Residual-Modus), ersetzt nichts.

## Kernmechanik (Full AttnRes)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Standard-PreNorm-Residuals akkumulieren mit festen Einheitsgewichten; Magnituden wachsen O(L), Beitraege verduennen | Z.16, 80, 144 | "hidden-state magnitudes to grow as O(L) with depth, progressively diluting each layer's relative contribution"; Rekurrenz-Expansion | Exakt unser Ist-Zustand: eadd-Residuals in moo_nn-Blockverdrahtung und ki_gpu_v3c.c (Baseline (a) der Paper-Figur). | keins | Befund |
| AttnRes: h_l = SUM alpha_{i->l} * v_i, alpha = Softmax ueber Tiefe | Z.89, 218-220 (Gl. 4) | "h_l = SUM alpha_i->l * v_i, where alpha are softmax attention weights computed from a single learned pseudo-query" | Reine Op-Komposition: matmul + softmax (letzte Achse) + gewichtete Summe — alles vorhandene Registry-Ops, B2-Gradcheck deckt automatisch. | keins | GO |
| Kernel phi(q,k) = exp(q^T RMSNorm(k)); RMSNorm NUR auf Keys, Values roh | Z.200-216 (Gl. 2-3) | "we adopt phi(q,k) = exp(q^T RMSNorm(k)) with normalization"; "The RMSNorm inside phi prevents layers with large-magnitude outputs from dominating" | rmsnorm_kern existiert (KIP-G4d) — Key-Normierung ist eine Zeile. WICHTIG: Values NICHT normieren. | Verwechslung Key- vs. Value-Normierung | GO |
| q_l = w_l ist EIN gelernter Vektor in R^d pro (Sub-)Layer, vom Forward ENTKOPPELT | Z.204-216 (Gl. 3), 232-234 | "the query q_l = w_l is a layer-specific learnable vector"; "learned parameter decoupled from the layer's forward computation" | Ein [d]-Parameter pro Sub-Layer-Position, requires_grad, Teil von parameter(). Entkopplung = Blockbatching moeglich. | keins | GO |
| v_0 = h_1 (Token-Embedding immer als Quelle dabei) | Z.204-210 (Gl. 3), Z.311-313 | "k_i = v_i = { h1 wenn i=0; f_i(h_i) sonst }"; "b_0 = h_1 so that the token embedding is always included" | Embedding-Ausgang als erste Block-Repraesentation fuehren. | leicht zu vergessen | GO |
| Full: O(L^2 d) Arithmetik, O(Ld) Speicher; im Vanilla-Training KEIN Mehr-Speicher (ueberlappt mit Backprop-Aktivierungen) | Z.222-230 | "requires O(L2d) arithmetic and O(Ld) memory"; "overlaps entirely with the activations already retained for backpropagation" | Fuer moo-Toy (L klein) ist sogar Full AttnRes billig; Block-Variante trotzdem bauen (papertreue Ziel-Form + partial-Reset-Mechanik). | keins | GO |

## Block AttnRes (die Bau-Variante)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| N Bloecke; intra-block Standard-Summe b_n = SUM_{j in B_n} f_j(h_j); inter-block Attention nur ueber [b_0..b_{n-1}, b_n^partial]; O(Nd) | Z.244-260 (Gl. 5), 305-318 (Gl. 6) | "within each block, the layer outputs are reduced to a single representation via summation"; V-Definition mit Partial-Sum fuer i>=2 | Zustand pro Forward: Liste bloecke[] + partial. Erste Schicht eines Blocks sieht nur fertige Bloecke, spaetere zusaetzlich den Partial. | Zustandsfuehrung im Netz-Dict (wie KV-Cache-Muster) | GO |
| Referenz-Pseudocode: AttnRes PRO SUB-LAYER (vor Attn UND vor MLP), je EIGENE proj (w) + RMSNorm; block_size zaehlt ATTN+MLP (Layer = 2) | Z.266-303 (Figure 2) | Zeilen 17-36 des Pseudocodes: attn_res_proj/mlp_res_proj getrennt; "block_size counts ATTN + MLP; each transformer layer has 2" | Sub-Layer-Granularitaet: 2 Pseudo-Queries pro Transformer-Layer. Fuer moo-Bau Stufe 1 zulaessige Vereinfachung: EIN AttnRes pro Layer-Eingang (dokumentierte Abweichung); papertreue Sub-Layer-Form als Ausbaustufe. | Abweichung sauber dokumentieren | GO |
| w null-initialisiert -> initiale alpha UNIFORM ueber Quellen | Z.536-538 | "initialized to zero. This ensures that the initial attention weights are uniform across source layers" | KRITISCH fuers Gate: Init ist MITTEL (1/(n+1) je Quelle), NICHT die Standard-Residual-SUMME. AttnRes-Netz ist ab Schritt 0 eine andere Funktion als Baseline — Bit-Identitaets-Gate gilt nur fuer attnres=AUS. | Falsches "identisch bei Init"-Gate waere unehrlich | GO mit Praezisierung |
| Ergebnisse: Block AttnRes (N=8) = Baseline mit 1.25x Compute; GPQA +7.5, HumanEval +3.1; Setup Kimi Linear 48B/3B activated, 54 Layer, 1.4T Tokens, MoE 8/256+1 | Z.560, 589, 603-605, 613-617, 27, 103-104 | "Block AttnRes reaches 1.692 versus the Baseline's 1.714, equivalent to a 1.25x compute advantage"; Konfigurationsdetails | Paper-Zahlen von 48B-Scale — KEIN Zielwert fuer moo-Toy (gleiche Regel wie VERIFY-04). Eigenes Gate: Toy-LM-Loss attnres an vs. aus bei gleichem Budget, dokumentiert, ohne Zahlenversprechen. | Scale-Transfer unbelegt | GO (Referenz) |
| Verwandt/abgegrenzt: mHC(-lite) als Vergleich in Tabelle 2 | Z.560 | "Baseline vs Block AttnRes (N=8) vs Full AttnRes vs mHC(-lite)" | Hyper-Connections-Familie; fuer moo nicht Scope. | — | Notiz |

## moo-Ist-Stand (codebelegt)

| Aussage | Status | Beleg |
|---|---|---|
| Residuals im Stack sind Standard-eadd mit Einheitsgewicht | BESTAND (= Paper-Baseline) | ki_gpu_v3c.c Z.327/335 (eadd Stream), gleiches Muster in moo_nn-Blockverdrahtung; kein Treffer fuer Tiefen-Attention/Pseudo-Query projektweit (code_intel, lexikalisch+semantisch, 2026-07-19) |
| Alle benoetigten Primitiven vorhanden | BESTAND | rmsnorm_kern (KIP-G4d), matmul, softmax (letzte Achse), concat, mul/adds — AttnRes ist reine Komposition, kein neuer Registry-Op noetig |
| Zustandsfuehrung-Muster vorhanden | BESTAND | KV-Cache (KI-M2c): opt-in Flag + Zustand im Netz-Dict + leeren()-Vertrag — identisches Muster fuer bloecke[]/partial |

## Gesamt-Entscheid

**GO fuer KI-R1 — UMGESETZT (2026-07-19):** schicht_attnres(dim) + attnres(schicht, quellen)
in moo_nn.c (Registry-Eintrag, Save/Load-Hooks, Bindings, Codegen DE/EN), Harness
tests/test_attnres_asan.c (16 Checks R1-R6, ASan+LSan+UBSan gruen), test_nn 157 gruen.
**(d)-Gate-Messung** (beispiele/eval_r1_attnres_lm.moos, deterministisch, identische
Seeds/Budget, 600 Schritte, synthetische t-3-Abhaengigkeit): Fenster-Loss
Standard 1.72875 -> 0.564507, Block-AttnRes 1.68285 -> 0.472046. Toy-Scale-Befund,
richtungskonsistent mit dem Paper, KEIN Beleg fuer die 48B-Zahlen.
Verbindliche Bau-Vorgaben aus diesem Verify:
1. phi = exp(w^T RMSNorm(v)) — Norm NUR auf Keys; Softmax ueber Quellen-Achse; Values roh (Gl. 2-4).
2. b_0 = Embedding-Ausgang; Partial-Reset an Blockgrenzen exakt nach Figure-2-Semantik (Gl. 5-6).
3. w-Parameter null-initialisiert (uniforme Start-alpha, Z.538); requires_grad; in parameter() enthalten.
4. Stufe 1 darf EIN AttnRes pro Layer-Eingang sein (statt pro Sub-Layer) — als dokumentierte Abweichung; Sub-Layer-Form als Ausbaustufe.
5. Gates: (a) attnres=AUS bit-/byte-identisch zu heute (Basisgates unberuehrt); (b) Gradcheck via Komposition automatisch gruen; (c) Determinismus geseedet; (d) Toy-LM-Vergleichslauf an/aus dokumentiert ohne Scale-Zahlenversprechen; (e) alpha-Summe==1 numerisch.
