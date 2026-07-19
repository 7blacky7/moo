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
HOST_UNAME="$(uname -s)"
HOST_ARCH="$(uname -m)"
case "$HOST_UNAME" in
  MINGW*|MSYS*|CYGWIN*) HOST_OS="windows"; EXE_SUFFIX=".exe" ;;
  Darwin)               HOST_OS="macos";   EXE_SUFFIX="" ;;
  Linux)                HOST_OS="linux";   EXE_SUFFIX="" ;;
  *)                    HOST_OS="unknown"; EXE_SUFFIX="" ;;
esac
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/moo_sanitize.XXXXXX")"
trap 'rm -rf "$OUT_DIR"' EXIT
HARNESS_TIMEOUT_SECONDS="${MOO_SANITIZER_TIMEOUT_SECONDS:-120}"
PYTHON_BIN="${PYTHON_BIN:-}"
if [ -z "$PYTHON_BIN" ]; then
  if command -v python3 >/dev/null 2>&1; then PYTHON_BIN=python3
  elif command -v python >/dev/null 2>&1; then PYTHON_BIN=python
  else
    echo "FATAL: Python 3 wird fuer den plattformuebergreifenden Harness-Timeout benoetigt."
    exit 1
  fi
fi

WINDOWS_COMPAT_DIR=""
if [ "$HOST_OS" = "windows" ]; then
  WINDOWS_COMPAT_DIR="$OUT_DIR/windows-compat"
  mkdir -p "$WINDOWS_COMPAT_DIR"
  "$PYTHON_BIN" - <<'PY'
import os

drive = os.path.splitdrive(os.getcwd())[0]
if not drive:
    raise SystemExit("Windows-Laufwerk fuer /tmp konnte nicht ermittelt werden")
root_tmp = drive + os.sep + "tmp"
os.makedirs(root_tmp, exist_ok=True)
print("WINDOWS_NATIVE_TMP=" + root_tmp)
PY
  cat > "$WINDOWS_COMPAT_DIR/moo_sanitizer_windows_compat.h" <<'EOF'
#ifndef MOO_SANITIZER_WINDOWS_COMPAT_H
#define MOO_SANITIZER_WINDOWS_COMPAT_H
#include <stdlib.h>
static int moo_test_setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name) != NULL) return 0;
    return _putenv_s(name, value);
}
static int moo_test_unsetenv(const char* name) {
    return _putenv_s(name, "");
}
#define setenv moo_test_setenv
#define unsetenv moo_test_unsetenv
#endif
EOF
  cat > "$WINDOWS_COMPAT_DIR/unistd.h" <<'EOF'
#ifndef MOO_SANITIZER_WINDOWS_UNISTD_H
#define MOO_SANITIZER_WINDOWS_UNISTD_H
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define close _close
#define unlink _unlink
#define rmdir _rmdir
#define mkdir(path, mode) _mkdir(path)
#define getpid _getpid
static int mkstemp(char* path_template) {
    const size_t length = strlen(path_template);
    if (_mktemp_s(path_template, length + 1u) != 0) return -1;
    return _open(path_template,
        _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
        _S_IREAD | _S_IWRITE);
}
static char* mkdtemp(char* path_template) {
    const size_t length = strlen(path_template);
    if (_mktemp_s(path_template, length + 1u) != 0) return NULL;
    if (_mkdir(path_template) != 0) return NULL;
    return path_template;
}
#endif
EOF
fi

# Windows/clang nutzt die MSVC-CRT: math.h exponiert M_PI nur mit
# _USE_MATH_DEFINES; libm und pthread sind dort keine separaten Libraries.
COMMON_CFLAGS=""
BASE_MATH_LIB="-lm"
if [ "$HOST_OS" = "windows" ]; then
  COMMON_CFLAGS="-D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS -Wno-deprecated-declarations -I$WINDOWS_COMPAT_DIR -include $WINDOWS_COMPAT_DIR/moo_sanitizer_windows_compat.h"
  BASE_MATH_LIB=""
fi

# --- Compiler ---------------------------------------------------------------
CC="${CC:-}"
if [ -z "$CC" ]; then
  if command -v clang >/dev/null 2>&1; then CC=clang
  elif command -v clang-18 >/dev/null 2>&1; then CC=clang-18
  elif command -v gcc >/dev/null 2>&1; then CC=gcc
  elif command -v cc >/dev/null 2>&1; then CC=cc
  else
    echo "SKIP: Kein C-Compiler (clang/gcc/cc) gefunden -> Sanitizer-Lauf nicht moeglich."
    exit 2
  fi
