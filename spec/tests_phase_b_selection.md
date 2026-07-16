# Phase-B-Selektion — Root-`test_*.moos` Triage

**Scope:** Alle 40 losen `.moos`-Dateien im Repo-Root (38 `test_*.moos` plus `beispiel.moos`, `example.moos`).

**Ziel:** Vorsortierung in 3 Listen, damit k3 in Phase B W2 reibungslos moven kann. Diese Datei ist der **Arbeitsauftrag** für W2.2.

**Klassifizierungs-Regel:**

- **A = `compiler/tests/`** — wenn: deterministisch ausführbar, Output pro-Ausführung konstant, ein Sprachfeature oder Runtime-Verhalten prüfend. `.expected` wird dabei evtl. erst erzeugt, aber die Datei ist Kandidat.
- **B = `scratch/`** — wenn: Ad-hoc, Demos, Showcases ohne `.expected`-Eignung, Parallel-Vorhandensein zu `beispiele/showcase/`-Kandidaten, oder semantisch Scratchpad.
- **C = offen** — wenn: Import-/Modul-Tupel (mehrere `.moos` gehören zusammen), 3D/Grafik (headful, keine CI-Variante), oder unklar ob produktions-tauglich.

---

## Liste A — `compiler/tests/` (Regressions-Kandidaten)

| Datei | Warum A | `.expected` schreibfertig? |
|---|---|---|
| `test_kern.moos` | Testet Builtins (`zahl`, `typ_von`, `länge`, `zeit`, `warte`) deterministisch | ja |
| `test_constfold.moos` | Constant-Folding-Verhalten, sprachspezifisch | ja |
| `test_typen.moos` | Typ-System | ja |
| `test_typwarn.moos` | Typ-Warnings (kann `-W`-Output prüfen, ggf. schwieriger) | wenn Warn-Output stabil |
| `test_interface.moos` | Interface/Trait-Feature | ja |
| `test_oop.moos` | Klassen/Vererbung | ja |
| `test_regex.moos` | Regex-Domäne | ja |
| `test_english.moos` | EN-Keywords parallel zu DE | ja |
| `test_lernmodus.moos` | Lang-Form-Keywords (`setze_variable`, `zeige_auf_bildschirm`) | ja |
| `test_vector.moos` | Listen-/Vektor-Verhalten | ja |
| `test_mathe_only.moos` | Import `mathe` isoliert | ja |
| `test_stdlib.moos` | Import `mathe` + `liste` | ja |
| `test_stdlib_klein.moos` | Kleiner Stdlib-Import | ja |
| `test_stdlib_voll.moos` | Alle 4 Stdlib-Module | ja |
| `test_stdlib_norec.moos` | Spezialfall: nicht-rekursiv | ja |
| `test_framework.moos` | `teste/erwarte`-Block-Demo, deterministisch | ja (teste-Output ist stabil) |
| `test_framework_fail.moos` | Erwartet FAIL-Output | ja (wenn FAIL stabil formatiert) |
| `test_unsafe.moos` | `unsicher`-Keyword | ja |

**18 Einträge → `compiler/tests/`**

## Liste B — `scratch/` (Ad-hoc/Showcase)

| Datei | Warum B |
|---|---|
| `test_alle_features.moos` | Feature-Demo-Spiel, wenig Assert-Charakter |
| `test_neue_features.moos` | Feature-Inkubator (noch in Entwicklung) |
| `test_mischmasch.moos` | Sammlung verschiedener Features, keine klare Zone |
| `test_fmt_input.moos` | Formatter-Eingabe (für `moo fmt`-Manuell-Tests) |
| `beispiel.moos` | Generisches Showcase, könnte in `beispiele/showcase/` |
| `example.moos` | EN-Variante eines Quickstarts, könnte in `beispiele/quickstart/` |

**6 Einträge → `scratch/`** (oder `beispiele/showcase/` bzw. `quickstart/` falls k4 sie in C1-Taxonomie als Kandidaten markiert hat.)

## Liste C — offen (braucht k3+k4-Entscheidung)

**Import-/Modul-Tupel** (mehrere `.moos` hängen zusammen):

