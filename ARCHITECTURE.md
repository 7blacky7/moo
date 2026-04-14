# moo — Architektur

Dieses Dokument beschreibt die **Schichten** des moo-Projekts, damit ein neuer Leser innerhalb von fünf Minuten versteht, was wo lebt, wie die Teile zueinander stehen und welche Entscheidungen **nicht** zufällig sind.

Es ist die Root-Landkarte — Details stehen in `spec/` und `docs/`.

## Kurzprofil

moo ist eine deutschsprachige Programmiersprache mit nativer LLVM-Kompilierung, C-Runtime und zusätzlicher Python-Toolchain (LSP, Formatter, Transpiler). Sprachoberfläche: Deutsch und Englisch gleichwertig, teilweise mit Kurzformen. Ziel: natürlich lesbarer Code ohne Bezeichner-Raum-Kollision.

## Fünf Ebenen (L0–L4)

```
┌────────────────────────────────────────────────────────────────┐
│ L4  Tooling / Doku / Beispiele / Tests                         │
│     editors/, docs/, beispiele/, compiler/tests/, scratch/     │
├────────────────────────────────────────────────────────────────┤
│ L3  Domänenmodule                                               │
│     Web / DB / Grafik / 3D / Welt / Sprites / Regex / JSON /    │
│     HTTP / Crypto / Profiler / Eval / Webserver / Netzwerk      │
├────────────────────────────────────────────────────────────────┤
│ L2  Runtime-Kern                                                │
│     value, string, list, dict, object, ops, error, memory,      │
│     print, result, thread, channel, file                        │
├────────────────────────────────────────────────────────────────┤
│ L1  Sprachoberfläche / Spezifikation                            │
│     compiler/src/* (Rust), src/moo/* (Python), spec/*           │
├────────────────────────────────────────────────────────────────┤
│ L0  Projektidentität / Toolchains                               │
│     pyproject.toml, Cargo.toml, README, LICENSE, CI-Workflows   │
└────────────────────────────────────────────────────────────────┘
```

### L0 — Projektidentität

Name, Lizenz, Runtime-Manager (mise), Top-Level-Build-Manifeste (`pyproject.toml`, `compiler/Cargo.toml`), CI-Konfiguration (`.github/workflows/`). Entscheidungen hier sind selten, aber folgenreich.

### L1 — Sprachoberfläche / Spezifikation

**Ground Truth:**
- `spec/tokens.yaml` — alle Tokens, Keywords, Soft-Keywords, Alias.
- `spec/soft_keywords.md` — Register der kontextsensitiven Wörter.
- `spec/drift_findings.md` — Rust/Python-Differential.

**Implementierungen (müssen zur Ground Truth synchron sein):**
- `compiler/src/tokens.rs` + `lexer.rs` + `parser.rs` — Rust-Frontend.
- `src/moo/tokens.py` + `lexer.py` + `parser.py` — Python-Toolchain.

CI-Check `tools/check_tokens.py` verhindert Drift.

### L2 — Runtime-Kern

C-Runtime in `compiler/runtime/`. Die **unstrittigen Basis-Typen und -Operationen**: `MooValue`-ABI, Referenzzählung (`moo_retain/release`), `moo_add/sub/...`, Listen/Dicts/Strings, `try/catch`-Infrastruktur, `print`, `error`.

Diese Schicht ist **bewusst klein** gehalten (GPT-Review `ce89cc0d`). Wenn ein Feature *potenziell* systemnahe Konsequenzen hat (Threading, Memory, Bare-Metal), gehört es eher in diese Schicht als in L3.

Siehe `spec/runtime_layers.md` für die Datei-für-Datei-Zuordnung.

### L3 — Domänenmodule

Alles was **eine Domäne** (Web, DB, Grafik, 3D, …) in die Runtime bringt. Jedes Modul hat:
- Eine oder mehrere `.c`-Dateien in `compiler/runtime/moo_<domäne>.c`,
- Registrierte C-Symbole `moo_<domäne>_<operation>`,
- Rust-Bindings in `compiler/src/runtime_bindings.rs`,
- Compiler-Alias-Matching in `compiler/src/codegen.rs` (DE/EN/Kurzform),
- Mindestens eine Referenz-Seite unter `docs/referenz/`.

