#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_P11_SIMD_WIN_OUT:-$root/build/p11/simd-win}
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/simd_orakel.moos --no-stdlib --target x86_64-pc-windows-gnu --emit-ir -o "$out/orakel.ll"
mise exec -- opt -passes='default<O2>' -S "$out/orakel.ll" -o "$out/orakel.opt.ll"
mise exec -- llvm-extract --delete --func=main "$out/orakel.opt.ll" -o "$out/orakel.bc"
mise exec -- llc -O2 -filetype=obj "$out/orakel.bc" -o "$out/orakel.obj"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static compiler/tests/p11/simd_orakel.cpp "$out/orakel.obj" -o "$out/moo-p11-simd-orakel.exe"
uv run --no-project --with pywinrm python skripte/p2/minipc_winrm.py upload "$out/moo-p11-simd-orakel.exe"
uv run --no-project --with pywinrm python skripte/p2/minipc_winrm.py run \
  '$env:MOO_SIMD_REQUIRE_GFNI="1"; & "D:\MooTest\moo-p11-simd-orakel.exe"; exit $LASTEXITCODE'
