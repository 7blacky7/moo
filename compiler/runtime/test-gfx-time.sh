#!/usr/bin/env bash
# Opt-in Linux correctness gate; llvmpipe timings are not GPU performance data.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
for gfx_tool in cargo gcc xvfb-run timeout; do
    command -v "$gfx_tool" >/dev/null || { echo "Missing prerequisite: $gfx_tool" >&2; exit 1; }
done
gfx_tmp=$(mktemp -d /tmp/moo-gfx-time.XXXXXX)
trap 'rm -rf -- "$gfx_tmp"' EXIT
mkdir "$gfx_tmp/runtime"
chmod 700 "$gfx_tmp/runtime"
cargo build --release --manifest-path compiler/Cargo.toml
gfx_target="${CARGO_TARGET_DIR:-$PWD/compiler/target}"
gcc -std=c11 -O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -Icompiler/runtime compiler/runtime/tests/test_gfx_render_time.c \
    -Wl,--gc-sections -lm -o "$gfx_tmp/numeric"
"$gfx_tmp/numeric"
"$gfx_target/release/moo-compiler" compile compiler/tests/gfx_render_time.moos -o "$gfx_tmp/fixture"
for gfx_mode in 3d hybrid; do
    echo "GFX-TIME mode=$gfx_mode renderer=llvmpipe correctness-only"
    env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_RUNTIME_DIR="$gfx_tmp/runtime" \
        GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
        __GLX_VENDOR_LIBRARY_NAME=mesa GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 \
        MOO_KI_GPU=0 MOO_3D_BACKEND=gl33 GFX_TIME_MODE="$gfx_mode" \
        xvfb-run -a timeout 45 "$gfx_tmp/fixture"
done
