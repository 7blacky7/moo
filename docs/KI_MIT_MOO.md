# KI mit moo – Einstieg

*English version: [AI_WITH_MOO.md](AI_WITH_MOO.md).*

Diese Anleitung richtet sich an alle, die das Repository frisch geklont haben und
(a) moo bauen und ausführen sowie (b) mit dem eingebauten KI-Stack arbeiten wollen:
Tensoren, Autograd, neuronale Netze bis zum Transformer, Training auf CPU oder
Vulkan-GPU, Tokenizer (Zeichen, BPE, deutsche Silben) und Trainingsmethoden wie
SFT, Distillation, DPO und Evaluation.

Alle Befehle werden aus dem Wurzelverzeichnis des Repositorys ausgeführt.

---

## 1. Voraussetzungen

moo besteht aus zwei Teilen, die zusammen gebaut werden:

- dem **Compiler** `moo-compiler` (Rust, erzeugt über LLVM 18 native Programme) und
- der **Runtime** (`compiler/runtime/*.c`), die `build.rs` beim Bauen mit dem C-Compiler
  zu einer Bibliothek übersetzt. Jedes moo-Programm wird gegen diese Runtime gelinkt.

Benötigt werden:

| Was | Wozu | Hinweis |
|---|---|---|
| **Rust** (cargo, ab 1.85, Edition 2024) | baut den Compiler | über rustup |
| **LLVM 18** inkl. `llvm-config` | Codegen (`inkwell`, Feature `llvm18-1`) | genau Version 18; Pfad über `LLVM_SYS_181_PREFIX` |
| **C-Compiler** | baut die Runtime | Linux/macOS: gcc oder clang; Windows: MSVC `cl.exe` |
| SDL2, SDL2_image, SQLite, GLFW, OpenGL, GTK3 (Linux) | Grafik, UI, Datenbank | Systempakete bzw. vcpkg |
| **Vulkan** (Loader + Treiber) | optional, GPU-Training | Feature `vulkan` ist Standard; ohne GPU rechnet der KI-Stack auf der CPU |

Details zu Versionen und Pflege: [DEPENDENCIES.md](../DEPENDENCIES.md).

## 2. Bauen

### 2.1 Linux (Ubuntu/Debian; Pakete wie in der KI-CI)

Der Standard-Build (Features `gl33`, `moo_ui`, `vulkan`) braucht:

```sh
sudo apt-get install -y \
  llvm-18-dev libpolly-18-dev libclang-18-dev \
  libgtk-3-dev libappindicator3-dev \
  libglfw3-dev libgl1-mesa-dev libvulkan-dev glslang-tools \
  libsdl2-dev libsdl2-image-dev libsqlite3-dev libssl-dev \
  libv4l-dev libasound2-dev pkg-config
export LLVM_SYS_181_PREFIX=/usr/lib/llvm-18

cd compiler
cargo build --release
cd ..
```

Auf Arch/CachyOS entsprechend mit pacman (`llvm18`, `sdl2`, `sdl2_image`, `sqlite`,
`glfw`, `mesa`, `gtk3`) und `LLVM_SYS_181_PREFIX` auf das LLVM-18-Verzeichnis setzen.
Die Standard-LLVM vieler Distributionen ist neuer oder älter als 18 – das passt nicht.

### 2.2 macOS (Homebrew)

```sh
brew install llvm@18 sdl2 sdl2_image openssl@3
export LLVM_SYS_181_PREFIX="$(brew --prefix llvm@18)"
export LIBCLANG_PATH="$LLVM_SYS_181_PREFIX/lib"
export CPATH="$(brew --prefix sdl2)/include:$(brew --prefix sdl2)/include/SDL2:$(brew --prefix openssl@3)/include"
export LIBRARY_PATH="$(brew --prefix sdl2)/lib:$(brew --prefix sdl2_image)/lib:$(brew --prefix openssl@3)/lib"

cd compiler && cargo build --release && cd ..
```

SQLite bringt macOS selbst mit.

### 2.3 Windows x64 (nativ, MSVC)

Auf einem frischen Windows 11 geprüft (Release-Build ca. 3,5 Minuten mit `-j 2`):

1. **Visual Studio 2022 Build Tools** mit der Arbeitslast „Desktopentwicklung mit C++“
   (VC-Tools + Windows SDK).
