# Lexer/Parser-Drift-Befunde (Rust vs Python)

Ergebnis von `tools/fuzz_differential.py` gegen `compiler/tests/*.moo`.

## Stand 2026-04-14

- Rust-Compiler: 26/26 OK
- Python-Toolchain: 23/26 OK
- **3 Drift-Fälle** (Rust akzeptiert, Python lehnt ab):
  - `test_bitwise_ops.moo` — Python kennt die Bit-Operatoren `&` `|` `^` `<<` `>>` `~` nicht. Token-SSoT (`spec/tokens.yaml`) markiert sie als `implementations: [rust]`, das ist konsistent.
  - `test_json.moo` — erste Analyse: vermutlich Lexer-/Parser-Unterstützung fehlt für eine bestimmte Syntax. Zu untersuchen.
  - `test_lists.moo` — analog, Detail-Analyse offen.

Außerdem: `beispiele/showcase.moo` → Python-TIMEOUT (>10s). Vermutlich Endlos-Schleife im Python-Parser bei bestimmter Eingabe. HIGH — separates Ticket.

## Phase 2 (TBD)

Token-Stream-Vergleich pro Datei: beide Lexer laufen lassen, `TokenType`-Sequenz diffen. Das identifiziert die Drift-Ursache präzise (Literal-Parse-Unterschied vs Keyword-Mapping vs Operator-Präzedenz).

AST-Vergleich: deutlich aufwendiger, erst nach Token-Stream-Differential sinnvoll.

## Nächste Schritte

1. Python-Transpiler: Bit-Operatoren als "Ablehnen mit klarer Fehlermeldung" statt Traceback (benötigt Lexer-Eintrag mit `status: legacy` → Fehler statt Crash).
2. `showcase.moo`-Timeout debuggen (Endlos-Schleife finden).
3. `test_json.moo` und `test_lists.moo` Detail-Analyse.
