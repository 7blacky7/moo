#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
gfx_tmp=$(mktemp -d /tmp/moo-gfx-exposure.XXXXXX)
trap 'rm -rf -- "$gfx_tmp"' EXIT
mkdir "$gfx_tmp/runtime"
chmod 700 "$gfx_tmp/runtime"
sha256sum compiler/runtime/moo_3d_gl33.c compiler/runtime/moo_3d_hdr_gl33.h compiler/runtime/moo_3d_exposure_gl33.h compiler/runtime/moo_3d_gl33_shaders.h compiler/runtime/tests/test_gfx_exposure.c > "$gfx_tmp/sources.sha256"
cat "$gfx_tmp/sources.sha256"
gcc -std=c11 -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    -Icompiler/runtime -Icompiler/runtime/glad/include $(pkg-config --cflags sdl2 glfw3) \
    compiler/runtime/tests/test_gfx_exposure.c compiler/runtime/moo_3d_math.c compiler/runtime/moo_3d_gl33_mesh.c compiler/runtime/glad/src/glad.c \
    -Wl,--gc-sections $(pkg-config --libs sdl2 glfw3) -lm -ldl -o "$gfx_tmp/exposure"
env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_RUNTIME_DIR="$gfx_tmp/runtime" \
    SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 __GLX_VENDOR_LIBRARY_NAME=mesa \
    GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 ASAN_OPTIONS=detect_leaks=0 \
    xvfb-run -a timeout 45 "$gfx_tmp/exposure"
sha256sum --check "$gfx_tmp/sources.sha256"