| Tupel | Dateien | Empfehlung |
|---|---|---|
| Import-Bug 1 | `test_import_bug_main.moos`, `test_import_bug_modul.moos` | Beide → `compiler/tests/` wenn der Bug gefixt ist und der Tupel als Regression bleibt; sonst → `scratch/`. Decision nach `git log` auf Bug-Fix prüfen. |
| Import-Bug 2 | `test_import_bug2_main.moos`, `test_import_bug2_modul.moos` | Analog. |
| Import-Doppelt | `test_import_doppelt_a.moos`, `test_import_doppelt_lib.moos`, `test_import_doppelt_main.moos` | → `compiler/tests/` als Regression für "Doppel-Import-Behandlung". |
| Import-Von | `test_import_von_main.moos` | Einzeldatei, testet `importiere ... aus ...`. → A. |
| Modul-Test | `test_modul_main.moos`, `test_modul_mathe.moos`, `test_modul_from.moos` | Tupel → A, Regression für Modul-System. |
| Mini-Import | `test_mini_import.moos` | → A. |

**13 Einträge → überwiegend A, aber als Tupel behandeln** (ein Commit pro Tupel).

**3D/Grafik-headful** (CI-problematisch):

| Datei | Empfehlung | Finale Entscheidung + Commit |
|---|---|---|
| `test_3d_minimal.moos` | → `scratch/` (headful) | ✓ `scratch/` — k4 bestätigt (warte(5000), kein Guard-Pattern). Move: k3 c3ae38f. |
| `test_3d_nolight.moos` | Analog | ✓ `scratch/` — k4 bestätigt (warte(3000)). Move: k3 c3ae38f. |
| `test_native.moos` | Quelle → A, wenn IR-Check stabil | ✓ `compiler/tests/` + `.expected` — deterministisch (10 Zeilen Output), k4 verifiziert. Move: k3 c3ae38f. `test_native.ll` entfernt (gitignored). |
| `test_vollständig.moos` | Unicode-Name prüfen | ✓ `compiler/tests/` + `.expected` — deterministisch, k2 verifiziert. Move + expected: k3 (W2-Abschluss). |
| `test_oop.moos` | Konflikt mit existierendem `compiler/tests/test_oop.moos` | ✓ Rename nach `scratch/test_oop_demo.moos`. Root ist 17-Zeilen-Demo, compiler/tests ist 31-Zeilen-Assert-Variante — nicht dieselbe Intention, kein Merge. Move: k3 (W2-Abschluss). |

**5 Einträge — alle entschieden und umgesetzt.**

---

## Zusammenfassung

- **A → `compiler/tests/`**: 18 Einzel + 13 Tupel + 2 aus Reste-Liste (`test_native`, `test_vollständig`) = 33
- **B → `scratch/`**: 6 B-Batch + 3 Reste (`test_3d_minimal`, `test_3d_nolight`, `test_oop_demo`) = 9
- **C → offen**: 0 — alle 40 Root-Dateien haben einen Zielort und sind **umgezogen** (Stand: W2-Abschluss, k3 11 Commits).

## Arbeitsauftrag für k3 (W2-Moves)

1. **Tupel zuerst**: Import-Bug-1, Import-Bug-2, Import-Doppelt, Modul-Test — je 1 Commit mit `git mv *.moos compiler/tests/`. `run_all.sh` lokal grün vor Push.
2. **Einzel-A**: die 18 aus Liste A in 2–3 Commits zusammenfassen (thematisch: Language-Features, Stdlib, Framework). Für jede fehlende `.expected` ein separater Folge-Commit, nachdem die Datei einmal lokal ausgeführt wurde.
3. **B-Batch**: `git mv test_alle_features.moos test_neue_features.moos test_mischmasch.moos test_fmt_input.moos beispiel.moos example.moos scratch/`. 1 Commit.
4. **C-Einzelfälle**: vor Move Rücksprache im Channel. 3D-Tests erst nach k4-Check ob `--emit-ir`-Only reicht.

## CI-Schutz (W2.2)

Vor jedem Move-Commit:

```
grep -rn "test_.*\.moos" .github/ compiler/tests/run_all.sh docs/ 2>/dev/null | grep -v target/
```

Falls ein Pfad auf Root-`test_*.moos` zeigt: **vorher** Pfad-Update in eigenem Commit, **dann** erst Move.
