#!/usr/bin/env bash
# run_sanitize.sh - Opt-in Sanitizer-Runner fuer die C-Runtime-Harnesses (Plan-007 P007-U1)
# ============================================================================
# ZWECK
#   Baut und laeuft ALLE Voxel-Runtime-Harnesses unter compiler/runtime/tests/
#   in zwei waehlbaren Sanitizer-Modi:
#     - asan   : AddressSanitizer wie bisher, mit detect_leaks=1
#     - ubsan  : UndefinedBehaviorSanitizer, -fsanitize=undefined
#                -fno-sanitize-recover=undefined  (UB => sofortiger Abbruch)
#     - all    : erst asan, dann ubsan (Standard, wenn kein Modus angegeben)
#
# GRUNDSATZ (Plan-007, Memory ub-arithmetik-policy + plan-007-c-runtime-ub-hardening-ohne-fwrapv)
#   * KEIN -fwrapv. Nirgends. UB wird sichtbar gemacht, nicht maskiert.
#   * Default-Build (cargo/build.rs) bleibt von diesem Script UNANGETASTET.
#     Dies ist eine reine opt-in Schiene zum Aufspueren von UB.
#   * Jeder Sanitizer-Fund => Exit-Code != 0. Klares Logging welcher Modus/
#     welche Flags aktiv sind.
#   * Plattform-/Toolchain-Skip ist transparent (Diagnose-Ausgabe), nie still.
#
# NUTZUNG
#   ./run_sanitize.sh            # = all  (asan + ubsan)
#   ./run_sanitize.sh asan       # nur AddressSanitizer
#   ./run_sanitize.sh ubsan      # nur UndefinedBehaviorSanitizer
#   ./run_sanitize.sh all        # beide Modi nacheinander
#   CC=clang ./run_sanitize.sh   # Compiler ueberschreibbar (Default: gcc, sonst cc)
#
# EXIT-CODES
#   0  alle gebauten/gelaufenen Harnesses sauber (rc=0, keine Sanitizer-Funde)
#   1  mindestens ein Build- oder Laufzeitfehler / Sanitizer-Fund
#   2  Sanitizer auf dieser Toolchain nicht verfuegbar -> transparenter Skip
#      (kein "grueneuger" Erfolg, aber auch kein hartes Versagen im CI-Gate)
#
# WICHTIG: LINK-MATRIX (QA1-Lehre, dokumentiert in den Harness-Headern)
#   Alle Harnesses ausser worldgen bringen ihren EIGENEN moo_noise_fbm-Stub mit
#   und linken NUR ../moo_voxel.c. Wer zusaetzlich ../moo_noise.c linkt, bekommt
#   eine multiple-definition (moo_noise_fbm doppelt). EINZIG test_voxel_worldgen
#   linkt das ECHTE ../moo_noise.c (kein Stub) - das ist die Determinismus-Probe.
#   test_voxel_jobqueue braucht zusaetzlich -lpthread.
# ============================================================================

set -u

# --- Pfade ------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || { echo "FATAL: kann nicht nach $SCRIPT_DIR wechseln"; exit 1; }
RUNTIME_DIR=".."          # compiler/runtime/
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/moo_sanitize.XXXXXX")"
trap 'rm -rf "$OUT_DIR"' EXIT

# --- Compiler ---------------------------------------------------------------
CC="${CC:-}"
if [ -z "$CC" ]; then
  if command -v gcc >/dev/null 2>&1; then CC=gcc
  elif command -v clang >/dev/null 2>&1; then CC=clang
  elif command -v cc >/dev/null 2>&1; then CC=cc
  else
    echo "SKIP: Kein C-Compiler (gcc/clang/cc) gefunden -> Sanitizer-Lauf nicht moeglich."
    exit 2
  fi
fi

