# moo

[Deutsch](README.md) · **English**

**One language from script to kernel — in German or English, comfortable when dynamic, fast when native.**

moo compiles to native code through LLVM, and its runtime is written in C. The same language carries scripts, AI training on the GPU, 3D games, its own user interfaces, network protocols written in pure moo, and a UEFI loader with its own network driver that has been booted on real hardware.

> **Top highlight: moo on real hardware.** A UEFI application written in moo boots from a USB stick on an Intel N100 mini PC and shows the MoOS interface drawn by moo. An own I226-V network driver written in moo ran there and reported a 100 Mbit link on port 0. *Demonstrated on real hardware as of 1 Nov 2026; the hardware run has not been repeated with the latest state.*

Maturity levels in this README: **tested** (automatic tests or gates), **works**, **partial**, **work in progress**. Details with weaknesses are in [docs/STAND.md](docs/STAND.md) (German).

## 1. Two levels, German and English

German and English are equal; the keywords are defined in `spec/tokens.yaml` and checked in CI. Ordinary code is dynamic and comfortable. With `@native` and types, a function becomes machine code without boxing. Maturity: language tested, native level works.

All snippets in this file use the English keywords and the English aliases of the built-in functions. Where a function has no English alias yet, its German name stays; these gaps are listed in [docs/STAND.md](docs/STAND.md).

```moo
func fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

show fibonacci(20)
```

The same in German (both spellings can be mixed in one file):

```moo
funktion fibonacci(n):
    wenn n <= 1:
        gib_zurück n
    gib_zurück fibonacci(n - 1) + fibonacci(n - 2)

zeige fibonacci(20)
```

Both programs print `6765`. The native level:

```moo
@native
func square_sum(n: i64) -> i64:
    set s: i64 to 0
    for i in 0..n:
        set s to s + i * i
    return s

show square_sum(1000)
```

Output: `332833500`. In a single measurement (loop of 10^8 iterations), `@native` took 0.033–0.039 s, C with `-O2` 0.071–0.073 s, dynamic code 4.9 s.

## 2. AI training, also on the GPU

Tensors with autograd (gradient check over the whole operation list), layers up to the transformer, optimizers, fine-tuning with SFT, distillation and DPO, and BPE and syllable tokenizers. Training can run through Vulkan compute with 28 shaders; a strict mode and `gpu_statistik()` show whether the GPU really did the work. Maturity: autograd tested, the rest works (one GPU verified).

English rendering of `beispiele/ki_xor.moos` (the whole training is 14 lines):

```moo
set net to [layer_dense(2, 8, "tanh", 7), layer_dense(8, 1, "sigmoid", 8)]
set opt to optimizer_adam(parameters(net), 0.05)
while i < 400:
    set loss to mse(forward(net, x), target)
    loss.backward()
    opt.step()
```

Guide: [docs/AI_WITH_MOO.md](docs/AI_WITH_MOO.md).

## 3. 3D, voxels and visual checks without a monitor

One `space_*` API for 3D on OpenGL 2.1, OpenGL 3.3 and Vulkan with lighting, shadows, fog, bloom, sky and time of day; a voxel engine for whole worlds. Games can be checked without a screen: a test API simulates input, reads pixels back and saves images; a GIF encoder is built in. Maturity: 3D and voxels work on all three backends (OpenGL 2.1, OpenGL 3.3, Vulkan), test API tested.

English rendering of `beispiele/voxel_sandbox_selftest.moos`:

```moo
set world to voxel_world_new(1337)
for gcx in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
    for gcy in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
        set new_chunks to new_chunks + voxel_generate(world, gcx, gcy)
set win to unified_window("voxel selftest", WIN_W, WIN_H)
space_perspective(win, 70.0, 0.1, 300.0)
set info to test_window_info(win)
```

According to the file header, the selftest runs headless: `xvfb-run -a -s "-screen 0 1280x800x24" env MOO_3D_BACKEND=gl33 moo-compiler run <file>`.

## 4. Own user interfaces and UI tests

Self-drawn widgets without GTK on X11, Wayland and Windows, with layout, menus, clipboard, glass theme and compositor. UI tests take a screenshot together with a JSON description (snapshot with sidecar) and click widgets by ID. Maturity: Linux and Windows work, UI tests tested; macOS was ported blind and is unverified.

