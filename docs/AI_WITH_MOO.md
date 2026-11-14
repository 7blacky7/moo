# AI with moo – Getting Started

*German version: [KI_MIT_MOO.md](KI_MIT_MOO.md).*

This guide is for anyone who has just cloned the repository and wants to
(a) build and run moo and (b) work with the built-in AI stack: tensors, autograd,
neural networks up to transformers, training on CPU or Vulkan GPU, tokenizers
(character, BPE, German syllables) and training methods such as SFT, distillation,
DPO and evaluation.

moo is a German-keyword language: `zeige` = print, `setze … auf` = set … to,
`solange` = while, `funktion` = function. Most built-ins also have English aliases
(see section 6).

All commands are run from the repository root.

---

## 1. Requirements

moo consists of two parts that are built together:

- the **compiler** `moo-compiler` (Rust, generates native programs via LLVM 18), and
- the **runtime** (`compiler/runtime/*.c`), which `build.rs` compiles into a library
  with the C compiler. Every moo program is linked against this runtime.

You need:

| What | Used for | Note |
|---|---|---|
| **Rust** (cargo, 1.85+, edition 2024) | builds the compiler | via rustup |
| **LLVM 18** incl. `llvm-config` | code generation (`inkwell`, feature `llvm18-1`) | exactly version 18; path via `LLVM_SYS_181_PREFIX` |
| **C compiler** | builds the runtime | Linux/macOS: gcc or clang; Windows: MSVC `cl.exe` |
| SDL2, SDL2_image, SQLite, GLFW, OpenGL, GTK3 (Linux) | graphics, UI, database | system packages or vcpkg |
| **Vulkan** (loader + driver) | optional, GPU training | feature `vulkan` is on by default; without a GPU the AI stack runs on the CPU |

Versions and maintenance: [DEPENDENCIES.md](../DEPENDENCIES.md).

## 2. Building

### 2.1 Linux (Ubuntu/Debian; same packages as the AI CI)

The default build (features `gl33`, `moo_ui`, `vulkan`) needs:

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

On Arch/CachyOS use pacman accordingly (`llvm18`, `sdl2`, `sdl2_image`, `sqlite`,
`glfw`, `mesa`, `gtk3`) and point `LLVM_SYS_181_PREFIX` at the LLVM 18 directory.
The default LLVM of many distributions is newer or older than 18 – that will not work.

### 2.2 macOS (Homebrew)

```sh
brew install llvm@18 sdl2 sdl2_image openssl@3
export LLVM_SYS_181_PREFIX="$(brew --prefix llvm@18)"
export LIBCLANG_PATH="$LLVM_SYS_181_PREFIX/lib"
export CPATH="$(brew --prefix sdl2)/include:$(brew --prefix sdl2)/include/SDL2:$(brew --prefix openssl@3)/include"
export LIBRARY_PATH="$(brew --prefix sdl2)/lib:$(brew --prefix sdl2_image)/lib:$(brew --prefix openssl@3)/lib"

cd compiler && cargo build --release && cd ..
```

SQLite ships with macOS.

### 2.3 Windows x64 (native, MSVC)

Verified on a fresh Windows 11 machine (release build about 3.5 minutes with `-j 2`):

1. **Visual Studio 2022 Build Tools** with the "Desktop development with C++"
   workload (VC tools + Windows SDK).
2. **Rust** via `rustup-init.exe`, toolchain `stable-x86_64-pc-windows-msvc`.
3. **LLVM 18 with `llvm-config.exe`**: the official LLVM installer does not ship
   `llvm-config.exe`. Use conda-forge instead (e.g. with micromamba):
   ```bat
   micromamba create -n moo-llvm -c conda-forge llvmdev=18.1.* clangdev=18.1.* lld=18.1.* zstd zlib libxml2 libxml2-devel
   ```
4. **vcpkg** for the C libraries:
   ```bat
   vcpkg install --triplet x64-windows sdl2 sdl2-image sqlite3 glfw3 vulkan
   ```
5. Build in a `cmd` console (adjust paths to your installation):
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

Pitfalls:

- Linker error `LNK1181` for `zstd.dll.lib` or similar: the conda LLVM expects these
  names. Create a copy in `...\moo-llvm\Library\lib`, e.g.
  `copy zstd.lib zstd.dll.lib` and `copy zlib.lib z.dll.lib`.
