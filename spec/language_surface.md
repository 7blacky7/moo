# Sprachoberflaeche (Language Surface)

Kanonisches Inventar der sichtbaren moo-APIs und Sprachkonstrukte.
Phase A — Inventar, keine Bewegung, keine Aenderung am Code.

## Spalten

- **name**: moo-seitig sichtbarer Name (Haupt-Alias; DE/EN gleichwertig)
- **layer**: `KERN` / `SYSTEM` / `DOMAENE:<cluster>` / `SYNTAX` / `STDLIB`
- **public**: `ja` (User-API) / `intern` (Codegen- oder Laufzeit-intern)
- **toolchain**: `rust+c` (nativer Compiler) / `python` (Transpiler/LSP) /
  `beide` / `rust-only` / `python-only`
- **docs_status**: `ja` (Referenz-Seite vorhanden) / `stub` / `fehlt`
- **test_status**: `ja` (automatisierter Test) / `smoke` (compile-only) /
  `fehlt`
- **notes**: kurzer Kommentar

Dieses Inventar ist NICHT vollstaendig — es deckt die 50+ wichtigsten
sichtbaren Flaechen ab. Vervollstaendigung in Phase B.

## Syntax & Kern-Kontrollfluss

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `setze … auf` / `set … to` | SYNTAX | ja | beide | ja | ja | Variable binden |
| `konstante … auf` / `const … to` | SYNTAX | ja | beide | ja | ja | Unveraenderlich |
| `funktion` / `func` | SYNTAX | ja | beide | ja | ja | Funktionsdefinition |
| `klasse` / `class` | SYNTAX | ja | beide | ja | ja | + Vererbung |
| `wenn` / `if` `sonst` / `else` | SYNTAX | ja | beide | ja | ja | — |
| `solange` / `while`, `für … in` / `for … in` | SYNTAX | ja | beide | ja | ja | — |
| `pruefe` / `match`, `fall` / `case`, `standard` / `default` | SYNTAX | ja | beide | ja | ja | Soft-Keyword-Register |
| `versuche`/`fange`/`wirf` — `try`/`catch`/`throw` | SYNTAX | ja | beide | ja | ja | + `fg`-Alias nur in Python |
| `importiere`/`aus`/`als` — `import`/`from`/`as` | SYNTAX | ja | beide | ja | teilweise | Diamond-Import-Bug historisch |
| `gib_zurueck` / `return` | SYNTAX | ja | beide | ja | ja | — |
| `stopp`/`weiter` — `break`/`continue` | SYNTAX | ja | beide | ja | ja | — |
| `neu Klasse(...)` / `new Class(...)` | SYNTAX | ja | beide | ja | ja | Konstruktor |
| `?.` (optional chaining), `??` (nullish coalescing) | SYNTAX | ja | beide | ja | ja | — |
| f-Strings `f"{x}"` | SYNTAX | ja | beide | ja | ja | — |
| List-Comprehension `[x für x in ...]` | SYNTAX | ja | beide | ja | ja | — |
| `ASYNC`/`AWAIT` | SYNTAX | intern (geparst, nicht ausgefuehrt) | python-only | fehlt | fehlt | Drift → spec/soft_keywords.md |
| Funktionsname als Wert (`setze f auf fn`) | SYNTAX | ja | rust+c | stub | ja | Erzeugt MOO_FUNC-MooValue via moo_func_new |
| Lambda `(x) => expr` mit Captures | SYNTAX | ja | rust+c | stub | ja | Closure via Trampoline + moo_func_with_captures |
| Einzeiler mit Method-Chaining `[..].filter(..).map(..)` | SYNTAX | ja | beide | stub | ja | Pass-through vom Parser — kein Grammar-Sonderfall noetig |

## Kern-Builtins (KERN + STDLIB)

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `zeige` / `show` / `p` | KERN | ja | beide | ja | ja | Ausgabe |
| `eingabe` / `input` | KERN | ja | beide | ja | ja | stdin |
| `typ_von` / `type_of` | KERN | ja | beide | ja | ja | — |
| `länge` / `length` / `len` | KERN | ja | beide | ja | ja | — |
| `text` / `string`, `zahl` / `number` | KERN | ja | beide | ja | ja | Typ-Konvertierung |
| `zeit` / `time`, `schlafe` / `sleep`, `warte` / `wait` | STDLIB | ja | beide | ja | ja | — |
| `args`, `env`, `exit` | STDLIB | ja | beide | ja | ja | — |
| `range`, `0..n` | KERN | ja | beide | ja | ja | — |
| `abs`, `sqrt`/`wurzel`, `runde`/`round`, `boden`/`floor`, `decke`/`ceil`, `min`, `max` | STDLIB | ja | beide | ja | ja | — |
| `sinus`/`sin`, `cosinus`/`cos`, `tangens`/`tan`, `atan2` | STDLIB | ja | beide | ja | ja | `kosinus` NICHT, GPT-Anmerkung |
| `zufall` / `random` | STDLIB | ja | beide | ja | ja | — |

