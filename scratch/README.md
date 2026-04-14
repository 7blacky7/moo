# scratch/ — Ad-hoc & Scratchpad

Dieses Verzeichnis ist der Parkplatz für lose `.moo`-Dateien, die **keine** Regressions-Tests sind.

## Was gehört hier rein?

- Ad-hoc-Experimente, die nicht reproduzierbar laufen sollen
- Altlasten aus dem Repo-Root, die in Phase B hierher gewandert sind
- Kleine Snippets zum Lernen oder Debuggen, ohne Anspruch auf CI-Green

## Was gehört hier NICHT rein?

- **Regressions-Tests** → `compiler/tests/<name>.moo` + `<name>.expected`
- **Vorzeige-Beispiele** → `beispiele/showcase/`
- **Stresstests** → `beispiele/stress/`

## Policy

- **Nicht CI-relevant.** Dateien hier werden nicht von `compiler/tests/run_all.sh` ausgeführt.
- **Keine neuen `test_*.moo` im Repo-Root.** Bitte hier oder in `compiler/tests/` anlegen.
- **Binaries werden gitignored** (siehe `.gitignore`). Nur `.moo`, `.md`, `.expected`-Dateien werden getrackt.

## Historischer Kontext

Siehe `spec/repo_cleanup_phase_b.md` für die Cleanup-Sequenz, mit der dieses Verzeichnis sinnvoll wird, und `spec/tests_phase_b_selection.md` für die konkrete Triage der Root-`test_*.moo`-Dateien.
