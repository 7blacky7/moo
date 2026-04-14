# k3 Audit-Befunde — Dead-Code + Doku-Luecken

Stand: 2026-04-14. Keine Umsetzung, nur Befunde. Quellen: `compiler/src/runtime_bindings.rs`, `compiler/src/codegen.rs`, `compiler/runtime/*.c`, `~/dev/moolang-docs/docs/referenz/*.md`, `docs/sprache/*.md`.

Methodik (reproduzierbar):
- Alias→C-Mapping via `/tmp/moo-audit/alias_to_c.tsv` (354 Paare aus codegen.rs).
- Doku-Coverage via Alias-Match (Backtick-/Wort-Grenze) gegen alle md-Dateien in `docs/referenz/` + `docs/sprache/` → `/tmp/moo-audit/doc_coverage.tsv`.
- Dead-Kandidaten: `runtime_bindings.rs`-Deklarationen ohne `self.rt.X`-Referenz in codegen.rs → `/tmp/moo-audit/dead_rt_decls.txt`.

---

## (3) Doku-Luecken

### HIGH

- **`bytes_neu` / `bytes_new`** (→ `moo_bytes_to_string`): in `codegen.rs:2241` als User-Builtin verfuegbar, aber nirgends dokumentiert. `bytes_zu_liste` / `string_zu_bytes` etc. stehen in `referenz/netzwerk.md:54`. Empfehlung: `bytes_neu(liste) → text` dort ergaenzen (erzeugt binary-safe String aus Byte-Liste).

### MED

- **`speicher_lesen` / `mem_read`** und **`speicher_schreiben` / `mem_write`**: volatile Memory-I/O fuer Bare-Metal-/System-Programmierung (`moo_mem_read`/`moo_mem_write`). User-Builtin, aber keine Referenz-Seite. Empfehlung: eigene Sektion `docs/referenz/bare.md` oder Anhang in `referenz/kern.md` mit Warnung "nur bare-metal / unsafe". Deckt sich mit GPT-Review 501192c2 (OS-Taugen + "weniger Magie an den tiefsten Stellen").

### LOW (bewusst undokumentiert — Verifikation)

- `__welt_*` (13 Bindings) sind interne Low-Level-Builtins; die User-API ist `stdlib/welt.moo`-Wrapper. k4 hat das in Prio2c bestaetigt; einzige aktuelle Luecke `welt_baeume`-Wrapper wurde ergaenzt. Kein Handlungsbedarf.
- `wasm` / `wasm32` → `moo_release`: interner codegen-Pfad (Refcount-Release in WASM-Target), kein User-Alias. Kein Doku-Bedarf.
- `abbilden`/`filtern`/`map`/`filter` → `moo_list_iter_len`: die String-Matches `.map`/`.filter` erscheinen in `referenz/{listen,strings,dicts,result}.md`. Der Runtime-Helper `moo_list_iter_len` ist ein Iterator-Hilfs-Primitiv, User sehen nur `.map(fn)` / `.filter(fn)` — bereits dokumentiert. False positive.

---

## (2) Dead-Code-Kandidaten

### Eigentlich alle indirekt genutzt (LOW)

10 `runtime_bindings.rs`-Deklarationen werden nie via `self.rt.X` direkt aus codegen.rs aufgerufen:

| Rust-Decl | Indirekte Nutzung (C-Runtime) |
|-----------|-------------------------------|
| `moo_channel_close` | via `moo_smart_close`, 3 `.c`-Refs |
| `moo_socket_close`  | via `moo_smart_close`, 2 `.c`-Refs |
| `moo_web_close`     | via `moo_smart_close`, 1 `.c`-Ref |
| `moo_list_contains` | via `moo_smart_contains`, 4 Refs |
| `moo_list_get`      | 10 Refs (viele C-Helper) |
| `moo_list_set`      | 2 Refs |
| `moo_string_index`  | via `[idx]`-Operator, 2 Refs |
| `moo_string_length` | via `.length`-Property, 1 Ref |
| `moo_object_set_parent` | Klassen-Vererbung, 1 Ref |
| `moo_error`         | 63 Refs (ueberall als Fehler-Factory) |

**Befund**: Keine echten Dead-Decls. Alle 10 werden ueber andere Runtime-Helfer oder via Dispatcher (smart_close, smart_contains, Operator-Methoden) erreicht.

**Nice-to-have (LOW)**: Diese 10 koennten aus `runtime_bindings.rs` entfernt werden (Rust-seitig nie referenziert → LLVM-Linker findet sie ueber C-to-C-Calls auch so). Spart ca. 20 Zeilen `FunctionValue<'ctx>`-Deklarationen + Init. Kein funktionaler Gewinn, nur Aufraeumen — passt zu GPT-Review 5060 Punkt 5 (`runtime_bindings.rs` ist "inzwischen riesig und faktisch manuelle zweite API-Definition").

### Exportiert-aber-ungenutzt im C-Runtime (tiefer Scan — HEURISTIK MED)

Fuer echte Dead-Funktionen haette ich jeden exportierten `moo_*` in `compiler/runtime/*.c` einzeln per `code_intel references` pruefen muessen. Stichproben-Ergebnis:
- `moo_sprite_width` / `moo_sprite_height` (in moo_sprite.c): exported, aber aktuell kein codegen-Alias + kein C-Runtime-Call. **Kandidat fuer Dead** (MED). k4-Scope, zur Verifikation empfohlen.
- `moo_3d_chunk_draw` / `moo_3d_chunk_delete`: exported, in codegen.rs deklariert aber `self.rt.moo_3d_chunk_delete` wird nicht aufgerufen → Chunk-Lifecycle endet ohne expliziten Free-Pfad auf moo-Ebene. **Leak-Risiko**, sollte k4 pruefen.

---

## Zusammenfassung

| Prio | Anzahl | Kategorie |
|------|--------|-----------|
| HIGH | 1 | `bytes_neu` in Docs ergaenzen |
| MED  | 2 + 2 | `speicher_lesen/schreiben` dokumentieren; k4-Scope: Sprite/Chunk-Handle-Lifecycle pruefen |
| LOW  | 10 + 3 | indirekt genutzte Rust-Decls (Aufraeumen), bewusste undokumentierte Internas |

Keine Breaking-Funde — alle Befunde sind additiv loesbar.
