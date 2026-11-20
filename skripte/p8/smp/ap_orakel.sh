#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t12/orakel"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile skripte/p8/smp/ap_probe.moos --no-stdlib --emit-ir --target x86_64-bare -o "$out/probe.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.ll"
mise exec -- llvm-extract -S --recursive --func=__moo_native.t12_icr --func=__moo_native.t12_karte \
    --func=__moo_native.ap_lowmem_belegt --glob=AP_ZUSTAND --glob=AP_TEST_INFO "$out/probe.ll" -o "$out/orakel.ll"
mise exec -- llc -filetype=obj -function-sections -data-sections "$out/orakel.ll" -o "$out/orakel.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -no-pie -Wl,--gc-sections skripte/p8/smp/ap_orakel.cpp "$out/orakel.o" -o "$out/ap-orakel"
"$out/ap-orakel"