2. **Rust** über `rustup-init.exe`, Toolchain `stable-x86_64-pc-windows-msvc`.
3. **LLVM 18 mit `llvm-config.exe`**: Der offizielle LLVM-Installer enthält kein
   `llvm-config.exe`. Stattdessen conda-forge (z. B. mit micromamba):
   ```bat
   micromamba create -n moo-llvm -c conda-forge llvmdev=18.1.* clangdev=18.1.* lld=18.1.* zstd zlib libxml2 libxml2-devel
   ```
4. **vcpkg** für die C-Bibliotheken:
   ```bat
   vcpkg install --triplet x64-windows sdl2 sdl2-image sqlite3 glfw3 vulkan
   ```
5. In einer `cmd`-Konsole bauen (Pfade an die eigene Installation anpassen):
   ```bat
   call "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
   set "LLVM_SYS_181_PREFIX=C:\moo-tools\mamba\envs\moo-llvm\Library"
   set "VCPKG_WIN=C:\vcpkg\installed\x64-windows"
   set "PATH=%USERPROFILE%\.cargo\bin;%LLVM_SYS_181_PREFIX%\bin;%VCPKG_WIN%\bin;%PATH%"
   set "LIB=%VCPKG_WIN%\lib;%LLVM_SYS_181_PREFIX%\lib;%LIB%"
   set "INCLUDE=%VCPKG_WIN%\include;%INCLUDE%"
   cd compiler
   cargo build --release
   ```

Stolpersteine:

- Meldet der Linker `LNK1181` für `zstd.dll.lib` o. ä.: Das conda-LLVM erwartet diese
  Namen. Im Ordner `...\moo-llvm\Library\lib` eine Kopie anlegen, z. B.
  `copy zstd.lib zstd.dll.lib` und `copy zlib.lib z.dll.lib`.
- Git for Windows bringt ein eigenes `link.exe` mit. Der MSVC-Linker muss im `PATH`
  davor stehen (`vcvars64.bat` sorgt dafür, solange es zuletzt aufgerufen wird).
- `/std:c11` für `cl.exe` setzt `build.rs` selbst, dafür ist nichts zu tun.

**Ohne eigenen Build:** Die CI erzeugt bei jedem Lauf von „UI Module Cross-Platform CI“
ein **portables Windows-SDK** (`moo-sdk-windows-x64`, als Artefakt des Laufs). Es
enthält `moo-compiler.exe`, die Runtime, `stdlib`, clang, lld-link und alle DLLs.
Entpacken und `moo.cmd` statt `moo-compiler` aufrufen, z. B. `moo.cmd run hallo.moos`.
`moo-shell.cmd` öffnet eine Konsole mit passend gesetztem `PATH`. Visual Studio,
Rust oder LLVM sind dafür nicht nötig.

### 2.4 Ergebnis prüfen

Das Ergebnis ist `compiler/target/release/moo-compiler` (Windows: `moo-compiler.exe`).

```sh
./compiler/target/release/moo-compiler --help
```

| Befehl | Zweck |
|---|---|
| `run <datei.moos>` | kompilieren und sofort ausführen |
| `compile <datei.moos> [-o name]` | native Binary erzeugen (Standardname: Datei ohne `.moos`) |
| `compile --emit-ir` | nur LLVM-IR (`.ll`) erzeugen |
| `compile --target ...` | WebAssembly (`.wasm`) erzeugen; Ziele: `targets` |
| `compile --lib-pfad <dir>` | zusätzlicher Bibliothekspfad (mehrfach möglich) |
| `compile --profile` | Laufzeit je Funktion messen |
| `repl` | interaktiver Modus (auch `mise run dev`) |
| `paket install/list/remove` | Pakete von github.com/moo-packages |
| `ast`, `layout`, `bindgen` | Diagnose bzw. C-Header-Bindungen |

Bare-Metal-Optionen (`--no-stdlib`, `--kernel`, `--board` usw.) zeigt
`moo-compiler compile --help`.

## 3. Ausführen

Tipp: Den Compiler in den `PATH` legen oder einen Alias setzen, dann wird es kürzer.
Der Name `moo` ist im Python-Umfeld des Repos schon für den Transpiler vergeben
(siehe [lernen.md](lernen.md)), deshalb hier `mooc`:

```sh
alias mooc="$PWD/compiler/target/release/moo-compiler"
```