English rendering of `beispiele/ui_login_demo.moos`:

```moo
import ui
ui_build(g, "Login", 340, 260, [
    [setup_login_form, ["Username:", "Password:", 100, on_login, on_cancel]],
    [setup_status_bar, ["Ready", "v1.0"]],
])
ui_show(g["hWindow"])
ui_run()
```

English rendering of `beispiele/ui_snapshot_demo.moos`:

```moo
ui_widget_set_id(g["btnSave"], "save")
func do_snapshot():
    set ok to ui_test_snapshot_with_sidecar(g["hWindow"], "snapshots/demo1")
```

## 5. Network and database — also in pure moo

TCP, UDP, HTTP server and client and TLS 1.3 are built in (tested); so are SQLite, JSON, regex, files, threads and channels. On top sit protocols written entirely in moo: a DNS resolver, a WebSocket client and a PostgreSQL client including SCRAM login. Maturity: built-ins tested, protocols in moo work.

English rendering of `beispiele/dns_selftest.moos` (selftest without network):

```moo
import dns
set a_resp to parse_response(build_packet(1, TYP_A, [1, 2, 3, 4]))
check("A value", a_resp["answers"][0]["rdata"] == "1.2.3.4")
```

English rendering of `beispiele/websocket_client_selftest.moos`:

```moo
import websocket
set ws to ws_connect(url, 10000)
ws.send_text("hello websocket")
set text_event to ws.receive()
check("text echo", text_event["data"] == "hello websocket")
```

English rendering of `beispiele/domain/db/bench/pg_test.moos` (Postgres protocol in moo):

```moo
func int32_be(n):
    set b0 to floor(n / (256 * 256 * 256)) % 256
    set b1 to floor(n / (256 * 256)) % 256
    set b2 to floor(n / 256) % 256
    set b3 to n % 256
    return [b0, b1, b2, b3]
```

English rendering of `beispiele/domain/web/http_api.moos` (shortened):

```moo
set server to web_server(3000)
while true:
    set req to server.web_accept()
    set path to req["pfad"]
    # ...
    if path == "/api/status":
        web_json(req, {"language": "moo", "todos": len(todos)})
        continue
```

## 6. Camera and microphone

Camera and audio capture: on Windows through Media Foundation and WASAPI (verified on hardware), on Linux through V4L2 and ALSA (with mock tests). Camera frames can be turned into tensors; pure moo writes PNG and WAV files; `beispiele/ki_audio.moos` turns audio into a spectrogram for a network. Recording as well as WAV and spectrogram are there; playback through speakers follows. Maturity: works.

English rendering of `beispiele/ki_kamera_live.moos`:

```moo
set camera to camera_open()
set frame to camera_frame(camera, 1000)
test_frame_save_bmp(frame, "/tmp/camera_live.bmp")
camera_close(camera)
```

## 7. Kernel, UEFI and own network driver

moo produces bare-metal programs: an x86_64 kernel in Multiboot2 format (tested under QEMU), UEFI applications as PE32+ and AArch64 on QEMU-virt. The loader and the network driver are written in moo; demonstrated on real hardware (Intel N100, as of 1 Nov 2026) are UEFI boot with a moo interface and the I226-V driver with link. The kernel foundation is still mostly C; MoOS is **work in progress** (plan P7: 7 of 16 steps). Scheduler, processes, file system and PCI management are still missing.

English rendering of `beispiele/uefi/hallo_uefi.moos`:

```moo
from "./netz_treiber" import nt_pci_init, nt_ports_oeffnen, nt_anzahl, nt_port, nt_link, nt_udp_log, nt_log, nt_status_text, nt_dma, nt_dez

func net_log(ports, buf, text):
    set n to nt_anzahl(ports)
    if n == 0:
        kern_net_send(text)
        return none
    nt_log(ports, buf, text)
```

English rendering of `beispiele/kernel/hallo_kern.moos` (build: `compile --no-stdlib --target x86_64-bare --kernel`):

```moo
func main():
    unsafe:
        set address to kern_alloc(64)
        kern_serial_hex(address)
        kern_free(address)
        kern_idt_init()
        kern_pic_remap(0x20, 0x28)
        kern_timer_init(100)
```

## 8. Binding C libraries

`extern "C"`, callbacks, structs by value, opaque handles and C++ through adapters; the binding generator `moo-compiler bindgen` produces a draft from C headers, and `layout --c-header` checks struct layouts. Maturity: works; pointers and `unsafe` only partial.