**Game-Domäne** ist ein separater Strang mit eigener Grenze — siehe `spec/game_module_boundary.md`.

### L4 — Tooling, Doku, Beispiele, Tests

- `editors/vscode/` — VSCode-Extension + LSP.
- `docs/` + `docs/referenz/` + `docs/lernen.md` — User-Doku (mkdocs).
- `beispiele/` — Lauffähige moolang-Programme, klassifiziert in `spec/examples_taxonomy.md` (quickstart / showcase / stress / domain/{web,db,game,system}).
- `compiler/tests/` — kanonische Regressions-Tests mit `.expected`-Gegenstücken.
- `scratch/` (Phase B) — Ad-hoc-Skripte und Altlasten aus dem Root.

## Wer spricht mit wem?

```
   User-Quellcode (*.moo)
         │
         ▼
  compiler/src/lexer.rs ──► tokens ──► compiler/src/parser.rs ──► AST
                                                                   │
                                                                   ▼
                                                        compiler/src/codegen.rs
                                                                   │ (LLVM IR)
                                                                   ▼
                                                          compiler/src/main.rs
                                                                   │ (ruft C-Runtime)
                                                                   ▼
                                                      compiler/runtime/moo_*.c
```

Parallel dazu: die Python-Toolchain (`src/moo/`) teilt sich die **Sprachoberfläche** aber kompiliert auf andere Targets (Python-Transpiler, WASM). Die gemeinsame Quelle beider Seiten ist `spec/tokens.yaml`.

## Public vs. Internal

Siehe `spec/public_vs_internal_api.md`. Grob:

- **Public Moo-API** (DE/EN) ist das, was Anwender in `.moo`-Code aufrufen sollen. Stabil, dokumentiert in `docs/referenz/`.
- **Compiler-/Binding-/Alias-Schicht** (`codegen.rs` + `runtime_bindings.rs`) — Implementation Detail, kann sich ändern.
- **C-Runtime-Symbole** (`moo_*` in `.c`) — ABI-stabil für LLVM-Interop, aber nicht direkt Teil der Anwender-API.

## Was ändert sich nicht?

- `MooValue` ist 16 Bytes, Tag + Data, LLVM-kompatibel.
- Alle Heap-Typen haben `refcount` als erstes Feld.
- Rust und Python lesen denselben `spec/tokens.yaml`.
- Deutsch und Englisch sind gleichwertig, keine Sprache ist primär.
- Soft-Keywords bleiben als Identifier nutzbar (siehe `spec/soft_keywords.md`).

## Phase-B-Umbau

`spec/repo_cleanup_phase_b.md` beschreibt die kontrollierte Move-Sequenz für Root-Aufräumung (lose `test_*.moo`-Dateien in `compiler/tests/` oder `scratch/`, `.ll`-Artefakte in `.gitignore`, etc.). Wird **nicht** ohne User-Abnahme umgesetzt.

## Weitere Spec-Dokumente

- `spec/tokens.yaml` — Token-SSoT
- `spec/soft_keywords.md` — kontextabhängige Keywords
- `spec/runtime_layers.md` — Runtime-Datei-Zuordnung (k3)
- `spec/game_module_boundary.md` — Game-Strang-Grenze (k4, in Arbeit)
- `spec/public_vs_internal_api.md` — API-Schichten (k3+k4, in Arbeit)
- `spec/examples_taxonomy.md` — Beispiel-Klassifizierung (k4, in Arbeit)
- `spec/tests_taxonomy.md` — Test-Zonen (k3, in Arbeit)
- `spec/repo_cleanup_phase_b.md` — Phase-B-Plan (k2, fertig)
- `spec/drift_findings.md` — Rust/Python-Differential (k2)
- `spec/followup-tickets.md` — bekannte Hebel für später
