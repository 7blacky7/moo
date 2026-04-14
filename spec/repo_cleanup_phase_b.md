# Repo-Cleanup Phase B — Kontrollierter Umbauplan

**Status:** Vorschlag. Keine Umsetzung in dieser Phase. Umsetzung erst nach
Review + Abnahme durch den User.

Dieses Dokument benennt die lauten Stellen im Repo-Root und schlägt eine
Reihenfolge vor, in der sich die Architektur-Schichtung (L0–L4, siehe
ChatGPT-Architekturplan) physisch im Repo sichtbar machen lässt, ohne
bestehende Arbeitsabläufe zu brechen.

## Aktueller Ist-Zustand (Repo-Root, 2026-04-14)

`ls ~/dev/moo/` zeigt **66 Einträge** im Root. Davon **40 `.moo`-Dateien**,
darunter 28 mit Prefix `test_*.moo` und 2 Freitext-Skripte (`beispiel.moo`,
`example.moo`). Plus loose Artefakte wie `test_native.ll`,
`test_mini_import.ll`, `test_alle`, `test_en`, `test_mix`, `test_native`,
`test_neu`, `test_oop` (ohne Endung → Binaries), `adventure_save.json`.

Sowie die bestehenden Unterverzeichnisse, die bereits klar sind:
`compiler/`, `src/`, `stdlib/`, `beispiele/`, `docs/`, `editors/`,
`scripts/`, `spec/`.

## Zielbild (Phase B)

```
/              ← nur noch: README, LICENSE, pyproject.toml, .gitignore,
                 DEPENDENCIES.md, Top-Level-Konfig, CI-Config
/compiler/     ← Rust-Compiler + C-Runtime + tests
/src/moo/      ← Python-Toolchain (LSP/Transpiler/Formatter)
/stdlib/       ← moo-Stdlib-Module
/beispiele/    ← nach Taxonomie (siehe spec/examples_taxonomy.md):
   quickstart/, showcase/, stress/,
   domain/{web,db,game,system}/
/scratch/      ← NEU: Freitext-/Ad-hoc-/Scratchpad-Dateien aus dem
                 Root (`beispiel.moo`, `example.moo`, alte
                 `test_*.moo` die keine Tests sind — siehe
                 spec/tests_taxonomy.md). Gitignore die Binaries.
/spec/         ← Single-Source-of-Truth Spezifikationen (tokens.yaml,
                 soft_keywords.md, followup-tickets.md,
                 runtime_layers.md, game_module_boundary.md,
                 public_vs_internal_api.md, examples_taxonomy.md,
                 tests_taxonomy.md, repo_cleanup_phase_b.md)
/docs/         ← Benutzer-Doku unverändert
/editors/      ← VSCode-Extension + LSP-Client unverändert
```

## Reihenfolge (keine Kette brechen)

1. **Spec-Phase abschließen (B-Block + C-Block, laufend).** Bevor
   irgendwas bewegt wird, muss jedes Root-`.moo` in `tests_taxonomy.md`
   eine Zielzone haben (kompilertest / Ad-hoc / Scratch / verschrottbar).

2. **`.gitignore` aktualisieren.** `*.ll`, `test_alle`, `test_en`,
   `test_mix`, `test_native`, `test_neu`, `test_oop` (Binaries), plus
   `adventure_save.json` (Laufzeit-Save-Datei) — additiv, keine Moves.

3. **Neues `scratch/`-Verzeichnis anlegen.** Leer, mit README:
   "Ad-hoc-Skripte die nicht zu `compiler/tests` oder `beispiele`
   passen. Keine CI-Relevanz."

4. **Einzelne Dateien verschieben — immer in kleinen Paketen, je
   1 Commit pro Kategorie.** Reihenfolge:
   - (a) `test_*.moo` die echte Regressions-Tests sind → `compiler/tests/`
     (bereits dort laufende Struktur, `.expected` nachliefern wo nötig).
   - (b) `test_*.moo` die Demos/Ad-hoc sind → `scratch/` oder
     `beispiele/showcase/` je nach Taxonomie.
   - (c) `beispiel.moo`, `example.moo` → `scratch/` (oder
     `beispiele/quickstart/` falls tatsächlich kanonische Beispiele).
   - (d) `.ll`-Dateien: prüfen ob produktionsrelevant; wenn ja
     → `compiler/tests/fixtures/`, sonst löschen.

