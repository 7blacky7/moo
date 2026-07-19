# PAPER-VERIFY-04: TurboQuant / PolarQuant / QJL — Verify-Matrix

Stand: 2026-07-19. Muster wie paper_verify_01/02/03. Quellen: text/QJL_2406.03482.txt,
text/PolarQuant_2502.02617.txt, text/TurboQuant_2504.19874.txt (gezielte Fundstellen mit
Zeilennummern, keine PDFs geladen). Basis-Analyse: turboquant_kv_quant_moo_idee.md (Opus,
2026-07-19). Interpretation vs. Paper-Fakt ist je Zeile markiert. Ergebnis unten:
Gesamt-Entscheid + verbindliche Korrekturen an der Opus-Analyse.

## QJL — 1-Bit-Sign-JL (Baustein fuer KI-Q1)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| QJL = JL-Transform + Sign-Bit-Quantisierung | QJL:27, 260-261 | "JL transform followed by sign-bit quantization"; "comprising a JL transformation followed by quantization to a single sign bit" | PAPER-FAKT. Kernmechanik fuer den Sign-JL-Kernel in KI-Q1. | keins | GO |
| Unbiased Inner-Product-Estimator, asymmetrisch (Key quantisiert, Query nicht) | QJL:177-183, 262-263, 284-285 | "unbiased and low-distortion"; "applying the same JL transform to the query embedding WITHOUT quantization, we still obtain an unbiased estimator"; "asymmetric" | PAPER-FAKT. WICHTIG fuers Design: Kernel-Paar noetig — Q_qjl(key) + Estimator(query_jl, key_bits). Unbiasedness-Gate numerisch pruefbar (Erwartungswert ueber viele Seeds). | keins | GO |
| Zero Overhead: keine Zero-Point/Scale-Speicherung pro Block | QJL:191-193 | "quantize vectors with zero overhead because it does not require grouping the data" + klassisch "+storing quantization constants (zeros and scales) per group" | PAPER-FAKT. Der eigentliche Hebel gegen den klassischen +1..2-Bit-Overhead. | keins | GO |
| Data-oblivious → online-faehig, kalibrierungsfrei | QJL:158, 193 | "efficient, data-oblivious"; "this is a data-oblivious" | PAPER-FAKT. Passt exakt zum moo-Gate-Muster: fester Seed, deterministisch, kein Kalibrier-Datensatz. | keins | GO |
| 3-Bit KV-Cache ohne Accuracy-Verlust, >5x Speicherreduktion | QJL:34-35, 207-209 | "quantize the KV cache to only 3 bits ... more than fivefold reduction ... without compromising accuracy" | Paper-Zahl fuer grosse LLMs. Fuer moo-Toy NICHT als Zielwert uebernehmen — eigene Fehlerschranken-Gates definieren. | Zahlen-Uebertragung auf Toy waere unserioes | GO (Zahl nur als Paper-Referenz) |
| JL-Matrix = random GAUSSIAN Projektion | QJL:177, 257-258, 267 | "random Gaussian projection"; "multiplying by a random Gaussian matrix"; Def 3.1 S in R^{m x d} | **ABWEICHUNG zur Opus-Bausteintabelle:** QJL selbst nutzt Gauss, NICHT Hadamard. Hadamard-als-Rotation ist separat belegt (siehe PolarQuant-Zeile unten) — fuer den Sign-JL-Kernel selbst gilt: Gauss-Matrix mit festem Seed ist die papertreue Form. | Verwechslung Gauss-JL vs. Hadamard-Rotation in Doku/Gates | GO mit Praezisierung |

