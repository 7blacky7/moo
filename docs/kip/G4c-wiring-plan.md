
# KIP-G4c — Production-Wiring: moo_nn-Pfade auf residente GPU-Ops (STRIKT-Modus)

**Autor:** kip-gpu · **Task:** ab09b47c · **Stand:** 2026-07-10 (Phase 1, Prework)
**Status:** Design — Phase 1 (Discovery + Plan). Phase 2 (Shared-File-Umsetzung) startet erst
nach FREI von kip-ops ([KIP-B4b], Shared Files aktuell BELEGT) UND nach Vorliegen von
[KIP-G4c-PREFLIGHT] (kip-kern, MooTensor-/Ownership-Vertrag).
**Depends:** KIP-G4 (16cbc7d, PoC), KIP-G4b (e35436b+5879a6b, strided MHA/GQA/RoPE-Kernel),
KIP-G4c-PREFLIGHT (9d808cf8, offen), KIP-G4c-QA (8972d152, offen, kip-daten).

---

## 1. Ist-Zustand (verifiziert via code_intel, 2026-07-10)

### 1.1 Zwei GPU-Schichten existieren bereits, beide unvollstaendig fuer Produktion

- **GPU2 (non-resident, `moo_ki_gpu.c` Zeile 1-20 Kommentarkopf):** hookt bereits
  `moo_ki_gpu_matmul/_ew/_reduce_sum` DIREKT in `moo_tensor_ops.c` (z.B.
  `moo_tensor_matmul` Zeile ~226: `if (!moo_ki_gpu_matmul(...)) matmul_ikj(...)`).
  ABER: Upload/Download PRO EINZELCALL (kein Puffer bleibt auf der GPU), Groessen-Schwelle
  (matmul >= 2^24, ew/reduce >= 2^20), ENV `MOO_KI_GPU=0/1`, `MOO_KI_GPU_ERZWINGEN=1`.
  Scope-Kommentar im File: *"KEIN Autograd/NN-Umbau — die Hooks sitzen nur im
  Forward-Compute von moo_tensor_ops.c und fallen bei JEDEM Fehler transparent auf den
  CPU-Pfad zurueck."* Das ist die einzige GPU-Beteiligung, die heute im Produktivpfad
  (`moo_nn_trainiere`) ueberhaupt aktiv sein kann — und sie ist NICHT resident, NICHT
  STRIKT-faehig (Fallback ist by-design lautlos) und deckt nur matmul/ew/reduce ab
  (kein softmax/norm/rope/gather/optimizer).
- **G1-G4b (resident, `moo_ki_gpu_api.h`/`moo_ki_gpu.c` `_res`-Suffix):** vollstaendige
  residente Op-Bibliothek (31 Funktionen, siehe §3), differentiell gegen CPU verifiziert,
  aber NUR in Standalone-Test-Harnessen verdrahtet
  (`tests/ki_gpu_g4_lm.c`, `tests/ki_gpu_g4b_lm.c`, `tests/ki_gpu_coverage.c`) — von Hand
  gebaute, hartkodierte Transformer-Forward/Backward-Ketten, die NICHT ueber
  `moo_nn.c`/`moo_tensor_ops.c`/`moo_autograd.c` laufen. `moo_nn_vorwaerts`/
  `moo_nn_trainiere` rufen diese `_res`-Ops aktuell **gar nicht** auf.

### 1.2 `MOO_KI_GPU_STRIKT` ist heute nur ein Name, keine Implementierung

Volltextsuche ueber das gesamte Repo: `MOO_KI_GPU_STRIKT` kommt EIN einziges Mal vor —
als Env-Var-Aufruf in `skripte/kip_gpu_coverage.sh`
(`MOO_KI_GPU_ERZWINGEN=1 MOO_KI_GPU_STRIKT=1 /tmp/ki_gpu_coverage`), gelesen von
`tests/ki_gpu_coverage.c` (eigener Test-Binary, nicht Teil der Runtime). Es gibt KEINE
Stelle in `moo_nn.c`/`moo_tensor_ops.c`/`moo_autograd.c`/`moo_ki_gpu.c`, die diese Variable
kennt. **G4c muss den STRIKT-Vertrag erst schaffen**, nicht nur verdrahten.

### 1.3 Architektur-Fakt: moo_nn.c ist reine Komposition — Hebel liegt in moo_tensor_ops.c

