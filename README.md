# moo

Eine zweisprachige Programmiersprache (Deutsch/Englisch gleichwertig) mit nativer LLVM-Kompilierung, handgeschriebener C-Runtime und Python-Toolchain — vom Hallo-Welt über 3D-Spiele und Webserver bis zum eigenen OS-Kernel und LLM-Training auf der GPU.

```moo
funktion fibonacci(n):
    wenn n <= 1:
        gib_zurück n
    gib_zurück fibonacci(n - 1) + fibonacci(n - 2)

zeige fibonacci(20)
```

## Aktueller Entwicklungsstand (November 2026)

moo ist ein aktives Ein-Personen-Projekt, das mit KI-Agenten entwickelt wird.
Vieles läuft bereits stabil, einige Bereiche sind bewusst noch im Umbau.
Damit klar ist, was fertig ist und was noch kommt:

### KI-Stack

| Bereich | Stand | Was noch ansteht |
|---|---|---|
| Tensoren, Autograd, Transformer-Bausteine, Vulkan-GPU-Training | **funktioniert** (siehe „Was kann moo heute?“) | laufende Optimierungen |
| Trainingsmethoden direkt aus Moo aufrufbar (SGD/Adam/AdamW, SFT mit Masken, Lehrer-Korrektur, Distillation, DPO, Evaluation, Checkpoint/Resume) | **17 von 20 Schritten fertig** — Beispiele in `beispiele/p5/` | Messlauf ~1-Mio.-Parameter-Modell, Training vollständig GPU-resident machen, Vergleichstraining 3 × 10-Mio.-Modelle (BPE / Silbe / Zeichen) |
| Deutscher Silben-Tokenizer (zusätzlich zu Zeichen- und Byte-BPE-Tokenizer) | **14 von 17 Schritten fertig** — `beispiele/p4_silben/`, `skripte/p4_silben/` | Neutraining eines kleinen Silbenmodells (36.000 Schritte), syntaxbewusster Tokenizer für Code |
| GPU-Vergleich SGD vs. JEV-Steuerung | geplant, noch nicht gestartet | — |
| Adaptive Sparse-Berechnung / hierarchisches MoE-Routing | **Forschungsidee**, nur geplant | gesamte Umsetzung |

Hinweis: Trainingskorpora, eigene Testdaten und Modell-Checkpoints sind
absichtlich **nicht** im Repository. Die Beispiele zeigen die API; zum
Nachtrainieren braucht man eigene Texte.

### Sprache und Compiler

- **Native Ebene (`@nativ`)** — echte Maschinentypen (i8…u64, f32/f64, bool),
  C-kompatible Strukturen, C-/C++-FFI für Engine- und Grafikcode: **in Arbeit**
  (ca. 30 von 74 Schritten). Der bestehende dynamische Moo-Code bleibt unverändert.
  Teile davon sind fertig und getestet (u. a. native Zahlen/Casts, nativer
  CPU-Compositor), anderes ist noch nicht abschließend geprüft.
- **Eigener Kernel + UEFI-Loader in Moo:** Loader und minimaler Kern laufen
  in QEMU, Boot auf echter Hardware steht noch aus.

## Wo ist was?

- **Bauen & ausführen:** siehe `DEPENDENCIES.md`, dann
  `cd compiler && cargo build --release` und
  `./compiler/target/release/moo-compiler run <datei>.moos`.
