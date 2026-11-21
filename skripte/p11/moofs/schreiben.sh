#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/moofs
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/moofs_schreiben.moos -o "$out/schreiben"
"$out/schreiben" > "$out/schreiben.txt"
diff -u compiler/tests/p11/moofs_schreiben.expected "$out/schreiben.txt"
"$compiler" compile compiler/tests/p11/moofs_schreiben_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/schreiben-bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/schreiben-bare.ll"
"$compiler" compile compiler/tests/p11/moofs_memmove_linux.moos --no-stdlib --target x86_64-bare -o "$out/memmove-linux.o"
cc -nostartfiles -no-pie -Wl,-e,_start -o "$out/memmove" "$out/memmove-linux.o"
"$out/memmove"
"$compiler" compile compiler/tests/p11/moofs_memmove_probe.moos --no-stdlib --target x86_64-bare -o "$out/memmove-bare.o"
mise exec -- llvm-nm --defined-only "$out/memmove-bare.o" > "$out/memmove-symbole.txt"
rg -q ' T memmove$' "$out/memmove-symbole.txt"
echo 'MooFS CoW: Mutatoren, ENOSPC, Recycling und memmove-Träger grün'
