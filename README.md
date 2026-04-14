# moo

Eine deutschsprachige Programmiersprache mit nativer LLVM-Kompilierung, C-Runtime und Python-Toolchain. Syntax lesbar auf Deutsch und Englisch gleichwertig.

```moo
funktion fibonacci(n):
    wenn n <= 1:
        gib_zurück n
    gib_zurück fibonacci(n - 1) + fibonacci(n - 2)

zeige fibonacci(20)
```

## Wo ist was?

- **Bauen & ausführen:** siehe `DEPENDENCIES.md`, dann
  `cd compiler && cargo build --release` und
  `./compiler/target/release/moo-compiler run <datei>.moo`.
- **Sprache lernen:** `docs/tutorial.md` und `docs/lernen.md`.
- **Referenz aller Builtins:** [moolang-docs](https://7blacky7.github.io/moolang-docs/).
- **Beispiele:** `beispiele/` mit Zonen `domain/{web,db,game,system}/` plus lose Showcases (Taxonomie in `spec/examples_taxonomy.md`).
- **Architektur in 5 Minuten:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Wie ist das Projekt strukturiert?

```
compiler/   Rust-Compiler (compiler/src) + C-Runtime (compiler/runtime) + Tests
src/moo/    Python-Toolchain: LSP, Formatter, Transpiler
stdlib/     moo-Stdlib-Module (mathe, liste, text, ...)
beispiele/  Lauffähige .moo-Programme (130+ Samples; Zonen: domain/{web,db,game,system})
scratch/    Ad-hoc-Skripte, nicht CI-relevant (siehe scratch/README.md)
docs/       Benutzer-Doku (mkdocs-Material)
editors/    VSCode-Extension + LSP-Client
spec/       Spezifikation (Single Source of Truth)
scripts/    Hilfsskripte
```

Details: siehe [ARCHITECTURE.md](ARCHITECTURE.md).

## Status

Alpha — Name und Dateiendung sind nicht final. Der Sprachkern ist
stabil, die Runtime wird kontinuierlich konsolidiert. Siehe `spec/` für
laufende Arbeitsstränge (Tokens-SSoT, Runtime-Layers, Test-Taxonomie,
Beispiel-Taxonomie, Phase-B-Cleanup-Plan).

## Root-Inhalt — was ist dauerhaft, was ist Altlast?

Dauerhaft im Root:
`README.md`, `ARCHITECTURE.md`, `DEPENDENCIES.md`, `LICENSE` (folgt),
`pyproject.toml`, `.gitignore`, `.github/`.

Alles andere ist entweder Code-Verzeichnis (`compiler/`, `src/`,
`stdlib/`, `beispiele/`, `docs/`, `editors/`, `scripts/`, `spec/`,
`scratch/`). Die Phase-B-Aufräumung (W1–W3) ist **abgeschlossen** —
Root ist frei von losen `test_*.moo`-Dateien. Bitte weiterhin keine
neuen losen `test_*.moo` im Root anlegen (siehe
`scratch/README.md`). Details zum Verlauf: `spec/phase_b_progress.md`.

## Lizenz

Wird vor v1.0 festgelegt. Bis dahin: Open Source, Details im Projekt.