fi

echo "HOST : $HOST_OS ($HOST_UNAME/$HOST_ARCH), EXE_SUFFIX='$EXE_SUFFIX'"
echo "CFLAGS: ${COMMON_CFLAGS:-<keine host-spezifischen>}"
echo "SANITIZER-VERTRAG: ASan + UBSan, KEIN -fwrapv; jeder Plattform-Skip wird einzeln begruendet"
echo "HARNESS-TIMEOUT: ${HARNESS_TIMEOUT_SECONDS}s pro Prozess via $PYTHON_BIN"

platform_skip_reason() {
  local tag="$1"
  if [ "$HOST_OS" = "macos" ] && [ "$HOST_ARCH" != "x86_64" ] && [ "$tag" = "test_bare_alloc_asan" ]; then
    echo "Harness prueft den x86-16550-Portpfad; auf macOS $HOST_ARCH ist dieser Produktionspfad absichtlich nicht vorhanden"
    return 0
  fi
  case "$HOST_OS:$tag" in
    linux:*) return 1 ;;
    windows:test_capture_v4l2_ops_asan|macos:test_capture_v4l2_ops_asan)
      echo "V4L2/Linux-Header und libv4l2 sind auf $HOST_OS nicht verfuegbar"; return 0 ;;
    windows:test_capture_alsa_ops_asan|macos:test_capture_alsa_ops_asan)
      echo "ALSA/Linux-Header und libasound sind auf $HOST_OS nicht verfuegbar"; return 0 ;;
    windows:test_video_wiring_asan)
      echo "Harness modelliert explizit POSIX fork/execvp/mkdtemp/sh; Windows-Core hat einen getrennten CreateProcess-Pfad"; return 0 ;;
  esac
  return 1
}

normalize_extra_libs() {
  local libs="$1" token
  local normalized=()
  for token in $libs; do
    if [ "$HOST_OS" = "windows" ] && { [ "$token" = "-lm" ] || [ "$token" = "-lpthread" ]; }; then
      continue
    fi
    normalized+=("$token")
  done
  printf '%s' "${normalized[*]-}"
}

mode_skip_reason() {
  local mode="$1" tag="$2"
  if [ "$mode" = "ubsan" ] && [ "$tag" = "test_gather_leak" ]; then
    echo "reines /proc-RSS-Leak-Gate mit 5%-Schwelle; ASan/LSan deckt den Leakvertrag ab, UBSan misst hier keine zusaetzliche UB-Eigenschaft"
    return 0
  fi
  return 1
}

