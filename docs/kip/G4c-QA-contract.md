# KIP-G4c-QA — E2b↔G4c End-to-End-Gate: Vertrag + Testmatrix

Owner: **kip-daten** · Task `8972d152` · Priorität low
Depends: KIP-E2 (`cd6a48b`, done), KIP-E2b (`493b15c`, done), KIP-E2c (`a4d05a4`, done)
Bezug: `docs/kip/G4c-production-wiring-plan.md` (kip-gpu, Owner der eigentlichen
Production-Wiring-Aufgabe `ab09b47c`) — §4 dort verweist explizit hierher.

> **Scope-Abgrenzung (Koordinator-Dispatch Channel 12804):** Nur neue Test-/
> Script-Dateien. Keine Änderungen an `moo_nn.c`, `moo_autograd.c`,
> `moo_tensor_ops.c`, `moo_runtime.h`. Dieses Dokument beschreibt, was **heute**
> mit den vorhandenen E2/E2b-Hooks beweisbar ist, und was **fehlt**, bis
> KIP-G4c (kip-gpu, Phase 2) die Production-Wiring-Hooks liefert.

---

## 1. Warum ein eigenes Gate (Abgrenzung zu E2/E2b)

`test_checkpoint_asan.c` (E2) und `test_e2b_device_ckpt.c` (E2b) beweisen das
**Checkpoint-Datenformat** durch Kill+Resume **innerhalb eines einzigen
Prozesses** (Objekte freigeben, dann im selben `main()` neu laden). Das ist
stark für Format-Korrektheit, beweist aber NICHT, dass ein **echter
Prozessneustart** — neuer Adressraum, neue Vulkan-Geräte-Initialisierung,
keine überlebenden globalen C-Statics (`static MooAgNode* tape` in
`moo_autograd.c`, Telemetrie-Zähler in `moo_ki_gpu.c`, u.a.) — denselben
Zustand liefert.

Die Aufgabenstellung (Koordinator, Channel 12786/12804) verlangt explizit
*„Prozessneustart"*. Deshalb: `test_g4c_e2e_qa.c` ist ein **CLI-Multi-Mode-
Tool** — jede Trainings-/Restore-Phase ist ein eigener `argv`-Modus, das
Orchestrierungs-Skript `skripte/kip_g4c_qa.sh` startet jede Phase als
**eigenen OS-Prozess** und vergleicht die auf stdout gedruckten
`RESULT`-Zeilen (Checksum über die Rohbytes aller Parameter + Loss + Adam-t +
Dropout-Zähler + globaler Schritt + Residenztelemetrie).

---

## 2. Testmatrix

| # | Szenario | Hooks (heute vorhanden) | Modus (test_g4c_e2e_qa.c) | Status |
|---|---|---|---|---|
| A | CPU-Training → echter Prozessneustart → CPU-Restore → Weitertrainieren | `moo_nn_vorwaerts/_mse/_rueckwaerts/opt_schritt`, `moo_nn_ckpt_speichern/_laden` (E2) | `cpu_ref` / `cpu_train` / `cpu_resume` | ✅ **AKTIV, GRÜN** |
| B | Negativ-Kontrolle: Tokenizer-/Arch-Versions-Mismatch über Prozessgrenze | `moo_nn_ckpt_laden(pfad, erwartungen)` | `cpu_mismatch` | ✅ **AKTIV, GRÜN** |
| C | GPU-Training (Adam auf residenten Buffern) → echter Prozessneustart → GPU-Restore → Weitertrainieren, inkl. Adam m/v/t | `moo_ki_gpu_opt_adam_res`, `moo_ki_gpu_upload/_download`, `moo_nn_ckpt_speichern/_laden` (E2b-Muster) | `gpu_ref` / `gpu_train` / `gpu_resume_gpu` | ✅ **AKTIV, GRÜN** (SKIP ohne libvulkan) |
| D | Cross-Device-Restore: GPU-Checkpoint laden auf Prozess **ohne jede Vulkan-Bindung** UND auf Vulkan-gebundenem Prozess, danach echtes CPU-Forward/Backward-Weitertraining | `moo_nn_ckpt_laden` + `moo_nn_vorwaerts/_mse/_rueckwaerts/opt_schritt` (rein CPU, kein GPU-Call im Restore-Pfad) | `gpu_resume_cpu` (auf Vulkan- und Nicht-Vulkan-Binary) | ✅ **AKTIV, GRÜN** |
| E | Residenztelemetrie: `cpu_fallbacks==0` während GPU-Resume + Weitertraining | `moo_ki_gpu_telemetrie()` | in `gpu_resume_gpu` mitgeprüft (Delta vor/nach) | ✅ **AKTIV, GRÜN** |
| F | Dropout-Zustand (`zaehler`) überlebt echten Prozessneustart | `dget(schicht,"__nn")`-Layer-Dict, E2-Serialisierung | in `cpu_ref`/`cpu_train`/`cpu_resume` mitgeprüft (`dropz`-Feld) | ✅ **AKTIV, GRÜN** |
| G | Globaler Schrittzähler (`schritt`-Feld im Checkpoint-Dict) überlebt Neustart | E2-Metadaten-Roundtrip | in allen `cpu_*`/`gpu_*`-Modi (`t`-Feld) | ✅ **AKTIV, GRÜN** |
| H | Echter CPU↔GPU-Loss-**Kurvenvergleich** (identischer Forward/Backward-Pfad, nur anderes Device) | **FEHLT** — kein GPU-geroutetes `moo_nn_vorwaerts` | `gpu_vs_cpu_curve` (Platzhalter, druckt fehlenden Hook) | 🕓 **PENDING** |

