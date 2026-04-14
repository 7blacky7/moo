# Full Project Audit — Gesamtbericht

## HIGH-Befunde (5)

1. **codegen.rs Bit-Ops TODO** (k2): RShift/LShift werfen stellenweise logische statt bitweise Ops. Silent-Bug-Risiko bei `&`, `|`, `^`, `<<`, `>>`. Kein Test.
2. **moo_wasm.c moo_pow = Stub** (k2): Potenz-Operator im WASM-Target liefert immer 0. Jeder WASM-Output mit `**` → falsches Ergebnis.
3. **Null Produktions-Beispiele für neue APIs** (k4): 0/130 `beispiele/*.moo` nutzen `_mit_headers` / `_mit_params` / `db_vorbereite`. Risiko: Features driften ohne Regressions-Signal.
4. **Keine compiler/tests/ für P2b+P3c-Landungen** (k4): Ad-hoc-Smokes nur in `/tmp/moo-verify`. Wenn Session endet, verschwinden sie.
5. **bytes_neu/bytes_new dokumentations-lose** (k3): User-Builtin in codegen.rs:2241, nirgends in docs/referenz/.

## MED-Befunde (7)

- **GL33 + Vulkan ambient-Uniform-TODO** (k2): `welt_ambient()` wirkungslos auf zwei Backends.
- **welt_sonne Custom-Position TODO** (k2): nur Spieler-relativ verfügbar.
- **speicher_lesen / speicher_schreiben ohne Doku** (k3): Bare-Metal-Builtins, deckt GPT-501192c2.
- **test_net_tcp/udp, test_http_client, test_regex, test_events, test_result fehlen** (k4): keine Runtime-Tests.
- **moo_sprite_width/height + moo_3d_chunk_delete nicht aus Rust gerufen** (k3→k4): mögliches Handle-Leak-Risiko für Chunks.
- **GPT-501192c2 Bare-Metal-Grenzfläche** — noch keine klare Trennung hosted/bare, keine Tests für minimalistische Toolchain.
- **Differential-Fuzzing Rust↔Python** noch nicht gemacht (Token-Ebene ok, aber Parser/Indent/Escape-Verhalten ungeprüft).

## LOW-Befunde (4)

- 10 Rust-Decls ohne direkten codegen-Call (Dispatcher-Umweg, kein echter Dead-Code, ~20 Zeilen Aufräum-Potential) — k3.
- minesweeper Rechtsklick-Kommentar-TODO (bekannter offener Thought 20d64530) — k2.
- `__welt_*` bewusst internal — kein Findung.
- `map/filter` false-positive — dokumentiert als Methoden.

## Konsolidierungs-Empfehlung (Prio-Vorschlag, keine Umsetzung)

Nächste Runde nach Nutzer-Entscheidung:

**A — Regressions-Sicherung (blockiert nichts):**
  A1. `compiler/tests/test_http_with_headers.moo` + `.expected` (k3/k4)
  A2. `compiler/tests/test_db_with_params.moo` + `.expected` (k3)
  A3. `compiler/tests/test_db_prepared.moo` + `.expected` — mit Bulk+Named+Rollback-Mini-Version (k3)
  A4. `compiler/tests/test_bitwise_ops.moo` + `.expected` — fängt HIGH #1 ab (k2)

**B — Produktions-Beispiele migrieren (zeigt moo in echt):**
  B1. `beispiele/blog_engine.moo`: `sql_escape`→Prepared + Set-Cookie via `_mit_headers` (k3)
  B2. `beispiele/chat/chat_server.moo`: Prepared + CORS-Header (k3)
  B3. `beispiele/http_api.moo`: Auth-Token aus `req["headers"]` (k3)

**C — Silent-Bugs fixen:**
  C1. codegen.rs Bit-Ops-Branches korrigieren (HIGH #1, k2)
  C2. moo_wasm.c moo_pow — echte Potenz-Implementation (HIGH #2)

**D — Doku-Lücken schließen:**
  D1. `bytes_neu` in referenz/netzwerk.md (k3)
  D2. `speicher_lesen/schreiben` in neuer referenz/bare-metal.md (k3)
  D3. `welt_ambient/welt_sonne` Einschränkungen pro Backend in referenz/welt.md (k4)

**E — Hardening (optional):**
  E1. Handle-Leak-Audit sprite/chunk (k4)
  E2. Dispatcher-Dead-Decls aufräumen (k3, 20 Zeilen)
  E3. Differential-Fuzzing Rust↔Python-Toolchain (k2)

## Kennzahlen

- 5 HIGH / 7 MED / 4 LOW.
- Gesamt-Aufwand B+C (User-sichtbar): ~2-3 Tage.
- A (Regressions-Sicherung alleine): ~0.5 Tag.
- Audit-Artefakte: `/tmp/moo-audit/{k2,k3,k4}_befunde.md` + `alias_to_c.tsv`, `doc_coverage.tsv`, `dead_rt_decls.txt`.
