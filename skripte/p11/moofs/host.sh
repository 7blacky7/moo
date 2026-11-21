#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/moofs
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/moofs.moos -o "$out/host"
"$out/host" > "$out/host.txt"
diff -u compiler/tests/p11/moofs.expected "$out/host.txt"
"$compiler" compile compiler/tests/p11/moofs_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/probe-bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe-bare.ll"
echo 'MooFS Host: CRC32C, Baum/Extents, Lesen und Korruptionsdiagnosen grün'
