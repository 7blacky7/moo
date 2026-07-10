# KIP-G4c — Production-Wiring: moo_nn-Pfade auf residente GPU-Ops (STRIKT-Modus)

Owner: **kip-gpu** · Task `ab09b47c` (in_progress) · Priorität low
Depends: KIP-G4 (16cbc7d, done), KIP-G4b (5879a6b, done)
Stand HEAD bei Planerstellung: `87ef09e`

> **Phase-Trennung (Koordinator-Dispatch Channel 12804):**
> - **Phase 1 (dieses Dokument):** Prework NUR in NEUEN Dateien/Thoughts. Dispatch-/Wiring-Plan,
>   Symbol-/Callgraph-Inventur, STRIKT-Vertrag, Telemetrie-API, Gate-Harness-Skelett.
>   **KEINE** Änderungen an `moo_nn.c`, `moo_autograd.c`, `moo_tensor_ops.c`, `moo_runtime.h`
>   solange kip-ops (KIP-B4b) diese Dateien reserviert hält.
> - **Phase 2 (nach FREI-Ping von kip-ops):** Production-Wiring in die geteilten Dateien nach
>   Konvention v1 (BELEGT scope=all → files-plan/commit → FREI).

---

## 1. Schnittstellen-Inventur (verifiziert gegen HEAD 87ef09e, read-only)

### 1.1 Forward-Pfad (`moo_nn.c`)
Layer-Dispatch ist **registry-basiert** (kein switch): `nn_layer_registry[]` (`moo_nn.c:1695`),
Dispatcher `schicht_vorwaerts` (`:1503`) liest `dget(schicht,"__nn")` → `nn_layer_lookup` (`:1707`)
→ `d->fw(schicht,x)` (`:1513`). Netz-Ebene: `moo_nn_vorwaerts` (`:1525`).

| Layer | Forward | Zeile | CPU-Ops (moo_tensor_ops.c) | Ziel-`_res`-Op |
|---|---|---|---|---|
| dicht (Dense) | `fw_dicht` | 646 | `moo_tensor_matmul`, `_add`, `akt_anwenden` | `matmul_res` + `ew_res(ADD)` + `unary_res` |
| dropout | `fw_dropout` | 669 | `moo_tensor_mul` (determ. Maske) | `ew_res(MUL)` (Maske resident) |
| layernorm | `fw_layernorm` | 696 | `_mittel/_sub/_mul/_adds/_sqrt/_div/_add` | `norm_res(NORM_LAYER)` |
| rmsnorm | `fw_rmsnorm` | 721 | `_mul/_mittel/_adds/_sqrt/_div` | `norm_res(RMS)` |
| ffn_gated (SwiGLU) | `fw_ffn_gated` | 742 | `matmul`×3, `_gelu`/`_sigmoid`, `_mul` | `matmul_res`×3 + `unary_res` + `ew_res` |
| embedding | `fw_embedding` | 770 | `moo_tensor_gather` | `gather_res` |
| attention (MHA/GQA+RoPE+KV) | `fw_attention` | 1125 (Cache `990`) | `matmul/_transponieren/_muls/_add/_softmax/_verbinden` | `matmul_res`+`transpose_res`+`unary_res(MULS)`+`softmax_res`+`rope_res`+`head_slice_res` |
| position | `fw_position` | 1285 | `moo_tensor_add` | `ew_res(ADD)` |
| moe | `fw_moe` | 1373 | matmul + Router/Top-K | `matmul_res` + Router (CPU/host, klein) |

MatMul-Kern: `moo_tensor_matmul` (`moo_tensor_ops.c:211`) → CPU `matmul_ikj` (`:197`).
Transponieren: `moo_tensor_transponieren` (`:299`).

### 1.2 Backward-Pfad (`moo_autograd.c`)
Globales Tape `static MooAgNode* tape` (`:35`); Record `moo_ag_record(op,a,b,c,out)` (`:58`);
lazy Backward-Registrierung `moo_ag_init_bw()` (`:720`) mappt 26 Ops via `moo_tensor_op_set_bw`.
Grad-Akkumulation überall `zg[i] += …` (Fan-out-`+=`), Buffer lazy `grad_sicherstellen→calloc` (`:50`).
Schlüssel-BW: `bw_matmul` (`:467`, da=g@Bᵀ, db=Aᵀ@g), `bw_transpose` (`:495`), `bw_gather` (`:451`),
`bw_softmax` (`:684`). Backward-Einstieg `moo_tensor_rueckwaerts` (Decl `moo_runtime.h:268`).
**Alles rein CPU — kein GPU-Hook im Autograd.**