- Git for Windows ships its own `link.exe`. The MSVC linker must come first in `PATH`
  (`vcvars64.bat` takes care of that as long as it is called last).
- `build.rs` adds `/std:c11` for `cl.exe` itself; nothing to do.

**Without building yourself:** every run of "UI Module Cross-Platform CI" produces a
**portable Windows SDK** (`moo-sdk-windows-x64`, attached to the run as an artifact).
It contains `moo-compiler.exe`, the runtime, `stdlib`, clang, lld-link and all DLLs.
Unpack it and use `moo.cmd` instead of `moo-compiler`, e.g. `moo.cmd run hello.moos`.
`moo-shell.cmd` opens a console with the right `PATH`. No Visual Studio, Rust or
LLVM required.

### 2.4 Checking the result

The result is `compiler/target/release/moo-compiler` (Windows: `moo-compiler.exe`).

```sh
./compiler/target/release/moo-compiler --help
```

| Command | Purpose |
|---|---|
| `run <file.moos>` | compile and run immediately |
| `compile <file.moos> [-o name]` | produce a native binary (default name: file without `.moos`) |
| `compile --emit-ir` | emit LLVM IR (`.ll`) only |
| `compile --target ...` | produce WebAssembly (`.wasm`); targets: `targets` |
| `compile --lib-pfad <dir>` | additional library search path (repeatable) |
| `compile --profile` | measure time per function |
| `repl` | interactive mode (also `mise run dev`) |
| `paket install/list/remove` | packages from github.com/moo-packages |
| `ast`, `layout`, `bindgen` | diagnostics and C header bindings |

Bare-metal options (`--no-stdlib`, `--kernel`, `--board`, …) are listed by
`moo-compiler compile --help`.

## 3. Running

Tip: put the compiler on your `PATH` or define an alias. The name `moo` is already
taken by the Python transpiler CLI in this repo (see [lernen.md](lernen.md)), so we
use `mooc`:

```sh
alias mooc="$PWD/compiler/target/release/moo-compiler"
```

First test – a file `hello.moos`:

```moo
zeige "Hello world"
```

```sh
mooc run hello.moos            # compile and run immediately
mooc compile hello.moos        # produces ./hello (Windows: hello.exe)
./hello
mooc compile beispiele/ki_xor.moos -o ki_xor && ./ki_xor
```

`run` builds a full native program behind the scenes and then starts it; there is no
interpreter. For repeated runs `compile` is worth it because linking is skipped.
Project tasks (tests, benchmarks, AI experiments) are mise tasks: `mise tasks` lists
them, `mise run <task>` starts one.

The language itself (German): [docs/lernen.md](lernen.md).

---

## 4. AI quick start

### 4.1 Tensor, autograd and a small network (XOR) – `beispiele/ki_xor.moos`

