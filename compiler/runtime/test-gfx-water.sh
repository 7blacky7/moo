#!/usr/bin/env bash
# Water correctness: actual containers under sanitizers, native GL33/Hybrid pixels.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
for gfx_tool in gcc timeout sha256sum xvfb-run; do command -v "$gfx_tool" >/dev/null; done
mkdir -p build
gfx_tmp=$(mktemp -d "$PWD/build/gfx-water.XXXXXX")
mkdir -m 700 "$gfx_tmp/runtime"
if [[ "${GFX_SKIP_BUILD:-0}" != 1 ]]; then cargo build --release --manifest-path compiler/Cargo.toml; fi
gfx_target="${CARGO_TARGET_DIR:-$PWD/compiler/target}"
sha256sum "$gfx_target/release/moo-compiler" compiler/runtime/moo_3d_water_*.h compiler/runtime/moo_dict.c compiler/runtime/moo_list.c compiler/runtime/moo_memory.c compiler/runtime/moo_string.c compiler/runtime/moo_3d_gl33.c compiler/tests/gfx_water*.moos compiler/runtime/tests/test_gfx_water_ownership.c > "$gfx_tmp/sources.sha256"
gcc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -Icompiler/runtime compiler/runtime/tests/test_gfx_water_ownership.c compiler/runtime/moo_dict.c compiler/runtime/moo_list.c compiler/runtime/moo_string.c compiler/runtime/moo_memory.c -Wl,--gc-sections -lm -o "$gfx_tmp/ownership"
ASAN_OPTIONS=detect_leaks=1 timeout 30 "$gfx_tmp/ownership" | tee "$gfx_tmp/ownership.log"
for gfx_test in gfx_water gfx_water_radiance; do
    "$gfx_target/release/moo-compiler" compile "compiler/tests/$gfx_test.moos" -o "$gfx_tmp/$gfx_test"
    for gfx_mode in 3d hybrid; do
        printf "%s mode=%s\n" "$gfx_test" "$gfx_mode"
        env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_RUNTIME_DIR="$gfx_tmp/runtime" GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 __GLX_VENDOR_LIBRARY_NAME=mesa GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 MOO_KI_GPU=0 MOO_3D_BACKEND=gl33 GFX_TIME_MODE="$gfx_mode" xvfb-run -a timeout 30 "$gfx_tmp/$gfx_test" | tee "$gfx_tmp/$gfx_test-$gfx_mode.log"
        grep -q "^GFX-WATER .*PASS$" "$gfx_tmp/$gfx_test-$gfx_mode.log"
    done
done
sha256sum --check "$gfx_tmp/sources.sha256"
printf "GFX-WATER PASS artifacts=%s\n" "$gfx_tmp"