Ziel-Mapping Backward → `_res`:
- `bw_matmul` → `moo_ki_gpu_matmul_bw_res` (`api.h:184`, liefert da/db in einem Call)
- Grad-`+=` (Fan-out) → `moo_ki_gpu_grad_accum_res` (`:98`)
- `bw_transpose` → `moo_ki_gpu_transpose_res` (`:90`)
- `bw_softmax` → `moo_ki_gpu_softmax_bw_res` (`:131`)
- `bw_gather` → `moo_ki_gpu_scatter_add_res` (`:178`)
- Aktivierungs-BW (relu/gelu/…) → `moo_ki_gpu_unary_bw_res` (`:86`)
- Norm-BW → `moo_ki_gpu_norm_bw_res` (`:148`)

### 1.3 Trainiere-Pfad
- High-Level: `moo_nn_trainiere` (`moo_nn_easy.c:202`), Loop `:407-434`:
  `vorwaerts → loss(ce|mse) → rueckwaerts → grad_clip? → opt_schritt`.
- Optimizer-Kern (in-place, kein Tape): `moo_nn_opt_schritt` (`moo_nn.c:2294`):
  SGD `:2314-2325`, Adam/AdamW `:2326-2351`, Grad-Nullung + `p->valid=MOO_V_DATA` `:2353`,
  `moo_ag_reset()` `:2362`. Konstruktoren `_sgd:2242 / _adam:2260 / _adamw:2271`; State m/v in `opt_basis:2218`.
- **Low-Level-Loop (Gate-Ziel):** `beispiele/ki_sprachmodell.moo:178-193` — manuelle Schleife
  `vorwaerts → kreuzentropie → rueckwaerts → opt.schritt()`, nutzt NICHT `trainiere()`.
  Optimizer `optimierer_sgd(params, rate, 0.9)` `:158`.

Ziel-Mapping Optimizer → `_res`:
- SGD (Momentum) `:2314` → `moo_ki_gpu_opt_sgd_res` (`api.h:101`)
- Adam/AdamW `:2326` → `moo_ki_gpu_opt_adam_res` (`:108`, mv=2n-Buffer, `bc=1-β^t` host-seitig)

### 1.4 GPU-Hooks heute
`moo_nn.c` / `moo_nn_easy.c` / `moo_autograd.c` haben **NULL GPU-Hooks (rein CPU)**.
GPU nur in `moo_tensor_ops.c` an 3 **nicht-residenten Legacy-Punkten** (Host-in/out, pro Call kopiert):
- EW `:150` `moo_ki_gpu_ew(...)` (Fallback `ew_same`)
- MatMul `:232` `moo_ki_gpu_matmul(...)` (Fallback `matmul_ikj`)
- Reduce `:433` `moo_ki_gpu_reduce_sum(...)`
Decls `moo_runtime.h:347-349`; ENV `MOO_KI_GPU=0` / `MOO_KI_GPU_ERZWINGEN=1` (`moo_runtime.h:346`).

**Die residenten `_res`-Ops (G1/G3/G4b) werden NIRGENDS im NN-/Tensor-Pfad aufgerufen — das ist die G4c-Lücke.**
`MOO_KI_GPU_STRIKT` und `gpu_statistik()` existieren HEUTE nur in Design-Docs, NICHT im Code.

### 1.5 MooTensor-Struct (`moo_runtime.h:140-157`) — bereits G4-ready
Vorhandene Residenz-Felder (kip-kern-Revier, **kein Struct-Redesign nötig**):
`float* data` (nullable ab D1), `float* grad` (immer f32), `uint8_t device` (CPU/GPU `:153`),
`uint8_t valid` (Bitmaske `MOO_V_DATA|MOO_V_STORE|MOO_V_DEV=0x4`), `uint8_t grad_valid`,
`void* gpu_buf` (opaque `MooKiGpuBuf*`, `:155`), `void* gpu_grad` (`:156`, seit G3c).
Repr-Sicherungen `moo_tensor_f32_sichern` u.a. (ab `:177`) sind im f32/CPU-Skelett No-ops.

### 1.6 Telemetrie-API (Backend für `gpu_statistik()`)
`moo_ki_gpu_api.h:207-223`:
```c
typedef struct { uint64_t submits, uploads, downloads, cpu_fallbacks; } MooKiGpuTelemetrie;
void moo_ki_gpu_telemetrie(MooKiGpuTelemetrie* out);
void moo_ki_gpu_telemetrie_reset(void);
```
Zähler in `moo_ki_gpu.c`: `g_tel.submits++` je Dispatch, `g_tel.cpu_fallbacks++` an jedem `_res`-Guard.
**G4-Gate-Invariante: `cpu_fallbacks==0` im residenten Pfad.**

---

## 2. Wiring-Strategie (Phase-2-Design)

