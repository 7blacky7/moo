#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t14/atom"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile beispiele/kernel/kern_laufzeit_atom.moos --no-stdlib --emit-ir -o "$out/atom.ll"
mise exec -- opt -passes=verify -disable-output "$out/atom.ll"
mise exec -- llvm-extract -S --recursive --func=__moo_native.laufzeit_atom_init \
    --func=__moo_native.laufzeit_atom_laden --func=__moo_native.laufzeit_atom_speichern \
    --func=__moo_native.laufzeit_atom_tauschen --func=__moo_native.laufzeit_atom_addieren \
    --func=__moo_native.laufzeit_atom_vergleichen --rglob='sys[.]fehler[.]text.*' "$out/atom.ll" -o "$out/probe.ll"
mise exec -- llc -filetype=obj "$out/probe.ll" -o "$out/probe.o"
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -pthread -no-pie skripte/p8/smp/laufzeit_atom_orakel.cpp "$out/probe.o" -o "$out/atom-orakel"
"$out/atom-orakel"
"$compiler" compile beispiele/kernel/kern_laufzeit_atom.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/bare.ll"
grep -E 'cmpxchg .* seq_cst seq_cst' "$out/probe.ll" > /dev/null
echo "PASS Atom-Kern hosted/Bare, folgerichtige CAS-Speicherordnung"
