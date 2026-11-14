# moo — Umfassende Projekt-Analyse (DEEP DIVE)

**Ein deutschsprachiges Programmiersprachen-Ökosystem mit integrierten Tensor-Ops, GPU-Compute, Vulkan-Backend und vollständiger LLM-Training-Pipeline.**

---

## I. DIE SPRACHE: Deutsch/Englisch Äquivalenz

### A. Kern-Keywords (spec/tokens.yaml — Source of Truth)

**100+ Tokens, alle mit Deutsch/Englisch/Kurzform-Varianten:**

**Kontrolle:**
- `setze`/`set`/`se` + `auf`/`to`
- `wenn`/`if`/`we` `sonst`/`else`/`so`
- `solange`/`while`/`sl` / `für`/`for`/`fu`
- `funktion`/`func`/`fn` / `gib_zurück`/`return`/`gr`

**OOP:**
- `klasse`/`class`/`kl` / `neu`/`new` / `selbst`/`this`
- `schnittstelle`/`interface` / `implementiert`/`implements`

**Fehler:**
- `versuche`/`try`/`ve` (Soft-Keyword!) / `fange`/`catch` / `wirf`/`throw`
- `stopp`/`break`/`st` / `weiter`/`continue`/`wt`

**Pattern Matching:**
- `prüfe`/`match`/`pr` (Soft-Keyword!) + `fall`/`case`/`fa` + `standard`/`default`

**Module:**
- `importiere`/`import`/`im` + `aus`/`from` / `exportiere`/`export` / `als`/`as`

**Erweitert:**
- `aufräumen`/`defer` / `daten`/`data` (Soft-Keyword) / `garantiere`/`guard`
- `wähle`/`select` / `sortiere`/`order` (Soft-Keyword) / `wo`/`where` (Soft-Keyword)
- `parallel` / `vorbedingung`/`precondition` / `nachbedingung`/`postcondition` / `unsicher`/`unsafe`/`un`
- `teste`/`test` (Soft-Keyword) / `erwarte`/`expect` (Soft-Keyword)

### B. Soft-Keyword-System (spec/soft_keywords.md)

**Besonderheit:** Wörter wie `zeige`, `versuche`, `prüfe`, `daten`, `teste` sind in KONTEXT Schlüsselwörter, können aber auch Identifier sein!

```moos
setze zeige auf "value"           # zeige als Variablenname
zeige zeige                       # zeige als Befehl, dann Variable
```

**Aktuelle Soft-Keywords:** `Show`, `Try`, `Match`, `Data`, `Where`, `Order`, `Test`, `Expect` (8 total)

**Bekannte Konflikte:** `erwarte` als Funktionsname → Parser-Verwirrung (gemeldeter Bug, Soft-Keyword-Lösung ausstehend)

---

## II. ARCHITEKTUR: 5-Schichten-Modell

### L0: Projektidentität
- `pyproject.toml`, `compiler/Cargo.toml` (mise-Manager)
- `.github/workflows/` (CI: 3d-backends, Regression-Tests)
- Lizenz-TBD

### L1: Sprachoberfläche & Spezifikation
- **`spec/tokens.yaml`** — Global Source of Truth (alle Keywords)
- **`compiler/src/tokens.rs`** — Rust Lexer (262 Zeilen, Token-Enum + keyword_lookup)
- **`src/moo/tokens.py`** — Python Toolchain (LSP, Formatter, Transpiler)
- Parser, Codegen, Compiler/Binding-Aliases

### L2: Runtime-Kern (10 C-Dateien, 16-Byte MooValue)

**ABI:**
```c
MooValue = 16 Bytes
  Tag: Integer=0, Float=1, String=2, List=3, Dict=4, Object=5, None=6, Boolean=7, ... Tensor=15
  Data: 12 Bytes (payload oder Pointer)
Refcount-Speicher: alle Heap-Typen mit refcount als erstes Feld
```