# --- Harness-Matrix ---------------------------------------------------------
# Format je Zeile: "<basename>|<zusaetzliche-quellen>|<zusaetzliche-libs>"
#   <zusaetzliche-quellen>: weitere .c relativ zu RUNTIME_DIR (leer = nur moo_voxel.c)
#   <zusaetzliche-libs>   : zusaetzliche Linker-Flags ueber -lm hinaus
# ../moo_voxel.c und -lm sind IMMER dabei (gemeinsamer Nenner aller Harnesses).
HARNESSES=(
  "test_voxel_section_asan.c||"
  "test_voxel_palette_asan.c||"
  "test_voxel_mesher_asan.c||"
  "test_voxel_dirty_rendercache_asan.c||"
  "test_voxel_raycast_asan.c||"
  "test_voxel_jobqueue_asan.c||-lpthread"
  "test_voxel_downgrade_asan.c||"
  "test_voxel_worldgen_asan.c|moo_noise.c|"   # EINZIGER mit echtem moo_noise.c
)

# --- P008-Harness-Matrix (non-voxel) ----------------------------------------
# Die Plan-008-Harnesses (Frame/GIF/Sim-Input) linken KEIN moo_voxel.c, sondern
# ihren je EIGENEN, vollstaendigen Quell-Satz. Format je Zeile:
#   "<basename>|<komplette-quell-liste relativ zu RUNTIME_DIR>|<extra-libs>"
# Die Quell-Liste ist VOLLSTAENDIG (kein impliziter gemeinsamer Nenner ausser
# -lm). Begruendung der Saetze (verifiziert P008-FINAL-Gate):
#   * frame:    moo_frame.c + moo_memory.c. moo_memory.c->moo_release() dispatcht
#               MOO_FRAME->moo_frame_free UND MOO_GIF->moo_gif_handle_free
#               (P008 A3A/A3B) -> ohne moo_gif_handle.c + moo_gif.c = undefined
#               reference. Restliche *_free-Dispatch-Ziele stubbt der Harness.
#   * gif_core: NUR moo_gif.c (Encoder-Kern, KEINE MOO-Abhaengigkeit, keine Stubs).
#   * gif_wiring: Encoder + Wrapper + MOO_FRAME + Core-Runtime-Bausteine.
#   * sim_input: NUR moo_3d.c (Dispatcher, kein GL/GLFW; Backend wird gestubbt).
EXTRA_HARNESSES=(
  "test_frame_asan.c|moo_frame.c moo_memory.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c|-lm"
  # frame_tensor: Frame<->Tensor-Bruecke (KI-MULTI-V1). Braucht Frame- UND
  #               Tensor-Familie (f32_sichern -> ki_gpu/autograd-Symbole).
  "test_frame_tensor_asan.c|moo_frame_tensor.c moo_frame.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c|-lm"
  # audio: KI-MULTI-A1 — FFT/Parseval/STFT/WAV, kein Autograd/Hardware.
  #        Gleicher Tensor/Core-Satz + Test-throw-Modell wie frame_tensor.
  "test_audio_asan.c|moo_audio.c moo_frame.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c|-lm"
  "test_gif_core_asan.c|moo_gif.c|-lm"
  "test_gif_wiring_asan.c|moo_gif.c moo_gif_handle.c moo_frame.c moo_value.c moo_memory.c moo_string.c moo_dict.c moo_error.c moo_print.c moo_list.c moo_ops.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_video_handle.c moo_video.c|-lm"
  "test_sim_input_asan.c|moo_3d.c|-lm"
  #   video_wiring: MOO_VIDEO-Core (moo_video.c) + immer-gebauter Heap-Wrapper
  #               (moo_video_handle.c) + Core-Runtime. moo_memory.c->moo_release()
  #               dispatcht MOO_VIDEO->moo_video_handle_free (P009-V0); die uebrigen
  #               *_free-Dispatch-Ziele stubbt der Harness. moo_value.c braucht
  #               moo_string_new -> moo_string.c (+ dict/list/ops als deren Deps).
  #               Mock-ffmpeg: der Harness schreibt zur Laufzeit ein "ffmpeg"-sh-
  #               Skript in ein mkdtemp-Dir + setzt PATH -> KEIN echtes ffmpeg/GPU.
  "test_video_wiring_asan.c|moo_video.c moo_video_handle.c moo_memory.c moo_value.c moo_error.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c|-lm"
  #   tensor:     Plan-014 A1 — MOO_TENSOR-Kern (Konstruktoren/Zugriff/Refcount/
  #               Determinismus). Core-Runtime-Satz OHNE moo_error.c: der Harness
  #               bringt das Test-throw-Modell mit (Flag + free des strdup-Texts,
  #               Muster Voxel-Harnesses) — sonst leaken Fehlerpfade by design.
  "test_tensor_asan.c|moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   tensor_ops: Plan-014 A2 — Op-Registry + Kern-Ops (Broadcasting/matmul/
  #               Reduktionen/Aktivierungen/Softmax-Stabilitaet). Gleicher
  #               Quell-Satz + Test-throw-Modell wie tensor.
  "test_tensor_ops_asan.c|moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   autograd:   Plan-014 B1 — Tape-Lifecycle (record/rueckwaerts/reset,
  #               Fan-out-Akkumulation, Broadcast-Reduktion Bias-Grad,
  #               no_grad). ASan haelt das Tape-Retain/Release-Gate.
  "test_autograd_asan.c|moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   gradcheck:  Plan-014 B2 — DAS Ehrlichkeits-Gate: numerischer Gradient
  #               vs. Autograd fuer JEDEN Registry-Op (automatische Iteration
  #               via op_count/at — neuer Op ohne Gradcheck faellt hier auf).
  "test_gradcheck.c|moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   contrastive: KI-MULTI-L1 — kosinus + symmetrisches InfoNCE als reine
  #                Registry-Op-Komposition; eigener numerischer FD-Gradcheck.
  "test_contrastive_asan.c|moo_contrastive.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   gather_leak: KIP-T1 — Refcount-/Tape-Leak-Gate fuer gather fwd+bwd. Unter
  #               ASan zaehlt LeakSanitizer (RSS-Check dort uebersprungen); der
  #               echte 1M-RSS-Stabilitaetslauf laeuft standalone ohne Sanitizer.
  "test_gather_leak.c|moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   nn:         Plan-014 C1 — Schichten/Loss/Optimizer (Dict-basiert, reine
  #               Op-Komposition, kein neuer Registry-Op). Inkl. XOR-
  #               Konvergenz-Gate im Harness; Quell-Satz + Test-throw-Modell
  #               wie autograd, plus moo_nn.c.
  "test_nn_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   tensor_dtype: KIP-D1 — bf16-Storage-DType. Roundtrip/Special-Values,
  #               Valid-Masken-Vertrag (D0 §2), Stale-Store-Regression,
  #               bf16-Op-Matrix (26+ Ops bit-exakt vs rundungs-f32) +
  #               ce/mse/vorwaerts. Quell-Satz wie nn. ASan-Leak-Gate.
  "test_tensor_dtype_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   mixed_precision: KIP-D2 — Mixed-Precision-Training bf16 (Aktivierungen
  #               bf16-gerundet, Parameter-Master/Optimizer f32). bf16_runden ==
  #               als_dtype-Storage-Pfad bit-identisch; XOR+CE bf16 vs f32 in
  #               Toleranz + bf16 deterministisch; Default-AUS == reines f32.
  "test_mixed_precision_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   safeten-dt: KIP-D3 — Safetensors DType-Vertrag: F32/BF16/F16-Import (->f32),
  #               f32-Export byte-identisch, bf16-Export->Import bit-exakt,
  #               Negativ unbekannter dtype. Quell-Satz wie test_nn_asan.
  "test_safetensors_dtype_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  "test_dataset_asan.c|moo_dataset.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   tokenizer:  KIP-T2 — Byte-level BPE (train/encode/decode/save/load/hash).
  #               Test-throw-Modell wie dataset; Tensor-Kern + Core-Runtime.
  #               Gates: Determinismus ohne Seed, byte-exakter Roundtrip inkl.
  #               invalid-UTF8/NUL/Emoji, Artefakt-Roundtrip, UNK-Negativ-Gate.
  "test_tokenizer_asan.c|moo_tokenizer.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   shard:      KIP-E1 — Streaming-Token-Shards + Dataloader (Format/CRC,
  #               gefenstertes Lesen, seed-deterministische Blockreihenfolge,
  #               SFT-Loss-Maske, Train/Val-Split). Test-throw-Modell.
  "test_shard_asan.c|moo_shard.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   checkpoint: KIP-E2 — CPU-Voll-Checkpoint v2 (Modell+Optimizer m/v/t+
  #               Dropout-Zaehler+Dataloader-Pos, atomisch tmp+rename, Rotation).
  #               GATE: Kill+Resume bit-identisch MIT Dropout. Quell-Satz wie nn.
  "test_checkpoint_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   ckpt_ag:    KIP-B4b — Activation Checkpointing (Sub-Tape Re-Forward +
  #               Dropout-Zaehler-Restore). GATE: Grad mit/ohne moo_nn_checkpoint
  #               BIT-identisch MIT Dropout (Param+Input), Determinismus, tiefes
  #               Multi-Dropout-Segment. Quell-Satz wie nn/checkpoint.
  "test_checkpoint_ag_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   bare_alloc: Plan-010 T1 — Bare-Allocator (K3) + serielle Formatter (K2)
  #               auf dem Host. Linkt NUR moo_bare_alloc.c + moo_bare_console.c;
  #               moo_bare.c/moo_bare_boot.c bewusst NICHT (echte in/out-Asm
  #               wuerde im Userspace GP-faulten; _start-Trampolin gehoert nicht
  #               auf den Host). Stubs im Harness: kern_inb/kern_outb (16550-Emu
  #               mit DLAB+Loopback), kern_panic (longjmp), moo_number/bool/none.
  #               VGA-Pfade werden nie aufgerufen (0xB8000), nur gelinkt.
  "test_bare_alloc_asan.c|moo_bare_alloc.c moo_bare_console.c|-lm"
  "test_bare_portio_asan.c|moo_bare.c moo_bare_console.c|-DMOO_BARE_PORT_STUB -lm"
  #   stackprot: P012-A4 — Fail-Bruecke __stack_chk_fail -> kern_panic auf dem
  #              Host. Linkt NUR moo_bare_stackprot.c; Stubs im Harness:
  #              kern_panic (longjmp), kern_seriell_text (no-op), moo_none.
  #              Der echte Canary-Kipp-Beweis ist scripts/sp-smoke.sh (QEMU).
  "test_stackprot_asan.c|moo_bare_stackprot.c|"
  #   bare_mmio: P012-B3 — portables MMIO-API (moo_mem_read/write +
  #              kern_mmio_*) gegen malloc-Arena, volatile 1/2/4/8,
  #              invalid-size-Semantik. Port-Funktionen nur gelinkt.
  "test_bare_mmio_asan.c|moo_bare.c|"
)

