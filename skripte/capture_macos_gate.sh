#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/compiler/runtime"
OUT="${1:-/tmp/moo-c2mac}"
if [[ "$(uname -s)" != Darwin ]]; then
  echo "SKIP: C2-MAC benötigt macOS/Apple-Frameworks; Shared Fault-Matrix läuft plattformneutral via run_sanitize.sh"
  exit 0
fi
mkdir -p "$OUT"
PLIST="$RT/moo_capture_macos_info.plist"
plutil -lint "$PLIST"
clang -std=gnu11 -O2 -Wall -Wextra -Werror -c "$RT/moo_capture_pull.c" -I"$RT" -o "$OUT/pull.o"
clang -fobjc-arc -O2 -Wall -Wextra -Werror -I"$RT" \
  "$RT/tests/test_capture_macos_smoke.m" "$RT/moo_capture_macos_system.m" \
  -framework AVFoundation -framework CoreMedia -framework CoreVideo \
  -framework CoreAudio -framework AudioToolbox -framework Foundation -lobjc \
  -Wl,-sectcreate,__TEXT,__info_plist,"$PLIST" -o "$OUT/c2mac-smoke"
"$OUT/c2mac-smoke"
otool -s __TEXT __info_plist "$OUT/c2mac-smoke" >/dev/null
echo "C2MAC_GATE PASS: ARC/Werror/Framework-Link/Info.plist/Enumeration"