## Listen / Dicts / Strings (KERN)

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `liste.append`/`.hinzufügen` | KERN | ja | beide | ja | ja | — |
| `liste[i]` / `liste[i] = x` | KERN | ja | beide | ja | ja | — |
| `liste.pop`, `.sortiere`/`.sort`, `.reverse`/`.umkehren` | KERN | ja | beide | ja | ja | — |
| `liste.enthält` / `.contains`, `.join`/`.verbinden` | KERN | ja | beide | ja | ja | — |
| `.map`/`.abbilden`, `.filter`/`.filtern` | KERN | ja | beide | ja | ja | Lambda-Arg |
| `dict["k"]`, `dict.hat`/`.has`, `dict.keys`/`.schluessel` | KERN | ja | beide | ja | ja | — |
| `.gross`/`.upper`, `.klein`/`.lower`, `.trimmen`/`.trim` | KERN | ja | beide | ja | ja | — |
| `.teilen`/`.split`, `.ersetzen`/`.replace`, `.enthält`/`.contains` | KERN | ja | beide | ja | ja | — |
| `.teilstring`/`.slice` | KERN | ja | beide | ja | ja | — |

## Result & Events (KERN + STDLIB)

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `ok(x)`, `fehler(x)`/`err(x)` | KERN | ja | beide | ja | ja | Result-Typ |
| `.is_ok`, `.is_err`, `.unwrap`/`.entpacke` | KERN | ja | beide | ja | ja | — |
| `on(event, fn)`, `emit(event, daten)` | STDLIB | ja | beide | ja | ja | Event-System |
| `freeze`, `is_frozen` | KERN | ja | beide | ja | ja | — |

## Domaene: JSON / HTTP / Dateien / Datenbank / Krypto / Regex

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `json_lesen`/`json_parse`, `json_text`/`json_string` | DOMAENE:json | ja | beide | ja | ja | — |
| `http_hole`/`http_get`, `http_sende`/`http_post` | DOMAENE:http | ja | rust+c | ja | smoke | libcurl |
| `http_hole_mit_headers`/`http_get_with_headers` | DOMAENE:http | ja | rust+c | ja | smoke | neu P2b |
| `http_sende_mit_headers`/`http_post_with_headers` | DOMAENE:http | ja | rust+c | ja | smoke | neu P2b |
| `datei_lesen`/`file_read` + 8 weitere Datei-Builtins | DOMAENE:file | ja | rust+c | ja | ja | siehe referenz/dateien.md |
| `datei_mtime`/`file_mtime` | DOMAENE:file | ja | rust+c | stub | ja | Unix-Timestamp; -1 bei Fehler; cross-platform (POSIX stat / Win `_stat64`) |
| `datei_ist_verzeichnis`/`file_is_dir` (`ist_verzeichnis`/`is_dir`) | DOMAENE:file | ja | rust+c | stub | ja | Bool; cross-platform `S_ISDIR` / `_S_IFDIR` |
| `db_verbinde`/`db_connect`, `db_abfrage`/`db_query`, `db_ausführen`/`db_execute`, `db_schliessen`/`db_close` | DOMAENE:db | ja | rust+c | ja | ja | SQLite |
| `db_abfrage_mit_params`/`db_query_with_params`, `db_ausführen_mit_params`/`db_execute_with_params` | DOMAENE:db | ja | rust+c | ja | ja | neu P3c-B |
| `db_vorbereite`/`db_prepare` + stmt.binde/ausfuehren/abfrage/schritt/zuruecksetzen/schliessen | DOMAENE:db | ja | rust+c | ja | ja | neu P3c-A, Tag MOO_DB_STMT |
| `sha256`, `sha1`, `sha1_bytes`, `sichere_zufall`/`secure_random` | DOMAENE:crypto | ja | rust+c | ja | ja | — |
| `base64_kodieren`/`base64_encode`, `base64_dekodieren`/`base64_decode` | DOMAENE:crypto | ja | rust+c | ja | ja | — |
| `html_bereinigen`/`sanitize_html`, `sql_bereinigen`/`sanitize_sql` | DOMAENE:crypto | ja (sql_bereinigen deprecated) | rust+c | ja | ja | — |
| `regex`/`muster`, `passt`/`matches`, `finde`/`find`, `finde_alle`/`find_all`, `ersetze`/`replace` | DOMAENE:regex | ja | rust+c | ja | ja | POSIX |