# --- Sanitizer-Verfuegbarkeit pruefen (Probe-Kompilat) ----------------------
# Gibt 0 zurueck wenn der Sanitizer baut+laeuft, sonst != 0. Stille Probe.
sanitizer_available() {
  local flags="$1"
  local probe="$OUT_DIR/_probe.c"
  local probe_bin="$OUT_DIR/_probe.bin"
  cat > "$probe" <<'EOF'
int main(void) { return 0; }
EOF
  # shellcheck disable=SC2086
  "$CC" $flags -o "$probe_bin" "$probe" >/dev/null 2>&1 || return 1
  "$probe_bin" >/dev/null 2>&1 || return 1
  return 0
}

# --- Ein Harness in einem Modus bauen + laufen ------------------------------
# Args: <mode> <build-flags> <run-env> <basename> <extra-sources> <extra-libs>
# Rueckgabe: 0 ok, 1 build-fail, 2 run-fail/sanitizer-fund
build_and_run() {
  local mode="$1" bflags="$2" renv="$3" base="$4" extra_src="$5" extra_libs="$6"
  local tag="${base%.c}"
  local bin="$OUT_DIR/${tag}.${mode}"
  local log="$OUT_DIR/${tag}.${mode}.log"

  local srcs=( "$base" "$RUNTIME_DIR/moo_voxel.c" )
  if [ -n "$extra_src" ]; then
    srcs+=( "$RUNTIME_DIR/$extra_src" )
  fi

  echo "  [build] $tag  (src: ${srcs[*]#$RUNTIME_DIR/}  libs: -lm $extra_libs)"
  # shellcheck disable=SC2086
  if ! "$CC" $bflags -g -std=c11 -I"$RUNTIME_DIR" \
        "${srcs[@]}" -lm $extra_libs -o "$bin" > "$log" 2>&1; then
    echo "  [BUILD-FAIL] $tag"
    sed 's/^/      | /' "$log"
    return 1
  fi

  echo "  [run]   $tag"
  # env-String wie "ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=..."
  # shellcheck disable=SC2086
  if env $renv "$bin" > "$log" 2>&1; then
    echo "  [PASS]  $tag"
    return 0
  else
    local rc=$?
    echo "  [FAIL]  $tag  (rc=$rc) -- moeglicher Sanitizer-Fund:"
    sed 's/^/      | /' "$log"
    return 2
  fi
}

