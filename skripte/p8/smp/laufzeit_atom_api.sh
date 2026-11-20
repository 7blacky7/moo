#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t14/atom-api"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile skripte/p8/smp/laufzeit_atom_api_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/api.ll"
mise exec -- opt -passes=verify -disable-output "$out/api.ll"
mise exec -- llvm-extract -S --recursive --func=moo_atom_neu --func=moo_atom_laden \
    --func=moo_atom_speichern --func=moo_atom_addieren --func=moo_atom_tauschen \
    --func=moo_atom_vergleichen_und_tauschen --func=moo_atom_free \
    --rglob='(LW_.*|sys[.]fehler[.]text.*)' "$out/api.ll" -o "$out/probe.ll"
mise exec -- llc -filetype=obj "$out/probe.ll" -o "$out/probe.o"
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -no-pie -fsanitize=address,undefined \
    skripte/p8/smp/laufzeit_atom_api_orakel.cpp "$out/probe.o" -o "$out/api-orakel"
ASAN_OPTIONS=detect_leaks=1 "$out/api-orakel"
echo "PASS echte Runtime-ABI, Typdiagnosen und Besitz der Fehlertexte"
