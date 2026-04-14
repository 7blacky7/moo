# Phase-B Progress

Laufend aktualisiert von k2 (Lead/Moderator). Einzige verbindliche Quelle für den Gesamtstand von Phase B.

| Welle | Scope | Owner | Status | Commits | Notes |
|---|---|---|---|---|---|
| W1 | Root-/Artefakt-Hygiene | k2 | **done** | `8e060a3` | `.gitignore`-Policy-Kommentar, `scratch/` mit README, `spec/audits/2026-04-14/` Archiv |
| W2.1 | Root-Test-Triage | k2 | **done** | `317ce48` | `spec/tests_phase_b_selection.md` (40 Root-`.moo`) |
| W2.2 | Root-Test-Moves | k3 | **done** | `b9aadad`, `83d6fcc`, `6255b03`, `c66106f`, Einzel-A + B-Batch + `c3ae38f` (3D + native) + test_oop_demo + test_vollständig = 11 Commits | Tests: 19 bestanden / 0 fehlgeschlagen nach den Moves. `.expected`-Nachzüge als separate Commits. |
| W2.3 | CI-/Pfad-Schutz | k2 | **done** | (Grep-One-Liner) | `grep -rn "test_.*\.moo" .github/ compiler/tests/run_all.sh docs/` — 0 Root-Pfade in CI. |
| W3 | Beispiele-Reorganisation | k4 | **open** | — | Auf Basis `spec/examples_taxonomy.md`; Kandidaten-Top-25 mit Phase-B-Zielpfaden; kleine Batches. |
| W4 | Sichtbarkeit / Feinschliff / Abschluss | k2 + Team | **open** | — | README/ARCHITECTURE-Links nachziehen, eventuelle Soft-Deprecations in `MOVED.md`-Platzhaltern, Abschluss-Aggregat. |

## Schlüsselfakten

- **Root `test_*.moo` = 0** nach W2.
- **Keine Breaking Changes** in Phase A oder B1/B2 bisher.
- **Gate** (kleine Commits, lokal grün, keine Mass-Moves ohne Spec-Rückendeckung) durchgängig eingehalten.

## Noch offen

- W3 (Beispiele-Moves) wartet auf ChatGPT-Anweisung bzw. k4-Start.
- W4 kommt am Ende — Aggregat-Post "fertig / verschoben / bewusst vertagt / noch riskant".

## Known-Followups (nicht Phase-B-blockierend)

- E1 Sprite/Chunk-Slot-Auto-Free (atexit-Design-Frage).
- Python-Parser-Endlosschleife auf `beispiele/showcase.moo` (aus `tools/fuzz_differential.py` E3-Lauf).
- Differential-Fuzzer Phase 2 (Token-Stream-Diff statt nur Build-OK).
