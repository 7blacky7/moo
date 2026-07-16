#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/compiler/runtime"
TEST="$RT/tests/test_ui_host_parity_cocoa_native.m"
OUT="${1:-${TMPDIR:-/tmp}/moo-p016-macos-appkit-gate}"
RUN_TIMEOUT_SECONDS="${MOO_MACOS_GATE_TIMEOUT_SECONDS:-30}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  printf 'P016 O6 MACOS APPKIT PLATFORM_UNAVAILABLE: host=%s required=Darwin+AppKit\n' "$(uname -s)"
  exit 77
fi

for tool in xcrun sw_vers uname shasum otool nm grep; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'P016 O6 MACOS APPKIT GATE ERROR: missing_tool=%s\n' "$tool" >&2
    exit 2
  fi
done

case "$RUN_TIMEOUT_SECONDS" in
  ''|*[!0-9]*|0)
    printf 'P016 O6 MACOS APPKIT GATE ERROR: invalid_timeout=%s\n' "$RUN_TIMEOUT_SECONDS" >&2
    exit 2
    ;;
esac

CLANG="${CLANG:-$(xcrun --find clang)}"
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
mkdir -p "$OUT"

COMMON=(-O2 -Wall -Wextra -Werror -I"$RT" -isysroot "$SDKROOT")
OBJC=(-fobjc-arc -fblocks -std=gnu11 "${COMMON[@]}")

"$CLANG" -std=c11 "${COMMON[@]}" -c   "$RT/moo_ui_host_parity.c" -o "$OUT/moo_ui_host_parity.o"
"$CLANG" "${OBJC[@]}" -c   "$RT/moo_ui_host_parity_cocoa.m" -o "$OUT/moo_ui_host_parity_cocoa.o"
"$CLANG" "${OBJC[@]}" -c   "$RT/moo_ui_cocoa.m" -o "$OUT/moo_ui_cocoa.o"
"$CLANG" "${OBJC[@]}" -c   "$TEST" -o "$OUT/test_ui_host_parity_cocoa_native.o"

"$CLANG" -bundle "$OUT/moo_ui_cocoa.o"   -Wl,-undefined,dynamic_lookup   -framework AppKit -framework Foundation -framework Cocoa -lobjc   -o "$OUT/moo-ui-cocoa.bundle"
"$CLANG"   "$OUT/test_ui_host_parity_cocoa_native.o"   "$OUT/moo_ui_host_parity.o" "$OUT/moo_ui_host_parity_cocoa.o"   -framework AppKit -framework Foundation -lobjc   -o "$OUT/macos-appkit-parity-gate"

otool -L "$OUT/moo-ui-cocoa.bundle" > "$OUT/backend-frameworks.txt"
otool -L "$OUT/macos-appkit-parity-gate" > "$OUT/runner-frameworks.txt"
grep -q '/AppKit.framework/' "$OUT/backend-frameworks.txt"
grep -q '/Foundation.framework/' "$OUT/backend-frameworks.txt"
grep -q '/AppKit.framework/' "$OUT/runner-frameworks.txt"
nm -g "$OUT/moo-ui-cocoa.bundle" > "$OUT/backend-symbols.txt"
grep -Eq '[[:space:]]_moo_ui_init$' "$OUT/backend-symbols.txt"

{
  printf 'gate_version=1\n'
  printf 'sdkroot=%s\n' "$SDKROOT"
  printf 'clang=%s\n' "$CLANG"
  printf 'deployment_target=%s\n' "${MACOSX_DEPLOYMENT_TARGET:-host-default}"
  sw_vers
  uname -a
  xcrun --sdk macosx --show-sdk-version
  "$CLANG" --version
} > "$OUT/host.txt"

shasum -a 256   "$ROOT/scripts/macos_appkit_gate.sh"   "$TEST"   "$RT/moo_ui_host_parity.h"   "$RT/moo_ui_host_parity.c"   "$RT/moo_ui_host_parity_cocoa.m"   "$RT/moo_ui_cocoa.m" > "$OUT/source-sha256.txt"

printf 'command_path=%s\nworking_directory=%s\nargv_count=0\ntimeout_seconds=%s\n' \
  "$OUT/macos-appkit-parity-gate" "$(pwd -P)" "$RUN_TIMEOUT_SECONDS" \
  > "$OUT/command.txt"
rm -f "$OUT/timeout.flag"
"$OUT/macos-appkit-parity-gate" > "$OUT/runtime.txt" 2>&1 &
runner_pid=$!
(
  sleep "$RUN_TIMEOUT_SECONDS"
  if kill -0 "$runner_pid" >/dev/null 2>&1; then
    printf 'timeout_triggered=1\n' > "$OUT/timeout.flag"
    kill -TERM "$runner_pid" >/dev/null 2>&1 || true
    sleep 1
    kill -KILL "$runner_pid" >/dev/null 2>&1 || true
  fi
) &
watchdog_pid=$!

set +e
wait "$runner_pid"
runner_status=$?
set -e
kill "$watchdog_pid" >/dev/null 2>&1 || true
wait "$watchdog_pid" >/dev/null 2>&1 || true
if [[ -f "$OUT/timeout.flag" ]]; then
  timeout_triggered=1
  runner_status=124
else
  timeout_triggered=0
fi
printf 'runner_exit=%s\ntimeout_triggered=%s\n'   "$runner_status" "$timeout_triggered" > "$OUT/exit.txt"
cat "$OUT/runtime.txt"

if [[ "$runner_status" -ne 0 ]]; then
  printf 'P016 O6 MACOS APPKIT GATE ERROR: runner_exit=%d timeout=%d\n'     "$runner_status" "$timeout_triggered" >&2
  exit "$runner_status"
fi

grep -Fxq   'P016 O6 MACOS APPKIT GATE EXECUTED: PARITY_STATUS=UNSUPPORTED domains=0xff checks=20'   "$OUT/runtime.txt"

shasum -a 256   "$OUT/moo_ui_host_parity.o"   "$OUT/moo_ui_host_parity_cocoa.o"   "$OUT/moo_ui_cocoa.o"   "$OUT/moo-ui-cocoa.bundle"   "$OUT/macos-appkit-parity-gate"   "$OUT/runtime.txt" > "$OUT/artifact-sha256.txt"
shasum -a 256   "$OUT/command.txt"   "$OUT/exit.txt"   "$OUT/host.txt"   "$OUT/source-sha256.txt"   "$OUT/backend-frameworks.txt"   "$OUT/runner-frameworks.txt"   "$OUT/backend-symbols.txt"   "$OUT/runtime.txt"   "$OUT/artifact-sha256.txt"   > "$OUT/final-evidence-sha256.txt"

printf 'P016 O6 MACOS APPKIT EVIDENCE_READY: parity=UNSUPPORTED artifacts=%s\n' "$OUT"
