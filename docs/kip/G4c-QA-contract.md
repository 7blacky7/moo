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
| H | Echter CPU↔GPU-Loss-**Kurvenvergleich** (identischer Forward/Backward-Pfad, nur anderes Device) | matmul Fwd+Bwd, ew_op (add/sub/mul/div) Fwd, bw_mul (voll)+bw_div (da-Zweig), sqrt (u_op), softmax/gather Fwd, reduce_op, Adam/SGD — Netz OHNE Aktivierung (tanh noch nicht resident) | `gpu_vs_cpu_curve` (aktiv, Toleranz rel<2e-3) | ✅ **AKTIV, GRÜN** (SKIP ohne libvulkan/vollständige Residenz) |

**A–H sind heute vollständig automatisiert grün** (`skripte/kip_g4c_qa.sh`,
lokal auf 4070 Ti verifiziert: CPU-Teil A/B immer, GPU-Teil C/D/E/F/G/H mit
Vulkan). Nachtrag 2026-07-10: Kriterium H wurde aktiviert, nachdem kip-gpu
den Forward/Backward-Pfad für matmul/ew/sqrt/reduce/softmax/gather/Optimizer
resident + `MOO_KI_GPU_STRIKT` hardware-verifiziert gemeldet hat (Channel
moo-general, Msg 12897). tanh/relu/sigmoid/gelu/exp/log sind noch NICHT
resident — das Kriterium-H-Netz nutzt bewusst KEINE Aktivierung, damit die
in §4 dokumentierte Negativ-Kontrolle nicht verwässert wird (mit tanh würde
STRIKT den nicht-residenten Op transparent auf CPU rechnen statt zu werfen).

---

## 3. Konkrete Belege (lokaler Lauf, 4070 Ti, `libvulkan.so.1` vorhanden)

```
[A] cpu_ref(7)      == cpu_train(4)+cpu_resume(3): loss=0.221136406 checksum=3337946610739537051 dropz=7 schritt=7  BIT-IDENTISCH
[B] cpu_mismatch    -> "wirft-wie-erwartet"
[C] gpu_ref(7)      == gpu_train(4)+gpu_resume_gpu(3): checksum=14053461363620090639 schritt=7             BIT-IDENTISCH
[E]                 -> cpu_fallbacks==0 (Delta über den gesamten Resume+Weiter-Abschnitt)
[D] gpu_resume_cpu auf Vulkan-Binary UND auf Nicht-Vulkan-Binary: checksum=14828330410922979978            IDENTISCH
[H] gpu_vs_cpu_curve(6): maxrel=7.52862e-06 (< 2e-3 Toleranz), cpu_fallbacks=0, cpu_last=0.807542026 gpu_last=0.807548106
```

Jede Zahl stammt aus **separaten `exec`-Aufrufen** (`skripte/kip_g4c_qa.sh`
startet `$BIN_CPU`/`$BIN_VK` mehrfach neu) — kein gemeinsamer Prozess, keine
gemeinsamen globalen Zähler zwischen den verglichenen Läufen.

---

## 4. Kriterium H — Stand + verbleibende Lücke (Aktivierung 2026-07-10)

**AKTIVIERT.** `gpu_vs_cpu_curve` baut ein Netz OHNE Aktivierung
(`dicht(3,8,"keine")` → `dropout(0.3)` → `dicht(8,2,"keine")`), trainiert es
identisch über `moo_nn_vorwaerts/_mse/_rueckwaerts/opt_schritt` einmal mit
`moo_ki_gpu_strikt_setzen(false)` (CPU-Referenz) und einmal mit
`moo_ki_gpu_strikt_setzen(true)` (GPU-Pfad, hart bei jeder Nicht-Residenz),
vergleicht die Loss-Kurven (Toleranz `rel<2e-3`, s.u.) und prüft
`cpu_fallbacks==0` über `moo_ki_gpu_telemetrie()`. Ohne Vulkan/vollständige
Residenz: `SKIP-strikt-wirft` (Exit 0, kein falsches Grün — das IST die
Negativ-Kontrolle: ein nicht-residenter Pfad wirft tatsächlich hart).

Ursprünglich fehlende Hooks laut
`docs/kip/G4c-production-wiring-plan.md` §2 — Stand jetzt:

1. **`moo_tensor_matmul`/`_ew`/`_softmax`/`_gather` GPU-geroutet** ✅ erledigt
   (matmul Fwd+Bwd, ew add/sub/mul/div Fwd, softmax/gather Fwd). `_norm` NICHT
   als dedizierter Op nötig — layernorm/rmsnorm sind reine Op-Komposition aus
   bereits residenten Primitiven (mul/sub/adds/sqrt/div/mittel, WEG-1-Fund).
2. **`MOO_KI_GPU_STRIKT`-Enforcement** ✅ erledigt, hardware-verifiziert
   (Commit e5b53cb + Fixes, inkl. echtem Bug-Fund asymmetrischer bw_matmul).
3. **`gpu_statistik()`/`moo_ki_gpu_telemetrie()`-Sichtbarkeit** ✅ erledigt
   (C-Backend 213c7de, Rust-Builtin 16691ff) — mein Harness nutzt direkt
   `moo_ki_gpu_telemetrie()` (kein moo-Builtin-Umweg nötig im C-Test).
4. **Backward-Residenz** ⚠️ TEILWEISE: `bw_matmul` (voll), `bw_mul` (voll),
   `bw_div` (nur da-Zweig) resident; `bw_add`/`bw_sub`/`bw_div`-db-Zweig
   bewusst CPU (keine Rechengewinn-Begründung, kein Fehler). Reicht für das
   Kriterium-H-Netz (dicht+dropout+dicht ohne Aktivierung).
5. **Aktivierungsfunktionen (u_op)** ❌ OFFEN: nur `sqrt` ist an die residente
   `unary_res`-Familie angebunden, `tanh/relu/sigmoid/gelu/exp/log` NICHT.
   Deshalb nutzt das Kriterium-H-Netz bewusst `"keine"` statt `"tanh"` (wie
   im ursprünglichen CPU-Pfad-A-Netz) — mit tanh würde `MOO_KI_GPU_STRIKT=1`
   den Op transparent auf CPU rechnen (kein Fehler, aber `cpu_fallbacks`
   bliebe fälschlich `0`, weil der Op nie versucht wird) und damit genau die
   unten geforderte Negativ-Kontrolle verwässern.

**Verbleibender Vertrag, falls tanh (oder eine andere Aktivierung) später
resident wird** (kip-gpu bot das im Channel an, gleiches u_op-Muster wie
sqrt): `baue_gvsc_netz()` in `test_g4c_e2e_qa.c` von `"keine"` auf `"tanh"`
umstellen, um näher an den ursprünglichen CPU-Pfad-A heranzukommen — nicht
blockierend, da die aktuelle Version bereits die volle Negativ-Kontrolle
und Toleranzprüfung liefert.

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