## Domaene: Threads / Channels / Netzwerk / Webserver

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `starte`/`spawn`, `.warten`/`.wait`, `.fertig`/`.done` | DOMAENE:thread | ja | rust+c | ja | smoke | pthreads |
| `kanal`/`channel`, `.senden`/`.send`, `.empfangen`/`.recv`, `.schliessen`/`.close` | DOMAENE:thread | ja | rust+c | ja | ja | gepuffert |
| `tcp_server`/`tcp_verbinde`, `udp_*`, `.annehmen`/`.accept`, `.lesen`/`.schreiben` | DOMAENE:net | ja | rust+c | ja | smoke | — |
| `bytes_neu`/`bytes_new`, `bytes_zu_liste`/`bytes_to_list`, `string_zu_bytes`/`string_to_bytes` | DOMAENE:net | ja | rust+c | ja | smoke | binary-safe |
| `web_server`/`webserver`, `.web_annehmen`/`.web_accept`, `.antworten`/`.respond`, `.json_antworten`/`.json_respond` | DOMAENE:web | ja | rust+c | ja | smoke | HTTP 1.1 |
| `.antworten_mit_headers`, `.json_antworten_mit_headers` + `req["headers"]` | DOMAENE:web | ja | rust+c | ja | smoke | neu P2b |
| `.datei_senden`/`.serve_file`, `.template` | DOMAENE:web | ja | rust+c | ja | smoke | — |

## Domaene: Grafik / Sprites / 3D / Welt

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `fenster`/`window`, `zeichne_*`/`draw_*`, `.maus_*` | DOMAENE:game | ja | rust+c | ja | smoke | SDL2 |
| `sprite_lade`/`sprite_load`, `sprite_*` | DOMAENE:game | ja | rust+c | ja | smoke | slot-array 256 |
| `raum_*`/`3d_*` | DOMAENE:game | ja | rust+c | ja | smoke | GL21/GL33/Vulkan |
| `chunk_*` | DOMAENE:game | ja | rust+c | ja | smoke | Display-Lists |
| `welt_*` (stdlib-Wrapper) | DOMAENE:game | ja | rust+c | ja | smoke | Perlin/Biomes |
| `__welt_*` (`__world_*`) | DOMAENE:game | **intern** | rust+c | intern | indirekt | Raw-Runtime-Bindings |

## System / Bare-Metal / Test-API

| name | layer | public | toolchain | docs | test | notes |
|------|-------|--------|-----------|------|------|-------|
| `speicher_lesen`/`mem_read`, `speicher_schreiben`/`mem_write` | SYSTEM | ja | rust+c | ja (neu D2) | fehlt | `--no-stdlib` |
| `taste_simulieren`/`simulate_key`, `maus_simulieren`/`simulate_mouse` | SYSTEM | ja (Test-API) | rust+c | ja | smoke | GUI-Tests |
| `bildschirmfoto`/`screenshot` | SYSTEM | ja (Test-API) | rust+c | ja | smoke | — |
| `profile_enter`/`profile_exit`/`profile_report` | DOMAENE:profiler | ja | rust+c | ja | smoke | — |

## Interne Compiler-/Codegen-Bindings (nicht fuer User sichtbar)

| name | layer | public | toolchain | notes |
|------|-------|--------|-----------|-------|
| `moo_retain`/`moo_release` | KERN | intern | rust+c | RC-Management |
| `moo_smart_close` | SYSTEM | intern | rust+c | Tag-Dispatcher fuer close |
| `moo_smart_contains` | KERN | intern | rust+c | Dispatcher fuer contains |
| `moo_error`, `moo_throw` | KERN | intern | rust+c | Fehlerpfade |
| `moo_json_antworten` (C-Funktionsname) | DOMAENE:web | intern | rust+c | Codegen-Alias fuer `.json_antworten` |
| `__welt_*` (13 Bindings) | DOMAENE:game | intern | rust+c | nur via stdlib/welt.moo-Wrapper |

## Toolchain-Drift (Rust vs Python — aus E3-Fuzzing und GPT-Review 927d3272)

- `ASYNC`/`AWAIT`, `fg` (catch-Alias): nur Python-Tokenizer.
- Python-Transpiler kennt groessere Lexer-Oberflaeche als der Rust-Compiler, hat aber keinen Test-Runner fuer Showcase-Beispiele.
- `beispiele/showcase.moo`: Python-Timeout > 10s beim Parsen (separates Ticket, gpt 5060).
- Drift-Tracking: `tools/check_tokens.py` und `tools/fuzz_differential.py` laufen pro Commit (k2 P1/E3).

## Offen fuer Phase B

- Datei-Pfad-Umbauten aus `runtime_layers.md` vollziehen.
- Game-Strang in `modules/game/` trennen (k4 B2).
- Python-Tooling auf Rust-Oberflaeche einschraenken oder explizit als "Transpiler-Profil" markieren.
- `__welt_*`-API vollstaendig hinter Wrapper verstecken (aktuell nur `welt_baeume` ergaenzt).
