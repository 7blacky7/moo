# Tests-Taxonomie

Kanonisches Inventar aller Test-Artefakte im moo-Repo. Phase A — nur Ordnung
sichtbar machen, keine Moves. Zielpfade in Spalte `phase_b_ziel` sind
Vorschlaege fuer Phase B.

## Zonen

### Zone 1 — `compiler/tests/` (offiziell, CI-relevant)

Die **stabile** Testsuite. Hat einen Test-Runner (`run_all.sh`), jede Datei
hat optional eine `.expected`-Datei fuer stdout-Vergleich. `0 fehlgeschlagen`
ist die Merge-Bedingung.

| Zweck | owner | CI-Relevanz | phase_b_ziel |
|-------|-------|-------------|--------------|
| Regression + Feature-Lock fuer Compiler + Runtime | alle | **pflicht** vor Merge | `tests/compiler/` |

**Inhalt** (27 `.moo`-Dateien, 17 aktiv via `.expected`, 10 Skip als Compile-Smoke):

Kern/Sprache:
- `test_bitwise_ops` (k2 A4 bit ops), `test_closures`, `test_global_scope_in_function` (P2a Regressions-Muster), `test_lists`, `test_match`, `test_oop`, `test_strings`, `test_json`, `test_fileio`

Imports (Bug-Regressions):
- `test_import_combined`, `test_import_diamond{,_a,_b,_shared}`, `test_import_vars{,_modul}`

Runtime-APIs:
- `test_crypto`, `test_db`, `test_threads` (Compile-Smoke)

Neue APIs aus Konsolidierungs-Welle:
- `test_db_with_params_{basic,injection,types}` (P3c-B, k3)
- `test_db_prepared_{named,bulk,rollback}` (P3c-A, k3)
- `test_http_with_headers_{client,server}` (P2b, k3-Client + k4-Server)

### Zone 2 — Root-Level `test_*.moo` (Ad-hoc / historisch)

38 `.moo`-Dateien im Projekt-Root. Kein Runner, kein `.expected`. Gemischte
Qualitaet: manche sind fruehe Bug-Repros, manche experimentelle Snippets.

| Zweck | owner | CI-Relevanz | phase_b_ziel |
|-------|-------|-------------|--------------|
| Scratchpad-Regressions (Historie) | historisch | keine | `tests/scratchpad/` + Selektion nach `tests/compiler/` |

**Kategorisierung (38 Dateien)**:

**Import-Bug-Regressions (11)** — kandidat fuer Konsolidierung in `compiler/tests/`:
- `test_import_{bug{,2}_main, bug{,2}_modul, doppelt_{a,lib,main}, von_main}`
- `test_mini_import`, `test_modul_{from,main,mathe}`

**Stdlib-Scratchpad (4)**:
- `test_stdlib{,_klein,_norec,_voll}`

**Feature-Scratchpad (9)**:
- `test_alle_features`, `test_constfold`, `test_english`, `test_fmt_input`, `test_framework{,_fail}`, `test_neue_features`, `test_typen`, `test_typwarn`

**Domaene-Scratchpad (10)**:
- `test_3d_{minimal,nolight}`, `test_kern`, `test_mathe_only`, `test_mischmasch`, `test_native`, `test_regex`, `test_unsafe`, `test_vector`, `test_vollständig`

**Nischen (4)**:
- `test_interface`, `test_lernmodus`, `test_oop` (Root-Duplikat zum offiziellen), `test_modul_mathe`

**Empfehlung** Phase B:
1. Jede Datei gegen `compiler/tests/`-Liste pruefen — bei inhaltlicher
   Ueberschneidung **die bessere Version behalten** (Kriterien: `.expected`
   existiert? Runner-faehig? deckt aktuellen Bug ab?).
2. Was echt relevant ist → nach `compiler/tests/` mit `.expected`.
3. Rest → `tests/scratchpad/` (archiviert, kein CI).

### Zone 3 — `compiler/runtime/test_3d.c`

Einzelner C-Level-Smoke fuer 3D-Rendering. Kein Automation-Runner.

| Zweck | owner | CI-Relevanz | phase_b_ziel |
|-------|-------|-------------|--------------|
| Manueller 3D-Smoke | k4 | keine | `tests/runtime/3d/smoke.c` |

### Zone 4 — `/tmp/moo-verify/` (ephemer)

Arbeitsverzeichnis der Koordinatoren-Verifikation. 107+ `.moo`-Dateien aus
A-Block (Kompat-Smokes, Regressions-Aufbau), `.ll`-Dateien, result-logs.

| Zweck | owner | CI-Relevanz | phase_b_ziel |
|-------|-------|-------------|--------------|
| Session-Scratchpad (wird gereged gewechselt) | k2/k3/k4 | keine (temporaer) | **bleibt `/tmp/moo-verify/`** — nicht ins Repo |

**Regel**: Was sich in `/tmp/moo-verify/` bewaehrt hat, wird selektiv nach
`compiler/tests/` mit `.expected` portiert (siehe P3c — k3 hat 6 Tests
portiert).

### Zone 5 — `/tmp/moo-audit/` (ephemer)

Audit-Befunde (k3 + k4 Befunde.md, BERICHT.md, diff-Tabellen). Markdown,
keine Skripte.

| Zweck | owner | CI-Relevanz | phase_b_ziel |
|-------|-------|-------------|--------------|
| Eine Session-Audit-Historie | k2/k3/k4 | keine (temporaer) | **bleibt `/tmp/moo-audit/`** — nicht ins Repo |

### Zone 6 — `tests/` (Top-Level, Python-Transpiler)

| Zweck | owner | CI-Relevanz | phase_b_ziel |
|-------|-------|-------------|--------------|
| Python-Transpiler-Tests (falls vorhanden) | python-only | pflicht fuer Python-Toolchain | `tests/transpiler/` |

### Zone 7 — `tools/fuzz_differential.py`

k2 E3-Differential-Fuzzing: pruft Rust-Compiler vs Python-Transpiler auf
Signatur- und Output-Drift.

| Zweck | owner | CI-Relevanz | phase_b_ziel |
|-------|-------|-------------|--------------|
| Drift-Detektion Rust vs Python | k2 | empfohlen (manuell pro Release) | `tests/differential/` + CI-Workflow |

## Phase-B-Zielstruktur (Vorschlag)

```
tests/
  compiler/            # Zone 1 unveraendert, nur umbenannt
    test_*.moo
    test_*.expected
    run_all.sh
  runtime/
    3d/smoke.c         # Zone 3
  scratchpad/          # Zone 2 (was nicht nach compiler/ portiert wurde)
  transpiler/          # Zone 6 (falls vorhanden)
  differential/        # Zone 7
```

Ephemere Zonen 4 + 5 bleiben unter `/tmp/`.

## CI-Matrix (Stand Phase A)

| Runner | Pflicht | Zone | Command |
|--------|---------|------|---------|
| `compiler/tests/run_all.sh` | **ja** | 1 | `bash compiler/tests/run_all.sh` |
| `.github/workflows/3d-backends.yml` | ja (gl21/gl33), opt (vulkan) | — | cargo build pro Feature |
| `tools/fuzz_differential.py` | empfohlen | 7 | `python3 tools/fuzz_differential.py` |

## Offen fuer Phase B

- Root-Level-Selektion: welche der 38 `test_*.moo` sind echte Regressions,
  welche historischer Scratchpad? (handarbeit, ca. 1 h).
- CI-Hook fuer differential-fuzzer.
- Ephemer-Verzeichnisse aus `.gitignore` bestaetigen — aktuell ohnehin
  `/tmp/...`.