Erster Test – eine Datei `hallo.moos`:

```moo
zeige "Hallo Welt"
```

```sh
mooc run hallo.moos            # kompilieren und sofort ausführen
mooc compile hallo.moos        # erzeugt ./hallo (Windows: hallo.exe)
./hallo
mooc compile beispiele/ki_xor.moos -o ki_xor && ./ki_xor
```

`run` baut das Programm im Hintergrund vollständig nativ und startet es dann; es gibt
keinen Interpreter. Für wiederholte Läufe lohnt sich `compile`, weil das Linken dann
entfällt. Projektaufgaben (Tests, Benchmarks, KI-Experimente) liegen als mise-Tasks vor:
`mise tasks` listet sie, `mise run <task>` startet einen.

Zur Sprache selbst: [docs/lernen.md](lernen.md).

---

## 4. KI-Schnellstart

### 4.1 Tensor, Autograd und ein kleines Netz (XOR) – `beispiele/ki_xor.moos`

Der vollständige Low-Level-Weg: Tensor anlegen, Schichten bauen, Verlust berechnen,
`rueckwaerts()` (Autograd), Optimiererschritt.

```moo
setze x auf tensor_aus_liste([[0,0],[0,1],[1,0],[1,1]])
setze ziel auf tensor_aus_liste([[0],[1],[1],[0]])
setze netz auf [schicht_dicht(2, 8, "tanh", 7), schicht_dicht(8, 1, "sigmoid", 8)]
setze opt auf optimierer_adam(parameter(netz), 0.05)
setze i auf 0
solange i < 400:
    setze fehler auf mse(vorwaerts(netz, x), ziel)
    fehler.rueckwaerts()
    opt.schritt()
    setze i auf i + 1
autograd_aus()
zeige "Vorhersage (Ziel 0,1,1,0): " + text(vorwaerts(netz, x).zu_liste())
```

```sh
./compiler/target/release/moo-compiler run beispiele/ki_xor.moos
```

Ausgabe (geprüft):

```
Verlust nach 400 Iterationen: [2.20211e-05]
Vorhersage (Ziel 0,1,1,0): [0.00425452, 0.995528, 0.995059, 0.0050562]
```

### 4.2 Die „Kinderleicht“-API: trainieren, speichern, laden – `beispiele/ki_kinderleicht.moos`

```moo
setze netz auf ki_netz([schicht_dicht(2, 8, "tanh", 7), schicht_dicht(8, 1, "sigmoid", 8)])
setze verlauf auf netz.trainiere(daten, ziele, {"epochen": 400, "rate": 0.05, "batch": 4, "ausgabe": 0})
zeige "Genauigkeit: " + text(netz.genauigkeit(daten, ziele))
netz.speichern("/tmp/xor_netz.mook")
setze kopie auf ki_laden("/tmp/xor_netz.mook")
```

```sh
./compiler/target/release/moo-compiler run beispiele/ki_kinderleicht.moos
```

Ausgabe (geprüft): `Genauigkeit: 1`, das geladene Netz liefert exakt dieselbe Vorhersage.
Optionen werden als Dict übergeben. Modelle werden im `.mook`-Format gespeichert.

### 4.3 Tokenizer: Zeichen und deutsche Silben – `beispiele/p4_silben_demo.moos`

Trainiert einen Zeichen-Tokenizer und einen Silben-Tokenizer (auf Basis der
Silbentrennmuster in `compiler/runtime/data/silben/`), speichert, lädt und
prüft den Roundtrip. Kernzeilen:

```moo
setze muster auf datei_lesen("compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat")
setze ct auf zeichen_fixed_trainiere(korpus, 320)
setze st auf silben_trainiere(korpus, 320, muster)
silben_speichern(st, s_pfad)
setze sl auf silben_laden(s_pfad)
silben_dekodiere(sl, silben_kodiere(sl, probe))
```

Das Beispiel braucht ein **leeres, existierendes** Arbeitsverzeichnis:

```sh
mkdir -p /tmp/p4demo   # muss leer sein
MOO_P4_DEMO_DIR=/tmp/p4demo ./compiler/target/release/moo-compiler run beispiele/p4_silben_demo.moos
# Ausgabe (geprüft): P4 DEMO OK
```

