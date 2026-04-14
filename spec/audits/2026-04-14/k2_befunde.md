# Audit k2: TODOs/FIXMEs + Rust/Python-Drift

## (1) TODO/FIXME/HACK/XXX in *.rs / *.c / *.moo

Quelle: code_intel search. FIXME/HACK/XXX haben keine Code-Treffer.

### HIGH (verhalten-relevant)

- **compiler/src/codegen.rs** — `// Bit-Operatoren: TODO — nutzen vorerst die logischen Ops als Fallback` und `BinOp::RShift => self.rt.moo_rshift, // TODO: echte Bit-Ops`. **Wirkung:** Bitweise Ops (`&`, `|`, `^`, `<<`, `>>`) sind im Runtime zwar als moo_bitand/bitor/bitxor/bitnot/lshift/rshift deklariert — aber der Codegen setzt sie teils auf die *logischen* Operatoren (moo_and/or) ab, je nach Branch-Pfad. Kann zu falschen Ergebnissen führen. Test fehlt.
- **compiler/runtime/moo_wasm.c** — `moo_pow(...) { return moo_number(0); } /* TODO */`. Potenz-Operator im WASM-Target liefert **immer 0**. Wer WASM-Output kompiliert und `**` nutzt → stilles falsches Ergebnis.

### MED (feature-Lücke, kein Silent-Bug)

- **compiler/runtime/moo_3d_gl33.c** — `GL33 ambient ist im Shader hardcoded (0.15) — TODO: Uniform`. `welt_ambient(w, wert)` hat daher auf GL33 keinen Effekt.
- **compiler/runtime/moo_3d_vulkan.c** — analoger TODO, gleicher Effekt auf Vulkan.
- **compiler/runtime/moo_world.c** — `Sonnen-Position wird in update() relativ zum Spieler gesetzt. TODO: Custom-Position wenn gewuenscht`. `welt_sonne()` Custom-Position-Override fehlt.

### LOW (Doku / UI)

- **beispiele/minesweeper.moo** — Kommentar `TODO: braucht rechte Maustaste`. Relates to existing Synapse-Thought 20d64530 (maus_gedrueckt_rechts fehlt).
- `beispiele/todo.moo`, `beispiele/http_api.moo` — "Todo" im Titel/Inhalt, keine Code-TODOs.

## (4) Drift Rust vs Python post-Token-SSoT

```
[spec]   96 Token-Einträge in spec/tokens.yaml
[rust]   122 String-Varianten in compiler/src/tokens.rs
[python] 100 String-Varianten in src/moo/tokens.py
Summary:  active=93, legacy=3, soft-keywords=8
OK — alle YAML-Varianten in ihren Zielimplementierungen vorhanden.
```

`check_tokens.py` = **GRÜN**: kein in YAML deklarierter Token fehlt.

Differenzen dokumentiert (Status klar):

- Rust hat **22 Tokens mehr** als Python: Bitwise-Ops (`BitAnd/Or/Xor/Not/LShift/RShift`), `Guard/Defer/Where/Select/Order/QueryFrom/Interface/Implements/Parallel/Precondition/Postcondition/Unsafe/Test/Expect/Question/ThinArrow/At`. Alle sind in spec/tokens.yaml mit `implementations: [rust]` markiert — **bewusster Zustand**, nicht Drift.
- Python hat **Async/Await + `fg`/`wa` / `fuer`** — status: legacy, Rust-Seite lehnt ab. **Bewusster Zustand.**
- Es gibt keinen Fall wo eine Variante in `implementations: [rust, python]` nur in einer Seite existiert.

**MED:** Keine Codegen-/Parser-Drift gefunden. Kompatibilitäts-Matrix unterhalb der Token-Ebene (z.B. Parser-Reguläre-Klammern, Indent-Verhalten, String-Escapes) **nicht** geprüft — aus Zeitgründen zurückgestellt. Empfehlung: Differential-Fuzzing in einer späteren Audit-Runde.

## Zusammenfassung

- 2 HIGH-Befunde (Bit-Ops-Drift, WASM moo_pow-Stub).
- 3 MED-Befunde (Ambient-Uniform in zwei 3D-Backends, welt_sonne Custom-Pos).
- 1 LOW-Befund (minesweeper-Doku-TODO + Folge-Thought).
- Token-SSoT-Drift: **keine**; Stand dokumentiert und CI-abgedeckt.