run_sanitized_binary() {
  local renv="$1" bin="$2" log="$3"
  local env_name="${renv%%=*}" env_value="${renv#*=}"
  local process_bin="$bin" process_log="$log"
  if [ "$HOST_OS" = "windows" ] && command -v cygpath >/dev/null 2>&1; then
    process_bin="$(cygpath -w "$bin")"
    process_log="$(cygpath -w "$log")"
  fi

  "$PYTHON_BIN" - "$env_name" "$env_value" "$process_bin" "$process_log" "$HARNESS_TIMEOUT_SECONDS" <<'PY'
import os
import subprocess
import sys

env_name, env_value, binary, log_path, timeout_text = sys.argv[1:]
env = os.environ.copy()
env[env_name] = env_value
timeout = int(timeout_text)
with open(log_path, "wb") as log:
    try:
        result = subprocess.run(
            [binary],
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        log.write((f"SANITIZER-HARNESS-TIMEOUT: {timeout}s\n").encode("utf-8"))
        sys.exit(124)
sys.exit(result.returncode)
PY
}

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
  # capture: KI-MULTI-C1 — injizierte Handle-/Fault-Matrix ohne Hardware.
  "test_capture_asan.c|moo_capture.c moo_capture_camera_stub.c moo_capture_audio_stub.c moo_frame.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_error.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c|-lm"
  # C2-WIN/C2-MAC: plattformneutraler Pull-Zustandsautomat mit vollständig injizierten
  # Media-Foundation-/WASAPI-Operationen; läuft hardwarefrei auf jedem Host.
  "test_capture_pull_ops_asan.c|moo_capture.c moo_capture_pull.c moo_frame.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  # Native Low-Level-Ops-Fault-Matrizen: echte Backend-Logik, alle OS-/Treiber-
  # Aufrufe injiziert; benoetigen Linklibs nur fuer ungenutzte system_ops-Defaults.
  "test_capture_v4l2_ops_asan.c|moo_capture_v4l2.c moo_capture_audio_stub.c tests/capture_test_stubs.c moo_capture.c moo_memory.c moo_value.c moo_error.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_frame.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_nn.c moo_nn_easy.c moo_json.c moo_dataset.c|-lv4l2 -lm"
  "test_capture_alsa_ops_asan.c|moo_capture_alsa.c moo_capture_camera_stub.c tests/capture_test_stubs.c moo_capture.c moo_memory.c moo_value.c moo_error.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_frame.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_nn.c moo_nn_easy.c moo_json.c moo_dataset.c|-lasound -lm"
  "test_frame_asan.c|moo_frame.c moo_memory.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c|-lm"
  # surface: P016-O1 — freestanding Rasterkern + refcounteter Wrapper mit
  #             eigenem Minimal-Moo-Scaffold; keine UI-/Hardware-Abhaengigkeit.
  "test_surface_asan.c|moo_surface_core.c moo_surface.c moo_memory.c|-DMOO_HAS_SURFACE -lm"
  # compositor: P016-O3 — allocatorfreier Multi-Client-Core, deterministischer
  #             Rasterpfad und caller-owned Buffers; keine UI oder Hardware.
  "test_compositor_asan.c|moo_ui_host_parity_instrumentation.c moo_compositor_core.c moo_compositor_raster.c moo_compositor_effects_state.c moo_compositor_animation.c moo_compositor_effects_math.c moo_compositor_effects_cpu.c moo_compositor_effects_damage.c|-lm"
  # effects: P016-I1/G1 — portable State/Animation/CPU/Damage/GPU contracts.
  #          Jeder Harness bekommt nur seine explizite Produktions-Quellmenge;
  #          Integration bindet den vollständigen Compositor-Stack zusammen.
  "test_effects_asan.c|moo_compositor_effects_state.c moo_compositor_animation.c moo_compositor_effects_math.c moo_compositor_effects_cpu.c|-lm"
  "test_effects_damage.c|moo_compositor_effects_math.c moo_compositor_effects_damage.c|-lm"
  "test_effects_determinism.c|moo_compositor_effects_state.c moo_compositor_animation.c moo_compositor_effects_math.c moo_compositor_effects_cpu.c|-lm"
  "bench_effects.c|moo_compositor_effects_state.c moo_compositor_effects_math.c moo_compositor_effects_cpu.c|-lm"
  "test_effects_integration.c|moo_compositor_core.c moo_compositor_raster.c moo_compositor_effects_state.c moo_compositor_animation.c moo_compositor_effects_math.c moo_compositor_effects_cpu.c moo_compositor_effects_damage.c moo_compositor_effects_gpu.c|-lm"
  # frame_tensor: Frame<->Tensor-Bruecke (KI-MULTI-V1). Braucht Frame- UND
  #               Tensor-Familie (f32_sichern -> ki_gpu/autograd-Symbole).
  "test_frame_tensor_asan.c|moo_frame_tensor.c moo_frame.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c|-lm"
  # audio: KI-MULTI-A1 — FFT/Parseval/STFT/WAV, kein Autograd/Hardware.
  #        Gleicher Tensor/Core-Satz + Test-throw-Modell wie frame_tensor.
  "test_audio_asan.c|moo_audio.c moo_frame.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_gif_handle.c moo_gif.c moo_video_handle.c moo_video.c|-lm"
  "test_gif_core_asan.c|moo_gif.c|-lm"
  "test_gif_wiring_asan.c|moo_gif.c moo_gif_handle.c moo_frame.c moo_value.c moo_memory.c moo_string.c moo_dict.c moo_error.c moo_print.c moo_list.c moo_ops.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_video_handle.c moo_video.c|-lm"
  "test_sim_input_asan.c|moo_3d.c|-lm"
  #   video_wiring: MOO_VIDEO-Core (moo_video.c) + immer-gebauter Heap-Wrapper
  #               (moo_video_handle.c) + Core-Runtime. moo_memory.c->moo_release()
  #               dispatcht MOO_VIDEO->moo_video_handle_free (P009-V0); die uebrigen
  #               *_free-Dispatch-Ziele stubbt der Harness. moo_value.c braucht
  #               moo_string_new -> moo_string.c (+ dict/list/ops als deren Deps).
  #               Mock-ffmpeg: der Harness schreibt zur Laufzeit ein "ffmpeg"-sh-
  #               Skript in ein mkdtemp-Dir + setzt PATH -> KEIN echtes ffmpeg/GPU.
  "test_video_wiring_asan.c|moo_video.c moo_video_handle.c moo_memory.c moo_value.c moo_error.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c|-lm"
  #   tensor:     Plan-014 A1 — MOO_TENSOR-Kern (Konstruktoren/Zugriff/Refcount/
  #               Determinismus). Core-Runtime-Satz OHNE moo_error.c: der Harness
  #               bringt das Test-throw-Modell mit (Flag + free des strdup-Texts,
  #               Muster Voxel-Harnesses) — sonst leaken Fehlerpfade by design.
  "test_tensor_asan.c|moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   tensor_ops: Plan-014 A2 — Op-Registry + Kern-Ops (Broadcasting/matmul/
  #               Reduktionen/Aktivierungen/Softmax-Stabilitaet). Gleicher
  #               Quell-Satz + Test-throw-Modell wie tensor.
  "test_tensor_ops_asan.c|moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   autograd:   Plan-014 B1 — Tape-Lifecycle (record/rueckwaerts/reset,
  #               Fan-out-Akkumulation, Broadcast-Reduktion Bias-Grad,
  #               no_grad). ASan haelt das Tape-Retain/Release-Gate.
  "test_autograd_asan.c|moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   gradient_setzen: KIP-X1b Phase A — gradient_setzen(quelle) aus Tensor
  #               gleicher Form ODER flacher Zahlenliste; Puffer-on-demand,
  #               grad_valid=MOO_V_DATA, Selbstzuweisung, Negativ/kein-Teilzustand.
  #               ASan haelt das Listen-Element-Release-Gate (moo_list_get +1).
  "test_gradient_setzen_asan.c|moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   gradcheck:  Plan-014 B2 — DAS Ehrlichkeits-Gate: numerischer Gradient
  #               vs. Autograd fuer JEDEN Registry-Op (automatische Iteration
  #               via op_count/at — neuer Op ohne Gradcheck faellt hier auf).
  "test_gradcheck.c|moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   contrastive: KI-MULTI-L1 — kosinus + symmetrisches InfoNCE als reine
  #                Registry-Op-Komposition; eigener numerischer FD-Gradcheck.
  "test_contrastive_asan.c|moo_contrastive.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   quant: KI-Q1 — Hadamard-Registry-Op + inferenz-only QJL Sign-JL.
  #          Eigene Determinismus-, Orthogonalitaets-, Gradcheck- und
  #          Unbiasedness-Gates; gleicher Tensor-/Autograd-Quellsatz.
  "test_quant_asan.c|moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   gather_leak: KIP-T1 — Refcount-/Tape-Leak-Gate fuer gather fwd+bwd. Unter
  #               ASan zaehlt LeakSanitizer (RSS-Check dort uebersprungen); der
  #               echte 1M-RSS-Stabilitaetslauf laeuft standalone ohne Sanitizer.
  "test_gather_leak.c|moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   nn:         Plan-014 C1 — Schichten/Loss/Optimizer (Dict-basiert, reine
  #               Op-Komposition, kein neuer Registry-Op). Inkl. XOR-
  #               Konvergenz-Gate im Harness; Quell-Satz + Test-throw-Modell
  #               wie autograd, plus moo_nn.c.
  "test_nn_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   tensor_dtype: KIP-D1 — bf16-Storage-DType. Roundtrip/Special-Values,
  #               Valid-Masken-Vertrag (D0 §2), Stale-Store-Regression,
  #               bf16-Op-Matrix (26+ Ops bit-exakt vs rundungs-f32) +
  #               ce/mse/vorwaerts. Quell-Satz wie nn. ASan-Leak-Gate.
  "test_tensor_dtype_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   mixed_precision: KIP-D2 — Mixed-Precision-Training bf16 (Aktivierungen
  #               bf16-gerundet, Parameter-Master/Optimizer f32). bf16_runden ==
  #               als_dtype-Storage-Pfad bit-identisch; XOR+CE bf16 vs f32 in
  #               Toleranz + bf16 deterministisch; Default-AUS == reines f32.
  "test_mixed_precision_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   safeten-dt: KIP-D3 — Safetensors DType-Vertrag: F32/BF16/F16-Import (->f32),
  #               f32-Export byte-identisch, bf16-Export->Import bit-exakt,
  #               Negativ unbekannter dtype. Quell-Satz wie test_nn_asan.
  "test_safetensors_dtype_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  "test_dataset_asan.c|moo_dataset.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   tokenizer:  KIP-T2 — Byte-level BPE (train/encode/decode/save/load/hash).
  #               Test-throw-Modell wie dataset; Tensor-Kern + Core-Runtime.
  #               Gates: Determinismus ohne Seed, byte-exakter Roundtrip inkl.
  #               invalid-UTF8/NUL/Emoji, Artefakt-Roundtrip, UNK-Negativ-Gate.
  "test_tokenizer_asan.c|moo_tokenizer.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   shard:      KIP-E1 — Streaming-Token-Shards + Dataloader (Format/CRC,
  #               gefenstertes Lesen, seed-deterministische Blockreihenfolge,
  #               SFT-Loss-Maske, Train/Val-Split). Test-throw-Modell.
  "test_shard_asan.c|moo_shard.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   checkpoint: KIP-E2 — CPU-Voll-Checkpoint v2 (Modell+Optimizer m/v/t+
  #               Dropout-Zaehler+Dataloader-Pos, atomisch tmp+rename, Rotation).
  #               GATE: Kill+Resume bit-identisch MIT Dropout. Quell-Satz wie nn.
  "test_checkpoint_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
  #   ckpt_ag:    KIP-B4b — Activation Checkpointing (Sub-Tape Re-Forward +
  #               Dropout-Zaehler-Restore). GATE: Grad mit/ohne moo_nn_checkpoint
  #               BIT-identisch MIT Dropout (Param+Input), Determinismus, tiefes
  #               Multi-Dropout-Segment. Quell-Satz wie nn/checkpoint.
  "test_checkpoint_ag_asan.c|moo_nn.c moo_nn_easy.c moo_json.c moo_quant.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c|-lm"
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
  local probe_bin="$OUT_DIR/_probe${EXE_SUFFIX}"
  cat > "$probe" <<'EOF'
int main(void) { return 0; }
EOF
  local probe_log="$OUT_DIR/_probe.log"
  # shellcheck disable=SC2086
  "$CC" $flags -o "$probe_bin" "$probe" >"$probe_log" 2>&1 || return 1
  run_sanitized_binary "MOO_SANITIZER_PROBE=1" "$probe_bin" "$probe_log" || {
    echo "SANITIZER-PROBE-FEHLER ($flags):"
    sed 's/^/      | /' "$probe_log"
    return 1
  }
  return 0
}

