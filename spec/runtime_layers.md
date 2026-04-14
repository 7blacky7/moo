# Runtime-Schichten

Kanonisches Schicht-Inventar aller Dateien unter `compiler/runtime/`. Phase A:
rein additiv, keine Datei wird bewegt. `future_target_path` ist der
Vorschlag fuer Phase B (kontrollierte Repo-Umbauten) — noch nicht zum
Ausfuehren, nur zur Orientierung.

## Schichten

- **KERN**: Wertmodell, Refcount, generische Ops, Low-Level-Fehler, Druck.
  Dazwischen liegt nichts — ohne diese Schicht kompiliert kein moo-Programm.
- **SYSTEM**: Hosted/Freestanding-Trennung, Dateisystem, Threads/Channels,
  Netzwerk-Sockets, Memory-Mapped I/O, WASM-Glue.
- **DOMAENEN**: Einzelne Fach-APIs, die auf KERN+SYSTEM aufbauen: DB,
  HTTP-Client, Webserver, 2D-/3D-Grafik, Sprites, Welt-Engine, Regex, JSON,
  Profiler, Eval.

## KERN (10 Dateien)

| Datei | Funktion | Public? | Depends on | future_target_path |
|-------|----------|---------|------------|--------------------|
| `moo_runtime.h` | ABI-Definition: `MooValue`, Tags, Heap-Typen | intern (ABI) | — | `runtime/kern/moo_runtime.h` |
| `moo_value.c` | Konstruktoren, Tagged-Union-Layout | intern | `moo_runtime.h` | `runtime/kern/moo_value.c` |
| `moo_memory.c` | Allokation, RC (`moo_retain`/`moo_release`), `moo_alloc`/`moo_free` | intern | `moo_runtime.h` | `runtime/kern/moo_memory.c` |
| `moo_ops.c` | Arithmetik, Vergleiche, Logik, Bitops, Fast-Paths fuer Zahlenlisten | intern (Codegen-Pfad) | `moo_value.c` | `runtime/kern/moo_ops.c` |
| `moo_string.c` | String-Heap, Concat, Slice, Contains, Split, Trim | oeffentlich (via `.ersetzen` etc) | KERN | `runtime/kern/moo_string.c` |
| `moo_list.c` | List-Heap, Append/Get/Set/Pop/Sort/Reverse/Contains/Join | oeffentlich (via Listen-Methoden) | KERN | `runtime/kern/moo_list.c` |
| `moo_dict.c` | Dict-Heap, Hash-Map, Get/Set/Has/Keys | oeffentlich | KERN | `runtime/kern/moo_dict.c` |
| `moo_object.c` | MooObject fuer Klassen, set_parent, Vererbung | oeffentlich (via `klasse`) | KERN | `runtime/kern/moo_object.c` |
| `moo_error.c` | Fehler-Factory (`moo_error(...)`), `moo_throw`, try/catch-State | intern (Codegen-Pfad) | KERN | `runtime/kern/moo_error.c` |
| `moo_result.c` | Result-Typ `ok`/`fehler`/`unwrap`/`is_ok`/`is_err` | oeffentlich | KERN | `runtime/kern/moo_result.c` |
| `moo_print.c` | `zeige`/`show` + `moo_to_string` | oeffentlich (via `zeige`) | KERN | `runtime/kern/moo_print.c` |
| `moo_core.c` | Bootstrap, Kern-Registrierung, Init-Hooks | intern | KERN | `runtime/kern/moo_core.c` |

## SYSTEM (8 Dateien)

| Datei | Funktion | Public? | Depends on | future_target_path |
|-------|----------|---------|------------|--------------------|
| `moo_bare.c` | Freestanding/no-std Runtime (nur Zahlen, Bool, None, `mem_read`/`mem_write`) | oeffentlich (mit `--no-stdlib`) | KERN-Tag-Layout | `runtime/system/moo_bare.c` |
| `moo_file.c` | Datei-I/O, Dir-Listing | oeffentlich | KERN | `runtime/system/moo_file.c` |
| `moo_thread.c` | pthreads + Channels | oeffentlich | KERN | `runtime/system/moo_thread.c` |
| `moo_net.c` | TCP/UDP-Sockets, `smart_close`-Dispatcher | oeffentlich | KERN | `runtime/system/moo_net.c` |
| `moo_memory.c` | (bereits KERN — kein SYSTEM-Bezug) | — | — | — |
| `moo_test_input.c` | Simulierte Tastatur-/Maus-Events fuer Tests | oeffentlich (Test-API) | KERN | `runtime/system/moo_test_input.c` |
| `moo_wasm.c` | Browser/WASM-Glue, `moo_release` fuer WASM | intern | KERN | `runtime/system/moo_wasm.c` |
| `moo_stdlib.c` | Stdlib-Bootstrap (Math, Random, Zeit, Typcheck) | oeffentlich | KERN | `runtime/system/moo_stdlib.c` |