### 2.1 Grundprinzip — Residenz-Session um den Trainingsschritt
Der G4/G4b-PoC hat volle Residenz **manuell** (standalone Harness) bewiesen. G4c verdrahtet dieselbe
Residenz in die Produktivpfade über ein **op-granulares Routing an der `moo_tensor_ops`-Grenze**,
mit residenter `gpu_buf`-Lebensdauer über den ganzen Fwd+Bwd-Schritt (uploads NUR an den Blättern
= Params/Inputs, downloads NUR Loss). Kein Per-Op-Host-Roundtrip → `cpu_fallbacks==0`, `uploads==#leaves`.

### 2.2 Routing-Schalter (3-Zustands-Modell)
Neue Laufzeit-Zustände (Env + interner Flag), Auflösung in `moo_ki_gpu.c`:

| Zustand | Env | Verhalten bei GPU-fähiger Op |
|---|---|---|
| AUS (Default) | `MOO_KI_GPU=0` oder kein Vulkan | Reiner CPU-Pfad, **bit-identisch zu heute** |
| AN (opportunistisch) | `MOO_KI_GPU=1` | Route auf `_res`; bei Nicht-Route (unsupported shape) still CPU-Fallback, `cpu_fallbacks++` |
| STRIKT | `MOO_KI_GPU_STRIKT=1` | Route auf `_res`; **jeder** CPU-Fallback = harter Fehler (`moo_ki_gpu_strikt_verletzung`) |

STRIKT impliziert AN. Ohne Vulkan-Build ist STRIKT ein Init-Fehler (fail-loud), kein stiller CPU-Lauf.

### 2.3 Resident-Buffer-Lebenszyklus (op-lokal, in moo_tensor_ops-Wrappern)
Pro GPU-geroutetem Op (Pseudocode, Phase-2-Impl):
```
res_ensure(t):                     # Operand resident machen
    if !(t->valid & MOO_V_DEV):
        if !t->gpu_buf: t->gpu_buf = moo_ki_gpu_buf_belegen(t->size)
        moo_ki_gpu_upload(t->gpu_buf, t->data, t->size)   # uploads++
        t->valid |= MOO_V_DEV
out = op_res(a->gpu_buf, b->gpu_buf, o->gpu_buf, dims)     # submits++
o->valid = MOO_V_DEV                # NUR Device gültig → lazy Download-on-read
o->device = MOO_DEV_GPU
```
Host-Lesezugriff (`moo_tensor_f32_sichern` / Loss-Download) triggert genau EINEN Download
(`downloads++`) und setzt `MOO_V_DATA`. Innerhalb Fwd+Bwd bleibt alles resident.

**Backward-Residenz:** `gpu_grad` analog zu `gpu_buf`; `bw_matmul`→`matmul_bw_res`,
Fan-out-`+=`→`grad_accum_res`. Der resident-Start-Barrier in `dispatch_sync` macht Zwischen-Writes
sichtbar (bewiesen G3d-e / Coverage).

### 2.4 Optimizer-Residenz
`moo_nn_opt_schritt`: wenn Params resident (`gpu_buf` gültig) und GPU aktiv → `opt_adam_res`/`opt_sgd_res`
(State m/v in `mv`-Buffer 2n, resident über Schritte); `t` host-seitig, `bc=1-β^t` in double.
Grad-Nullung resident via `unary_res(MULS, g, g, n, 0.0f)` (g muss initial 0-uploaded sein).

### 2.5 Minimal-invasiver Einstieg (empfohlene Phase-2-Reihenfolge, low-risk zuerst)
1. **matmul-Routing** in `moo_tensor_matmul` (`:211`) auf `matmul_res` + resident `gpu_buf` (heißester Pfad, größter Gewinn, kleinste Fläche).
2. **Optimizer-Routing** `moo_nn_opt_schritt` (`:2294`) auf `opt_*_res`.
3. **norm/softmax/ew/gather** entlang der Layer-Forwards.
4. **Backward** (`bw_matmul`/`grad_accum`) resident.
5. **STRIKT-Enforcement** + `gpu_statistik()`-Builtin.
Jede Stufe einzeln durch das G4c-Gate (§4) abgesichert, bevor die nächste dazukommt.

---

## 3. STRIKT-Vertrag & Telemetrie-Sichtbarkeit (neue Runtime-API, Phase 2)

