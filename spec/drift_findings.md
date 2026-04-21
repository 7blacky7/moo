# Verified-Fixed Bugs

## Top-Level-Variablen in Funktionen (Synapse-Thought `23c1749b`, 2026-04-11)

**Symptom (Thought-Text koordinator2, 2026-04-11 10:14):**
`setze db auf db_verbinde(...)` am Top-Level, `db_abfrage(db, ...)` in einer
Funktion → `"Variable 'db' nicht gefunden"`.

**Fix:** Commit `6b18fe8` (2026-04-11 09:57:42, Moritz Kolar) —
`fix(compiler): Globale Variablen in Funktionen sichtbar`. Top-Level-
Assignments werden als LLVM-Globals (`__moo_g_<name>`) registriert;
`load_var`/`store_var` fallen auf `self.globals` zurueck (codegen.rs
Z. 157–176, 405–452).

**Pikant:** Der Thought wurde 17 Minuten **nach** dem Fix-Commit
geschrieben — vermutlich wurde der Fix vom Reporter noch nicht gepullt.
Bug ist seitdem geschlossen, verifiziert per Regressions-Tests:
- `compiler/tests/test_global_in_func.moo` (Lesen aus Funktion)
- `compiler/tests/test_global_counter.moo` (Schreiben/Counter-Pattern)

Beide grün.

## First-Class Funktionen + Closures (Synapse-Thought `23c1749b`, 2026-04-11)

**Symptom:** `starte(worker, 42)` → `"Variable 'worker' nicht gefunden"`.
`(x) => x * 2` als Wert an Thread → Segfault (exit 139).

**Fix:** Zwei Commits im Branch `feat/first-class-functions`:
1. `c2b8864` — Funktionsnamen als First-Class-Values via `moo_func_new`
   + `load_var`-Fallback auf `module.get_function`.
2. `2b7668b` — Closure-Lambdas via Trampoline-Dispatch. MooFunc
   erweitert um `captured` + `n_captured`; Codegen emittet
   `__lambda_N_tramp(MooFunc* env, ...)` der die Captures via
   `moo_func_captured_at` auspackt. `moo_thread_spawn` +
   `moo_event_emit` unterscheiden plain/closure Call-Konvention.

Verifiziert durch Tests (siehe unten).

---

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