`moo_runtime.h` (Kommentar ueber der NN-API): *"Vorwaerts/Loss sind reine KOMPOSITIONEN
der Registry-Ops (das B2-Gradcheck-Gate deckt damit jeden Gradienten)."* D.h.
`fw_dicht`/`fw_rmsnorm`/`fw_attention`/`fw_ffn_gated`/`fw_embedding`/`fw_moe`/`fw_position`
in `moo_nn.c` rufen ausschliesslich `moo_tensor_*`-Funktionen aus `moo_tensor_ops.c` auf
(matmul, add, mul, transpose, softmax, ...). Backward laeuft NICHT in `moo_nn.c`, sondern
im Autograd-Tape (`moo_autograd.c`): jede `moo_tensor_*`-Op registriert bei Aufruf via
`moo_ag_record` einen Tape-Node mit ihrer `bw`-Funktion (Registry via
`moo_tensor_op_set_bw`). Backward-Funktionen selbst sind wiederum oft Kompositionen
derselben Registry-Ops (z.B. matmul-Backward ruft intern erneut `moo_tensor_matmul` +
`moo_tensor_transpose` auf — nicht verifiziert im Detail, aber aus dem G3d-e-Pattern
(GPU-seitige Kette identisch: `matmul_bw_res` = Komposition aus `transpose_res` +
`matmul_res`) sehr wahrscheinlich strukturgleich).

**Konsequenz:** Der wirksamste Hebel fuer "moo_nn Forward/Backward auf residente Ops" ist
NICHT `moo_nn.c` selbst umzuschreiben, sondern die Registry-Ops in `moo_tensor_ops.c`
GPU-resident zu machen — dann profitieren Forward (moo_nn.c-Komposition) UND Backward
(Autograd-Komposition derselben Registry-Ops) automatisch, ohne moo_nn.c anzufassen.
`moo_autograd.c` wird nur beruehrt, falls einzelne `bw`-Funktionen NICHT ueber die
Registry laufen, sondern direkte Elementarschleifen sind (muss in Phase 2 pro Op
verifiziert werden — siehe §4 Prüfliste).

### 1.4 Der Residenz-Bruch: MooTensor haelt heute KEINEN GPU-Buffer-Handle

Das ist die zentrale Huerde, die G4c von einem trivialen "if-Umschreiben" trennt: die
`_res`-API arbeitet auf rohen `void*`-Handles (`moo_ki_gpu_buf_belegen/upload/download`),
die der PoC-Harness manuell pro Tensor verwaltet (eigene `hA/hB/...`-Variablen). Damit
`moo_tensor_matmul(av, bv)` productionsreif resident dispatchen kann, muss zur Laufzeit
ermittelbar sein: "hat Tensor A bereits einen gueltigen GPU-Buffer? ist er dirty (CPU-Seite
neuer)?" — das ist exakt die Frage, die [KIP-G4c-PREFLIGHT] (kip-kern) klaeren soll:
MooTensor-Struct-Erweiterung (GPU-Handle-Feld + valid/dirty-Masken) vs. Side-Table
(Hashmap Tensor-Pointer → GPU-Handle, KEIN Struct-Touch). Diese Entscheidung ist
**Blocker fuer Phase 2** und liegt ausserhalb meines Scopes (MooTensor-Struct = nur
kip-kern). Siehe §6 fuer beide Architektur-Optionen.

---

## 2. Symbol-/Callgraph-Inventar (Wiring-Kandidaten)

### 2.1 moo_nn.c — Forward-Kompositionsfunktionen (KEIN direkter Touch erwartet, siehe §1.3)

| Funktion | Zeilen | Komponiert aus (moo_tensor_ops.c) |
|---|---|---|
| `fw_dicht` | 645-667 | matmul, add, Aktivierung (ew/unary) |
| `fw_dropout` | 668-694 | mul (Maske), Skalierung |
| `fw_layernorm` | 695-719 | reduce (mean/var), ew, mul/add (affine) |
| `fw_rmsnorm` | 720-740 | reduce (mean sq), sqrt/div, mul (g) |
| `fw_ffn_gated` | 741-768 | 2x matmul, SiLU/GELU (unary), mul (gate) |
| `fw_embedding` | 769-825 | gather (Vokabular-Lookup) |
| `fw_attention` / `fw_attention_cache` | 1125-1279 / 990-1120 | matmul (QKV, Scores, Ctx), softmax, RoPE (transpose+mul-Komposition CPU-seitig), Maske (add) |
| `fw_position` | 1285-1325 | add/gather (additiv/gelernt) |
| `fw_moe` | 1373-1499 | Top-K-Maske, matmul (Experten), gather/scatter |
| `moo_nn_vorwaerts` | 1525-1571 | Schleife ueber `schicht_vorwaerts` (1503-1522) je Layer |

