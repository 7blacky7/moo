# AI with moo – Getting Started

*German version: [KI_MIT_MOO.md](KI_MIT_MOO.md).*

This guide is for anyone who has just cloned the repository and wants to
(a) build and run moo and (b) work with the built-in AI stack: tensors, autograd,
neural networks up to transformers, training on CPU or Vulkan GPU, tokenizers
(character, BPE, German syllables) and training methods such as SFT, distillation,
DPO and evaluation.

moo supports both English and German keywords equally (`show` / `zeige`,
`set … to` / `setze … auf`, `while` / `solange`, `func` / `funktion`), and
most built-in functions and methods also have English aliases (see section 6).
German and English keywords/aliases can be mixed freely in the same file:

```moo
set x to tensor_from_list([[1, 2], [3, 4]])
setze y auf tensor_einsen([2, 2])
set z to x + y
zeige "Mixed: " + str(z.zu_liste())   # -> Mixed: [2, 3, 4, 5]
```

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
show "Hello world"
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
them, `mise run <task>` starts one (main groups: `p2-*` for native/FFI/bindgen,
`p4-*` for syllables/Vulkan/parity, `p5-*` for training API/SFT/eval,
`p6-*` for knowledge mix/tokenizers, `test-*` for test gates).

The language itself (German): [docs/lernen.md](lernen.md).

---

## 4. AI quick start

### 4.1 Tensor, autograd and a small network (XOR) – `beispiele/ki_xor.moos`

The full low-level path: create tensors, build layers, compute the loss,
`.backward()` (autograd), optimizer step.

```moo
set x to tensor_from_list([[0,0],[0,1],[1,0],[1,1]])
set target to tensor_from_list([[0],[1],[1],[0]])
set net to [layer_dense(2, 8, "tanh", 7), layer_dense(8, 1, "sigmoid", 8)]
set opt to optimizer_adam(parameters(net), 0.05)
set i to 0
while i < 400:
    set loss to mse(forward(net, x), target)
    loss.backward()
    opt.step()
    set i to i + 1
autograd_off()
show "Prediction (target 0,1,1,0): " + str(forward(net, x).to_list())
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
set net to ai_net([layer_dense(2, 8, "tanh", 7), layer_dense(8, 1, "sigmoid", 8)])
set history to net.train(data, targets, {"epochen": 400, "rate": 0.05, "batch": 4, "ausgabe": 0})
show "Accuracy: " + str(net.accuracy(data, targets))
net.save("/tmp/xor_netz.mook")
set copy to ai_load("/tmp/xor_netz.mook")
```

```sh
mooc run beispiele/ki_kinderleicht.moos
```

Output (verified): `Accuracy: 1` (the German repo example prints `Genauigkeit: 1`); the loaded network gives exactly the
same prediction. Options are passed as a dict whose keys remain German (`"epochen"` = epochs, `"rate"` = learning
rate, `"batch"`, `"ausgabe"` = logging interval). Models are stored in the `.mook` format.

### 4.3 Tokenizers: characters and German syllables – `beispiele/p4_silben_demo.moos`

Trains a character tokenizer and a syllable tokenizer (based on the hyphenation
patterns in `compiler/runtime/data/silben/`), saves, loads and checks the round trip.
Key lines:

```moo
set patterns to file_read("compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat")
set ct to char_fixed_train(corpus, 320)
set st to syllable_train(corpus, 320, patterns)
syllable_save(st, s_path)
set sl to syllable_load(s_path)
syllable_decode(sl, syllable_encode(sl, probe))
```

The example needs an **empty, existing** working directory:

```sh
mkdir -p /tmp/p4demo   # must be empty
MOO_P4_DEMO_DIR=/tmp/p4demo mooc run beispiele/p4_silben_demo.moos
# Output (verified): P4 DEMO OK
```

A BPE tokenizer works the same way with `tokenizer_train(corpus, vocab_size)`,
`tokenizer_encode` / `tokenizer_decode` / `tokenizer_save` / `tokenizer_load`
(see `beispiele/p4_silben/train_tokenizers.moos`, `beispiele/tests/p4_tokenizer_api.moos`).
For simple character language models there is `char_tokenizer(text)`, which returns a
dict with German keys `ids`, `vokab` and `id_zu_zeichen`.

