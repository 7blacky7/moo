# Beispiele-Taxonomie

Stand: 2026-04-14 · Phase A (additiv) gemäss ChatGPT-Block C.1 (#5088 / moderiert via k2 #5105).
Klassifiziert `beispiele/*.moos` nach Zweck und ordnet die 25 wichtigsten Dateien
als Produktion / Demo / Legacy / Kandidat-für-Umzug ein.

Quelle: `ls beispiele/` (130+ `.moos`-Dateien + 9 Unterverzeichnisse mit
Multi-File-Beispielen) und Inhaltsscans via `code_intel`.

## 1. Zonen

### 1.1 `quickstart/` — Einstiegsbeispiele (kurz, selbsterklärend)

Programme, die in < 50 Zeilen ein Sprachfeature oder einen Denkpfad erklären.
Ziel: User, die moo zum ersten Mal sehen.

- `fibonacci.moos` — Zahl-/Schleifen-Grundlagen.
- `taschenrechner.moos` — CLI, `eingabe()`.
- `todo.moos` — Listen + Dict.
- `adressbuch.moos` — Datei-IO + einfache Struktur.
- `wetter_api.moos` — `http_get`.

### 1.2 `showcase/` — Flaggschiff-Demos (mehrere Features zusammen)

Zeigen, was mit moo realistisch baubar ist. Werden in README / ARCHITECTURE
referenziert.

- `welten.moos` — prozedurale 3D-Welt, 400 LOC, Perlin + Chunks.
- `welt_test.moos` — kanonische `importiere welt` Nutzung.
- `3d_demo.moos` — minimales 3D.
- `zelda.moos` — Top-Down mit Sprite-Atlas.
- `raytracer/` — Multi-File, Weekend-Tracer.
- `pong.moos` — 2D-Input + Draw-Loop.
- `breakout.moos` — Collision + Score.
- `snake.moos` — klassisches 2D-Game.
- `blog_engine_v2.moos` — Web + DB + Cookies (Post-P2b/P3c-A).
- `chat/` — HTTP Long-Polling + SQLite.
- `http_api.moos` / `http_api_cors.moos` — REST-API + CORS.
- `mini_db/` — DB-Prepared-Statements.
- `neuralnet.moos` — Matrizen + Lerner.
- `schach.moos` — klassischer Engine-Loop.

### 1.3 `stress/` — Performance- und Grenzfall-Programme

Weniger Tutorial, mehr "kann moo das?"-Beweise. Nicht für Einsteiger.

- `benchmark.moos` — Microbench.
- `memory.moos` — Allokations-Stresstest.
- `stackvm.moos` — kleiner Bytecode-Interpreter.
- `sat_solver.moos` — Backtracking auf Klauseln.
- `regex_engine.moos` — NFA/DFA in moo selbst.
- `mini_lisp.moos` — Eval-Loop.
- `mini_sql.moos` — Query-Parser.
- `x86dis.moos` — Disassembler-Skizze.
- `bf_interp.moos` — Brainfuck-Interpreter.
- `tar_reader.moos` — Binary-Parsing.
- `elf_reader.moos` — ELF-Header.
- `png_encoder.moos` / `gif_encoder/` — Bild-Serialisierung.
- `synth.moos` / `midi_player/` — Audio-Sample-Gen.
- `physik.moos` — Physik-Loop.
- `welten_noise_test.moos` / `welten_render_test.moos` — Welt-Noise/Render-Tuning.

### 1.4 `domain/` — echte Anwendungs-Demos pro Cluster

#### `domain/web/`
- `blog_engine.moos` *(legacy, siehe v2)*
- `blog_engine_v2.moos` **(production)**
- `chat/chat_server.moos` **(production)**
- `http_api.moos` **(production)**
- `http_api_cors.moos` **(production, neu P2b)**
- `cms_posts/` *(production, mini-CMS)*
- `mqtt_broker.moos` *(demo)*
- `websocket_server.moos` *(demo, RFC-6455-Server + One-Shot-E2E-Referenz)*
- `websocket_client_selftest.moos` / `websocket_wss_smoke.moos` *(Regression: ws IPv4/IPv6 + wss OpenSSL/mbedTLS)*
- `proxy.moos` *(demo, TCP-Reverse-Proxy)*
- `dns_resolver.moos` *(demo, Konsument von `stdlib/dns.moos`)*
- `wetter_api.moos` *(quickstart, auch hier gelistet als web-Konsument)*

#### `domain/db/`
- `mini_db/` **(production, Prepared-Statements-Referenz)**
- `mini_sql.moos` *(stress, nicht domain)*
- `mini_redis/` *(demo)*
- `mysql_client.moos` *(demo, Wire-Protocol)*
- `postgres_client.moos` *(demo)*
- `redis_client.moos` *(demo)*
- `markdown_cms.moos` / `markdown/` *(demo, Content + SQLite)*

#### `domain/game/`
Siehe `spec/game_module_boundary.md §5` für Abgrenzung.
Produktion: `pong`, `breakout`, `snake`, `tetris`, `asteroids`, `pacman`,
`zelda`, `welten`, `welt_test`, `3d_demo`, `invaders`, `sokoban`, `solitaire`,
`connect4`, `minesweeper`, `bomberman`, `frogger`, `tower_defense`,
`tic_tac_toe`, `flappy`, `boulder_dash`, `galaga`, `missile_command`,
`paratrooper`, `pinball`, `qix`, `racing`, `rhythm`, `siedler`, `simon`,
`sokoban`, `space_shooter`, `tron`, `whack`, `wordle`.
Experimentell: `adventure`, `artillery`, `brawler/`, `bubble_shooter/`,
`columns`, `defender/`, `doodle_jump/`, `dungeon/`, `farm/`,
`fighter/`, `fruit_ninja/`, `gravity_well`, `helicopter/`, `lemmings/`,
`lights_out`, `mastermind`, `match3`, `maze`, `mine_run`, `pipe_puzzle`,
`pipes`, `platformer`, `rpg_world`, `ski`, `snake_plus`, `strategie`,
`sudoku`, `survival`, `td_procgen`, `pong_plus`.
Test-Suiten: `test_pong`, `test_breakout`, `test_snake`, `test_match3`,
`test_minesweeper`, `test_platformer`, `test_racing`, `test_sokoban`,
`test_space_shooter`, `test_tetris`, `test_tower_defense`, `test_zelda`.

#### `domain/system/`
- `kernel_hello.moos` *(demo, bare-metal Hello)*
- `kernel.ld` *(nicht-moo, Linker-Script; Kandidat für `modules/bare/`)*
- `bf_interp.moos`, `x86dis.moos`, `stackvm.moos`, `mini_lisp.moos`, `sat_solver.moos` *(stress, werden auch als Domäne "system" gelesen)*
- `elf_reader.moos`, `tar_reader.moos`, `png_encoder.moos`, `gif_encoder/` *(binary-I/O Demos)*
- `datei_suche/` *(CLI-Tool)*
- `explorer/` *(CLI-Tool)*
- `ascii_editor/` *(TUI-Editor, kein Fenster)*

### 1.5 Ordner-Artefakte außerhalb der Taxonomie

- `beispiele/assets/` — Grafik-Assets (PNG, BMP, Tilesets). Bleibt.
- `beispiele/playground/` — Dev-Sandbox, nicht dokumentiert.
- `beispiele/test_screenshots/` — Output-Artefakte aus Test-Suite. Sollte
  nicht mehr eingecheckt werden (Phase B: `.gitignore` + `target/screenshots/`).
- `beispiele/web/` — statische HTML/CSS-Ressourcen für Web-Demos.
- `tmp/` (root) — Build-Artefakt, gehört nicht ins Repo.

## 2. Top-25 wichtigste Dateien mit Label

Label-Semantik:
- **production**: wird gepflegt, doku-verlinkt, Referenz-Beispiel.
- **demo**: zeigt ein Feature, nicht hartes Produkt.
- **legacy**: funktioniert, aber durch ein neueres Beispiel abgelöst.
- **candidate-for-move**: sollte in Phase B umgeordnet / ausgedünnt werden.

| # | Datei | Zone | Label | Bemerkung |
|---|-------|------|-------|-----------|
| 1 | `welten.moos` | showcase | production | kanonische Low-Level-3D-Demo (400 LOC, Perlin). Zielpfad `examples/game/3d/welten.moos`. |
| 2 | `welt_test.moos` | showcase | production | kanonische `importiere welt`-Demo. Zielpfad `examples/game/welt/welt_test.moos`. |
| 3 | `3d_demo.moos` | showcase | production | minimaler 3D-Einstieg. |
| 4 | `zelda.moos` | showcase | production | Sprite-Atlas-Referenz. |
| 5 | `pong.moos` | showcase | production | 2D-Input + Game-Loop Referenz. |
| 6 | `breakout.moos` | showcase | production | Collision + Score Referenz. |
| 7 | `snake.moos` | showcase | production | 2D-Klassiker. |
| 8 | `raytracer/*.moos` | showcase | production | Multi-File-Beispiel, Math-Heavy. |
| 9 | `blog_engine_v2.moos` | domain/web | production | Prepared-Statements + Cookies (Post-P2b/P3c-A). |
| 10 | `chat/chat_server.moos` | domain/web | production | Long-Polling + SQLite. |
| 11 | `http_api_cors.moos` | domain/web | production | CORS + Headers-API (P2b). |
| 12 | `http_api.moos` | domain/web | production | einfache REST-API. |
| 13 | `mini_db/` | domain/db | production | Prepared-Statements-Referenz. |
| 14 | `wetter_api.moos` | quickstart | production | `http_get` Referenz. |
| 15 | `fibonacci.moos` | quickstart | production | Einstieg Schleifen. |
| 16 | `todo.moos` | quickstart | production | Einstieg Listen+Dict. |
| 17 | `adressbuch.moos` | quickstart | production | Datei-IO Referenz. |
| 18 | `taschenrechner.moos` | quickstart | production | CLI-Einstieg. |
| 19 | `benchmark.moos` | stress | demo | Mikrobench, Perf-Tracking. |
| 20 | `regex_engine.moos` | stress | demo | Sprachkomplexitäts-Beweis. |
| 21 | `sat_solver.moos` | stress | demo | Backtracking. |
| 22 | `kernel_hello.moos` | domain/system | demo | bare-metal-Einstieg. |
| 23 | `elf_reader.moos` | domain/system | demo | binary-I/O. |
| 24 | `dns_resolver.moos` | domain/web | demo | Konsument des self-contained RFC-1035-Moduls `stdlib/dns.moos` (A/AAAA/CNAME). |
| 25 | `websocket_server.moos` + Client-Smokes | domain/web | demo | RFC 6455, maskierte Clientframes, IPv4/IPv6 und wss-Dualbackend. |
| 26 | `blog_engine.moos` | domain/web | legacy | durch `blog_engine_v2.moos` abgelöst (Prepared + Cookies). Kandidat für `legacy/`-Ordner oder `_deprecated`-Suffix in Phase B. |

## 3. Weitere Kandidaten-für-Umzug

Nicht in den Top-25, aber relevant für Phase B:

- `beispiele/playground/*` — **candidate-for-move** nach `playground/` im Root,
  nicht mehr als Beispiel sichtbar.
- `beispiele/test_screenshots/*` — **candidate-for-remove** aus Git, wird von
  Tests re-erzeugt.
- `beispiele/welten_noise_test.moos` + `welten_render_test.moos` — **candidate-for-move**
  nach `tests/runtime/welten/` (sind Regression-Fixtures).
- Root-Level `test_*.moos` (falls vorhanden — laut k2-README-Policy sollen sie nicht mehr
  entstehen) — ebenfalls umziehen oder löschen.
- Viele Einzel-Games in `beispiele/` (z.B. `ski`, `pipe_puzzle`, `mine_run`,
  `snake_plus`, `pong_plus`) sind Varianten/Experimente — **candidate-for-move**
  in `examples/game/experiments/`, damit der Hauptpfad überschaubar bleibt.

## 4. Gate-Einhaltung

- Rein Inventar, keine Moves.
- Alle Pfade existieren aktuell wie angegeben.
- Zielpfade sind Vorschläge für Phase B — werden dort gegen ein separates
  Move-Playbook validiert (Imports, Referenzen aus Docs und CI).