## DOMAENEN (27 Dateien)

| Datei | Domaene | Public? | Depends on | future_target_path |
|-------|---------|---------|------------|--------------------|
| `moo_db.c` | SQLite: connect/execute/query/close + `_mit_params` + Statement-Objekt | oeffentlich | KERN | `runtime/domain/db/moo_db.c` |
| `moo_json.c` | JSON parse/serialize | oeffentlich | KERN | `runtime/domain/json/moo_json.c` |
| `moo_http.c` | HTTP-Client (libcurl) + `_with_headers` | oeffentlich | KERN+SYSTEM | `runtime/domain/http/moo_http.c` |
| `moo_web.c` | HTTP-Server, Template-Engine, `_with_headers` | oeffentlich | KERN+SYSTEM (`moo_net.c`) | `runtime/domain/web/moo_web.c` |
| `moo_regex.c` | POSIX-Regex | oeffentlich | KERN | `runtime/domain/regex/moo_regex.c` |
| `moo_crypto.c` | SHA-1/SHA-256, Base64, secure_random, sanitize_html/sql | oeffentlich | KERN | `runtime/domain/crypto/moo_crypto.c` |
| `moo_profiler.c` | profile_enter/exit/report | oeffentlich (Test-API) | KERN | `runtime/domain/profiler/moo_profiler.c` |
| `moo_eval.c` | Eval-Stub (moo-Code zur Laufzeit parsen) | oeffentlich | KERN | `runtime/domain/eval/moo_eval.c` |
| `moo_graphics.c` | 2D-SDL: Fenster, Pixel, Linien, Kreise, Text | oeffentlich (Game-Strang) | KERN+SYSTEM | `runtime/domain/game/moo_graphics.c` |
| `moo_sprite.c` | Sprite-Load/Draw/Frei | oeffentlich (Game-Strang) | `moo_graphics.c` | `runtime/domain/game/moo_sprite.c` |
| `moo_3d.c` | 3D-Frontend (Dispatcher auf Backend) | oeffentlich (Game-Strang) | KERN | `runtime/domain/game/moo_3d.c` |
| `moo_3d_backend.h` | Backend-Interface-Struct (function pointers) | intern | — | `runtime/domain/game/moo_3d_backend.h` |
| `moo_3d_math.c`/`.h` | 3D-Math (Matrix, Perspective, LookAt) | intern (Backend-shared) | — | `runtime/domain/game/moo_3d_math.*` |
| `moo_3d_gl21.c` | OpenGL 2.1 Backend | intern (Backend) | `moo_3d_backend.h` | `runtime/domain/game/backend_gl21/moo_3d_gl21.c` |
| `moo_3d_gl33.c` + `_mesh.c/.h` + `_shaders.h` | OpenGL 3.3 Backend | intern (Backend) | `moo_3d_backend.h` | `runtime/domain/game/backend_gl33/...` |
| `moo_3d_terrain.c`/`.h` | Terrain-Meshing fuer Chunks | intern (Backend-shared) | — | `runtime/domain/game/moo_3d_terrain.*` |
| `moo_3d_vulkan.c` + `_mem.c/.h` + `_sync.h` + `_shaders.h` + `_*_spv.h` | Vulkan Backend | intern (Backend, optional) | `moo_3d_backend.h` | `runtime/domain/game/backend_vulkan/...` |
| `moo_world.c` | Prozedurale Welt-Engine (Perlin, Chunks, Biomes, Tag/Nacht) | oeffentlich (via stdlib/welt.moo) | `moo_3d.c` | `runtime/domain/game/moo_world.c` |
| `moo_world_daynight.h` | Tag/Nacht-Presets | intern | — | `runtime/domain/game/moo_world_daynight.h` |
| `test_3d.c` | Manueller 3D-Smoke (kein Automation-Test) | intern | — | `runtime/domain/game/tests/test_3d.c` |

## Sonderfälle

- `moo_runtime.h` ist kein reiner Header einer KERN-Datei — es ist die
  **globale ABI-Datei** fuer alle C-Runtime-Module. Liegt formal in KERN,
  aber in Phase B sollte er symbolisch als `runtime/abi.h` sichtbar werden.
- `moo_wasm.c` und `moo_bare.c` sind **Alternativen** zur vollen Runtime,
  keine Ergaenzung — Phase B sollte sie klarer als "Profile" markieren
  (`runtime/system/profile_bare.c`, `runtime/system/profile_wasm.c`).
- `moo_3d_*`-Backends sind mutually-exclusive (Cargo-Feature-Flags). Gehoeren
  gemeinsam in ein Unterverzeichnis.

## Gate

Phase A ist abgeschlossen, sobald diese Datei gemerged ist und
`public_vs_internal_api.md` (B3) + `game_module_boundary.md` (k4, B2) stehen.
Erst dann darf irgendeine Runtime-Datei auf einen `future_target_path`
verschoben werden — mit Git-Move + Symlink/Include-Shim fuer Kompat.