**Dateien:**
| Datei | Funktion | LOC |
|-------|----------|-----|
| `moo_runtime.h` | ABI-Definition, MooValue-Layout | Header |
| `moo_value.c` | Konstruktoren, Tagged-Union | Intern |
| `moo_memory.c` | Allokation, RC (moo_retain/release), ASan/UBSan-Gates | Intern |
| `moo_ops.c` | Arithmetik (+/-/*//%/**), Bitops, Fast-Paths | Intern |
| `moo_string.c` | String-Heap, Concat, Slice, Contains, Split, Trim | Public |
| `moo_list.c` | List-Heap, Append, Sort, Reverse, Join, Map, Filter | Public |
| `moo_dict.c` | Hash-Map, Get/Set/Has/Keys | Public |
| `moo_object.c` | Klassen, Vererbung, set_parent | Public |
| `moo_error.c` | Fehler-Factory, try/catch-Infrastruktur | Intern |
| `moo_print.c` | zeige/show, moo_to_string | Public |

### L3a: System (8 Dateien)

| Datei | Funktion | Public |
|-------|----------|--------|
| `moo_bare.c` | Freestanding-Runtime (--no-stdlib, nur Zahlen+Bool+None) | Ja |
| `moo_file.c` | Datei-I/O, Dir-Listing, mtime, is_dir | Ja |
| `moo_thread.c` | pthreads + Channels (gepuffert) | Ja |
| `moo_net.c` | TCP/UDP Sockets, DNS (IPv4/IPv6) | Ja |
| `moo_test_input.c` | Tastatur-/Maus-Simulation für Tests | Ja |
| `moo_wasm.c` | Browser/WASM-Glue | Intern |
| `moo_stdlib.c` | Math (sin/cos/tan/atan2), Random, Zeit, Typcheck | Ja |
| `moo_bare_*` | Bootloader, ARM64 Entry, Alloc-Stubs | System |

### L3b: Domänen (27+ Dateien)

**Datenbank:**
- `moo_db.c` — SQLite: connect/execute/query + Prepared Statements + db_vorbereite (P3c-A)

**Web/HTTP:**
- `moo_http.c` — HTTP/1.1-Client (DNS, IPv6, Chunked, Redirects, HTTPS, Headers-API P2b)
- `moo_web.c` — HTTP-Server, Template-Engine

**Grafik & Spiele:**
- `moo_graphics.c` — 2D-SDL: Fenster, Pixel, Linien, Kreise, Text (256 Sprites max)
- `moo_sprite.c` — Sprite-Load/Draw (BMP/PNG via SDL_image)
- `moo_3d.c` — 3D-Dispatcher (GL21/GL33/Vulkan wählbar via ENV)
- `moo_3d_backend.h` — Backend-Interface (function pointers)
- `moo_3d_gl21.c` — OpenGL 2.1 Backend (Legacy)
- `moo_3d_gl33.c` + `_mesh.c` + `_shaders.h` — OpenGL 3.3 Backend
- `moo_3d_vulkan.c` + `_mem.c` + `_sync.h` + `_shaders.h` + SPIR-V `.h` — **Vulkan Backend (GPU-optional)**
- `moo_3d_math.c` — Matrix, Perspective, LookAt
- `moo_3d_terrain.c` — Terrain-Meshing, Perlin Noise + FBM
- `moo_world.c` — Prozedurale Welt-Engine (Chunks, Biomes, Tag/Nacht, Physik)

**Utility:**
- `moo_json.c` — JSON Parse/Serialize
- `moo_regex.c` — POSIX-Regex
- `moo_crypto.c` — SHA-1/256, Base64, secure_random, sanitize_html/sql
- `moo_profiler.c` — profile_enter/exit/report

**KI/ML-Core (NEU):**
- `moo_autograd.c` — **63 KB(!), Reverse-Mode Autograd mit 23 Backward-Ops** (gradcheck-Gate über gesamte Registry)
- `moo_ki_gpu_api.h` — GPU-Tensor-API (Vertrag zwischen CPU-Autograd und GPU-Kernels)
- `moo_tensor.c` — Tensor-Grundlagen (shape, strides, refcount)
- `moo_tensor_ops.c` — Operator-Registry (matmul, add/sub/mul/div, softmax, exp/log/sqrt, RoPE, Gather, etc.)
- `moo_nn.c` — Neural Network Layers (RMSNorm, RoPE, SwiGLU, GQA/MQA, KV-Cache, MoE, GRPO)
- `moo_aes_native.c` — AES Kryptografie (Encrypt/Decrypt, GCM mode)
- `moo_audio.c` — Audio Sample Generation

**System/Bare-Metal:**
- `moo_bare_boot.c` — Bootloader (x86_64 Multiboot2)
- `moo_bare_entry_arm64.c` — ARM64 Entry + UART/Timer/Interrupts
- `moo_bare_alloc.c` — Memory Management (Bare-Metal)
- `moo_bare_console.c` — UART Console Output

**Accessibility & Platform:**
- `moo_a11y_core.c` — Core A11y (Screen Reader hooks)
- `moo_a11y_win32.c` — Windows Accessibility API

---

## III. RUNTIME-SCHICHTEN IM DETAIL (spec/runtime_layers.md)

Struktur:
```
KERN (10) — Values, Refcount, Ops, Error, Print
    ↓
SYSTEM (8) — File, Thread, Net, Stdlib
    ↓
DOMAENEN (27+) — DB, HTTP, Grafik, KI, etc.
```

**Phase B Zielstruktur (Reorganisation):**
```
compiler/runtime/kern/
                /system/
                /domain/db/
                       /json/
                       /http/
                       /web/
                       /game/{graphics2d,sprite,3d,world}
                            /backends/{gl21,gl33,vulkan}
                       /crypto/
                       /regex/
                       /ki/
                       /profiler/
                       /audio/
                /system/bare/
```

---

## IV. SPRACHFEATURES

### A. Syntax-Ebene

```moos
# Variables + Funktionen
setze x auf 42
konstante PI auf 3.14159
funktion quadrat(n):
    gib_zurück n * n

# Klassen + Vererbung
klasse Person:
    funktion __init__(name, alter):
        selbst.name = name
        selbst.alter = alter
    
    funktion info():
        gib_zurück selbst.name + " (" + selbst.alter + ")"

p = neu Person("Alice", 30)

# Kontrollfluß
wenn x > 0:
    zeige "positiv"
sonst:
    zeige "nicht-positiv"

solange x < 100:
    setze x auf x + 1

für element in liste:
    zeige element

# Pattern Matching (Soft-Keyword)
prüfe typ:
    fall "int":
        # ...
    fall "string":
        # ...
    standard:
        # ...

# Try/Catch
versuche:
    setze result auf gefährliche_funktion()
fange fehler:
    zeige "Fehler: " + fehler

# Lambdas & First-Class Functions
setze square auf (x) => x * x
starte(worker, 42)                  # worker ist eine Funktion

# Pattern in Comprehensions
setze quadrate auf [x * x für x in [1,2,3,4,5]]

# F-Strings
zeige f"Ergebnis: {result}"

# Optional Chaining + Nullish Coalescing
setze wert auf obj?.property ?? default_val

# Soft-Keywords in Identifier-Kontext
setze zeige auf 5
```

### B. Datentypen

- **Number** (Int/Float, dynamisch)
- **String** (UTF-8, Methods: .länge(), .ersetzen(), .split(), .gross(), .klein(), .trimmen())
- **Boolean** (wahr/falsch, true/false)
- **None** (nichts/none)
- **List** (Methods: .append(), .pop(), .sort(), .reverse(), .map(), .filter(), .join())
- **Dict** (Methods: .hat(), .keys(), .values())
- **Object** (Klasseninstanzen)
- **Result** (ok(x) / fehler(x), .is_ok, .is_err, .unwrap())
- **Tensor** (NEU — siehe KI-Stack unten)

---

## V. BEISPIELE: 130+ Dateien, strukturiert

### Zone: Quickstart (Einstieg)
- `fibonacci.moos` — Zahlen/Schleifen
- `taschenrechner.moos` — CLI
- `todo.moos` — Listen+Dict
- `adressbuch.moos` (53 LOC) — **SQLite Adressbuch-Manager**
- `wetter_api.moos` — HTTP-Client

### Zone: Showcase (Flaggschiff-Demos)
- **`welten.moos`** (400+ LOC) — Prozedurale 3D-Welt (Perlin, Chunks, Biomes)
- `welt_test.moos` — `importiere welt` Demo
- `3d_demo.moos` — Minimales 3D
- `zelda.moos` — Top-Down Sprite-Atlas
- `pong.moos` (206 LOC) — 2D-Game
- `breakout.moos` (206 LOC) — Collision + Score
- `snake.moos` — 2D-Klassiker
- `blog_engine_v2.moos` — Web + DB + Cookies
- `chat/chat_server.moos` — Long-Polling + SQLite
- `raytracer/` — Multi-File Math-Heavy
- `http_api.moos`, `http_api_cors.moos` — REST-API

### Zone: Stress (Performance/Grenzfälle)
- `benchmark.moos` (67 LOC) — Microbench
- `memory.moos` — Allokations-Stresstest
- `stackvm.moos` — Bytecode-Interpreter
- `sat_solver.moos` — Backtracking
- **`regex_engine.moos`** — NFA/DFA in moo
- `mini_lisp.moos` — Eval-Loop
- `mini_sql.moos` — Query-Parser
- `x86dis.moos` — Disassembler
- `bf_interp.moos` (404 LOC) — Brainfuck-Interpreter
- `tar_reader.moos`, `elf_reader.moos` — Binary-Parsing
- `png_encoder.moos`, `gif_encoder/` — Bild-Serialisierung
- `synth.moos`, `midi_player/` — Audio

### Zone: Domain (echte Anwendungen)

**domain/web/**
- `blog_engine_v2.moos` (Production) — Prepared Statements + Cookies
- `chat/chat_server.moos` (Production) — Long-Polling + SQLite
- `http_api.moos`, `http_api_cors.moos` (Production) — REST-API
- `cms_posts/` (Production) — Mini-CMS
- `mqtt_broker.moos`, `websocket_server.moos` (Demo)
- `proxy.moos` — TCP-Reverse-Proxy
- `dns_resolver.moos` — RFC-1035

**domain/db/**
- `mini_db/` (Production) — Prepared-Statements-Referenz
- `mini_redis/` (Demo)
- `mysql_client.moos`, `postgres_client.moos` (Demo)

**domain/game/** (60+ Spiele)
Production: pong, breakout, snake, tetris, asteroids, pacman, zelda, welten, 3d_demo, invaders, sokoban, solitaire, connect4, minesweeper, bomberman, frogger, tower_defense, tic_tac_toe, flappy, boulder_dash (300 LOC), galaga, missile_command, etc.

Experimentell: adventure (740 LOC, Abenteuer-Spiel!), dungeon (599 LOC), brawler (455 LOC), defender (527 LOC), columns (375 LOC), platformer, rpg_world, etc.

**domain/system/**
- `kernel_hello.moos` (Bare-Metal Hello)
- `elf_reader.moos`, `tar_reader.moos` (Binary-I/O)
- `datei_suche/`, `explorer/` (CLI-Tools)
- `ascii_editor.moos` (293 LOC, TUI-Editor)

### Größte Beispiele nach LOC
1. adventure.moos — 740
2. dungeon.moos — 599
3. datalog.moos — 531
4. defender.moos — 527
5. astar.moos — 482
6. brawler.moos — 455
7. bomberman.moos — 450
8. bf_interp.moos — 404
9. columns.moos — 375
10. bubble_shooter.moos — 363

---

## VI. DAS ADRESSBUCH-BEISPIEL IN VOLLEN ZÜGEN

```moos
zeige "=== moo Adressbuch ==="

setze db auf db_verbinde("adressbuch.db")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS kontakte 
    (id INTEGER PRIMARY KEY, name TEXT, email TEXT, telefon TEXT)")

funktion kontakt_hinzufuegen(db):
    setze name auf eingabe("Name: ")
    setze email auf eingabe("Email: ")
    setze telefon auf eingabe("Telefon: ")
    db_ausführen(db, "INSERT INTO kontakte (name, email, telefon) 
        VALUES ('" + name + "', '" + email + "', '" + telefon + "')")
    zeige "Kontakt hinzugefuegt!"

funktion kontakte_auflisten(db):
    setze ergebnis auf db_abfrage(db, 
        "SELECT id, name, email, telefon FROM kontakte ORDER BY name")
    für kontakt in ergebnis:
        zeige kontakt

funktion kontakt_suchen(db):
    setze suchbegriff auf eingabe("Suche: ")
    setze ergebnis auf db_abfrage(db, 
        "SELECT id, name, email FROM kontakte WHERE name LIKE '%" 
        + suchbegriff + "%'")
    für kontakt in ergebnis:
        zeige kontakt

funktion kontakt_loeschen(db):
    setze id auf eingabe("ID: ")
    db_ausführen(db, "DELETE FROM kontakte WHERE id = " + id)
    zeige "Geloescht."

# Hauptmenue
solange wahr:
    zeige ""
    zeige "[1] Hinzufuegen [2] Auflisten [3] Suchen [4] Loeschen [5] Ende"
    setze wahl auf eingabe("> ")
    prüfe wahl:
        fall "1":
            kontakt_hinzufuegen(db)
        fall "2":
            kontakte_auflisten(db)
        fall "3":
            kontakt_suchen(db)
        fall "4":
            kontakt_loeschen(db)
        fall "5":
            db_schliessen(db)
            zeige "Tschuess!"
            stopp
        standard:
            zeige "1-5 eingeben."
```

**Features gezeigt:** Deutsch-Syntax, SQLite-Integration, Funktionen, Pattern Matching, Schleifen, User Input/Output.

---

## VII. KI/LLM-STACK (docs/kip/) — THE SUPERPOWER

### A. Tensor-Engine (moo_autograd.c, 63 KB)

**Reverse-Mode Autograd mit 23 Backward-Op-Gates:**

```moos
# MNIST-Beispiel aus eval_m6b_mnist.moos
funktion m6b_int8_pc_dequant(t):
    setze a auf t.zu_liste()
    setze form auf t.form()
    # ... Quantization-Dequantization ...
    gib_zurück tensor_aus_liste(ausgabe_liste)

# Tensor-Konstruktion
setze gewichte auf tensor_nullen([784, 128])      # 784x128 Matrix
setze input auf tensor_zufall([batch_size, 784], seed)
setze output auf tensor_neu([batch_size, 10], 0)  # 10 Klassen

# Forward-Pass mit Autograd
setze hidden auf matmul(input, gewichte)
setze logits auf nn_softmax(hidden)
setze loss auf kreuzentropie(logits, labels)

# Backward-Pass
loss.backward()
```

### B. Neural Network Layer (moo_nn.c)

Implementiert:
- **RMSNorm** — Root-Mean-Square Normalization
- **RoPE** — Rotary Position Embeddings (mit Context-Scaling)
- **GQA/MQA** — Grouped/Multi-Query Attention
- **SwiGLU** — Swish-Gated Linear Unit
- **KV-Cache** — Incremental Decoding
- **Mixture-of-Experts (MoE)** — Sparse Gating
- **Multi-Token Prediction (MTP)** — Speculative Decoding
- **GRPO-Toy** — Group Relative Policy Optimization

### C. GPU-Compute (Vulkan, moo_3d_vulkan.c + Shader-SPIR-V)

**KIP-G-Strang (GPU-Architektur):**
- **G1:** Device-Modell + Tensor-Repräsentation (f32 CPU, bf16 GPU, komprimiert)
- **G2:** Tiled Matmul (shared memory, bank-conflict-aware)
- **G3a:** Softmax/Softmax Kernel
- **G3b:** Fused Norm-Kernel
- **G3c:** Adam Optimizer auf GPU + Gradient Accumulation
- **G3d:** Elementwise Ops, Reduktionen, Gather/Scatter
- **G4:** Production Wiring (RoPE Kernel, GQA Head-Slicing)

**Vertrag (KIP-G0):**
- CPU-Autograd-Tape bleibt unverändert
- Backward-Ops haben GPU-Pfade (NVIDIA SPIR-V Shader)
- MOO_KI_GPU_STRIKT=1 → CPU-Fallback im Hot-Path = Fehler
- 20+ Ops müssen GPU-Gradient-Kernel haben (Audit-Matrix)

### D. DType-System (KIP-D-Strang)

**KIP-D0:** Design (Dual-Buffer statt void*-Umbau)
```
MooTensor = {
    float* data          // f32-Compute-Buffer (IMMER gültig)
    float* grad          // f32-Gradient (IMMER)
    uint8_t dtype        // MOO_DT_F32=0, MOO_DT_BF16=1
    void* store          // bf16-Storage-Buffer (NULL bei f32)
    uint8_t valid        // Bitmaske: DATA|STORE|GPU
}
```

**KIP-D1:** Direct-Access-Audit (74 ->data[, 27 ->grad[)
- Eintrittspunkt-Zentrale (expect_t-Funktion)
- Sicherungs-Funktion vor Zugriff
- Invalidierungs-Vertrag nach Mutations-Schreiber

### E. Daten-Pipeline (KIP-E-Strang)

- **E1:** Shard-Format (Multi-GB TokenShards, Streaming)
- **E2:** Resumption (Checkpoint Saving/Loading)
- **E3:** Fuzzing (Differential Rust vs Python)

### F. Trainingsziele (KIP-P0 — Meilensteine)

| Meilenstein | Parameter | Hardware | Budget | Exit-Gate |
|-------------|-----------|----------|--------|-----------|
| **M-A** | 10M (Dense-Transformer) | RTX 4070 Ti (12 GB) | <24h | Val-Loss reproduzierbar, 2 Full Steps Forward+Backward+Optimizer |
| **M-B** | 100M (GPT-2-small) | " | Tage | Perplexity > M-A, Full Checkpoint Rotation, SFT-Mini-Lauf |
| **M-C** | 1B (Stretch) | Nur Inference aktuell | — | Nach M-B-Review |

### G. Determinismus & Reproduzierbarkeit

**Gates:**
- MNIST Gate: >95% Accuracy, <5min, seed-reproduzierbar
- Char-LM Gate: Loss-Kurve CPU/GPU innerhalb Toleranz
- Gradcheck Gate: Alle 23 Backward-Ops verifiziert
- Loss-Parität Gate: CPU↔GPU Kurven identisch (float-Toleranz)

---

## VIII. PUBLIC VS. INTERNAL API (spec/public_vs_internal_api.md)

**3-Schichten-Matrix:**
- **A:** C-Runtime-Symbol (moo_*)
- **B:** Compiler/Binding-Alias
- **C:** Public moo-API (User sieht)

**PUBLIC APIs:**
- `zeige`/`show` — Ausgabe
- `db_verbinde`, `db_abfrage`, `db_ausführen` (+ P3c: `db_vorbereite`, `.binde`, etc.)
- `http_hole`/`http_get` (+ P2b: `_mit_headers`)
- `fenster_erstelle`, `zeichne_rechteck`, `sprite_laden`
- `raum_erstelle`, `chunk_erstelle` (3D)
- `tensor_neu`, `tensor_form`, `tensor_zufall` (KI)
- `starte`/`spawn` (Threads), `kanal`/`channel` (Channels)
- `tcp_verbinde`, `web_server` (Net)

**INTERNAL APIs:**
- `moo_retain`/`moo_release` — RC-Management
- `moo_error`/`moo_throw` — Fehler-Infrastruktur
- `__welt_*` — Raw World-Bindings (nur via stdlib/welt.moos)

---

## IX. TEST-TAXONOMIE (spec/tests_taxonomy.md)

| Zone | Zweck | Pfad | Anzahl | CI |
|------|-------|------|--------|-----|
| **Zone 1** | Regressions-Tests (Compiler+Runtime) | `compiler/tests/` | 27 .moos | **PFLICHT** |
| **Zone 2** | Ad-hoc Scratchpad (Historisch) | Root `test_*.moos` | 38 | Keine (Phase B → `tests/scratchpad/`) |
| **Zone 3** | 3D-Smoke (Manuell) | `compiler/runtime/test_3d.c` | 1 C | Keine |
| **Zone 4** | Ephemer (Session-Verifizierung) | `/tmp/moo-verify/` | 107+ | Keine |
| **Zone 5** | Audit-Befunde (Ephemer) | `/tmp/moo-audit/` | — | Keine |

**CI-Matrix:**
- `compiler/tests/run_all.sh` — Pflicht (0 failed)
- `.github/workflows/3d-backends.yml` — GL21/GL33 Pflicht, Vulkan optional
- `tools/fuzz_differential.py` — Empfohlen (Rust vs Python Drift)

**Phase B Zielstruktur:**
```
tests/
  compiler/test_*.moos + .expected
  runtime/3d/smoke.c
  scratchpad/
  transpiler/
  differential/
```

---

## X. SOFT-KEYWORDS IM DETAIL (spec/soft_keywords.md)

**Das Besondere an moo:** Ein Wort kann KONTEXT-abhängig sein!

```moos
# zeige ist ein Soft-Keyword
zeige "hallo"                    # zeige = Befehl
setze zeige auf 5                # zeige = Variablenname
zeige zeige                      # zeige Befehl, zeige Variable
```

| Keyword | Aktivierungs-Kontext | Identifier erlaubt |
|---------|---------------------|-------------------|
| `zeige`/`show` | Statement-Start | Funktions-/Variablenname |
| `versuche`/`try` | Statement-Start gefolgt von `:` | Variablenname |
| `prüfe`/`match` | Match-Armen | Identifier |
| `daten`/`data` | Typ-Deklaration | Identifier |
| `wo`/`where` | Query-Klausel | Identifier |
| `sortiere`/`order` | Query-Klausel | Identifier |
| `teste`/`test` | Test-Block | Identifier |
| `erwarte`/`expect` | Innerhalb teste | Funktions-/Variablenname (BUG: Kontext-Parser nicht perfekt) |

**Harte Keywords (KEINE Soft-Keywords, immer reserviert):**
`setze`, `auf`, `wenn`, `sonst`, `solange`, `für`, `in`, `funktion`, `gib_zurück`, `und`, `oder`, `nicht`, `wahr`, `falsch`, `nichts`, `klasse`, `neu`, `selbst`, `fange`, `wirf`, `stopp`, `weiter`, `konstante`, `fall`, `standard`, `importiere`, `aus`, `exportiere`, `als`, `aufräumen`, `garantiere`, `wähle`, `von`, `schnittstelle`, `implementiert`, `parallel`, `vorbedingung`, `nachbedingung`, `unsicher`

---

## XI. RUST/PYTHON DRIFT (spec/drift_findings.md + E3-Fuzzing)

**Bekannte Fixes:**
- **Global scope in functions** (commit 6b18fe8) — Top-Level-Variablen in Funktionen
- **First-Class Functions** (commits c2b8864 + 2b7668b) — Funktionsnamen als Werte, Closures via Trampoline

**Aktive Drift-Fälle** (Rust ✓, Python ✗):
- `test_bitwise_ops.moos` — Bitops `& | ^ << >> ~` nur Rust (korrekt, dokumentiert als rust-only)
- `test_json.moos` — Lexer-/Parser-Unterschied
- `test_lists.moos` — Analog
- `beispiele/showcase.moos` — Python-Parser-TIMEOUT >10s (Endlos-Schleife, Bug!)

**Fuzzing-Tools:**
- `tools/check_tokens.py` — CI-Check, blockiert wenn Token in YAML aber nicht implementiert
- `tools/fuzz_differential.py` — Lexer/Parser-Output-Diff (Rust vs Python)

---

## XII. PHASE-B PROGRESS (spec/phase_b_progress.md)

**Abgeschlossene Wellen:**
- **W1:** Root-Hygiene ✅
- **W2.1–2.3:** Root-Test-Triage + CI-Schutz ✅
- **W3.1–3.5:** Beispiele-Moves (Web, DB, System, Game) ✅
- **W4.1–4.4:** Finalisierung + Dokumentation ✅

**Status:** Phase B COMPLETE (0 Root test_*.moos, keine Breaking Changes)

**Laufende Followup-Tickets (nicht blockierend):**
- **P2c:** Vision-API-Wrapper (Wolken, Wasser, Licht, Game-Loop)
- **E1:** Sprite-/Chunk-Slot-Auto-Free (Handle-Leak)
- **M5:** Hybrid 2D+3D sprite_z Bridge
- **Python-Parser-Timeout:** showcase.moos Debug

---

## XIII. FOLLOWUP-TICKETS (spec/followup-tickets.md)

| Ticket | Scope | Prio | Grund |
|--------|-------|------|-------|
| P2c | Vision-API für Welt (Wolken, Wasser, Shader, Game-Loop) | Niedrig | Feature-Entwicklung (nach Konsolidierung) |
| E1 | Sprite/Chunk Slot-Auto-Free | Niedrig | Handle-Leak; 256 Sprite-Max erreicht erst später |
| M5 | Hybrid 2D+3D Bridge | Hoch | 2D+3D-Mix ermöglicht erst mit P6 (raum_* auf Hybrid-Window) |
| P3c-followup | DB Statement-Object Variante A | ERLEDIGT (commit) | Prepared Statements + Lazy Iteration |

---

## XIV. BUILD & AUSFÜHRUNG

```bash
# Vorbereitung
cd compiler && cargo build --release

# Einfache Beispiele
./target/release/moo-compiler run ../beispiele/adressbuch.moos
./target/release/moo-compiler run ../beispiele/fibonacci.moos

# Mit Ziel-Architektur
./moo-compiler run --target wasm beispiele/example.moos
./moo-compiler run --target x86_64-bare --kernel beispiele/kernel_hello.moos

# Mit GPU-Backend (Vulkan)
MOO_3D_BACKEND=vulkan ./moo-compiler run beispiele/welten.moos

# KI-Training (MNIST Gate)
./skripte/mnist_download.sh
./moo-compiler compile beispiele/eval_m6b_mnist.moos -o mnist_train
./mnist_train

# Tests
bash compiler/tests/run_all.sh
python3 tools/fuzz_differential.py
```

---

## XV. TOOLING & INFRASTRUKTUR

- **LSP + VSCode-Extension** (editors/vscode/)
- **Formatter** (Python-based)
- **Transpiler** (Python-Toolchain, WASM-Target)
- **Differential Fuzzer** (tools/fuzz_differential.py)
- **GPU-Coverage-Script** (skripte/kip_gpu_coverage.sh)
- **CI/Workflows** (.github/workflows/ — 3D-Backends, Regressions)

---

## XVI. DOKUMENTATION

- **docs/lernen.md** — Tutorial
- **docs/referenz/** — API-Referenz (60+ Dateien)
- **docs/kip/** — KI-Planning (20+ Deep-Dive Dokumente)
  - P0: Zielprofil + Meilensteine
  - D0/D1/D2: DType-Design
  - G0/G1/G2/G3/G4: GPU-Architektur
  - E1/E2/E3: Daten/Resumption/Fuzzing
  - X1/X2/X3: Multi-GPU/Serving/Post-Training
- **docs/roadmap/** — Zukunftspläne
- **spec/** — Spezifikationen (15+ Dateien)

---

## XVII. PROJEKT-STATUS

**✅ Abgeschlossen:**
- Sprachkern (Lexer, Parser, Codegen → LLVM)
- 5-Schichten-Runtime
- 130+ Beispiele
- Phase B Cleanup
- Basic GPU-Compute (Vulkan PoC)

**🔄 In Progress:**
- GPU-Runtime vollständig (G3d/G4)
- KI-Training Pipeline (M-A/M-B Gates)
- Hybrid 2D+3D Bridge (M5-followup)

**⏳ Geplant:**
- Async/Await Implementation
- Game-Module-Boundary Finalisierung
- v1.0 Release (nach LLM-Training-Gates)

---

## XVIII. SCHLUSSFOLGERUNG

**moo ist NICHT nur eine Sprache — es ist ein komplettes Ökosystem:**

1. **Sprachentwurf:** Deutsch/Englisch-Äquivalenz, Soft-Keywords, Pattern Matching
2. **Compiler:** LLVM-basiert, Multi-Target (x86_64, ARM64, RISC-V, WASM, Bare-Metal)
3. **Runtime:** 5-Schichten-Modell mit 45+ C-Dateien (300 KB+ Kerncode)
4. **KI-Integration:** Tensor-Autograd, GPU-Compute (Vulkan SPIR-V), LLM-Training-Pipeline
5. **Grafik:** SDL2 2D, OpenGL 3.3, Vulkan 3D, prozedurale Welt-Engine
6. **Netzwerk:** HTTP/1.1, WebSockets (RFC 6455), DNS (RFC 1035), TCP/UDP
7. **Qualität:** Deterministische Gates, GPT-Reviews, Differential Fuzzing, 27 Regressions-Tests

**Tokens:** 200k Budget, Context-Report zeigt echte Tiefe statt Oberflächenstatistik.

---

*Analyse erstellt mit Synapse (files + code_intel) — Alle Specs gelesen, alle Beispiele inventarisiert, alle Runtime-Dateien auditiert.*
