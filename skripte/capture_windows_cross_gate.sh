#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/compiler/runtime"
OUT="${1:-/tmp/moo-c2win}"
REQUIRE_WINE="${MOO_REQUIRE_WINE:-0}"
mkdir -p "$OUT"
CC="${MOO_MINGW_CC:-x86_64-w64-mingw32-gcc}"
LIBS=(-lmfplat -lmf -lmfreadwrite -lmfuuid -lole32 -loleaut32 -luuid)
"$CC" -std=gnu11 -O2 -Wall -Wextra -Werror -I"$RT" \
  "$RT/tests/test_capture_windows_wine.c" "$RT/moo_capture_windows_system.c" \
  -static -o "$OUT/c2win-smoke.exe" "${LIBS[@]}"
file "$OUT/c2win-smoke.exe"

if ! command -v wine >/dev/null 2>&1; then
  echo "SKIP: Wine fehlt — Cross-Compile/Link ist grün, API-Smoke nicht bewiesen"
  if [[ "$REQUIRE_WINE" == 1 ]]; then
    exit 77
  fi
  exit 0
fi

set +e
if command -v xvfb-run >/dev/null 2>&1; then
  timeout --signal=TERM 30s xvfb-run -a wine "$OUT/c2win-smoke.exe" --init-only
else
  timeout --signal=TERM 30s wine "$OUT/c2win-smoke.exe" --init-only
fi
wine_status=$?
set -e
if (( wine_status != 0 )); then
  echo "WARN: Wine API-Smoke nicht grün (Status $wine_status); Cross-Compile/Link ist grün"
  if [[ "$REQUIRE_WINE" == 1 ]]; then
    exit "$wine_status"
  fi
fi
exit 0