### 4.4 Small language model (transformer) – `beispiele/ki_sprachmodell.moos`

A mini transformer (2 blocks, dim 64, about 120k parameters) learns Grimm's fairy
tales character by character. A learning demonstrator, not a chatbot. Structure:

```moo
set tok to char_tokenizer(corpus)
set emb  to layer_embedding(vocab, dim, 1)
set pos  to layer_position(block, dim, "learned", 2)
set att1 to layer_attention(dim, heads, 3)
# ...
func model(ids_block):
    set x to forward([emb], ids_block)
    set x to forward([pos], x)
    set x to x + forward([ln1a, att1], x)     # residual via autograd
    # ...
set loss to cross_entropy(model(ids.rows(0, block)), ids.rows(1, block + 1))
loss.backward()
clip_gradients(params, 1.0)
opt.step()
```

Generation runs under `autograd_off()` with greedy or temperature sampling.

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
| `tensor_from_list`, `tensor_zeros`, `tensor_ones`, `tensor_random` | create tensors (from list, zeros, ones, random) |
| `.to_list()`, `.rows(a, b)`, `.size()`, `.softmax()` | tensor methods (to list, rows, shape, softmax) |
| `autograd_on`, `autograd_off`, `autograd_reset`, `.backward()` | control autograd / compute gradients |
| `layer_dense`, `layer_conv2d`, `layer_pooling`, `layer_flatten`, `layer_dropout` | basic layers (dense, conv, pooling, flatten, dropout) |
| `layer_embedding`, `layer_position`, `layer_attention`, `layer_layernorm`, `layer_rmsnorm`, `layer_ffn_gated`, `layer_moe`, `layer_attnres` | transformer building blocks |
| `forward(layers, x)`, `parameters(layers)` | forward pass, trainable parameters |
| `mse`, `cross_entropy` | loss functions (MSE, cross-entropy) |
| `optimizer_sgd`, `optimizer_adam`, `optimizer_adamw`, `.step()`, `clip_gradients` | optimization (step, gradient clipping) |
| `ai_net`, `.train`, `.predict`, `.accuracy`, `.save`, `ai_load` | child's-play API (train, predict, accuracy, save, load) |
| `checkpoint_save`, `checkpoint_load`, `checkpoint_rotate`, `checkpoint_waehlen` *(German only)* | checkpoints (`.mook`) |
| `shard_write`, `shard_info`, `shard_check`, `shard_window`, `shard_mask_window`, `shard_order`, `shard_split` | streaming token shards |
| `trainiere_sft`, `trainiere_korrekturen`, `trainiere_distillation`, `trainiere_praeferenzen`, `trainiere_methode`, `evaluiere_modell` *(German only)* | post-training & evaluation |
| `char_tokenizer` | simple character tokenizer |
| `tokenizer_train`, `tokenizer_trainiere_parser` *(German only)*, `tokenizer_encode`, `tokenizer_encode_batch`, `tokenizer_decode`, `tokenizer_save`, `tokenizer_load`, `tokenizer_info`, `tokenizer_hash` | BPE tokenizer (incl. parser BPE) |
| `chat_tokenizer`, `tokenizer_special_id`, `tokenizer_render` | chat / special tokens & rendering |
| `syllable_train`, `syllable_boundaries`, `syllable_encode`, `syllable_decode`, `syllable_save`, `syllable_load`, `syllable_info`, `syllable_hash`, `syllable_hash64` | German syllable tokenizer |
| `char_fixed_train`, `char_fixed_encode`, `char_fixed_decode`, `char_fixed_save`, `char_fixed_load` | fixed character tokenizer |
| `BildModell` (`import ki_bild_easy`, `.beschreibe(frame)` — *German stdlib class/method*) | high-level stdlib API ("image → text") |
| `gpu_statistik`, `gpu_statistik_reset` *(German only)* | check GPU usage |