# --- Ein Harness mit EXPLIZITER Quell-Liste bauen + laufen ------------------
# Wie build_and_run, aber OHNE impliziten moo_voxel.c-Nenner: die uebergebene
# Quell-Liste ist vollstaendig. Fuer die P008-Harnesses (Frame/GIF/Sim-Input).
# Args: <mode> <build-flags> <run-env> <basename> <source-list> <extra-libs>
#   <source-list>: leerzeichengetrennte .c relativ zu RUNTIME_DIR
# Rueckgabe: 0 ok, 1 build-fail, 2 run-fail/sanitizer-fund
build_and_run_explicit() {
  local mode="$1" bflags="$2" renv="$3" base="$4" src_list="$5" extra_libs="$6"
  local tag="${base%.c}"
  local bin="$OUT_DIR/${tag}.${mode}"
  local log="$OUT_DIR/${tag}.${mode}.log"

  local srcs=( "$base" )
  local s
  for s in $src_list; do
    srcs+=( "$RUNTIME_DIR/$s" )
  done

  echo "  [build] $tag  (src: $base $src_list  libs: $extra_libs)"
  # gnu11 + _GNU_SOURCE wie der cc-crate-Default (strdup &Co in der Runtime).
  # shellcheck disable=SC2086
  if ! "$CC" $bflags -g -std=gnu11 -D_GNU_SOURCE -I"$RUNTIME_DIR" \
        "${srcs[@]}" $extra_libs -o "$bin" > "$log" 2>&1; then
    echo "  [BUILD-FAIL] $tag"
    sed 's/^/      | /' "$log"
    return 1
  fi

  echo "  [run]   $tag"
  # shellcheck disable=SC2086
  if env $renv "$bin" > "$log" 2>&1; then
    echo "  [PASS]  $tag"
    return 0
  else
    local rc=$?
    echo "  [FAIL]  $tag  (rc=$rc) -- moeglicher Sanitizer-Fund:"
    sed 's/^/      | /' "$log"
    return 2
  fi
}