### 2.2 moo_tensor_ops.c — Registry-Ops mit vorhandenem `_res`-Gegenstueck (Wiring-Ziel)

| CPU-Op (moo_tensor_ops.c) | GPU2-Hook heute (non-resident) | `_res`-Gegenstueck vorhanden |
|---|---|---|
| `moo_tensor_matmul` (210-238) | ja (`moo_ki_gpu_matmul`) | `matmul_res` / `matmul_naiv_res` |
| add/sub/mul/div (ew) | ja (`moo_ki_gpu_ew`) | `ew_res` |
| reduce sum/mean | ja (`moo_ki_gpu_reduce_sum`) | `reduce_sum_res`, `reduce_axis_res` |
| exp/log/sqrt/neg/pow/relu/sigmoid/tanh/gelu (unary) | NEIN | `unary_res` / `unary_bw_res` |
| transpose/reshape/concat | NEIN | `transpose_res` / `copy_res` |
| softmax/log-softmax | NEIN | `softmax_res` / `softmax_bw_res` |
| Cross-Entropy | NEIN | `ce_fwd_res` / `ce_bw_res` |
| LayerNorm/RMSNorm | NEIN | `norm_res` / `norm_bw_res` |
| Achsen-Reduktion/Broadcast/max-bw | NEIN | `reduce_axis_res` / `broadcast_res` / `reduce_max_bw_res` |
| gather/scatter-add (Embedding) | NEIN | `gather_res` / `scatter_add_res` |
| matmul-Backward (dA/dB) | NEIN (nutzt normalen matmul-Hook, falls Komposition) | `matmul_bw_res` |
| RoPE-Rotation | NEIN | `rope_res` (KIP-G4b) |
| Head-Slice (MHA/GQA) | NEIN | `head_slice_res` (KIP-G4b) |
| grad_accum (Fan-out-Summierung im Tape) | NEIN | `grad_accum_res` |
| SGD/Adam/AdamW-Schritt | NEIN (opt_basis/opt_schritt in moo_nn.c, reine CPU-Schleifen) | `opt_sgd_res` / `opt_adam_res` |

**Luecke:** 12 von 13 Op-Kategorien haben HEUTE gar keinen GPU-Pfad im Produktivcode —
nur matmul/ew/reduce_sum ueber den alten non-resident GPU2-Hook. Softmax, Norm, CE, RoPE,
Head-Slice, Gather/Scatter, Optimizer-Schritt laufen in `moo_nn_trainiere` IMMER auf CPU,
unabhaengig von jedem GPU-Flag. Das ist der eigentliche Umfang von G4c.

### 2.3 moo_autograd.c — Tape/Backward-Infrastruktur

- `moo_ag_record(op_name, a, b, skalar, out)` — zeichnet Tape-Node bei jeder Registry-Op.
- `MooAgNode{op (Registry-Eintrag mit ->bw), ...}` (`moo_runtime.h` 237-266).
- `moo_tensor_op_set_bw(name, bw)` — Backward-Registrierung pro Op-Name.
- Rueckwaerts-Traversal ruft `bw`-Funktionen; ob diese selbst durch die Registry
  (und damit transparent durch den GPU-Hook) laufen, oder direkte C-Schleifen sind, ist
  PRO OP zu pruefen (Phase-2-Aufgabe, siehe §4 P1).

### 2.4 moo_nn_easy.c — Produktions-Entry-Point (nicht in Sperrliste, aber shared)

- `moo_nn_trainiere(netz, x, y, optionen)` (202-466): liest Optionen-Dict
  (epochen/rate/batch/optimierer/ausgabe/seed/verlust/momentum), Epochen-Schleife,
  ruft `moo_nn_vorwaerts` → Loss → `moo_nn_grad_clip` → `moo_nn_opt_schritt`.
  **Das ist der Ort fuer STRIKT-Enforcement + Telemetrie-Reset/Read pro Schritt/Epoche**
  (siehe §5). Datei steht NICHT in der expliziten Sperrliste des Tasks, wird aber in
  Phase 2 mit BELEGT/FREI behandelt (Konvention v1 gilt fuer jede geteilte Datei).

---

