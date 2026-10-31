#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
gfx_tmp=$(mktemp -d /tmp/moo-gfx-ao-api.XXXXXX)
trap 'rm -rf -- "$gfx_tmp"' EXIT
mkdir "$gfx_tmp/runtime"
chmod 700 "$gfx_tmp/runtime"
if [[ "${GFX_SKIP_BUILD:-0}" != 1 ]]; then
    cargo build --release --manifest-path compiler/Cargo.toml
fi
gfx_target="${CARGO_TARGET_DIR:-$PWD/compiler/target}"
sha256sum compiler/runtime/moo_3d.c compiler/runtime/moo_3d_gl33.c compiler/runtime/moo_3d_gl33_shaders.h compiler/runtime/moo_3d_ao_glsl.h compiler/runtime/moo_3d_ao_gl33.h compiler/runtime/moo_hybrid.c compiler/tests/gfx_ao_api.moos "$gfx_target/release/moo-compiler" > "$gfx_tmp/sources.sha256"
cat "$gfx_tmp/sources.sha256"
"$gfx_target/release/moo-compiler" compile compiler/tests/gfx_ao_api.moos -o "$gfx_tmp/fixture"
for gfx_mode in 3d hybrid; do
    echo "GFX-AO-API mode=$gfx_mode"
    env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_RUNTIME_DIR="$gfx_tmp/runtime" \
        GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
        __GLX_VENDOR_LIBRARY_NAME=mesa GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 \
        MOO_KI_GPU=0 MOO_3D_BACKEND=gl33 GFX_TIME_MODE="$gfx_mode" \
        xvfb-run -a timeout 30 "$gfx_tmp/fixture"
done
sha256sum --check "$gfx_tmp/sources.sha256"
