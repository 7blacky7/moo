#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/array-q
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/array_q_format_host.moos -o "$out/format"
"$out/format" > "$out/format.txt"
printf '0\n' | diff -u - "$out/format.txt"
"$compiler" compile compiler/tests/p11/array_q_format.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/format-probe-bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/format-probe-bare.ll"
echo 'PASS P/Q-v3 Codec: fünf Mitglieder, letzte Serienbytes,27 resealte Negativfälle, keine Mutation bei Fehler'