## 3. Bestehende residente API (moo_ki_gpu_api.h, vollstaendig, 31 Funktionen)

Buffer: `buf_belegen/buf_freigeben/upload/download`.
Compute: `matmul_res/matmul_naiv_res/ew_res/unary_res/unary_bw_res/transpose_res/copy_res/
grad_accum_res/opt_sgd_res/opt_adam_res/reduce_sum_res/softmax_res/softmax_bw_res/
ce_fwd_res/ce_bw_res/norm_res/norm_bw_res/reduce_axis_res/broadcast_res/
reduce_max_bw_res/gather_res/scatter_add_res/matmul_bw_res/rope_res/head_slice_res`.
Telemetrie: `moo_ki_gpu_telemetrie(MooKiGpuTelemetrie*)` /`_reset()` —
`{submits, uploads, downloads, cpu_fallbacks}` (u64, `moo_ki_gpu_api.h` 206-226).

**Deckung:** vollstaendig fuer einen Standard-Transformer-Block (RMSNorm, GQA-Attention
mit RoPE, SwiGLU-FFN, Residual, CE-Loss, Adam/SGD). Kein neuer Shader noetig fuer G4c —
reines Wiring-Problem, kein Op-Luecken-Problem.

---

## 4. Phase-2-Preflight-Checkliste (pro Op, VOR jedem Edit an Shared Files)

1. **P0 — Op-Identifikation:** fuer jede Zeile in §2.2: liest die CPU-Backward-Funktion
   (`moo_autograd.c`) die Registry-Op erneut auf (→ GPU-Hook wirkt transitiv) oder ist es
   eine direkte Elementarschleife (→ braucht eigenen `_res`-Wiring-Punkt in
   `moo_autograd.c`)? Ergebnis dokumentieren bevor Code geschrieben wird.
2. **P1 — Residenz-Grenze pro Trainingsschritt:** ein `moo_nn_trainiere`-Schritt = Forward
   (N Layer) + Loss + Backward (N Layer) + Grad-Clip + Opt-Schritt. Ziel (wie G4/G4b-PoC):
   Parameter+Aktivierungen bleiben GPU-resident ueber den GESAMTEN Schritt, nur
   Input-Batch-Upload am Anfang + Loss-Download am Ende (Telemetrie-Beweis wie
   `kip_gpu_coverage.sh`: uploads/downloads NICHT pro Op, submits == exakte Op-Summe/Schritt).
3. **P2 — Fallback-Transparenz vs. STRIKT:** heutiger GPU2-Hook faellt lautlos auf CPU
   zurueck (`return false` → Aufrufer nimmt CPU-Pfad). Unter `MOO_KI_GPU_STRIKT=1` MUSS
   das zum harten Fehler werden (`moo_throw`), nicht zu `cpu_fallbacks++` (siehe §5).
4. **P3 — CPU-Default unveraendert:** ohne `MOO_HAS_VULKAN`-Build ODER ohne
   `MOO_KI_GPU=1`-Flag muss der komplette Pfad bit-identisch zum heutigen CPU-Code bleiben
   (Gate: bestehende Basisgates unveraendert gruen).

---

## 5. STRIKT-Modus-Vertrag (Entwurf, Phase-2-Implementierung)

```
ENV MOO_KI_GPU_STRIKT=1  (analog MOO_KI_GPU/MOO_KI_GPU_ERZWINGEN, lazy gelesen)
```

- **Semantik:** Wenn `MOO_KI_GPU=1` UND `MOO_KI_GPU_STRIKT=1` gesetzt sind und ein Op im
  `moo_nn_trainiere`-Pfad, der laut §2.2 einen `_res`-Gegenpart hat, NICHT auf der GPU
  ausgefuehrt werden kann (Init-Fehler, OOM, Upload/Dispatch-Fehler, kein Vulkan zur
  Laufzeit trotz `MOO_HAS_VULKAN`-Build) → **`moo_throw` mit klarer Fehlermeldung**
  ("GPU-STRIKT: Op X konnte nicht resident dispatcht werden: <Grund>"), KEIN stiller
  CPU-Fallback. Ohne `MOO_KI_GPU_STRIKT` bleibt das heutige Verhalten (transparenter
  Fallback) erhalten — STRIKT ist additiv, Default AUS.
- **Ohne `MOO_KI_GPU=1`:** STRIKT hat keine Wirkung (kein GPU angefordert = kein
  GPU-Fehler moeglich) — verhindert versehentliches Brechen von reinen CPU-Laeufen.