# --- P007-U3 ops/string-Harness bauen + laufen ------------------------------
# Args: <mode> <build-flags> <run-env>
# Rueckgabe: 0 ok, 1 build-fail, 2 run-fail/sanitizer-fund
build_ub_ops_string() {
  local mode="$1" bflags="$2" renv="$3"
  local tag="test_ub_ops_string"
  local bin="$OUT_DIR/${tag}.${mode}"
  local log="$OUT_DIR/${tag}.${mode}.log"
  local srcs=(
    "${tag}.c"
    "$RUNTIME_DIR/moo_ops.c" "$RUNTIME_DIR/moo_string.c"
    "$RUNTIME_DIR/moo_memory.c" "$RUNTIME_DIR/moo_value.c"
    "$RUNTIME_DIR/moo_error.c" "$RUNTIME_DIR/moo_print.c"
    "$RUNTIME_DIR/moo_list.c"
    # P008: moo_memory.c->moo_release() dispatcht MOO_FRAME->moo_frame_free und
    # MOO_GIF->moo_gif_handle_free. Ohne diese drei .c = undefined reference.
    # moo_frame.c baut intern ein Pixel-Dict (moo_dict_new/_set) -> moo_dict.c.
    "$RUNTIME_DIR/moo_frame.c" "$RUNTIME_DIR/moo_gif_handle.c"
    "$RUNTIME_DIR/moo_gif.c" "$RUNTIME_DIR/moo_dict.c"
    # P009-V0: moo_memory.c->moo_release() dispatcht MOO_VIDEO->moo_video_handle_free.
    "$RUNTIME_DIR/moo_video_handle.c" "$RUNTIME_DIR/moo_video.c"
    # P014-A3: moo_ops.c (add/sub/mul/div/pow/neg) dispatcht auf den
    # MOO_TENSOR-Tag -> moo_tensor_ops.c, dieser auf moo_tensor.c.
    # P014-B1: moo_tensor_ops.c zeichnet auf den Autograd-Tape auf.
    "$RUNTIME_DIR/moo_tensor.c" "$RUNTIME_DIR/moo_tensor_ops.c"
    "$RUNTIME_DIR/moo_autograd.c" "$RUNTIME_DIR/moo_nn.c" "$RUNTIME_DIR/moo_nn_easy.c"
    "$RUNTIME_DIR/moo_json.c" "$RUNTIME_DIR/moo_dataset.c" "$RUNTIME_DIR/moo_ki_gpu.c"
  )
  echo "  [build] $tag  (ops/string-Pfade, P007-U3)"
  # shellcheck disable=SC2086
  if ! "$CC" $bflags -g -std=gnu11 -D_GNU_SOURCE -I"$RUNTIME_DIR" \
        "${srcs[@]}" -lm -o "$bin" > "$log" 2>&1; then
    echo "  [BUILD-FAIL] $tag"
    sed 's/^/      | /' "$log"
    return 1
  fi
  # Dieser Harness uebt absichtlich Fehler-/Wurf-Pfade aus. Jeder moo_throw legt
  # seine Meldung in der globalen moo_last_error ab; im echten Programm raeumt
  # der catch-Handler das auf, im Harness-Kontext bleiben diese Mess-Strings
  # liegen. Das sind KEINE Bugs in den getesteten Pfaden, sondern Test-Artefakte
  # der Wurf-Simulation. ASan-Leak-Detection wuerde sie faelschlich anschlagen
  # lassen — daher hier detect_leaks=0. Die ADRESS-Checks (heap-overflow etc.,
  # der eigentliche Zweck) UND der komplette UBSan-Lauf bleiben voll aktiv.
  local run_env="$renv"
  case "$mode" in
    asan) run_env="ASAN_OPTIONS=detect_leaks=0:abort_on_error=1" ;;
  esac
  echo "  [run]   $tag"
  # shellcheck disable=SC2086
  if env $run_env "$bin" > "$log" 2>&1; then
    echo "  [PASS]  $tag"
    return 0
  else
    local rc=$?
    echo "  [FAIL]  $tag  (rc=$rc) -- moeglicher Sanitizer-Fund:"
    sed 's/^/      | /' "$log"
    return 2
  fi
}