**A–G sind heute vollständig automatisiert grün** (`skripte/kip_g4c_qa.sh`,
lokal auf 4070 Ti verifiziert: CPU-Teil A/B immer, GPU-Teil C/D/E/F/G mit
Vulkan). Nur **H** ist blockiert.

---

## 3. Konkrete Belege (lokaler Lauf, 4070 Ti, `libvulkan.so.1` vorhanden)

```
[A] cpu_ref(7)      == cpu_train(4)+cpu_resume(3): loss=0.221136406 checksum=3337946610739537051 dropz=7 schritt=7  BIT-IDENTISCH
[B] cpu_mismatch    -> "wirft-wie-erwartet"
[C] gpu_ref(7)      == gpu_train(4)+gpu_resume_gpu(3): checksum=14053461363620090639 schritt=7             BIT-IDENTISCH
[E]                 -> cpu_fallbacks==0 (Delta über den gesamten Resume+Weiter-Abschnitt)
[D] gpu_resume_cpu auf Vulkan-Binary UND auf Nicht-Vulkan-Binary: checksum=14828330410922979978            IDENTISCH
```

Jede Zahl stammt aus **separaten `exec`-Aufrufen** (`skripte/kip_g4c_qa.sh`
startet `$BIN_CPU`/`$BIN_VK` mehrfach neu) — kein gemeinsamer Prozess, keine
gemeinsamen globalen Zähler zwischen den verglichenen Läufen.

---

## 4. Fehlende Hooks für Kriterium H (an KIP-G4c, kip-gpu, Phase 2)

Ein echter CPU↔GPU-**Loss-Kurvenvergleich** (nicht nur Adam-Zustand, sondern
der volle Forward/Backward-Pfad eines `moo_nn`-Netzes) braucht laut
`docs/kip/G4c-production-wiring-plan.md` §2:

1. **`moo_tensor_matmul`/`_ew`/`_softmax`/`_norm`/`_gather` GPU-geroutet**
   (Wiring-Plan §2.5, Schritt 1–3) — ohne das läuft `moo_nn_vorwaerts` auch
   unter `MOO_KI_GPU_STRIKT=1` rein auf CPU, ein „Vergleich" wäre eine
   Nullaussage (identischer Code-Pfad auf beiden Seiten).
2. **`MOO_KI_GPU_STRIKT`-Enforcement** (Wiring-Plan §3.1) — um zu beweisen,
   dass der GPU-Lauf tatsächlich resident war und nicht still auf CPU
   zurückgefallen ist (sonst ist "GPU==CPU" trivial wahr, weil es derselbe
   CPU-Pfad war).
3. **`gpu_statistik()`/`moo_ki_gpu_telemetrie()`-Sichtbarkeit im Trainingslauf**
   — um `cpu_fallbacks==0` über den **gesamten** Trainingsschritt (nicht nur
   den isolierten Adam-Schritt wie heute in C/E) zu behaupten.
