#!/usr/bin/env bash
# Correctness only; private Xvfb/llvmpipe is not a performance environment.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
gfx_tmp=$(mktemp -d /tmp/moo-gfx-renderinfo.XXXXXX)
trap 'rm -rf -- "$gfx_tmp"' EXIT
mkdir "$gfx_tmp/runtime"
chmod 700 "$gfx_tmp/runtime"
if [[ "${GFX_SKIP_BUILD:-0}" != 1 ]]; then
    cargo build --release --manifest-path compiler/Cargo.toml
fi
gfx_target="${CARGO_TARGET_DIR:-$PWD/compiler/target}"
sha256sum compiler/runtime/moo_3d.c compiler/runtime/moo_hybrid.c compiler/runtime/moo_3d_gl33.c compiler/runtime/moo_3d_backend.h compiler/runtime/moo_test_api.c compiler/src/codegen.rs compiler/src/runtime_bindings.rs \
    compiler/runtime/moo_runtime.h compiler/runtime/tests/test_gfx_renderer_info.c \
    compiler/tests/gfx_renderer_info.moos compiler/runtime/test-gfx-renderinfo.sh \
    "$gfx_target/release/moo-compiler" > "$gfx_tmp/sources.sha256"
cat "$gfx_tmp/sources.sha256"
gcc -std=c11 -O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -Icompiler/runtime $(pkg-config --cflags sdl2 SDL2_image) compiler/runtime/tests/test_gfx_renderer_info.c \
    -Wl,--gc-sections -lm -o "$gfx_tmp/registry"
"$gfx_tmp/registry"
"$gfx_target/release/moo-compiler" compile compiler/tests/gfx_renderer_info.moos -o "$gfx_tmp/fixture"
for gfx_mode in 3d hybrid; do
    echo "GFX-RENDERINFO mode=$gfx_mode"
    env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_RUNTIME_DIR="$gfx_tmp/runtime" \
        GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
        __GLX_VENDOR_LIBRARY_NAME=mesa GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 \
        MOO_KI_GPU=0 MOO_3D_BACKEND=gl33 GFX_TIME_MODE="$gfx_mode" \
        xvfb-run -a timeout 30 "$gfx_tmp/fixture"
done
sha256sum --check "$gfx_tmp/sources.sha256"
