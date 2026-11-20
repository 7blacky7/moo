#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t11/madt"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile skripte/p8/smp/madt_probe.moos --no-stdlib --emit-ir --target x86_64-bare -o "$out/probe.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.ll"
mise exec -- llvm-extract -S --recursive \
    --func=__moo_native.smp_madt --func=__moo_native.smp_kern \
    --func=__moo_native.t11_anzahl --func=__moo_native.t11_ok --glob=SMP_ZUSTAND --glob=KERNE \
    "$out/probe.ll" -o "$out/parser.ll"
mise exec -- llc -filetype=obj -function-sections -data-sections "$out/parser.ll" -o "$out/parser.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -no-pie -Wl,--gc-sections skripte/p8/smp/madt_orakel.cpp "$out/parser.o" -o "$out/madt-probe"
"$out/madt-probe"
