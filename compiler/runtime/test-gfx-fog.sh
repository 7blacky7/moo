#!/usr/bin/env bash
# Correctness only: announce an exclusive GPU slot before invoking this gate.
set -euo pipefail
cd "$(dirname "$0")/../.."
for gfx_fog_tool in gcc sha256sum timeout xvfb-run; do
    command -v "$gfx_fog_tool" >/dev/null || { echo "Missing $gfx_fog_tool" >&2; exit 2; }
done
mkdir -p build
gfx_fog_out="$(mktemp -d "$PWD/build/gfx-fog.XXXXXX")"
echo "Artifacts: $gfx_fog_out"
sha256sum compiler/runtime/moo_3d_fog_math.h compiler/runtime/moo_3d_fog_glsl.h \
    compiler/runtime/moo_3d_fog_gl33.h compiler/runtime/moo_3d_gl33.c \
    compiler/runtime/moo_3d_gl33_shaders.h compiler/runtime/moo_3d.c \
    compiler/runtime/tests/test_gfx_fog_math.c compiler/tests/gfx_fog.moos \
    > "$gfx_fog_out/source.sha256"
gcc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    compiler/runtime/tests/test_gfx_fog_math.c -lm -o "$gfx_fog_out/math"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$gfx_fog_out/math" | tee "$gfx_fog_out/math.log"
if [[ "${GFX_SKIP_BUILD:-0}" != 1 ]]; then
    mise exec -- cargo build --release --manifest-path compiler/Cargo.toml
fi
gfx_fog_compiler="${MOO_COMPILER:-$PWD/compiler/target/release/moo-compiler}"
sha256sum "$gfx_fog_compiler" > "$gfx_fog_out/compiler.sha256"
"$gfx_fog_compiler" compile compiler/tests/gfx_fog.moos -o "$gfx_fog_out/native" > "$gfx_fog_out/compile.log" 2>&1
sha256sum -c "$gfx_fog_out/source.sha256"
mkdir -m 700 "$gfx_fog_out/private-runtime"
for gfx_fog_mode in 3d hybrid; do
    env -u DISPLAY -u WAYLAND_DISPLAY MOO_GLFW_PLATFORM=x11 MOO_3D_BACKEND=gl33 \
        XDG_RUNTIME_DIR="$gfx_fog_out/private-runtime" SDL_VIDEODRIVER=x11 GDK_BACKEND=x11 \
        XDG_SESSION_TYPE=x11 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
        GFX_TIME_MODE="$gfx_fog_mode" timeout 40 xvfb-run -a "$gfx_fog_out/native" \
        > "$gfx_fog_out/$gfx_fog_mode.log" 2>&1
    cat "$gfx_fog_out/$gfx_fog_mode.log"
    grep -q '^GFX-FOG native PASS$' "$gfx_fog_out/$gfx_fog_mode.log"
done
sha256sum -c "$gfx_fog_out/source.sha256"
sha256sum -c "$gfx_fog_out/compiler.sha256"
echo "GFX-FOG registered PASS (private Xvfb/llvmpipe; performance N/A)"