- **Geltungsbereich:** nur der `moo_nn_trainiere`-Hotpath (Forward/Backward/Opt-Schritt
  innerhalb der Trainingsschleife), NICHT jeder beliebige `tensor_*`-Aufruf ausserhalb
  eines Trainings (Konsistenz mit Task-Beschreibung "STRIKT-Modus im trainiere-Pfad").
- **Kein versteckter Fallback (Gate-Kriterium des Auftrags):** jeder Punkt, an dem der
  Code unter STRIKT auf CPU umschalten koennte, muss stattdessen werfen. Code-Review-Regel
  fuer Phase 2: `grep` nach jedem `if (!moo_ki_gpu_..._res(...))`-Fallback-Zweig im
  Trainingspfad — JEDER braucht einen STRIKT-Guard davor.

---

## 6. Telemetrie moo-sichtbar (Entwurf)

- Neuer Builtin `gpu_statistik()` (Name vorlaeufig, Analogie zu `moo_nn_opt_schritt`),
  liest `moo_ki_gpu_telemetrie()` und gibt ein Moo-Dict zurueck:
  `{"submits": n, "uploads": n, "downloads": n, "cpu_fallbacks": n}`.
- Erfordert Eintrag in `moo_runtime.h` (Deklaration), `moo_nn.c` oder eigene kleine
  C-Funktion (z.B. in `moo_nn_easy.c`, Wrapper um `moo_ki_gpu_telemetrie`), UND
  `codegen.rs` (Builtin-Dispatch-Pattern wie `nn_opt_schritt`, siehe Fund in §Discovery:
  `compiler/src/codegen.rs` NN-Builtins-Block). `codegen.rs` steht in der projektweiten
  Shared-File-Liste (Datei-Markierung-Konvention) — Phase 2 muss dafuer ebenfalls
  BELEGT/FREI setzen (kein anderer Agent aktuell dort gemeldet, siehe Channel-Historie).
- Reset-Punkt: `moo_ki_gpu_telemetrie_reset()` sinnvollerweise zu Beginn jeder
  `moo_nn_trainiere`-Epoche (oder pro Schritt, TBD in Phase 2 je nach Gate-Bedarf) —
  damit `gpu_statistik()` aus Moo-Code heraus den Residenz-Beweis (uploads≈0 im
  Steady-State) unmittelbar nachvollziehbar macht, analog `kip_gpu_coverage.sh`.

---

## 7. Gate-Harness-Design (Phase-2-Deliverables)

Neue Dateien (Phase 1 darf diese bereits vorbereiten/skizzieren, Umsetzung nach Bedarf
in Phase 2 parallel zum Shared-File-Wiring):