# --- Einen kompletten Modus durchfahren -------------------------------------
# Args: <mode>
# Rueckgabe: 0 alle ok, 1 mind. ein fail, 2 modus geskippt (sanitizer fehlt)
run_mode() {
  local mode="$1"
  local bflags renv label
  case "$mode" in
    asan)
      bflags="-fsanitize=address -fno-omit-frame-pointer -O1"
      renv="ASAN_OPTIONS=detect_leaks=1:abort_on_error=1"
      label="AddressSanitizer (detect_leaks=1)"
      ;;
    ubsan)
      # KEIN -fwrapv. UB soll trappen, nicht definiert/maskiert werden.
      bflags="-fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -O1"
      renv="UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
      label="UndefinedBehaviorSanitizer (-fsanitize=undefined, -fno-sanitize-recover, KEIN -fwrapv)"
      ;;
    *)
      echo "FATAL: unbekannter Modus '$mode' (erlaubt: asan|ubsan|all)"; exit 1 ;;
  esac

  echo ""
  echo "============================================================"
  echo " MODUS: $mode"
  echo " FLAGS: $bflags"
  echo " ENV  : $renv"
  echo " INFO : $label"
  echo " CC   : $CC ($($CC --version 2>/dev/null | head -1))"
  echo "============================================================"

  if ! sanitizer_available "$bflags"; then
    echo "SKIP[$mode]: Toolchain '$CC' kann '$bflags' nicht bauen/ausfuehren."
    echo "SKIP[$mode]: -> auf dieser Plattform uebersprungen (transparent, kein stiller Skip)."
    return 2
  fi

  local fails=0 passes=0
  local entry base extra_src extra_libs
  for entry in "${HARNESSES[@]}"; do
    IFS='|' read -r base extra_src extra_libs <<< "$entry"
    build_and_run "$mode" "$bflags" "$renv" "$base" "$extra_src" "$extra_libs"
    case $? in
      0) passes=$((passes+1)) ;;
      *) fails=$((fails+1)) ;;
    esac
  done

  # --- P008-Harnesses (Frame/GIF/Sim-Input, je eigener Link-Satz) -------------
  local esrc elibs
  for entry in "${EXTRA_HARNESSES[@]}"; do
    IFS='|' read -r base esrc elibs <<< "$entry"
    build_and_run_explicit "$mode" "$bflags" "$renv" "$base" "$esrc" "$elibs"
    case $? in
      0) passes=$((passes+1)) ;;
      *) fails=$((fails+1)) ;;
    esac
  done

  # --- P007-U3 ops/string-Harness (eigener Link-Satz, KEIN moo_voxel.c) -------
  # Uebt die gehaerteten Shift-/Groessen-Pfade (moo_ops.c, moo_string.c,
  # moo_memory.c Checked-Helper) aus. Braucht moo_value/_error/_print/_list,
  # daher nicht in der HARNESSES-Matrix (die linkt moo_voxel.c). Strdup &Co
  # brauchen -D_GNU_SOURCE; std bewusst gnu11 (wie der cc-crate-Default).
  build_ub_ops_string "$mode" "$bflags" "$renv"
  case $? in
    0) passes=$((passes+1)) ;;
    *) fails=$((fails+1)) ;;
  esac

  echo "------------------------------------------------------------"
  echo " MODUS $mode ABGESCHLOSSEN: $passes ok, $fails fehlgeschlagen (von $((${#HARNESSES[@]} + ${#EXTRA_HARNESSES[@]} + 1)))"
  echo "------------------------------------------------------------"
  [ "$fails" -eq 0 ] && return 0 || return 1
}