The full low-level path: create tensors, build layers, compute the loss,
`rueckwaerts()` (backward/autograd), optimizer step.

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
mooc run beispiele/ki_xor.moos
```

Output (verified on Linux and native Windows):

```
Verlust nach 400 Iterationen: [2.20211e-05]
Vorhersage (Ziel 0,1,1,0): [0.00425452, 0.995528, 0.995059, 0.0050562]
```

### 4.2 The "child's play" API: train, save, load – `beispiele/ki_kinderleicht.moos`

```moo
setze netz auf ki_netz([schicht_dicht(2, 8, "tanh", 7), schicht_dicht(8, 1, "sigmoid", 8)])
setze verlauf auf netz.trainiere(daten, ziele, {"epochen": 400, "rate": 0.05, "batch": 4, "ausgabe": 0})
zeige "Genauigkeit: " + text(netz.genauigkeit(daten, ziele))
netz.speichern("/tmp/xor_netz.mook")
setze kopie auf ki_laden("/tmp/xor_netz.mook")
```

```sh
mooc run beispiele/ki_kinderleicht.moos
```

Output (verified): `Genauigkeit: 1` (accuracy); the loaded network gives exactly the
same prediction. Options are passed as a dict (`epochen` = epochs, `rate` = learning
rate). Models are stored in the `.mook` format.

### 4.3 Tokenizers: characters and German syllables – `beispiele/p4_silben_demo.moos`

Trains a character tokenizer and a syllable tokenizer (based on the hyphenation
patterns in `compiler/runtime/data/silben/`), saves, loads and checks the round trip.
Key lines:

```moo
setze muster auf datei_lesen("compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat")
setze ct auf zeichen_fixed_trainiere(korpus, 320)
setze st auf silben_trainiere(korpus, 320, muster)
silben_speichern(st, s_pfad)
setze sl auf silben_laden(s_pfad)
silben_dekodiere(sl, silben_kodiere(sl, probe))
```

The example needs an **empty, existing** working directory:

```sh
mkdir -p /tmp/p4demo   # must be empty
MOO_P4_DEMO_DIR=/tmp/p4demo mooc run beispiele/p4_silben_demo.moos
# Output (verified): P4 DEMO OK
```

A BPE tokenizer works the same way with `tokenizer_trainiere(korpus, vokabgroesse)`,
`tokenizer_kodiere` / `tokenizer_dekodiere` / `tokenizer_speichern` / `tokenizer_laden`
(see `beispiele/p4_silben/train_tokenizers.moos`, `beispiele/tests/p4_tokenizer_api.moos`).
For simple character language models there is `text_tokenizer(text)`, which returns a
dict with `ids`, `vokab` and `id_zu_zeichen`.

### 4.4 Small language model (transformer) – `beispiele/ki_sprachmodell.moos`

A mini transformer (2 blocks, dim 64, about 120k parameters) learns Grimm's fairy
tales character by character. A learning demonstrator, not a chatbot. Structure:

```moo
setze tok auf text_tokenizer(korpus)
setze emb  auf schicht_embedding(vokab, dim, 1)
setze pos  auf schicht_position(block, dim, "gelernt", 2)
setze att1 auf schicht_attention(dim, koepfe, 3)
# ...
funktion modell(ids_block):
    setze x auf vorwaerts([emb], ids_block)
    setze x auf vorwaerts([pos], x)
    setze x auf x + vorwaerts([ln1a, att1], x)     # residual via autograd
    # ...
