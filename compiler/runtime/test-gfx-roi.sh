#!/usr/bin/env bash
# CPU-only ROI correctness gate: no window, display server or GPU.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
for gfx_tool in gcc timeout sha256sum rg; do
    command -v "$gfx_tool" >/dev/null || { echo "Missing prerequisite: $gfx_tool" >&2; exit 1; }
done
gfx_tmp=$(mktemp -d "${TMPDIR:-/tmp}/moo-gfx-roi.XXXXXX")
trap 'rm -rf -- "$gfx_tmp"' EXIT
if [[ "${GFX_SKIP_BUILD:-0}" != 1 ]]; then
    cargo build --release --manifest-path compiler/Cargo.toml
fi
gfx_target="${CARGO_TARGET_DIR:-$PWD/compiler/target}"
gfx_compiler="$gfx_target/release/moo-compiler"
sha256sum "$gfx_compiler" compiler/runtime/moo_frame.c compiler/runtime/moo_runtime.h \
    compiler/runtime/tests/test_gfx_frame_statistics.c compiler/tests/gfx_frame_statistics.moos \
    compiler/runtime/test-gfx-roi.sh > "$gfx_tmp/sources.sha256"
cat "$gfx_tmp/sources.sha256"
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections -Icompiler/runtime \
    compiler/runtime/tests/test_gfx_frame_statistics.c compiler/runtime/moo_frame.c \
    -Wl,--gc-sections -lm -o "$gfx_tmp/numeric"
ASAN_OPTIONS=detect_leaks=1 timeout 30 "$gfx_tmp/numeric" "$gfx_tmp/fixture.bmp"
"$gfx_compiler" compile compiler/tests/gfx_frame_statistics.moos -o "$gfx_tmp/fixture"
GFX_ROI_FIXTURE="$gfx_tmp/fixture.bmp" timeout 30 "$gfx_tmp/fixture" | tee "$gfx_tmp/moo.log"
rg -q '^GFX-ROI MOO PASS$' "$gfx_tmp/moo.log"
sha256sum --check "$gfx_tmp/sources.sha256"
echo "GFX-ROI PASS"
