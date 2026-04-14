# Public vs. Internal API

Drei-Schichten-Matrix aller sichtbaren moo-APIs. Phase A — Inventar, keine
Moves. Ziel: trennscharf zeigen, welche C-Runtime-Symbole als **interne**
Implementierung leben, welche als **Compiler-/Codegen-Alias-Schicht** uebersetzt
werden und welche dem **User** als oeffentliche moo-API zur Verfuegung stehen.

## Drei Schichten

- **A — C-Runtime-Symbol** (`moo_*`): konkrete Funktion in
  `compiler/runtime/*.c`. Nicht direkt aus moo-Code aufrufbar.
- **B — Compiler/Binding/Alias**: Eintrag in `compiler/src/runtime_bindings.rs`
  + optionaler Dispatcher/Smart-Wrapper (z.B. `moo_smart_close`). Macht A
  ueber Codegen verfuegbar.
- **C — Oeffentliche moo-API**: der Name, den User in `.moo`-Dateien
  schreiben (DE/EN/Kurz-Alias).

## Kategorie-Spalte

- `PUBLIC` — alle drei Schichten: User darf C direkt aufrufen.
- `WRAPPED` — C existiert, aber User-API laeuft ueber einen stdlib-Wrapper
  oder Dispatcher.
- `INTERNAL` — nur A+B, kein C. User-Code darf das nicht sehen.
- `DISPATCHER` — B-Schicht ist ein Smart-Dispatcher, der auf mehrere A-Symbole
  verteilt; C sieht nur den Dispatcher-Namen.

## Problemfaelle (k3-Rohgeruest)

Gemeinsame Matrix — k4 ergaenzt in einem Folgecommit die Game-Zeilen
(sprite_*, raum_*, chunk_*, fenster_*, zeichne_*, screenshot, taste_simulieren)
auf Basis von `spec/game_module_boundary.md §2`.

### Daten / Persistence

| A C-Runtime | B Codegen-Alias | C moo-API | Kategorie | notes |
|-------------|-----------------|-----------|-----------|-------|
| `moo_json_parse` | `json_parse` | `json_lesen` / `json_parse` / `jp` | PUBLIC | — |
| `moo_json_string` | `json_string` | `json_text` / `json_string` / `js` | PUBLIC | — |
| `moo_http_get` | `http_get` | `http_hole` / `http_get` / `hg` | PUBLIC | Dict-Rueckgabe |
| `moo_http_post` | `http_post` | `http_sende` / `http_post` / `hp` | PUBLIC | Dict-Rueckgabe |
| `moo_http_get_with_headers` | `http_get_with_headers` | `http_hole_mit_headers` / `http_get_with_headers` | PUBLIC | neu P2b |
| `moo_http_post_with_headers` | `http_post_with_headers` | `http_sende_mit_headers` / `http_post_with_headers` | PUBLIC | neu P2b |
| `moo_db_connect/execute/query/close` | `db_connect/execute/query/close` | `db_verbinde`/`db_abfrage`/`db_ausführen`/`db_schliessen` | PUBLIC | + Kurz-Alias `dbv/dbe/dba/dbs` |
| `moo_db_execute_with_params` / `moo_db_query_with_params` | `db_{execute,query}_with_params` | `db_{ausführen,abfrage}_mit_params` | PUBLIC | neu P3c-B |
| `moo_db_prepare` + `moo_db_stmt_bind/execute/query/step/reset/close` | `db_prepare` + Method-Dispatch | `db_vorbereite` + `.binde/.ausfuehren/.abfrage/.schritt/.zuruecksetzen/.schliessen` | PUBLIC | neu P3c-A, Tag MOO_DB_STMT |

### Krypto / Regex / Bytes

| A | B | C | Kategorie | notes |
|---|---|---|-----------|-------|
| `moo_sha256`/`moo_sha1`/`moo_sha1_bytes` | `sha256`/`sha1`/`sha1_bytes` | gleichnamig | PUBLIC | — |
| `moo_secure_random` | `secure_random` | `sichere_zufall` / `secure_random` | PUBLIC | — |
| `moo_base64_encode/decode` | `base64_encode/decode` | `base64_kodieren`/`base64_dekodieren` + `b64e`/`b64d` | PUBLIC | — |
| `moo_sanitize_html` / `moo_sanitize_sql` | `sanitize_html/sql` | `html_bereinigen` / `sql_bereinigen` | PUBLIC | `sql_bereinigen` deprecated ab P3c |
| `moo_regex_new/match/find/find_all/replace` | `regex_{new,match,find,find_all,replace}` | `regex`/`muster`, `passt`/`matches`, `finde`, `finde_alle`, `ersetze` | PUBLIC | POSIX |
| `moo_bytes_to_string` | `bytes_to_string` | `bytes_neu` / `bytes_new` | PUBLIC | neu dokumentiert D1 |
| `moo_string_to_bytes` | `string_to_bytes` | `string_zu_bytes`, `bytes_zu_liste`, `string_to_bytes`, `bytes_to_list` | PUBLIC | binary-safe |

### Threads / Channels / Netzwerk / Webserver