All functions above also have German counterparts (e.g. `tensor_aus_liste`, `autograd_aus`,
`tokenizer_kodiere`, `checkpoint_speichern`, `shard_schreiben`, `silben_grenzen`); the few
built-ins without an English alias are marked *(German only)* above.

## 7. More examples and documents

- `beispiele/ki_*.moos` – including `ki_xor.moos`, `ki_kinderleicht.moos`,
  `ki_mnist.moos`, `ki_mnist_conv.moos`, `ki_embeddings.moos`,
  `ki_sprachmodell.moos`, `ki_sprachmodell_gpu.moos`, `ki_moe.moos`,
  `ki_grpo.moos`, `ki_mtp.moos`, `ki_denken.moos`, `ki_multimodal*.moos`
  (`_audio`, `_farbe`, `_joint`, `_vision`), `ki_bild_beschreiben.moos`,
  `ki_audio.moos`, `ki_kamera_live.moos`, `ki_mikro_spektrum.moos`,
  `ki_pong_auge.moos`, `ki_safetensors_import.moos`, `ki_verteilt.moos`.
- `beispiele/eval_*.moos` – evaluation and benchmark runs (`eval_m6b_mini_lm.moos`,
  `eval_m6b_mnist.moos`, `eval_q1_hadamard_mini_lm.moos`, `eval_q2_kv_cache_lm.moos`,
  `eval_q3_lloyd_max_codebuch.moos`, `eval_r1_attnres_lm.moos`).
- `beispiele/p4_silben/` – syllable language model pipeline (tokenizer, training, eval).
- `beispiele/p5/` – training API, SFT, distillation, DPO, evaluation.
- `beispiele/p6_wissensmix/` – knowledge-mix training, LLaMmlein import/fine-tuning,
  BPE/packing comparisons and GPU parity (`training_10m.moos`,
  `feintuning_llammlein.moos`, `llammlein_import.moos`, `messlauf_bpe.moos`,
  `packing_vergleich.moos`, `gpu_paritaet.moos`).
- `stdlib/ki_bild_easy.moos` – high-level class `BildModell` ("image → text").
- [docs/kip/](kip/) – design and contract documents for the AI stack (dtypes,
  GPU residency, distributed training, serving, post-training; German).

## 9. Compiler diagnostics for agents

Every compiler diagnostic has a code, a severity, a location and a German message.
The compiler collects **all** lexer and syntax errors of a run instead of stopping
at the first one.

```bash
moo-compiler pruefe datei.moos                    # alias: check – lex, parse, imports, codegen, LLVM verify; no binary
moo-compiler pruefe datei.moos --diagnose=json    # exactly one JSON object on stderr
moo-compiler erklaere M-S001                      # alias: explain – catalog entry; without code: list all
```

`--diagnose=text|json` works for `pruefe`, `compile`, `run`, `ast` and `layout`.
Exit code: 1 if there is at least one error, 0 if there are only hints.

Human format (default):

```
datei.moos:2:15: Fehler M-S001: Doppelpunkt ':' erwartet, aber Zeilenende gefunden. Nach Bedingungen ...
    2 |     wenn x > 1
      |               ^
    erwartet: Doppelpunkt ':' | gefunden: Zeilenende
1 Fehler
```

JSON format (keys in English, messages in German, unknown values are `null`):

```json
{"schema":1,"diagnostics":[{"code":"M-C001","severity":"error","file":"datei.moos","line":4,"column":null,
  "end_column":null,"function":null,"message":"Funktion 'rechen' nicht gefunden. Meintest du 'rechne'?",
  "expected":null,"found":"rechen","suggestion":"Meintest du 'rechne'?"}],"errors":1,"hints":0}
```

Agent workflow: run `pruefe --diagnose=json`, fix each diagnostic at `file:line:column`
(use `expected`/`found`/`suggestion`), repeat until `errors == 0`. Hints (`severity: hint`)
do not block the build.

Diagnostic catalog (source of truth: `compiler/src/diagnose.rs`, `moo-compiler erklaere`):

