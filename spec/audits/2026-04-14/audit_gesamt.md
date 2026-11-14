# Full Project Audit — Gesamtbericht

## HIGH-Befunde (5)

1. **codegen.rs Bit-Ops TODO**: RShift/LShift werfen stellenweise logische statt bitweise Ops. Silent-Bug-Risiko bei `&`, `|`, `^`, `<<`, `>>`. Kein Test.
2. **moo_wasm.c moo_pow = Stub**: Potenz-Operator im WASM-Target liefert immer 0. Jeder WASM-Output mit `**` → falsches Ergebnis.
3. **Null Produktions-Beispiele für neue APIs**: 0/130 `beispiele/*.moo` nutzen `_mit_headers` / `_mit_params` / `db_vorbereite`. Risiko: Features driften ohne Regressions-Signal.
4. **Keine compiler/tests/ für P2b+P3c-Landungen**: Ad-hoc-Smokes nur temporär.
5. **bytes_neu/bytes_new dokumentations-lose**: User-Builtin in codegen.rs:2241, nirgends dokumentiert.

## MED-Befunde (7)

- **GL33 + Vulkan ambient-Uniform-TODO**: `welt_ambient()` wirkungslos auf zwei Backends.
- **welt_sonne Custom-Position TODO**: nur Spieler-relativ verfügbar.
- **speicher_lesen / speicher_schreiben ohne Doku**: Bare-Metal-Builtins.
- **test_net_tcp/udp, test_http_client, test_regex, test_events, test_result fehlen**: keine Runtime-Tests.
- **moo_sprite_width/height + moo_3d_chunk_delete nicht aus Rust gerufen**: mögliches Handle-Leak-Risiko für Chunks.
- **Bare-Metal-Grenzfläche** — noch keine klare Trennung hosted/bare, keine Tests für minimalistische Toolchain.
- **Differential-Fuzzing Rust↔Python** noch nicht gemacht (Token-Ebene ok, aber Parser/Indent/Escape-Verhalten ungeprüft).

## LOW-Befunde (4)

- 10 Rust-Decls ohne direkten codegen-Call (Dispatcher-Umweg, kein echter Dead-Code, ~20 Zeilen Aufräum-Potential).
- minesweeper Rechtsklick-Kommentar-TODO.
- `__welt_*` bewusst internal — kein Befund.
- `map/filter` false-positive — dokumentiert als Methoden.

## Konsolidierungs-Empfehlung (Prio-Vorschlag, keine Umsetzung)

**A — Regressions-Sicherung (blockiert nichts):**
  A1. `compiler/tests/test_http_with_headers.moo` + `.expected`
  A2. `compiler/tests/test_db_with_params.moo` + `.expected`
  A3. `compiler/tests/test_db_prepared.moo` + `.expected` — mit Bulk+Named+Rollback-Mini-Version
  A4. `compiler/tests/test_bitwise_ops.moo` + `.expected` — fängt HIGH #1 ab

**B — Produktions-Beispiele migrieren (zeigt moo in echt):**
  B1. `beispiele/blog_engine.moo`: `sql_escape`→Prepared + Set-Cookie via `_mit_headers`
  B2. `beispiele/chat/chat_server.moo`: Prepared + CORS-Header
  B3. `beispiele/http_api.moo`: Auth-Token aus `req["headers"]`

**C — Silent-Bugs fixen:**
  C1. codegen.rs Bit-Ops-Branches korrigieren (HIGH #1)
  C2. moo_wasm.c moo_pow — echte Potenz-Implementation (HIGH #2)

**D — Doku-Lücken schließen:**
  D1. `bytes_neu` dokumentieren
  D2. `speicher_lesen/schreiben` (Bare-Metal) dokumentieren
  D3. `welt_ambient/welt_sonne` Einschränkungen pro Backend dokumentieren

**E — Hardening (optional):**
  E1. Handle-Leak-Audit sprite/chunk
  E2. Dispatcher-Dead-Decls aufräumen (20 Zeilen)
  E3. Differential-Fuzzing Rust↔Python-Toolchain

## Kennzahlen

- 5 HIGH / 7 MED / 4 LOW.
- Gesamt-Aufwand B+C (User-sichtbar): ~2-3 Tage.
- A (Regressions-Sicherung alleine): ~0.5 Tag.
