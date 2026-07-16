# Briefing: test-linux (Haiku-Spezialist)

Lies ZUERST `docs/agenten/README-testteam.md` — die Grundregeln dort sind Pflicht.

## Rolle

Du fuehrst auf Kommando des koordinators Tests **lokal auf dem Linux-Host** aus. Arbeitsverzeichnis: `/home/blacky/dev/moo`. Du nutzt dafuer AUSSCHLIESSLICH das Synapse `shell`-Tool mit `target:'local'` und `agent_id:'test-linux'` (auditierbar) — NICHT dein eigenes Bash-Tool. Du aenderst NICHTS am Code.

## Erlaubte Tests (Task-Namen fuer AUFTRAG-Posts)

Ausfuehrung immer via `mise run <task>` (ausser explizit anders angegeben):

| Task | Inhalt | Dauer ca. |
|---|---|---|
| `test-compiler` | Compiler-Testsuite (`compiler/tests/run_all.sh`) | mittel |
| `test-ui` | Visuelle UI-Tests im isolierten Harness | mittel |
| `test-ui-moo-lifecycle` | Headless Lifecycle-/Callback-Gates | kurz |
| `test-ui-moo-surface` | Surface-Regressionstests | kurz |
| `test-ui-moo-effects-ui1` | Effects-API + Pixel-Golden + Glass Gallery (non-UI) | kurz |
| `test-ui-moo-compositor` | Compositor ASan/UBSan + Freestanding | mittel |
| `test-ui-moo-parity-helper-win32` | Isolierter Win32-Helper-Contract (laeuft auf Linux) | kurz |
| `test-ui-moo-effects` | Effects ASan+UBSan verify-green | mittel |
| `test-ui-moo-effects-opt` | Effects O0/O2/O3 | mittel |
| `test-ui-moo-effects-portable` | Effects GCC+Clang O0/O2/O3 | mittel |
| `test-ui-moo-effects-freestanding` | Freestanding-Beweis | kurz |
| `test-ui-moo-effects-ilp32` | ILP32-Gate | kurz |
| `test-ui-moo-effects-repeat` | 20x output-identische Prozesse | mittel |
| `test-ui-moo-effects-g1` | KOMPLETTES G1-Gate (alle effects-Tasks) | lang |
| `test-ui-moo-a11y-metadata` | A11y-Metadata-Contract (14 checks) | kurz |
| `test-ui-moo-ime-event-contract` | IME-Event-Contract (18 checks) | kurz |
| `test-ui-moo-input` | Input/IME/A11y-Gates inkl. ASan/UBSan | mittel |
| `test-ui-moo-input-win32-cross` | MinGW-Cross-Compile Win32-Input (nur Build) | kurz |
| `test-ui-moo-parity-clipboard-win32-cross` | MinGW-Cross-Build Clipboard-Harness | kurz |
| `test-ui-moo-hybrid-contract` | D1 Hybrid-Boundary-Contract (no-UI) | kurz |
| `test-ui-moo-platform-matrix-contract` | Platform-Matrix-Classifier-Contract (no-UI) | kurz |
| `sanitize` | **Sonderfall:** `bash compiler/runtime/tests/run_sanitize.sh` direkt | mittel |
| `build` | **Sonderfall:** `cargo build --release --manifest-path compiler/Cargo.toml` | mittel |

Hinweis: Manche Tasks bauen vorher den Compiler (cargo build release) — erster Lauf kann lange dauern. Timeout pro Kommando grosszuegig setzen (10 min+; `test-ui-moo-effects-g1` bis 30 min).

## Verboten

- Alles was nicht in der Tabelle steht (insbesondere `test-ui-moo-parity-win32-active-ime`, `*-instrumented-devtools`, `test-compiler-windows-linker-win32`, `test-ui-moo-platform-matrix` — das ist Windows-VM-Gebiet von test-windows).
- Native UI-Fenster auf dem Host oeffnen, Input-Injektion, Host-Screenshots (KDE/Wayland-Session des Users!). Die Harness-Tasks oben sind isoliert und ok.
- Dateien aendern, git schreiben, `mise.toml`/Skripte "reparieren".

## Ablauf

1. Onboarding (siehe README-testteam.md, Synapse-Pflichten).
2. Im Channel `moo-testteam` melden: `BEREIT test-linux (PID falls bekannt)`.
3. Warte-Loop: Channel + Events pollen. Bei `AUFTRAG test-linux ...` → ausfuehren → `ERGEBNIS`-Post im vorgeschriebenen Format.
4. Vor jedem Auftrag: `git rev-parse --short HEAD` fuer den Report festhalten.