Ein BPE-Tokenizer entsteht analog mit `tokenizer_trainiere(korpus, vokabgroesse)`,
`tokenizer_kodiere` / `tokenizer_dekodiere` / `tokenizer_speichern` / `tokenizer_laden`
(siehe `beispiele/p4_silben/train_tokenizers.moos`, `beispiele/tests/p4_tokenizer_api.moos`).
Für einfache Zeichen-Sprachmodelle gibt es `text_tokenizer(text)`, das ein Dict mit
`ids`, `vokab` und `id_zu_zeichen` liefert.

### 4.4 Kleines Sprachmodell (Transformer) – `beispiele/ki_sprachmodell.moos`

Ein Mini-Transformer (2 Blöcke, dim 64, ca. 120k Parameter) lernt Zeichen für
Zeichen auf Grimm-Märchen. Ein Lern-Demonstrator, kein Chatbot. Aufbau:

```moo
setze tok auf text_tokenizer(korpus)
setze emb  auf schicht_embedding(vokab, dim, 1)
setze pos  auf schicht_position(block, dim, "gelernt", 2)
setze att1 auf schicht_attention(dim, koepfe, 3)
# ...
funktion modell(ids_block):
    setze x auf vorwaerts([emb], ids_block)
    setze x auf vorwaerts([pos], x)
    setze x auf x + vorwaerts([ln1a, att1], x)     # Residual über Autograd
    # ...
setze verlust auf kreuzentropie(modell(ids.zeilen(p, p + block)), ids.zeilen(p + 1, p + block + 1))
verlust.rueckwaerts()
gradienten_kappen(params, 1.0)
opt.schritt()
```

Generiert wird unter `autograd_aus()` mit Greedy- oder Temperatur-Sampling.

```sh
./skripte/maerchen_download.sh          # Korpus nach beispiele/daten/maerchen.txt laden
./compiler/target/release/moo-compiler compile beispiele/ki_sprachmodell.moos -o ki_lm
./ki_lm
```

> Nicht im Rahmen dieser Anleitung verifiziert (Laufzeit mehrere Minuten, Korpus-Download nötig).

### 4.5 GPU (Vulkan)

Mit dem Standard-Build (Feature `vulkan`) laufen große Tensor-Operationen automatisch auf
der GPU; kleine bleiben auf der CPU (Schwellen laut `compiler/runtime/moo_ki_gpu.c`:
MatMul ab M·K·N ≥ 2^24, elementweise ab 2^20 Elementen). Steuerung über Umgebungsvariablen:

| Variable | Wirkung |
|---|---|
| `MOO_KI_GPU=0` | GPU komplett abschalten |
| `MOO_KI_GPU_ERZWINGEN=1` | Schwellen ignorieren, auch kleine Tensoren auf die GPU |
| `MOO_KI_GPU_STRIKT` | strenger Modus, CPU-Ausführung wird gemeldet |

`gpu_statistik()` / `gpu_statistik_reset()` zeigen, was tatsächlich auf der GPU lief.
Ein komplettes Beispiel mit wiederaufnehmbaren Checkpoints ist
`beispiele/ki_sprachmodell_gpu.moos` (dort den Checkpoint-Pfad an das eigene System anpassen).

```sh
MOO_KI_GPU_ERZWINGEN=1 ./compiler/target/release/moo-compiler run beispiele/ki_xor.moos
```

Das Programm läuft so durch (geprüft); ob tatsächlich eine GPU benutzt wurde, zeigt
nur `gpu_statistik()`.

---

## 5. Fortgeschritten: Trainingsmethoden (SFT, Distillation, DPO, Evaluation)

Unter `beispiele/p5/` liegt eine einheitliche Trainings-API, in moo selbst geschrieben.
`trainiere_methode(forward, daten, konfig)` wählt über `konfig["methode"]`:

| methode | Direktaufruf | Doku |
|---|---|---|
| `sft` | `trainiere_sft` | [README_sft.md](../beispiele/p5/README_sft.md) |
| `korrekturen` | `trainiere_korrekturen` | [README_sft.md](../beispiele/p5/README_sft.md) |
| `distillation` | `trainiere_distillation` | [README_distill_dpo.md](../beispiele/p5/README_distill_dpo.md) |
| `praeferenzen` (DPO) | `trainiere_praeferenzen` | [README_distill_dpo.md](../beispiele/p5/README_distill_dpo.md) |

