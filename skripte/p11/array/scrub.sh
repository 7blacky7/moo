#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/array-scrub
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/array_pq_scrub_host.moos -o "$out/host"
"$out/host" > "$out/host.txt"
mapfile -t ergebnis < "$out/host.txt"
test "${#ergebnis[@]}" = 5
test "${ergebnis[0]}" = 0
for i in 1 2 3 4; do test "${ergebnis[$i]}" -gt 0; done
printf 'Abbruchpunkte Scrub/Aufnahme/P/Q: %s %s %s %s\n' "${ergebnis[1]}" "${ergebnis[2]}" "${ergebnis[3]}" "${ergebnis[4]}"
"$compiler" compile compiler/tests/p11/array_pq_scrub_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/bare.ll"
echo 'P/Q-Scrub: MooFS, alle Einzel-/Paarfehler, Nullaufnahme und P/Q-Wechsel mit Neustart grün'