| Code | Meaning |
|------|---------|
| M-L001 / M-L002 / M-L003 / M-L004 | Unknown character / unterminated string / f-string `}` missing / f-string unterminated |
| M-S001 | Token expected (`expected`, `found`, `suggestion` filled) |
| M-S002 | Name/identifier expected |
| M-S004 | Nested too deeply (parentheses, blocks, operator/call/index chains count together). The limit depends on the compiler thread's stack: normally 1000 levels (512 MiB), under a memory limit (`ulimit -v`) 250 (128 MiB) or 62 (32 MiB); the message names the active limit |
| M-S000 | Other syntax error |
| M-I001 / M-I002 / M-I003 / M-I000 | Module not found / module does not define the name / file not readable / other import error |
| M-C001 / M-C002 | Unknown function / unknown method (with "Meintest du ...?" suggestion) |
| M-C003 / M-C004 | Too few arguments for a built-in (exact or minimum count, derived from the code generator at build time; `moo-compiler builtins` lists them) / hint: surplus arguments are not evaluated |
| M-C999 | Internal compiler error (caught Rust panic, or the compiler thread could not start); a compiler bug, not a program error |
| M-C000 / M-N000 | Other code generation error / error in native code (`@nativ`, structs) – location: file + function, line if known |
| M-N001 | A `@nativ` function is used as a value (`setze f auf name`, `liste.map(name)`, callback argument) although that is not possible for it: its parameters or result are not numbers or booleans (structs, pointers, buffers), it has a system body (`unsicher:`) or it is a C export. Exception: a scalar hosted wrapper whose `unsicher:` body only calls an `extern "C"` function is allowed as a value; other `unsicher:` bodies are not. Call it directly or use a Moo function as a wrapper |
| M-N002 | Native function value in the bare-metal/UEFI profile (the catalog also names `wasm`): only the direct call of the `@nativ` function is possible |
| M-N003 | A function declared with `extern "C"` is used as a value; it is not a moo value; wrap the C call in a scalar `@nativ` function (see the exception at M-N001) |
| M-N004 | A `@nativ` function is called directly (from dynamic or native code) with a different number of arguments than its signature has; the message names the expected and the passed count. For a native function *value* the runtime checks the count instead and reports a catchable error |
| M-H001 … M-H005 | Hints: type mismatch on a method, own struct shadows a built-in type, module alias shadowed, float literal rounds to zero, unknown target |
| M-H006 / M-H007 | Hints: nonsensical operator between number and text (`--typpruefung`) / number literal without digits or above u64 |
| M-H008 | Hint: deprecated name (transitional alias). `kern_inw`/`kern_outw`/`kern_inl`/`kern_outl` → `port_lese16`/`port_schreibe16`/`port_lese32`/`port_schreibe32` (EN `port_in16`/`port_out16`/`port_in32`/`port_out32`); same effect, replace the name. Silent in stdlib modules |
| M-P001 | Compile error: a function, method or lambda reachable from `starte`/`spawn` reads or writes a top-level variable that is not marked `geteilt` (EN `shared`). Allowed without marker: a never reassigned `konstante` holding a number, text, boolean or `nichts`. Fix: send the value through a channel or pass it as a `starte` argument (`starte(f, a, b, ...)`, up to eight arguments, each shared and deep-frozen like any shared value), mark the global `geteilt konstante NAME auf wert` (deep-frozen), or use a lock/atomic. The message names the call chain |
| M-P002 | Access in code that is only *possibly* reachable from a thread (function value, method by name, unknown `starte` value). Only an error with `--typ-streng`; otherwise checked at run time in every build of programs that use `starte` (violation: message and exit 1, not catchable) |
| M-P003 | A `geteilt` global is assigned again, or `geteilt` is used below top level. Shared globals are deep-frozen at initialisation. Fix: unmarked global that only the main thread changes, or lock/atomic |
| M-T001 … M-T005 | Type checking (`--typpruefung`, errors with `--typ-streng`): annotation mismatch / argument type / return type / argument count (hint) / element or field type |
| M-F001 / M-F002 | Hints (errors with `--typ-streng`): a call to a function that throws, or to a failing built-in (network, database, file loading), is not inside `versuche:`. Fix: wrap the call in `versuche:` |
| M-W000 | Tool or command line error (outside the source) |