Evaluation und Checkpoint-Auswahl (`evaluiere_modell`, `checkpoint_waehlen`):
[README_eval.md](../beispiele/p5/README_eval.md). Übersicht: [README_train_api.md](../beispiele/p5/README_train_api.md).
Tests über mise, z. B. `mise run test-p5-train-api` (alle Tasks: `mise tasks`).

---

## 6. Die wichtigsten KI-Funktionen

| Funktion | Zweck |
|---|---|
| `tensor_aus_liste`, `tensor_nullen`, `tensor_einsen`, `tensor_zufall` | Tensoren erzeugen |
| `.zu_liste()`, `.zeilen(a, b)`, `.groesse()`, `.softmax()` | Tensor-Methoden |
| `autograd_an`, `autograd_aus`, `autograd_reset`, `.rueckwaerts()` | Autograd steuern / Gradienten berechnen |
| `schicht_dicht`, `schicht_faltung`, `schicht_pooling`, `schicht_flach`, `schicht_dropout` | Grundschichten |
| `schicht_embedding`, `schicht_position`, `schicht_attention`, `schicht_layernorm`, `schicht_rmsnorm`, `schicht_ffn_gated`, `schicht_moe`, `schicht_attnres` | Transformer-Bausteine |
| `vorwaerts(schichten, x)`, `parameter(schichten)` | Forward-Pass, trainierbare Parameter |
| `mse`, `kreuzentropie` | Verlustfunktionen |
| `optimierer_sgd`, `optimierer_adam`, `optimierer_adamw`, `.schritt()`, `gradienten_kappen` | Optimierung |
| `ki_netz`, `.trainiere`, `.vorhersage`, `.genauigkeit`, `.speichern`, `ki_laden` | Kinderleicht-API |
| `checkpoint_speichern`, `checkpoint_laden`, `checkpoint_rotieren`, `checkpoint_waehlen` | Checkpoints (`.mook`) |
| `text_tokenizer` | einfacher Zeichen-Tokenizer |
| `tokenizer_trainiere`, `tokenizer_kodiere`, `tokenizer_kodiere_stapel`, `tokenizer_dekodiere`, `tokenizer_speichern`, `tokenizer_laden`, `tokenizer_info`, `tokenizer_hash` | BPE-Tokenizer |
| `silben_trainiere`, `silben_kodiere`, `silben_dekodiere`, `silben_speichern`, `silben_laden`, `silben_info` | deutscher Silben-Tokenizer |
| `zeichen_fixed_trainiere`, `zeichen_fixed_kodiere`, `zeichen_fixed_dekodiere`, `zeichen_fixed_speichern`, `zeichen_fixed_laden` | fester Zeichen-Tokenizer |
| `gpu_statistik`, `gpu_statistik_reset` | GPU-Nutzung prüfen |

Viele Funktionen haben zusätzlich englische Aliasse (z. B. `tensor_from_list`,
`autograd_off`, `tokenizer_encode`, `checkpoint_save`).

## 7. Weitere Beispiele und Dokumente

- `beispiele/ki_*.moos` – u. a. `ki_mnist.moos`, `ki_mnist_conv.moos`, `ki_embeddings.moos`,
  `ki_moe.moos`, `ki_grpo.moos`, `ki_mtp.moos`, `ki_multimodal*.moos`,
  `ki_safetensors_import.moos`, `ki_verteilt.moos`.
- `beispiele/p4_silben/` – Silben-Sprachmodell-Pipeline (Tokenizer, Training, Eval).
- `beispiele/p5/` – Trainings-API, SFT, Distillation, DPO, Evaluation.
- [docs/kip/](kip/) – Design- und Vertragsdokumente zum KI-Stack (Dtypes, GPU-Residenz,
  verteiltes Training, Serving, Post-Training).

## 8. Wichtig: Daten und Modelle sind nicht im Repo

Trainingskorpora und Modell-Checkpoints werden **nicht** mitgeliefert. Für eigene
Experimente braucht man eigene Texte (UTF-8). Für das Märchen-Beispiel lädt
`skripte/maerchen_download.sh` einen gemeinfreien Korpus herunter. Einige Beispiele
unter `beispiele/p4_silben/` und `beispiele/p5/` erwarten Pfade bzw. Umgebungsvariablen
(z. B. `P4_TRAIN_CORPUS`) zu eigenen Daten.
