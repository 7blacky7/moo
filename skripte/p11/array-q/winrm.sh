#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/array-q/win
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/array_q_win.moos --no-stdlib --target x86_64-pc-windows-gnu --emit-ir -o "$out/q.ll"
mise exec -- opt -passes='default<O2>' -S "$out/q.ll" -o "$out/q.opt.ll"
mise exec -- llvm-extract --delete --func=main "$out/q.opt.ll" -o "$out/q.bc"
mise exec -- llc -O2 -filetype=obj "$out/q.bc" -o "$out/q.obj"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static compiler/tests/p11/array_q_win.cpp "$out/q.obj" -Wl,--wrap=write -Wl,--wrap=clock_gettime -o "$out/moo-p11-q-orakel.exe"
uv run --no-project --with pywinrm python skripte/p2/windows_minipc/smb_upload.py "$out/moo-p11-q-orakel.exe"
uv run --no-project --with pywinrm python skripte/p2/minipc_winrm.py run '& "D:\MooTest\moo-p11-q-orakel.exe"; exit $LASTEXITCODE'