English rendering of `beispiele/nativ/10_rueckruf.moos` — a moo function as comparison callback for libc `qsort`:

```moo
extern "C" func qsort(base: ptr[Item], count: u64, size: u64, compare: ptr[fn(ptr[Item], ptr[Item]) -> i32]) -> void

@native
@callback
func compare_items(a: ptr[Item], b: ptr[Item]) -> i32:
    unsafe:
        if a.value < b.value:
            return -1
        # ...

# in run_sort:
        qsort(pointer(u64(buffer_pointer(p))), length(p), 4, compare_items)
```

## 9. Testing culture

Sanitizer runs (memory and undefined-behavior checks) on three operating systems, a full run `run_all`, a conformance suite (levels G0–G7), gradient checks for the AI operations and ten CI workflows; the GitHub CI is green on Linux, Windows and macOS. Examples write their own tests in moo, like this (English rendering of `beispiele/websocket_client_selftest.moos`):

```moo
func check(name, condition):
    if not condition:
        throw "FAIL " + name
    show "PASS " + name
```

Maturity: tested.

## For AI agents (vision, work in progress)

moo should be easy for AI agents to program: unified diagnostics with error codes, JSON output (`--diagnose=json`) and `moo check` (German: `moo pruefe`) are in review (P2-T97). Type checking before execution, with file:line and a suggestion, follows. This is not finished; today there are compile errors with line and column.

## Outlook

Planned, not yet started: an A/B comparison of GPU training runs (P3), adaptive sparse computation in the AI stack (P8), and adaptive search with fast method dispatch in the language core (P9).

## Quick start

```bash
cd compiler && cargo build --release
./target/release/moo-compiler run ../beispiele/fibonacci.moos
./target/release/moo-compiler compile ../beispiele/nativ/03_casts.moos -o casts
```

Requirements: [DEPENDENCIES.md](DEPENDENCIES.md). All targets (x86_64, x86, ARM, AArch64, RISC-V, MIPS, PowerPC, WebAssembly, Windows and freestanding variants): `moo-compiler targets`.

## How it works

The compiler (Rust, `compiler/src`) translates moo to native code through LLVM 18 (crate `inkwell`) and links the C runtime (`compiler/runtime`). Dynamic code runs on the runtime; `@native` functions are compiled directly with machine types.

```
compiler/   compiler (Rust), C runtime, tests
stdlib/     standard library modules in moo: ui, ui_moo, websocket, dns, welt, vektor …
beispiele/  runnable .moos programs
docs/       user documentation
spec/       specification (incl. tokens.yaml)
src/moo/    Python tools (LSP, formatter, transpiler), not the main path
editors/    VSCode extension (small)
website/    landing page and playground (server-side)
```

More: [ARCHITECTURE.md](ARCHITECTURE.md), [spec/runtime_layers.md](spec/runtime_layers.md).

## Status

Alpha, an active one-person project developed with AI agents. File extension `.moos`. Plan status 15 Nov 2026: main roadmap 423 of 504 steps, native level 74 of 89, AI training methods 17 of 20, syllable tokenizer 14 of 17, MoOS 7 of 16. Maturity levels, weaknesses and open items: **[docs/STAND.md](docs/STAND.md)** (German).

## Documentation

- Learn the language: [docs/lernen.md](docs/lernen.md) (German)
- AI with moo: [docs/AI_WITH_MOO.md](docs/AI_WITH_MOO.md) · German: [docs/KI_MIT_MOO.md](docs/KI_MIT_MOO.md)
- 3D: [docs/raum_3d.md](docs/raum_3d.md); user interfaces: [docs/ui_moo.md](docs/ui_moo.md); TLS on Windows: [docs/windows_tls.md](docs/windows_tls.md) (German)
- Architecture: [ARCHITECTURE.md](ARCHITECTURE.md), dependencies: [DEPENDENCIES.md](DEPENDENCIES.md)
- Builtin reference: [moolang-docs](https://7blacky7.github.io/moolang-docs/)
- Native level: [language reference draft](dokumentation/p2/language-reference-draft-en.md), [migration guide](dokumentation/p2/migration-guide-en.md)

## License

To be decided before v1.0. The repository currently contains no LICENSE file.
