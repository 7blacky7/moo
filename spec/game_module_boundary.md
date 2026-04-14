# Game/3D/Welt — Modul-Grenze

Stand: 2026-04-14 · Zweck: Phase-A-Architektur-Dokumentation gemäss Block B.2
von ChatGPT (#5083). **Rein additiv**, keine Moves, keine Renames — nur
Trennschärfe für Phase B.

Quellen: `compiler/src/runtime_bindings.rs`, `compiler/src/codegen.rs`
(DE/EN/Kurz-Alias-Pattern), `compiler/runtime/*.c`, `stdlib/welt.moo`,
`beispiele/*.moo`, `docs/referenz/*.md`.

## 1. Scope-Definition "Game-Strang"

**Teil des Game-Strangs**: alles, was ohne Fenster/Grafik/Szene keinen
Sinn ergibt — 2D-Zeichnen, Sprites, 3D-Szene + Kamera, Welt-Engine
(prozedurale Voxel-Welt), Test-API für deterministische Spiel-Tests.

**NICHT Teil des Game-Strangs** (generisch, von Spielen UND Server-/CLI-
Programmen gleich benutzt): MooValue-Runtime, String/List/Dict, File-I/O,
HTTP, DB, Net-Sockets, JSON, Regex, Threads, Channels, Kryptografie,
Profiler, Events, Result-Typ, Bare-Metal-Pfad.

**Grenzfälle** (bewusst im Game-Strang dokumentiert, aber auch für
Nicht-Spiele nützlich): `moo_graphics.c` — 2D-Fenster sind das
klassische "erste Pixel"-Lernbeispiel, werden aber fast nur in Spielen/
Visualisierungen gebraucht. Bleibt im Game-Strang.

## 2. Öffentliche moo-APIs im Game-Strang

Namens-Quelle: `compiler/src/codegen.rs` Pattern-Match-Aliase.

### 2D-Grafik
| moo-Name (DE / EN / Kurz) | Runtime-Fn |
|---|---|
| `fenster_erstelle` / `window_create` / `fe` | `moo_window_create` |
| `fenster_löschen` / `window_clear` / `fl` | `moo_window_clear` |
| `fenster_aktualisieren` / `window_update` / `fa` | `moo_window_update` |
| `fenster_offen` / `window_is_open` | `moo_window_is_open` |
| `fenster_schliessen` / `window_close` | `moo_window_close` |
| `zeichne_rechteck` / `draw_rect` / `zr` | `moo_draw_rect` |
| `zeichne_kreis` / `draw_circle` / `zk` | `moo_draw_circle` |
| `zeichne_linie` / `draw_line` | `moo_draw_line` |
| `zeichne_pixel` / `draw_pixel` | `moo_draw_pixel` |
| `taste_gedrückt` / `key_pressed` | `moo_key_pressed` |
| `maus_x` · `maus_y` · `maus_gedrückt` | `moo_mouse_*` |
| `warte` / `delay` | `moo_delay` |

### Sprites
`sprite_laden`, `sprite_zeichnen`, `sprite_zeichnen_skaliert`,
`sprite_ausschnitt`, `sprite_breite`, `sprite_hoehe`, `sprite_freigeben`
(+ EN-Aliase `sprite_load` / `sprite_draw` / etc.) → `moo_sprite_*`.

### 3D + Chunks
`raum_erstelle|3d_create|re`, `raum_offen|3d_is_open`,
`raum_löschen|3d_clear`, `raum_aktualisieren|3d_update`,
`raum_schliessen|3d_close`, `raum_perspektive`, `raum_kamera|rk`,
`raum_rotiere`, `raum_verschiebe`, `raum_push|raum_pop`, `raum_würfel`,
`raum_kugel`, `raum_dreieck`, `raum_taste`, `raum_maus_fangen`,
`raum_maus_dx|dy`, `chunk_erstelle|chunk_create`,
`chunk_beginne|chunk_begin`, `chunk_ende|chunk_end`,
`chunk_zeichne|chunk_draw`, `chunk_lösche|chunk_delete` →
`moo_3d_*` / `moo_3d_chunk_*`.

### Welt-Engine (öffentliche Wrapper in `stdlib/welt.moo`)
`welt_erstelle`, `welt_offen`, `welt_aktualisieren`, `welt_beenden`,
`welt_seed`, `welt_biom`, `welt_baeume`, `welt_hoehe_bei`, `welt_sonne`,
`welt_nebel`, `welt_meeresspiegel`, `welt_render_distanz`,
`welt_tageszeit` — je als dünner moo-Wrapper um `__welt_*` (siehe §4).

### Test-API
`screenshot`, `taste_simulieren`, `maus_simulieren` → `moo_screenshot`,
`moo_simulate_key`, `moo_simulate_mouse`.

## 3. Runtime-Dateien im Game-Strang

Pfad `compiler/runtime/` · **keine Moves in Phase A**.

| Datei | Rolle | Ziel-Pfad in Phase B (Vorschlag) |
|---|---|---|
| `moo_graphics.c` | 2D-Renderer + 2D-Input + Test-API | `runtime/game/graphics2d.c` |
| `moo_sprite.c` | Sprite-Slot-Array + SDL-Texture | `runtime/game/sprite.c` |
| `moo_3d.c` | Backend-Dispatcher + MooValue→Backend | `runtime/game/3d.c` |
| `moo_3d_backend.h` | Backend-Interface (Function-Pointer-Struct) | `runtime/game/3d_backend.h` |
| `moo_3d_math.c/.h` | Shared 4×4-Matrix + LookAt/Perspective | `runtime/game/3d_math.*` |
| `moo_3d_gl21.c` | Backend: OpenGL 2.1 | `runtime/game/backends/gl21.c` |
| `moo_3d_gl33*.c/.h` | Backend: OpenGL 3.3 (inkl. Mesh + Shader-Header) | `runtime/game/backends/gl33/` |
| `moo_3d_vulkan*.c/.h` | Backend: Vulkan (Core + Memory + Sync + Shader-SPIRV) | `runtime/game/backends/vulkan/` |
| `moo_3d_terrain.c/.h` | Perlin + FBM (wird von Welt-Engine genutzt) | `runtime/game/terrain.*` |
| `moo_world.c` | prozedurale Welt (Chunks + Day-Night + Physik) | `runtime/game/world.c` |
| `moo_world_daynight.h` | Sonnen-/Farb-Ableitung | `runtime/game/world_daynight.h` |
| `moo_test_input.c` | Test-API: Key-/Mouse-Sim + BMP-Save | `runtime/game/test_input.c` |
| `test_3d.c` | 3D-Smoke-Binary (Dev-only) | `tests/runtime/test_3d.c` |

Andere Runtime-Dateien (Kern/System/Domänen non-game) bleiben außerhalb —
siehe `spec/runtime_layers.md` (k3-Lead).

## 4. Kompatibilitätswrapper, die vorläufig bleiben müssen

### `__welt_*`-Prefix-Pattern (codegen.rs:2374ff)
Die Runtime-Symbole `moo_world_*` sind im Compiler **nur** über die
interne Form `__welt_erstelle` / `__world_create` etc. aufrufbar; die
kompakten `welt_*`-Namen werden in `stdlib/welt.moo` als moo-Funktionen
definiert, die ihrerseits `__welt_*` aufrufen. **Grund**: historisch
entstandene Layer-B-Namespace-Trennung (Compiler-Builtin vs. User-API).
**Folge für Phase B**: das Pattern muss erhalten bleiben, damit
bestehender User-Code (`importiere welt` → `welt_erstelle(...)`) nicht
bricht. Weder `__welt_*` noch `stdlib/welt.moo` dürfen in Phase B
verschwinden.

### `stdlib/welt.moo` als Wrapper-Layer
Aktuell 13 Wrapper für 13 Runtime-Builtins (seit commit 4a39983 auch
`welt_baeume`). Vollständiges Inventar: `/tmp/moo-verify/welt_api_inventar.md`.

### Test-API Legacy-Namen
`screenshot` (EN) / `bildschirmfoto` (DE): beide Namen aktiv. Kein
Ausbau geplant.

### `chunk_*`-Builtins direkt (kein Wrapper-Layer)
`chunk_erstelle` etc. werden im Codegen direkt gemappt. Kein
`stdlib/chunk.moo`. In Phase B weiter so.

### `raum_*` + `3d_*` Doppel-Namespacing
Jede 3D-Funktion existiert unter **vier** Namen: `raum_X`, `space_X`,
`3d_X`, teils Kurzform. In Phase B nicht anrühren — beide Sprachfarben
(Deutsch + Englisch) sind gewollt.

## 5. Dokumentation + Beispiele im Game-Strang

### `docs/referenz/*.md` (moolang-docs-Repo)
Zu Game zählen: `grafik-2d.md`, `sprites.md`, `grafik-3d.md`, `welt.md`,
`test-api.md`. Bereits in der mkdocs-Nav in einer eigenen Sektion
"3D & Game-Dev Modul" (getrennt von "Sprache & Stdlib") seit
commit 863ae6f. ✓

### `beispiele/*.moo`
Game-Beispiele (~40 von 130, nicht abschließend):
`pong.moo`, `breakout.moo`, `asteroids.moo`, `bomberman.moo`,
`brawler.moo`, `columns.moo`, `connect4.moo`, `defender.moo`,
`doodle_jump.moo`, `fighter.moo`, `snake.moo`, `tetris.moo`,
`zelda.moo`, `3d_demo.moo`, `welten.moo`, `welt_test.moo`,
`block_dude.moo`, `boulder_dash.moo`, `astar.moo` (AI-Demo),
`bubble_shooter/`, `dungeon.moo`, `raytracer/`, `test_pong.moo`
(+ weitere Test-Suites). **Ziel-Struktur in Phase B**:
`examples/game/` (flach oder nach Genre 2d/3d unterteilt).

## 6. Was explizit NICHT im Game-Strang ist

Beispiele zur Abgrenzung (damit Phase B nicht versehentlich zu breit schneidet):
- `blog_engine.moo`, `chat_server.moo`, `http_api.moo`, `mqtt_broker.moo`,
  `dns_resolver.moo`, `proxy.moo`, `websocket_server.moo` → Domäne
  Web/Net, nicht Game.
- `adressbuch.moo`, `cms_posts.moo`, `datei_suche.moo`, `elf_reader.moo`,
  `mysql_client.moo` → CLI/Data-Domäne.
- `ascii_editor.moo` → CLI-Tool (text-basiert, kein Fenster).

Diese bleiben in Phase B in `examples/` (kein `game/`-Prefix).

## 7. Kein-Break-Garantie für Phase B

Damit Phase B (Dateien verschieben) keinen User-Code bricht:

1. `importiere welt` muss funktional identisch bleiben — `stdlib/welt.moo`
   darf in Phase B verschoben werden (z.B. `modules/game/welt.moo`), aber
   der Import-Resolver muss diesen Pfad kennen.
2. Alle `moo_*`-C-Symbole behalten ihre Namen; nur die Dateien ziehen um.
3. Alle moo-API-Namen (`raum_*`, `welt_*`, `sprite_*`, `fenster_*`,
   `zeichne_*`, `chunk_*`, `screenshot`, `taste_simulieren`, …) bleiben
   unveränderlich.
4. `compiler/src/codegen.rs`-Pattern-Matches bleiben — oder werden in
   ein neu entstehendes `modules/game/bindings.rs`-Modul ausgelagert,
   das von `codegen.rs` re-exportiert wird (dann keine Pfad-Änderung
   am Aufrufer).
5. `.github/workflows/3d-backends.yml`-Matrix muss auf neue Pfade
   nachgezogen werden.

## 8. Nicht im Scope dieses Dokuments

- Konkrete Git-Move-Operationen (Phase B — separate Spec).
- Runtime-Umbauten (siehe E1-followup für Sprite-Auto-Free, P2c-followup
  für Vision-Wrapper).
- `moo_graphics.c` aufspalten in 2D-Renderer vs. Test-API — das wäre eine
  Designentscheidung in Phase B, nicht hier.