# --- Hauptlauf --------------------------------------------------------------
MODE="${1:-all}"

echo "############################################################"
echo "# moo Sanitizer-Harness-Runner (Plan-007 P007-U1)"
echo "# Revier: compiler/runtime/tests/  |  KEIN -fwrapv"
echo "# Modus-Auswahl: $MODE"
echo "############################################################"

overall=0
skipped_all=1
case "$MODE" in
  asan|ubsan)
    run_mode "$MODE"; rc=$?
    [ "$rc" -ne 2 ] && skipped_all=0
    [ "$rc" -eq 1 ] && overall=1
    ;;
  all)
    for m in asan ubsan; do
      run_mode "$m"; rc=$?
      [ "$rc" -ne 2 ] && skipped_all=0
      [ "$rc" -eq 1 ] && overall=1
    done
    ;;
  *)
    echo "FATAL: ungueltiger Modus '$MODE'. Nutze: asan | ubsan | all"
    exit 1
    ;;
esac

echo ""
echo "############################################################"
if [ "$skipped_all" -eq 1 ]; then
  echo "# GESAMT: alle angeforderten Modi wurden GESKIPPT (Toolchain)."
  echo "############################################################"
  exit 2
fi
if [ "$overall" -eq 0 ]; then
  echo "# GESAMT: ALLE Harnesses sauber. Exit 0."
else
  echo "# GESAMT: Es gab Fehler/Sanitizer-Funde. Exit 1."
fi
echo "############################################################"
exit "$overall"