### 3.1 `MOO_KI_GPU_STRIKT` (Enforcement)
- Aktivierung: Env `MOO_KI_GPU_STRIKT=1` **oder** programmatisch `moo_ki_gpu_strikt_setzen(bool)`.
- Semantik: Im STRIKT-Modus ist **jeder** `cpu_fallbacks++`-Pfad ein harter Fehler → `moo_fehler`/abort
  mit klarer Meldung („STRIKT: Op X auf Shape Y nicht GPU-resident routbar"). **Keine versteckten
  CPU-Fallbacks** (Gate-Kriterium).
- Ohne Vulkan-Build: STRIKT-Init schlägt fehl (fail-loud), statt still auf CPU zu laufen.
- Deklaration geplant in `moo_ki_gpu_api.h` (MEINE Datei, nicht in der Tabu-Liste) — wird in Phase 2 ergänzt:
  ```c
  void moo_ki_gpu_strikt_setzen(bool an);
  bool moo_ki_gpu_strikt_aktiv(void);
  ```

### 3.2 `gpu_statistik()` — moo-sichtbare Telemetrie
- Neuer moo-Builtin `gpu_statistik()` → gibt Dict `{submits, uploads, downloads, cpu_fallbacks}` zurück
  (Backend: `moo_ki_gpu_telemetrie`). Ermöglicht dem `.moo`-Programm (ki_sprachmodell) die
  Residenz-Prüfung zur Laufzeit.
- Reset-Builtin `gpu_statistik_reset()` → `moo_ki_gpu_telemetrie_reset` (für Loop-Messfenster).
- Codegen/Binding-Anbindung (runtime_bindings.rs/codegen.rs) ist **kip-kern/kip-ops-koordiniert** —
  vor Edit an diesen geteilten Dateien Konvention v1 BELEGT. Alternativ als reine C-Runtime-Funktion
  `moo_gpu_statistik(...)` exponiert, falls Builtin-Registrierung zu breit streut (Entscheid Phase 2).

---

## 4. Gate-Harness (G4c-Abnahme)

Skript: `skripte/kip_g4c_gate.sh` (neu, Skelett in Phase 1 angelegt). Kriterien:

1. **CPU-Default bit-identisch:** `run_all` GPU-frei (ohne Vulkan/`MOO_KI_GPU=0`) 60/0 unverändert —
   Default-Pfad byte-/bit-identisch zu HEAD (Negativ-Beweis: G4c ändert den CPU-Pfad nicht).
2. **ki_sprachmodell GPU==CPU in Toleranz:** identischer Param-Snapshot; N Schritte CPU-Loss-Kurve
   vs. `MOO_KI_GPU_STRIKT=1`-Kurve; rel. Abweichung < Toleranz (float-GPU vs. double/f32-CPU,
   Adam-Drift beachten → enge Toleranz nur kleine Dims/wenige Schritte, vgl. G4-Muster).
3. **Vulkan-Smoke:** `ki-gpu-smoke.sh` + neuer G4c-Smoke auf 4070 Ti grün.
4. **kip_gpu_coverage.sh weiter grün** (G0 §3 Residenz-Beweis unverändert).
5. **Keine versteckten CPU-Fallbacks:** STRIKT-Lauf → `gpu_statistik().cpu_fallbacks == 0`;
   Negativ-Kontrolle: ein absichtlich nicht-geroutetes Op unter STRIKT MUSS hart fehlschlagen.
6. **Stub ohne MOO_HAS_VULKAN** kompiliert; STRIKT ohne Vulkan = sauberer Init-Fehler (kein stiller CPU-Lauf).

E2b↔G4c-End-to-End (Checkpoint→Restore→Weitertrainieren) ist **KIP-G4c-QA (kip-daten, 8972d152)** —
separates Gate, dieses Doc dokumentiert nur die von G4c zu exponierenden Hooks.

---

## 5. Datei-Grenzen (Konvention v1)

**Geteilte Dateien (Phase 2, BELEGT/FREI + files-plan/commit):**
`moo_nn.c`, `moo_autograd.c`, `moo_tensor_ops.c`, `moo_runtime.h` — aktuell durch kip-ops (B4b) belegt.

**Meine eigenen Dateien (kein BELEGT nötig, aber Phase-2-Timing):**
`moo_ki_gpu_api.h`, `moo_ki_gpu.c` (STRIKT-API + evtl. neue Routing-Helfer).

**Koordinierte Binding-Dateien (falls Builtin-Route gewählt):**
`runtime_bindings.rs`, `codegen.rs` — Konvention v1, mit kip-kern/kip-ops abstimmen.

**Neue Dateien (Phase 1, dieses Prework):**
`docs/kip/G4c-production-wiring-plan.md`, `skripte/kip_g4c_gate.sh`,
später `tests/ki_gpu_g4c_wiring.c` (Phase 2).

## 6. No-Go-Liste
- KEIN Struct-Redesign von MooTensor (Felder existieren; kip-kern-Revier).
- KEIN globales -fwrapv, KEINE stillen Fallbacks unter STRIKT.
- KEIN Antasten des CPU-Default-Pfads (muss bit-identisch bleiben).
- KEINE Edits an den 4 Shared Files vor kip-ops FREI-Ping.
- KEIN zweiter Serializer / kein Bruch des E2/E2b-Checkpoint-Vertrags.