| A | B | C | Kategorie | notes |
|---|---|---|-----------|-------|
| `moo_thread_spawn`/`wait`/`done` | `thread_spawn`/Method-Dispatch | `starte`/`spawn`, `.warten`/`.wait`, `.fertig`/`.done` | PUBLIC | pthreads |
| `moo_channel_new`/`send`/`recv` | `channel_new`/Method-Dispatch | `kanal`/`channel`, `.senden`/`.send`, `.empfangen`/`.recv` | PUBLIC | gepuffert |
| `moo_channel_close` | (nicht in B — via Dispatcher) | `.schliessen`/`.close` | DISPATCHER | via `moo_smart_close` |
| `moo_smart_close` | `smart_close` | `.schliessen`/`.close` (tag-dispatchend) | DISPATCHER | verteilt auf MOO_SOCKET/DB/STMT/WINDOW/WINDOW3D/Channels |
| `moo_smart_contains` | `smart_contains` | `.enthält`/`.contains` (tag-dispatchend) | DISPATCHER | Strings/Listen/Dicts |
| `moo_socket_*` | `tcp_*`/`udp_*`/Method-Dispatch | `tcp_server`, `tcp_verbinde`, `udp_*`, `.annehmen`, `.lesen`, `.schreiben` | PUBLIC | — |
| `moo_web_server/accept/respond/json/file/template/close` | entsprechende Aliase + Method-Dispatch | `web_server`, `.web_annehmen`, `.antworten`, `.json_antworten`, `.datei_senden`, `.template`, `.schliessen` | PUBLIC | HTTP 1.1 |
| `moo_web_respond_with_headers` / `moo_web_json_with_headers` | + `req["headers"]`-Dict aus `parse_http_request` | `.antworten_mit_headers` / `.json_antworten_mit_headers` | PUBLIC | neu P2b |

### System / Bare-Metal / Test-API / Profiler

| A | B | C | Kategorie | notes |
|---|---|---|-----------|-------|
| `moo_mem_read` / `moo_mem_write` | `mem_read` / `mem_write` | `speicher_lesen` / `speicher_schreiben` | PUBLIC (bare-metal) | `--no-stdlib` |
| `moo_simulate_key` / `moo_simulate_mouse` | gleichnamig | `taste_simulieren` / `maus_simulieren` | PUBLIC (Test-API) | — |
| `moo_screenshot` | `screenshot` | `bildschirmfoto` / `screenshot` | PUBLIC (Test-API) | — |
| `moo_profile_enter/exit/report` | gleichnamig | `profile_{enter,exit,report}` | PUBLIC | — |

### Welt-Engine (Kompat-Grenze aus P2c + k4 Scope)

| A | B | C | Kategorie | notes |
|---|---|---|-----------|-------|
| `moo_world_create/is_open/update/close/seed/biome/trees/sun/fog/sea_level/render_dist/time_of_day/height_at` (13 Bindings) | `__welt_*` / `__world_*` (raw) | `welt_*` (stdlib-Wrapper, siehe `stdlib/welt.moo`) | WRAPPED | `__welt_*` ist INTERNAL. User-Pfad via `importiere welt` |
| — | — | `welt_baeume` | WRAPPED | P2c-Nachzug 4a39983 |

### Rein interne Compiler-Bindings (nicht user-sichtbar)

| A | B | C | Kategorie | notes |
|---|---|---|-----------|-------|
| `moo_retain` / `moo_release` | `retain`/`release` | — | INTERNAL | RC, Codegen-Pfad |
| `moo_error` / `moo_throw` | (Codegen-Pfad) | — | INTERNAL | Fehler-Factory |
| `moo_curry` | `curry` | — | INTERNAL | Lambda-Currying |
| `moo_is_truthy` / `moo_is_none` | entsprechend | — | INTERNAL | Branch-Codegen |
| `moo_try_enter/check/leave` | entsprechend | — | INTERNAL | try/catch-Control-Flow |
| `moo_index_get/set` | — | `[i]`-Operator | DISPATCHER | Strings/Listen/Dicts gemeinsam |
| `moo_list_iter_len` | entsprechend | — | INTERNAL | Iterator-Helper fuer `.map/.filter` |

## Offen — k4 ergaenzt

- Game-Strang: `sprite_lade`/`sprite_*`, `fenster`, `zeichne_*`, `raum_*`,
  `chunk_*`, `welt_*` voll, `taste_*`/`maus_*`, `screenshot`.
- Grafik-Backend-Interna (`moo_3d_gl21`/`gl33`/`vulkan`) als INTERNAL markieren.

## Benutzung

- Neue moo-Features durchlaufen immer **A→B→C**. Eine C-Schicht ohne B + A
  ist nicht kompilierbar; eine A-Schicht ohne C ist INTERNAL und darf nicht
  in Docs oder Tutorials auftauchen.
- Bei Doku-/Audit-Fragen: diese Datei ist das Urteil darueber, ob ein
  `moo_*`-Symbol als oeffentlich dokumentiert werden soll (Spalte Kategorie
  = PUBLIC/WRAPPED → ja; INTERNAL/DISPATCHER → nein bzw. nur in
  Dispatcher-Doku).