## PolarQuant — Winkelquantisierung (Baustein fuer KI-Q2, Variante A)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Random Preconditioning + Polar-Transform, Winkel werden quantisiert | PolarQuant:28-29, 94 | "random preconditioning and polar transformation ... quantizes resulting angles"; "quantizing KV vectors in polar coordinates" | PAPER-FAKT. | keins | GO |
| Winkel nach Preconditioning: eng begrenzt, konzentriert, analytisch berechenbar → Normalisierung entfaellt | PolarQuant:29-33, 98-99, 256-269 | "tightly bounded and highly concentrated distribution with an analytically computable form. This nice distribution eliminates the need for explicit normalization"; Lemma mit PDF von theta = tan^-1(y/x) | PAPER-FAKT. Codebuch fuer Winkel ist offline aus der analytischen Verteilung berechenbar — passt zum Lookup-Table-Ansatz. | keins | GO |
| Effizienter REKURSIVER Polar-Algorithmus | PolarQuant:102-103, 215-217 | "computationally efficient recursive polar transformation" (Definition 1, Figure 1) | PAPER-FAKT. Fuer Implementierung: Definition 1 im PDF gezielt nachlesen (Rekursionsformel steht im Extrakt nur als Verweis). | Implementierungsdetail nicht vollstaendig im Text-Extrakt | GO, Detail-Read bei Bau |
| Hadamard als Preconditioner ist etablierte Praxis | PolarQuant:85-86 | "random Hadamard matrices as preconditioners before quantizing embedding vectors in attention layers to improve quality [33, 4]" | **DER Beleg fuer die Hadamard-Abbildung in der Opus-Tabelle** — Hadamard ist paper-gestuetzte Preconditioner-Wahl (QuaRot/QuIP#-Linie), auch wenn QJL/TurboQuant formal Gauss/allgemeine Rotation nutzen. Fuer moo: Walsh-Hadamard-Op mit festem Seed als Rotations-Primitiv ist legitim. | keins | GO |
| >x4.2 KV-Kompression bei besten Qualitaetswerten (Long-Context) | PolarQuant:33-34, 105-106 | "compresses the KV cache by over x4.2 while achieving the best quality scores" | Paper-Zahl grosse LLMs; wie oben nicht als Toy-Ziel. | — | GO (Referenz) |

## TurboQuant — Zwei-Stufen-VQ (Bausteine fuer KI-Q2 Variante B / KI-Q3)

| Claim | Quelle | Evidenz | moo-Implikation | Risiko | Go/No-Go |
|---|---|---|---|---|---|
| Zufallsrotation induziert Beta-Verteilung pro Koordinate | TurboQuant:28, 94-95, 201-202, 324-330, 574-577 | "randomly rotating input vectors, inducing a concentrated Beta distribution"; Lemma 1: Koordinate von Pi*x folgt (skalierter) Beta-Verteilung | PAPER-FAKT. Basis fuer datenunabhaengiges Codebuch. | keins | GO |
| MSE-optimaler Quantizer via kontinuierlichem k-means (Lloyd-Max), Codebuecher VORAB berechnet | TurboQuant:95-96, 209-210 | "optimal Lloyd-Max quantizer ... by solving a continuous k-means problem"; "We PRECOMPUTE AND STORE these optimal codebooks for a range of practically useful bit-widths" | PAPER-FAKT, bestaetigt exakt den KI-Q3-Ansatz: offline Tabelle, Runtime nur Lookup. | keins | GO |
| Zwei-Stufen fuer inner product: Qmse (Budget-1 Bit) + QJL auf Residual → unbiased | TurboQuant:31-32, 87-89, 97-99, 235-236, 431-433 | "two-stage approach: applying an MSE quantizer followed by a 1-bit QJL transform on the residual, resulting in an unbiased inner product quantizer" | PAPER-FAKT. Kompositionsprinzip: KI-Q1-Kernel (Sign-JL) ist wiederverwendbarer Baustein der Stufe 2 — bestaetigt die Task-Reihenfolge Q1 vor Q2/Q3. | keins | GO |
| Informationstheoretische untere Schranke bewiesen; TurboQuant bis auf kleinen konstanten Faktor optimal (Faktor <= 3/(2pi) approx 2.7 bei MSE); exponentielle Verbesserung in Bit-Breiten-Abhaengigkeit | TurboQuant:33-34, 101, 278-280, 296-298, 371, 564-566 | "information-theoretic lower bounds ... TurboQuant closely matches these bounds"; "provably within a factor of at most 3/(2pi) approx 2.7"; "an exponential improvement over existing methods in terms of bit-width dependence"; Shannon lower bound + Yao minimax | PAPER-FAKT (Theorie). Fuer moo nur Doku-Kontext, kein Gate-Kriterium. | keins | Notiz |
| Data-oblivious/online: kein Training, keine Kalibrierung | TurboQuant:26, 166 | "data-oblivious algorithms, suitable for online applications"; "Online (data-oblivious) quantization methods apply instantly" | PAPER-FAKT. | keins | GO |
| Experimente: EINE A100; NIAH + LongBench(-E); Llama-3.1-8B + Ministral-7B; Baselines PolarQuant/SnapKV/PyramidKV/KIVI; quantisiert auch generierte Tokens (anders als KIVI/PolarQuant) | TurboQuant:1066, 1219-1221, 1338-1346, 1411-1421 | "All experiments are performed using a single NVIDIA A100 GPU"; NIAH-Kapitel; Baseline-Liste; "applies quantization even during the streaming generation process" | PAPER-FAKT. **Bestaetigt die Opus-Korrektur: Blog-Zahlen (H100, 8x Speedup) stehen NICHT im Paper** — grep ueber den Extrakt: kein H100-Treffer. In moo-Doku ausschliesslich Paper-Setup zitieren. | Blog-Zahlen-Kontamination | GO |

## moo-Ist-Stand (codebelegt, 2026-07-19 — Korrekturen an der Opus-Analyse)

| Aussage | Status | Beleg |
|---|---|---|
| KV-Cache-Infrastruktur existiert in der Runtime | **BESTAND** (Opus-Analyse sagte "fehlt" — FALSCH) | compiler/runtime/moo_nn.c: cache_maske Z.925-939, cache_kv_lesen/schreiben Z.1024-1037, fw_attention_cache Z.1056-1186, Dispatch in fw_attention Z.1222-1233 (opt-in att["cache"]=1, nur autograd_aus, wirft bei autograd_an), cache_leeren_schicht/moo_nn_cache_leeren Z.1945-1979. Bindings: runtime_bindings.rs:282,1117; Codegen-Methoden "cache_leeren"/"clear_cache": codegen.rs:2984-2986. KI-M2 done. |
| generiere() in den LM-Beispielen nutzt den Cache | **FEHLT** (Opus im Ergebnis richtig, Begruendung falsch) | Lexikalischer Volltreffer-Scan: kein "cache" in beispiele/ki_sprachmodell.moos, beispiele/eval_m6b_mini_lm.moos, kip/*.moos; projektweit setzt kein .moos-Beispiel att["cache"]=1, kein Beispiel ruft cache_leeren. generiere() rechnet volle Attention pro Schritt. → KI-Q2-Vorstufe (Cache im LM-Beispiel verdrahten + Bit-Identitaets-Gate cache vs. non-cache) ist REAL noetig. |
| int8-Quant ist EVAL-only / Produkt-NO-GO | BESTAND | Task KI-M6b (done): eval_m6b_mini_lm.moos. KI-Q1-Auftrag: Re-Eval gegen dieselbe Messlatte NACH Incoherence-Processing. |
| Hadamard-/Rotations-Op existiert bereits im Tensor-Stack | **FEHLT** | code_intel functions name %hadamard%: 0 Treffer projektweit. Neuer Registry-Op noetig → B2-Vertrag (backward + Gradcheck-Zeile) gilt. |

## Luecken (quellenehrlich)

| Thema | Status |
|---|---|
| PolarQuant Definition 1 (exakte Rekursionsformel) | Im Text-Extrakt nur als Verweis (Z.215-217) — bei KI-Q2-Bau Variante A das PDF-Kapitel gezielt lesen. |
| TurboQuant Codebuch-Werte | Paper beschreibt Verfahren (Z.209-210), liefert die Tabellen nicht im Extrakt — Codebuch fuer KI-Q3 selbst via Lloyd-Max auf Beta-Verteilung berechnen (deterministisch reproduzierbar, gut gate-bar). |
| Blog "6x KV / 8x Speedup H100 / 3-Bit lossless" | KEIN Beleg im Paper-Extrakt (grep H100: 0 Treffer). In moo-Doku tabu. |

## Gesamt-Entscheid

**KI-Q1: GO — mit zwei verbindlichen Praezisierungen aus diesem Verify:**
1. Der Sign-JL-Kernel ist papertreu ein GAUSS-JL (QJL Def 3.1), asymmetrisch (Key-Bits + un-quantisierte JL-Query) — der Estimator gehoert zwingend mit zum Task, sonst ist Unbiasedness nicht gate-bar.
2. Walsh-Hadamard ist als separates Rotations-/Preconditioner-Primitiv legitim (PolarQuant:85-86, QuaRot-Linie) — als EIGENER Op neben dem Gauss-JL fuehren, nicht vermischen.

**KI-Q2: GO** — Vorstufe bestaetigt noetig (Cache-Verdrahtung im LM-Beispiel + Bit-Identitaets-Gate), danach Quant-Storage-Option. Verfahrens-Entscheid A (Polar) vs. B (TurboQuant zweistufig) erst nach Q1-Erfahrung; B hat den Vorteil, dass Stufe 2 = Q1-Kernel.

**KI-Q3: GO als Folge-Option** — Codebuch-Ansatz exakt paper-bestaetigt (precompute+store), Start nur nach explizitem Moo-Go (Task-Vermerk).

Alle drei ERWEITERN den Stack (neue Ops + Storage-Optionen), ersetzen nichts; MNIST-/LM-Basisgates muessen bit-/byte-identisch bleiben (M-Task-Arbeitsregel gilt fort).

---

## KI-Q1 UMGESETZT + Re-Eval-Messung (2026-07-19)

**Primitiven im Stack:** tensor.hadamard(seed) + tensor.hadamard_inv(seed) (exakte
Inverse A^T, beide mit Backward, Gradcheck 680/33 auto), sign_jl / jl_projektion /
sign_jl_skalarprodukt (QJL, quant_nur_inferenz-Guard). Harness test_quant_asan.c:
222 Checks (H1-H6, Q1-Q4) ASan+LSan+UBSan gruen; Unbiasedness rel=0.0041/200 Seeds.

**Re-Eval gegen die M6b-Messlatte** (beispiele/eval_q1_hadamard_mini_lm.moos,
identisches Grimm-Training, fester Zwei-Block-Eval, per-output-channel absmax,
Rotation je output-Kanal ueber Hadamard Seed 4242, Basis-Loss 1.84059):

| Variante        | Delta Loss   |
|-----------------|--------------|
| int8 pc         | +0.00060     |
| int8 pc + rot   | +0.00009     |
| int4 pc         | +0.02547     |
| int4 pc + rot   | **+0.06153** |

**Befund (Toy-Scale, ehrlich):** (1) int8 per-output-channel ist auf dieser
Messlatte bereits praktisch verlustfrei — die Rotation gewinnt dort nur im
Rauschen. (2) Bei int4 macht die Rotation es SCHLECHTER: per-channel-Skalen
fangen Kanal-Ausreisser schon ab, die Rotation gaussifiziert die Verteilung
(mehr Masse nahe 0 relativ zum absmax) und erhoeht bei 15 Stufen den mittleren
Rundungsfehler. Incoherence-Processing-Revier bleibt: per-TENSOR-Skalen,
Aktivierungs-/KV-Cache-Ausreisser (QJL, KI-Q2) — NICHT feinkoerniges
Weight-only-Quant. Offene Zusatzmessung (optional): int4 per-TENSOR plain vs.
rot wuerde die Erklaerung direkt testen.

---

## KI-Q2 UMGESETZT + Messung (2026-07-19)

**Verfahrens-Entscheid: B (TurboQuant-Stufe-2)** — Stufe 2 nutzt die
KI-Q1-Hadamard-Kerne direkt; Polar haette einen neuen Winkel-Kernel gebraucht,
und die Q1-Messung hat das Rotations-Revier (Aktivierungen/Cache) bestaetigt.

**Runtime:** fw_attention_cache quantisiert bei att["cache_quant"]=8|4 NUR die
neuen K/V-Zeilen vor dem Append (nach der RoPE-Rotation): je Token-Zeile
Hadamard-A (Seed att["cache_quant_seed"], Default 4242), absmax-Quant auf
2^(bits-1)-1 Stufen, exakte A^T-Ruecktransformation; der Cache haelt
dequantisierte f32-Werte (Storage-SIMULATION wie M6b/Q1 — Fehler real, Format
unveraendert). Kopf-Dim muss Zweierpotenz sein (Negativ-Gate). Non-Quant-Pfad
bit-identisch (Gate). test_nn_asan Sektion 18c: 161 Checks ASan+LSan+UBSan.

**Vorstufe bewiesen** (beispiele/eval_q2_kv_cache_lm.moos, RoPE-Mini-LM dh=16,
generiere() auf Prefill+Token-fuer-Token-Decode verdrahtet): f32-Cache-Decode
== Ohne-Cache-Lauf mit max|Delta-Logits| = 0 (BIT-identisch), 20/20 Token.

**Quant-Messung** (gleicher Lauf, T=27):

| Cache     | Token==f32 | max|dLogits| | Bytes (rechnerisch) |
|-----------|-----------|--------------|---------------------|
| f32       | 20/20     | 0            | 13824               |
| int8+rot  | 20/20     | 0.0052       | 4320 (3.2x)         |
| int4+rot  | 7/20      | 0.133        | 2592 (5.3x)         |

**Befund:** int8-Quant-Cache ist auf der Toy-Messlatte praktisch gratis
(Token-treu, Logits-Delta ~0.5% der Logit-Skala). int4 degradiert deutlich —
konsistent mit dem Q1-int4-Befund (15 Stufen); greedy divergiert nach dem
ersten Token-Flip. Kandidat fuer int4-Rettung: Lloyd-Max-Codebuch (KI-Q3),
das die gaussifizierte Nach-Rotations-Verteilung direkt adressiert.
Paper-Zahlen (>5x QJL, >4.2x PolarQuant auf A100/LLM) bleiben separat.

---

## KI-Q3 UMGESETZT + Messung (2026-07-19)

**Offline-Schritt** (beispiele/eval_q3_lloyd_max_codebuch.moos, deterministisch
LCG+Irwin-Hall, 20000 Samples in 16er-Zeilen): Lloyd-Max-16-Stufen-Codebuch
auf ABSMAX-NORMALISIERTEN Zeilen (TurboQuants Beta-Konstruktion). Erster
Versuch mit festem N(0,1)-Codebuch + RMS-Zeilenskala VERLOR gegen
zeilenadaptives uniform (MSE 0.0076 vs 0.0065) — Befund: 16er-Block-
Adaptivitaet dominiert die globale Form; erst im selben adaptiven Suchraum
gewinnt Lloyd-Max: **MSE 0.00514 vs uniform 0.00648 = 21% weniger** (das
uniforme q/7-Gitter ist dort ein Spezialfall, Lloyd-Max beweisbar optimal).
Codebuch (symmetrisch): +-{0.0560, 0.1679, 0.2761, 0.3952, 0.5246, 0.6612,
0.8046, 0.9815}.

**Runtime-Lookup**: att["cache_quant_codebuch"] = Liste (2..2^bits Werte)
schaltet cache_quant von uniform auf nearest-Codebuch (v/absmax -> nearest ->
*absmax; gleiche Speichergeometrie: Index + Skala je Zeile). Gates 18c
c2/c3 (Lookup laeuft; Codebuch > 2^bits wirft): test_nn_asan **163 Checks**
ASan+LSan+UBSan.

**Toy-LM-Messung** (eval_q2 Reihe E, nach Dict-Fix wiederholt — C/D-Zahlen
unveraendert bestaetigt): int4+Codebuch **7/20 Token wie int4-uniform**,
identischer Token-String — der 21%-MSE-Gewinn rettet den fruehen
greedy-Flip auf dieser Messlatte NICHT (End-Logits-Delta 0.173 ist
divergenz-dominiert). EHRLICH: fuer Token-Treue braeuchte int4 hier eher
feinere Gruppen/gemischte Praezision; das Codebuch bleibt als Baustein fuer
Vektor-Suche/RAG (TurboQuant-Zielpfad) und groessere dh sinnvoll.

**BEIFANG — Kern-Runtime-Bug gefunden & gefixt**: moo_dict_remove hatte
KEINE Tombstones; jedes Remove brach die lineare Probing-Kette (Keys hinter
dem entfernten Slot unauffindbar bis zur zufaelligen Wiederbelegung).
Sichtbar geworden, weil cache_leeren (dict_remove der cache_k/v-Slots) den
frisch gesetzten cache_quant_codebuch-Key fuer den naechsten fw-dget
unsichtbar machte. Fix in moo_dict.c/moo_runtime.h: deleted-Tombstone +
tombs-Zaehler, find_slot laeuft probes-begrenzt ueber Tombstones (Reuse beim
Insert), Grow-Trigger zaehlt count+tombs. Betraf potenziell JEDEN
remove-Nutzer (cache_leeren, pack_maske, ...).