4. **Backward-Residenz** (`bw_matmul`/`grad_accum` → `_res`, Wiring-Plan §2.3)
   — sonst bricht die Residenz-Kette beim ersten Backward-Schritt.

**Vertrag für Kriterium H, sobald diese Hooks existieren** (damit kip-gpu das
Gate direkt gegen dieses Dokument verdrahten kann, ohne Rückfrage):

- Neuer Modus `gpu_vs_cpu_curve` in `test_g4c_e2e_qa.c` (Platzhalter-Signatur
  bereits vorhanden) baut **ein** deterministisches Netz (dicht+dropout+dicht
  wie CPU-Pfad A, fester Seed), trainiert `N` Schritte einmal mit
  `MOO_KI_GPU=0` (CPU-Referenz) und einmal mit `MOO_KI_GPU_STRIKT=1`
  (GPU-Pfad), beide über `moo_nn_vorwaerts/_mse/_rueckwaerts/opt_schritt`
  (identischer Aufrufcode, nur Env-Variable unterscheidet sich).
- Toleranzvertrag **nicht bit-identisch** (im Gegensatz zu A/C, die
  elementweisen Adam ohne Reduktion nutzen): Forward/Backward sind
  reduktionslastig (Matmul-Summation, Softmax, LayerNorm-Mittelwert) →
  float-GPU-Reduktionsreihenfolge weicht von der CPU-Referenz ab. Toleranz
  `rel < 2e-3` pro Loss-Wert, analog zum bereits etablierten G4b-Muster
  (`ki_gpu_g4b_lm.c`, „GPU(float)==CPU(double) Loss-Kurve rel<2e-3").
- Negativ-Kontrolle: unter `MOO_KI_GPU_STRIKT=1` MUSS jeder nicht-geroutete
  Layer-Typ hart fehlschlagen (kein stiller CPU-Fallback) — deckt sich mit
  Wiring-Plan §4 Kriterium 5.
- Dieses Gate (`kip_g4c_qa.sh`) wird um einen `[H]`-Block erweitert, der beim
  ersten Auftreten von `MOO_KI_GPU_STRIKT` in `moo_runtime.h`/`moo_nn.c` aktiv
  wird — bis dahin bleibt `[H]` `PENDING` (Exit-neutral, siehe §5).

---

## 5. Exit-Semantik des Gates

`skripte/kip_g4c_qa.sh`:
- **FAIL (Exit 1):** irgendein heute prüfbares Kriterium (A–G) schlägt fehl.
- **PENDING (Exit 0, aber sichtbar markiert):** Kriterium H, solange die
  G4c-Hooks aus §4 fehlen — kein falsch-grünes Signal, aber auch kein
  Blocker für andere Agenten.
- **SKIP (Exit 0):** GPU-Teile (C/D/E) ohne `libvulkan` — Format-/CPU-Teil
  (A/B) läuft immer, unabhängig von Hardware.

---

## 6. Bekannte Nicht-Ziele (bewusst außerhalb dieses Gates)

- **Erneute Prüfung des Checkpoint-Dateiformats selbst** (Rotation, Atomarität,
  bf16, alte-Version-ladbar) — das ist bereits von E2/E2c/E2b bewiesen, wird
  hier nicht dupliziert.
- **STRIKT-Enforcement-Unit-Tests** (Negativ-Kontrolle „ein Op wird absichtlich
  nicht geroutet") — gehört ins G4c-Wiring-Gate (`kip_g4c_gate.sh`,
  kip-gpu-Revier), nicht ins Checkpoint-QA-Gate.
- **Mid-Gradient-Accumulation-Resume** — offener Follow-up aus E2, nicht
  Teil dieser Aufgabe.

---

## 7. Dateien dieser Task

- `compiler/runtime/tests/test_g4c_e2e_qa.c` — CLI-Multi-Mode-Harness.
- `skripte/kip_g4c_qa.sh` — Orchestrierung (echter Prozessneustart) + Gate.
- `docs/kip/G4c-QA-contract.md` — dieses Dokument.

Keine Änderung an geteilten Dateien (`moo_nn.c`, `moo_autograd.c`,
`moo_tensor_ops.c`, `moo_runtime.h`) — alle drei Dateien sind neu.