1. **`skripte/kip_g4c_wiring_gate.sh`** — orchestriert alle vier Gate-Kriterien aus dem
   Task-Text:
   - (a) **CPU-Default bitidentisch:** bestehende CPU-Basisgates (`run_all.sh`) ohne
     `MOO_HAS_VULKAN` UND mit `MOO_HAS_VULKAN`-Build-aber-`MOO_KI_GPU=0` laufen
     unveraendert gruen — reiner Regressions-Diff gegen aktuellen `run_all`-Stand.
   - (b) **ki_sprachmodell GPU vs CPU Toleranz:** `beispiele/`-Sprachmodell (oder
     kompaktes Trainings-Beispiel analog G4/G4b-Harness) einmal mit `MOO_KI_GPU=0`,
     einmal mit `MOO_KI_GPU=1 MOO_KI_GPU_STRIKT=1` trainieren, Loss-Kurven vergleichen
     (Toleranz analog G4b: float-GPU vs CPU-Referenz ~1e-4..1e-3 je nach Op-Kette,
     ADAM-Drift ueber Schritte beachten — siehe Skill-Pattern "float-GPU vs double-CPU
     ADAM driftet chaotisch").
   - (c) **Vulkan-Smoke:** minimaler End-to-End-Trainingslauf (paar Schritte) via
     `moo_nn_trainiere` mit STRIKT=1 auf echter Hardware (4070 Ti), SKIP ohne libvulkan
     (bestehendes Muster aus `ki-gpu-smoke.sh`).
   - (d) **`kip_gpu_coverage.sh` weiter gruen** — reiner Regressionscheck (Datei nicht
     angefasst, aber Verhalten der `_res`-API darf durch das neue Wiring nicht brechen).
2. **`compiler/runtime/tests/test_g4c_wiring.c`** (oder Erweiterung bestehender Tests) —
   Differentialtest auf Op-Ebene: fuer jede in §2.2 verdrahtete Op prueft ein Aufruf durch
   `moo_nn_trainiere` (nicht nur Standalone-`_res`-Call) denselben Output wie CPU.
3. **Kein-versteckter-Fallback-Beweis:** Testfall der `MOO_KI_GPU_STRIKT=1` auf einer
   Konfiguration erzwingt, die GPU-seitig garantiert scheitert (z.B. `MOO_HAS_VULKAN`
   nicht gebaut ODER kuenstlich invalidiertes Handle) → erwartet `moo_throw`, NICHT
   stillen CPU-Fallback. Negativ-Gate, analog E2c "NEGATIV-GATE" Pattern.

Diese Datei ist NEU und beruehrt keine Shared Files — kann in Phase 1 bereits als Skelett
angelegt werden, sobald das STRIKT-Vertrag-Detail (§5) mit dem Team abgestimmt ist.

---

## 8. Zwei Architektur-Optionen fuer die Residenz-Bruecke (§1.4) — Entscheidung bei kip-kern/Koordinator

**Option A — Side-Table (KEIN MooTensor-Struct-Touch, mein Vorschlag als Minimal-Risk-Pfad):**
Eigene Hashmap/Assoziativ-Struktur in `moo_ki_gpu.c` (oder neuer Datei
`moo_ki_gpu_residency.c`), die `MooTensor*` (Pointer als Key, Lifetime durch normale
Tensor-Refcounts gedeckt) auf GPU-Buffer-Handle + Dirty-Flag abbildet. `moo_tensor_matmul`
&Co. fragen vor jedem Op: "hat A/B einen validen GPU-Buffer? sonst upload, dann resident
weiterrechnen." Vorteil: KEIN Touch an `moo_runtime.h`/MooTensor-Struct, bleibt in meinem
Revier (`moo_ki_gpu.c` + Call-Sites in `moo_tensor_ops.c`). Nachteil: Tensor-Freigabe
(`moo_tensor_free`) muss die Side-Table mitpflegen (Leak-/Dangling-Handle-Risiko) — genau
das ist die Frage, die ich an [KIP-G4c-PREFLIGHT] (kip-kern, Ownership/Refcount-Pruefung)
delegiere, BEVOR ich das in Shared Files umsetze.

**Option B — MooTensor-Struct-Erweiterung** (GPU-Handle + valid/dirty-Feld direkt am
Tensor, wie in G1-Memory als "spaeteres GPU3/Struct-Vorhaben" skizziert): sauberer,
aber Struct-Aenderung ist explizit kip-kern-Revier — nur nach deren Go umsetzbar.

**Meine Empfehlung (vorlaeufig, siehe Team-Abstimmung):** Mit Option A starten (kleinster
Blast-Radius, kein Cross-Team-Blocker), Option B nur falls PREFLIGHT zeigt dass Side-Table
nicht ausreicht (z.B. wegen Refcount-Interaktion mit bestehendem CPU-Dirty-Tracking).

---

## 9. Offene Abhaengigkeiten vor Phase-2-Start

1. **[KIP-B4b] (kip-ops) FREI** fuer `moo_nn.c`/`moo_autograd.c`/`moo_tensor_ops.c`/
   `moo_runtime.h` — aktuell BELEGT (Stand Msg 12799/12804).
2. **[KIP-G4c-PREFLIGHT] (kip-kern)** — Antwort zur Architektur-Frage §1.4/§8
   (Side-Table vs. Struct), No-Go-Liste, Invarianten.
3. **[KIP-G4c-QA] (kip-daten)** — E2b↔G4c-Vertrag (welche Zustaende G4c exponieren muss,
   damit Checkpoint/Restore weiterhin funktioniert — insbesondere Adam t/m/v und
   Dropout-Zaehler MUESSEN unter Residenz weiterhin CPU-lesbar/downloadbar bleiben, sonst
   bricht der bestehende E2/E2b-Checkpoint-Pfad).

Bis (1) vorliegt: keine Aenderung an den vier gesperrten Dateien. Sobald FREI gemeldet
wird, starte ich direkt mit Option A (Side-Table) fuer matmul/ew/reduce zuerst (kleinster
Schritt, baut auf bestehendem GPU2-Hook auf), dann schrittweise softmax/norm/CE/rope/
head_slice/gather/optimizer gemaess §2.2-Tabelle.