5. **CI-Pfade aktualisieren.** Wenn `compiler/tests/run_all.sh` oder
   Workflows auf Root-Dateien referenzieren, Pfade anpassen —
   **separater Commit** nach jeder Move-Gruppe, damit Git-Blame
   nachvollziehbar bleibt.

6. **Beispiele-Subdirs** (`beispiele/web/`, `beispiele/db/`,
   `beispiele/game/`, …) nur anlegen wenn `examples_taxonomy.md` das
   rechtfertigt. Mass-Moves nur nach Doku-Freeze.

## Kompatibilitäts-Maßnahmen

- **Doku-Hinweise an lauten Stellen jetzt:** `README.md` mit einer
  kurzen "wo ist was?"-Sektion, die die Ist-Struktur beschreibt und
  auf `spec/repo_cleanup_phase_b.md` verlinkt. Kein Code-Impact.
- **Keine Symlinks** im Repo — bei Move erwarten wir saubere Git-Historie
  (`git mv`) plus Deprecation-Hinweis in einem `MOVED.md`-Platzhalter
  am Ursprungsort, der **eine Version lang** existiert.
- **Kein Path-Shim in Code.** Wenn ein Pfad wegziegeht, wird der Code
  aktualisiert, nicht per Laufzeit-Fallback umgeleitet.

## /tmp-Doppelungen (Audit-Artefakte)

Während der Konsolidierungs-Wellen sind folgende Zwischen-Ordner
entstanden:

- `/tmp/moo-verify/` — Ad-hoc-Smoketests aus Verifikations-Runde. **In
  Repo aufnehmen?** Nein. Diese Dateien sind pro-Session, keine
  Regression. `compiler/tests/test_*` sind die kanonische Alternative.
- `/tmp/moo-audit/` — Audit-Berichte + Befunde. **Empfehlung:** in
  `spec/audits/2026-04-14/` einchecken, damit die Entscheidungen
  rückverfolgbar bleiben. Binärdaten (`*_ir.ll`) rausfiltern.
- Keine Ordner unter `tmp/` im Repo-Root — der aktuelle `tmp/`-Eintrag
  in `ls` ist nur ein Link zu `/tmp` (nicht committet).

## Was NICHT in Phase B angefasst wird

- `compiler/target/` — Cargo-Build-Cache, bleibt gitignored.
- `compiler/runtime/glad/` — Vendored OpenGL-Bindings, bleibt.
- `docs/` — User-Doku bleibt wie sie ist, nur Verlinkungen erweitern.
- `editors/vscode/` — VSCode-Extension bleibt.
- `beispiele/assets/` — Sprite-Tilesets bleiben.

## Risiken & Gegenmaßnahmen

| Risiko | Gegenmaßnahme |
|---|---|
| CI bricht weil Root-Datei weg ist | Move + CI-Pfad-Update in einem Commit, lokal grün vor Push. |
| User-Skripte referenzieren Root-`.moo` | Deprecation-Hinweis im `MOVED.md`, eine Version lang. |
| Git-Blame geht verloren | `git mv` statt `rm + add`, in kleinen Paketen. |
| Entwickler macht neue Root-Dateien | `.gitignore` + README-Hinweis "Root ist dokumentiert". |
| Spec-Dateien driften von Realität weg | CI-Check erweitern: `spec/tokens.yaml` hat schon einen; für `examples_taxonomy`, `tests_taxonomy` analoges Skript (Phase C). |

## Zusammenfassung

Phase B ist **eine Sequenz kleiner, commit-granularer Moves**, jede
geführt von einer Spec-Datei aus dem B- und C-Block. Keine
Mass-Operation, keine Breaking Changes.

Nach Abnahme durch den User kann die Move-Sequenz in 1–2 Tagen
erledigt werden.
