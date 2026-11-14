# Phase-B Progress

Verbindliche Quelle für den Gesamtstand von Phase B.

| Welle | Scope | Status | Commits | Notes |
|---|---|---|---|---|
| W1 | Root-/Artefakt-Hygiene | **done** | `8e060a3` | `.gitignore`-Policy-Kommentar, `scratch/` mit README, `spec/audits/2026-04-14/` Archiv |
| W2.1 | Root-Test-Triage | **done** | `317ce48` | `spec/tests_phase_b_selection.md` (40 Root-`.moos`) |
| W2.2 | Root-Test-Moves | **done** | `b9aadad`, `83d6fcc`, `6255b03`, `c66106f`, Einzel-A + B-Batch + `c3ae38f` (3D + native) + test_oop_demo + test_vollständig = 11 Commits | Tests: 19 bestanden / 0 fehlgeschlagen nach den Moves. `.expected`-Nachzüge als separate Commits. |
| W2.3 | CI-/Pfad-Schutz | **done** | — | 0 Root-Pfade in `.github/`, `compiler/tests/run_all.sh`, `docs/`. |
| W3.1 | Beispiele WEB | **done** | 3 Commits bis `1e8cec0` | 5 Dateien → `beispiele/domain/web/`. CI 0 Brüche. |
| W3.2 | Beispiele DB | **done** | 3 Commits bis `bccdbd5` | 8 Dateien → `beispiele/domain/db/`. Alte `chat_server.moos` bleibt als Legacy-Referenz. |
| W3.3 | Beispiele SYSTEM | **done** | 2 Commits | `kernel_hello`, `x86dis`, `elf_reader`, `tar_reader` → `beispiele/domain/system/`. |
| W3.4 | Beispiele GAME | **done** | `c22bdda`, `91f8c9c`, `7eeaed8`, `79b6b77` | 17 Dateien → `beispiele/domain/game/{world,testscreens}/`. C2 enthielt Workflow-Pfad-Update für `3d-backends.yml` (Move + CI in einem Commit). |
| W3.5 | MOVED-Hinweise | **done** | — | Nicht nötig: keine inneren `beispiele/`-Cross-Refs. Optional: `beispiele/README.md` mit Sektions-Übersicht (separat, nicht blockierend). |
| W4.1 | phase_b_progress.md finalisieren | **done** | `80188db` | Tabelle pflegt Status pro Welle. |
| W4.2 | Wahrheitsabgleich README/ARCHITECTURE/repo_cleanup | **done** | `3f8b252` | Doku spiegelt jetzt die Realität nach W2/W3-Moves wider. |

## Schlüsselfakten

- **Root `test_*.moos` = 0** nach W2.
- **Keine Breaking Changes** in Phase A oder B1/B2 bisher.
- **Gate** (kleine Commits, lokal grün, keine Mass-Moves ohne Spec-Rückendeckung) durchgängig eingehalten.

## Status

- Alle Phase-B-Wellen (W1–W4) abgeschlossen.

## Known-Followups (nicht Phase-B-blockierend)

- E1 Sprite/Chunk-Slot-Auto-Free (atexit-Design-Frage).
- Python-Parser-Endlosschleife auf `beispiele/showcase.moos` (aus `tools/fuzz_differential.py` E3-Lauf).
- Differential-Fuzzer Phase 2 (Token-Stream-Diff statt nur Build-OK).