Concurrency in short (rule: a mutable value belongs to exactly one thread; sharing freezes it deeply):

```
geteilt konstante Z auf atom(0)                 # shared cell; EN: shared
geteilt konstante KONTO auf sperre({"stand": 0})

func work(n):                                    # EN: func, spawn, wait, atomic_*, lock, with
    atomic_add(Z, n)                            # returns the OLD value
    with KONTO as k:                            # DE: mit KONTO als k:
        k["stand"] = k["stand"] + n
    return n

set t to spawn(work, 5)                         # up to eight arguments: spawn(f, a, b, ...)
show t.wait()                                   # first waiter owns the result
```

Atom functions: `atom_laden|atomic_load`, `atom_speichern|atomic_store`, `atom_addieren|atomic_add`, `atom_tauschen|atomic_swap`, `atom_vergleichen_und_tauschen|atomic_compare_exchange`. A second thread calling `wait` on the same thread gets a catchable error. `kanal.senden(übergib p)` / `transfer p` moves a buffer to the receiver without copy or freeze (the sender's variable becomes `nichts`; extra references or views are a catchable error). Hardware syntax (`@nativ` + `unsicher:`): `port_lese*`/`port_schreibe*` (EN `port_in*`/`port_out*`) are the canonical names; `kern_in*`/`kern_out*` are deprecated (M-H008). Examples verified against the compiler: see `docs/lernen.md`, section "Multithreading".

Native function values: in hosted programs a `@nativ` function can be stored in a variable (`setze f auf quadrat`), passed as an argument, used in `map`/`filter`, imported from a module and started as a thread (`starte(quadrat, 6)`; the thread calls it with the arguments given after the function, and a native value checks their count before the thread starts). The first thread that waits owns the result: every further `warten` of that thread returns the same result again, any other waiting thread gets a catchable error. A call through the value checks the argument count and the argument types before the native call: a wrong count, a wrong type, a non-integral value for an integer parameter or an out-of-range value (for example `300` for `i8`) is a runtime error that `versuche:` can catch, and the program continues normally. What is not possible is reported at compile time: M-N001 (see table), M-N002 (bare/UEFI), M-N003 (`extern "C"` as a value). Evidence: the fixtures `compiler/tests/p2/native_wert_*`.

Native calls: a direct call of a `@nativ` function with the wrong number of arguments is the compile error M-N004 (with a line number when the call is in dynamic code or in a native function with control flow, i.e. a system body; a direct call between plain native functions is reported without a line; evidence: `compiler/tests/p2/native_call_arity*`). Recursive and mutually recursive `@nativ` functions are supported, including a pure `@nativ` caller of a recursive function (evidence: `compiler/tests/p2/native_call_rekursion_*`). `moo-compiler erklaere M-N004` prints the catalog entry.

Reserved ranges: `M-T` type checking, `M-M` memory, `M-F` error paths, `M-P` threads and shared state (P2-T114).
Runtime hints for unhandled errors (`Hinweis: unbehandelter Fehler ...`) are printed
by the running program, not by the compiler.

Exit code: an unhandled `wirf` (no `versuche:` in the call path) prints
`Unbehandelter Fehler: <text>` plus the throw site to **stderr** and exits with code 1,
independent of `MOO_STRENG`. An unhandled error in a worker thread does not stop the
process at once (thread isolation) but is reported and the process exits with 1 at the
end. `teste_alle()` exits with 1 if any test fails.

Known limits: code generation stops at its first error (lexer and parser collect all);
a missing `)` is reported at the next statement because line breaks inside brackets are joined.

## 8. Important: data and models are not in the repo

Training corpora and model checkpoints are **not** included. For your own experiments
you need your own texts (UTF-8). For the fairy-tale example,
`skripte/maerchen_download.sh` downloads a public-domain corpus. Some examples under
`beispiele/p4_silben/` and `beispiele/p5/` expect paths or environment variables
(e.g. `P4_TRAIN_CORPUS`) pointing to your own data.