- **Sprache lernen:** `docs/tutorial.md` und `docs/lernen.md`.
- **Referenz aller Builtins:** [moolang-docs](https://7blacky7.github.io/moolang-docs/).
- **Beispiele:** `beispiele/` mit Zonen `domain/{web,db,game,system}/` plus lose Showcases (Taxonomie in `spec/examples_taxonomy.md`), KI-Beispiele unter `beispiele/ki_*`.
- **KI-/LLM-Stack:** Design-Docs unter `docs/kip/` (Zielprofil, DType, GPU-Verträge, Serving/Post-Training), Gate-Skripte unter `skripte/kip_*`.
- **Kernel & Bootloader:** `beispiele/kernel/`, Pipeline via `moo compile --target x86_64-bare --kernel`, Smoke-Gates `kernel-smoke*.sh`/`loader-smoke.sh`.
- **Architektur in 5 Minuten:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Wie ist das Projekt strukturiert?

```
compiler/   Rust-Compiler (compiler/src) + C-Runtime (compiler/runtime) + Tests
            runtime/shader_ki/  Vulkan-Compute-Shader (Tensor-Ops, SPIR-V)
src/moo/    Python-Toolchain: LSP, Formatter, Transpiler
stdlib/     moo-Stdlib-Module (mathe, liste, text, ...)
beispiele/  Lauffähige .moos-Programme (130+ Samples; Zonen: domain/{web,db,game,system})
skripte/    KIP-/GPU-Gate-Skripte (Coverage, LM-Trainings-Gates, QA-Verträge)
scratch/    Ad-hoc-Skripte, nicht CI-relevant (siehe scratch/README.md)
docs/       Benutzer-Doku (mkdocs-Material) + docs/kip/ (KI-Design-Docs) + docs/roadmap/
editors/    VSCode-Extension + LSP-Client
spec/       Spezifikation (Single Source of Truth, u.a. tokens.yaml)
scripts/    Hilfsskripte
website/    Landingpage + Browser-Playground
```

Details: siehe [ARCHITECTURE.md](ARCHITECTURE.md).

## Was kann moo heute?

- **Sprache:** DE/EN-Keywords frei mischbar (`spec/tokens.yaml` als SSoT), Klassen,
  Pattern Matching, Lambdas, Data-Klassen, f-Strings, Comprehensions, Try/Catch,
  Threads/Channels, Refcount-Speichermodell mit ASan/UBSan-Gates.
- **KI/LLM-Runtime:** Tensoren mit dynamischem Autograd (Gradcheck-Gate über die
  komplette Op-Registry), NN-Layer bis Transformer (RMSNorm, RoPE inkl.
  Kontext-Skalierung, GQA/MQA, SwiGLU, KV-Cache, MoE, MTP/Speculative Decoding,
  GRPO-Toy), bf16-Mixed-Precision, byte-level BPE-Tokenizer, Streaming-Dataloader,
  exakte Voll-Checkpoints (CPU und Cross-Device), Sequence Packing, Activation
  Checkpointing. MNIST- und Char-LM-Gates deterministisch reproduzierbar.
- **GPU-Compute (Vulkan):** residentes Training komplett auf der GPU — Matmul,
  Elementwise, Reduktionen, Softmax/CE, Norm-Kerne, RoPE-/Head-Slicing-Kernels,
  residente SGD/Adam-Schritte und Gradienten-Akkumulation; STRIKT-Modus beweist
  „kein stiller CPU-Fallback" per Telemetrie. CPU↔GPU-Loss-Kurven-Parität als Gate.
- **Grafik & Spiele:** 2D/3D (GL33/GL21/Vulkan), Voxel-Engine mit
  Palette-Kompression, Licht/Specular/Tageszeit/Wellen/Transparenz,
  Test-API mit Input-Simulation, Screenshot-, GIF- und MP4-Capture.
- **System & Bare-Metal:** OS-Kernel in moo (x86_64 Multiboot2 + Long Mode,
  ARM64 qemu-virt mit UART/Timer), eigener Bootloader-Pfad (Stage2, UEFI-App
  PE32+), Port-I/O-, MSR-, MMIO-Builtins, `--emit elf|flat|sector`,
  Cross-Targets für ARM, RISC-V, WASM und weitere; Windows-Port (Winsock2).
- **Ökosystem:** LSP + VSCode-Extension, Formatter, Web-Playground,
  Webserver/DB/HTTP/JSON/Regex in der Stdlib.

## Status

Alpha — Dateiendung final: `.moos` (MooScript-Quellen; `.moo` wird als
Alt-Endung weiterhin gelesen). Der Sprachkern ist stabil,
die Runtime wird kontinuierlich konsolidiert. Qualitätsdisziplin: kein
Feature ohne echten Test, `run_all` bleibt QEMU-/GPU-frei, GPU-/Kernel-Beweise
laufen über eigene Gate-Skripte auf echter Hardware, Doppel-Verifikation
für Meilenstein-Abschlüsse. Laufende Arbeitsstränge: siehe `spec/` und
`docs/kip/`.

## Root-Inhalt — was ist dauerhaft, was ist Altlast?

Dauerhaft im Root:
`README.md`, `ARCHITECTURE.md`, `DEPENDENCIES.md`, `LICENSE` (folgt),
`pyproject.toml`, `.gitignore`, `.github/`.

Alles andere ist Code- oder Doku-Verzeichnis (`compiler/`, `src/`,
`stdlib/`, `beispiele/`, `docs/`, `editors/`, `scripts/`, `skripte/`,
`spec/`, `scratch/`, `tests/`, `tools/`, `website/`). Die
Phase-B-Aufräumung (W1–W3) ist **abgeschlossen** — Root ist frei von
losen `test_*.moos`-Dateien. Bitte weiterhin keine neuen losen
`test_*.moos` im Root anlegen (siehe `scratch/README.md`). Details zum
Verlauf: `spec/phase_b_progress.md`.

## Lizenz

Wird vor v1.0 festgelegt. Bis dahin: Open Source, Details im Projekt.
