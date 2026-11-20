#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t14/nullstruktur"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile skripte/p8/smp/nullstruktur_probe.moos --no-stdlib --emit-ir -o "$out/hosted.ll"
mise exec -- opt -passes=verify -disable-output "$out/hosted.ll"
mise exec -- llvm-extract -S --recursive --func=null_probe --func=null_rein --func=null_kopie --glob=native.abbruch.text "$out/hosted.ll" -o "$out/probe.ll"
mise exec -- llc -filetype=obj "$out/probe.ll" -o "$out/probe.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -no-pie skripte/p8/smp/nullstruktur_orakel.cpp "$out/probe.o" -o "$out/probe"
"$out/probe"
"$compiler" compile skripte/p8/smp/nullstruktur_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/bare.ll"
for fall in zahl gleit neuzuweisung parameter unterlauf; do
    if "$compiler" compile "skripte/p8/smp/nullstruktur_$fall.moos" --no-stdlib --emit-ir -o "$out/$fall.ll" >"$out/$fall.log" 2>&1; then
        echo "FAIL unerlaubte Strukturkonversion $fall" >&2; exit 1
    fi
    grep -F 'Fehler M-' "$out/$fall.log"
    grep -F 'NullS' "$out/$fall.log" >/dev/null
    if grep -F 'Nicht-reines natives Aufrufziel' "$out/$fall.log"; then
        echo "FAIL falscher Diagnosepfad $fall" >&2; exit 1
    fi
done
echo "PASS getypte Nullstruktur Hosted/Bare, Wiederinitialisierung und Konversionsgrenze"