setze verlust auf kreuzentropie(modell(ids.zeilen(p, p + block)), ids.zeilen(p + 1, p + block + 1))
verlust.rueckwaerts()
gradienten_kappen(params, 1.0)
opt.schritt()
```

Generation runs under `autograd_aus()` with greedy or temperature sampling.

```sh
./skripte/maerchen_download.sh          # downloads the corpus to beispiele/daten/maerchen.txt
mooc compile beispiele/ki_sprachmodell.moos -o ki_lm
./ki_lm
```

> Not verified as part of this guide (runs several minutes, needs the corpus download).

### 4.5 GPU (Vulkan)

With the default build (feature `vulkan`) large tensor operations run on the GPU
automatically; small ones stay on the CPU (thresholds in
`compiler/runtime/moo_ki_gpu.c`: MatMul from M·K·N ≥ 2^24, element-wise from 2^20
elements). Controlled by environment variables:

| Variable | Effect |
|---|---|
| `MOO_KI_GPU=0` | disable the GPU completely |
| `MOO_KI_GPU_ERZWINGEN=1` | ignore thresholds, put small tensors on the GPU too |
| `MOO_KI_GPU_STRIKT` | strict mode, CPU execution is reported |

`gpu_statistik()` / `gpu_statistik_reset()` show what actually ran on the GPU.
A complete example with resumable checkpoints is `beispiele/ki_sprachmodell_gpu.moos`
(adjust the checkpoint path to your system).

```sh
MOO_KI_GPU_ERZWINGEN=1 mooc run beispiele/ki_xor.moos
```

The program runs through this way (verified); only `gpu_statistik()` tells you
whether a GPU was actually used.

---

## 5. Advanced: training methods (SFT, distillation, DPO, evaluation)

`beispiele/p5/` contains a unified training API written in moo itself.
`trainiere_methode(forward, daten, konfig)` selects via `konfig["methode"]`:

| methode | direct call | docs (German) |
|---|---|---|
| `sft` | `trainiere_sft` | [README_sft.md](../beispiele/p5/README_sft.md) |
| `korrekturen` (corrections) | `trainiere_korrekturen` | [README_sft.md](../beispiele/p5/README_sft.md) |
| `distillation` | `trainiere_distillation` | [README_distill_dpo.md](../beispiele/p5/README_distill_dpo.md) |
| `praeferenzen` (DPO) | `trainiere_praeferenzen` | [README_distill_dpo.md](../beispiele/p5/README_distill_dpo.md) |

Evaluation and checkpoint selection (`evaluiere_modell`, `checkpoint_waehlen`):
[README_eval.md](../beispiele/p5/README_eval.md). Overview:
[README_train_api.md](../beispiele/p5/README_train_api.md).
Tests via mise, e.g. `mise run test-p5-train-api` (all tasks: `mise tasks`).

---

## 6. Key AI functions

| Function | Purpose |
|---|---|
| `tensor_aus_liste`, `tensor_nullen`, `tensor_einsen`, `tensor_zufall` | create tensors (from list, zeros, ones, random) |
| `.zu_liste()`, `.zeilen(a, b)`, `.groesse()`, `.softmax()` | tensor methods (to list, rows, shape, softmax) |
| `autograd_an`, `autograd_aus`, `autograd_reset`, `.rueckwaerts()` | control autograd / compute gradients |
| `schicht_dicht`, `schicht_faltung`, `schicht_pooling`, `schicht_flach`, `schicht_dropout` | basic layers (dense, conv, pooling, flatten, dropout) |
| `schicht_embedding`, `schicht_position`, `schicht_attention`, `schicht_layernorm`, `schicht_rmsnorm`, `schicht_ffn_gated`, `schicht_moe`, `schicht_attnres` | transformer building blocks |
| `vorwaerts(schichten, x)`, `parameter(schichten)` | forward pass, trainable parameters |
| `mse`, `kreuzentropie` | loss functions (MSE, cross-entropy) |
| `optimierer_sgd`, `optimierer_adam`, `optimierer_adamw`, `.schritt()`, `gradienten_kappen` | optimization (step, gradient clipping) |
| `ki_netz`, `.trainiere`, `.vorhersage`, `.genauigkeit`, `.speichern`, `ki_laden` | child's-play API (train, predict, accuracy, save, load) |
| `checkpoint_speichern`, `checkpoint_laden`, `checkpoint_rotieren`, `checkpoint_waehlen` | checkpoints (`.mook`) |
| `text_tokenizer` | simple character tokenizer |
| `tokenizer_trainiere`, `tokenizer_kodiere`, `tokenizer_kodiere_stapel`, `tokenizer_dekodiere`, `tokenizer_speichern`, `tokenizer_laden`, `tokenizer_info`, `tokenizer_hash` | BPE tokenizer |
| `silben_trainiere`, `silben_kodiere`, `silben_dekodiere`, `silben_speichern`, `silben_laden`, `silben_info` | German syllable tokenizer |
| `zeichen_fixed_trainiere`, `zeichen_fixed_kodiere`, `zeichen_fixed_dekodiere`, `zeichen_fixed_speichern`, `zeichen_fixed_laden` | fixed character tokenizer |
| `gpu_statistik`, `gpu_statistik_reset` | check GPU usage |

Many functions also have English aliases (e.g. `tensor_from_list`, `autograd_off`,
`tokenizer_encode`, `checkpoint_save`).

## 7. More examples and documents

- `beispiele/ki_*.moos` – including `ki_mnist.moos`, `ki_mnist_conv.moos`,
  `ki_embeddings.moos`, `ki_moe.moos`, `ki_grpo.moos`, `ki_mtp.moos`,
  `ki_multimodal*.moos`, `ki_safetensors_import.moos`, `ki_verteilt.moos`.
- `beispiele/p4_silben/` – syllable language model pipeline (tokenizer, training, eval).
- `beispiele/p5/` – training API, SFT, distillation, DPO, evaluation.
- [docs/kip/](kip/) – design and contract documents for the AI stack (dtypes,
  GPU residency, distributed training, serving, post-training; German).

## 8. Important: data and models are not in the repo

Training corpora and model checkpoints are **not** included. For your own experiments
you need your own texts (UTF-8). For the fairy-tale example,
`skripte/maerchen_download.sh` downloads a public-domain corpus. Some examples under
`beispiele/p4_silben/` and `beispiele/p5/` expect paths or environment variables
(e.g. `P4_TRAIN_CORPUS`) pointing to your own data.