# --- Ein Harness in einem Modus bauen + laufen ------------------------------
# Args: <mode> <build-flags> <run-env> <basename> <extra-sources> <extra-libs>
# Rueckgabe: 0 ok, 1 build-fail, 2 run-fail/sanitizer-fund
build_and_run() {
  local mode="$1" bflags="$2" renv="$3" base="$4" extra_src="$5" extra_libs="$6"
  local tag="${base%.c}"
  extra_libs="$(normalize_extra_libs "$extra_libs")"
  local bin="$OUT_DIR/${tag}.${mode}${EXE_SUFFIX}"
  local log="$OUT_DIR/${tag}.${mode}.log"

  local srcs=( "$base" "$RUNTIME_DIR/moo_voxel.c" )
  if [ -n "$extra_src" ]; then
    srcs+=( "$RUNTIME_DIR/$extra_src" )
  fi

  echo "  [build] $tag  (src: ${srcs[*]#$RUNTIME_DIR/}  libs: -lm $extra_libs)"
  # shellcheck disable=SC2086
  if ! "$CC" $bflags $COMMON_CFLAGS -g -std=c11 -I"$RUNTIME_DIR" \
        "${srcs[@]}" $BASE_MATH_LIB $extra_libs -o "$bin" > "$log" 2>&1; then
    echo "  [BUILD-FAIL] $tag"
    sed 's/^/      | /' "$log"
    return 1
  fi

  echo "  [run]   $tag"
  # Ein hängender Harness ist ein harter Fehler, blockiert aber nicht den gesamten
  # Matrixjob bis zum Job-Timeout.
  if run_sanitized_binary "$renv" "$bin" "$log"; then
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
  extra_libs="$(normalize_extra_libs "$extra_libs")"
  local bin="$OUT_DIR/${tag}.${mode}${EXE_SUFFIX}"
  local log="$OUT_DIR/${tag}.${mode}.log"

  # C1: moo_memory.c kennt Kamera/Mikro-Tags. Schwache Test-Stubs
  # verhindern Linkfehler in Harnesses ohne Capture; echte Symbole gewinnen.
  local srcs=( "$base" "capture_free_stubs.c" )
  local s
  for s in $src_list; do
    srcs+=( "$RUNTIME_DIR/$s" )
  done

  echo "  [build] $tag  (src: $base $src_list  libs: $extra_libs)"
  # gnu11 + _GNU_SOURCE wie der cc-crate-Default (strdup &Co in der Runtime).
  # shellcheck disable=SC2086
  if ! "$CC" $bflags $COMMON_CFLAGS -g -std=gnu11 -D_GNU_SOURCE -I"$RUNTIME_DIR" \
        "${srcs[@]}" $extra_libs -o "$bin" > "$log" 2>&1; then
    echo "  [BUILD-FAIL] $tag"
    sed 's/^/      | /' "$log"
    return 1
  fi

  echo "  [run]   $tag"
  if run_sanitized_binary "$renv" "$bin" "$log"; then
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
  local bin="$OUT_DIR/${tag}.${mode}${EXE_SUFFIX}"
  local log="$OUT_DIR/${tag}.${mode}.log"
  local srcs=(
    "${tag}.c" "capture_free_stubs.c"
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
  if ! "$CC" $bflags $COMMON_CFLAGS -g -std=gnu11 -D_GNU_SOURCE -I"$RUNTIME_DIR" \
        "${srcs[@]}" $BASE_MATH_LIB -o "$bin" > "$log" 2>&1; then
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
  if run_sanitized_binary "$run_env" "$bin" "$log"; then
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
      if [ "$HOST_OS" = "linux" ]; then
        renv="ASAN_OPTIONS=detect_leaks=1:abort_on_error=1"
        label="AddressSanitizer + LeakSanitizer (detect_leaks=1)"
      else
        # LSan ist im Windows-Clang-Runtimevertrag nicht enthalten und auf
        # macOS je nach upstream/Homebrew-Runtime nicht stabil verfuegbar.
        # AddressSanitizer bleibt voll aktiv; der Cap wird sichtbar geloggt.
        renv="ASAN_OPTIONS=detect_leaks=0:abort_on_error=1"
        label="AddressSanitizer (LSan auf $HOST_OS explizit deaktiviert)"
      fi
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

  local fails=0 passes=0 skips=0
  local entry base extra_src extra_libs tag skip_reason
  for entry in "${HARNESSES[@]}"; do
    IFS='|' read -r base extra_src extra_libs <<< "$entry"
    tag="${base%.c}"
    if skip_reason="$(platform_skip_reason "$tag")"; then
      echo "  [SKIP]  $tag ($skip_reason)"
      skips=$((skips+1))
      continue
    fi
    if skip_reason="$(mode_skip_reason "$mode" "$tag")"; then
      echo "  [SKIP]  $tag ($skip_reason)"
      skips=$((skips+1))
      continue
    fi
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
    tag="${base%.c}"
    if skip_reason="$(platform_skip_reason "$tag")"; then
      echo "  [SKIP]  $tag ($skip_reason)"
      skips=$((skips+1))
      continue
    fi
    if skip_reason="$(mode_skip_reason "$mode" "$tag")"; then
      echo "  [SKIP]  $tag ($skip_reason)"
      skips=$((skips+1))
      continue
    fi
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
  echo " MODUS $mode ABGESCHLOSSEN: $passes ok, $fails fehlgeschlagen, $skips plattformbedingt geskippt (von $((${#HARNESSES[@]} + ${#EXTRA_HARNESSES[@]} + 1)))"
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
    [ "$rc" -ne 0 ] && overall=1
    ;;
  all)
    for m in asan ubsan; do
      run_mode "$m"; rc=$?
      [ "$rc" -ne 2 ] && skipped_all=0
      [ "$rc" -ne 0 ] && overall=1
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
