#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t14/provider-abi"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile skripte/p8/smp/runtime_provider_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/probe.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.ll"
mise exec -- llvm-extract -S --recursive --func=moo_thread_done --func=moo_thread_spawn --func=kern_c_moo_number "$out/probe.ll" -o "$out/abi.ll"
mise exec -- llc -filetype=obj -function-sections -data-sections "$out/abi.ll" -o "$out/abi.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -no-pie -Wl,--gc-sections skripte/p8/smp/runtime_provider_orakel.cpp "$out/abi.o" -o "$out/abi-orakel"
"$out/abi-orakel"
for fall in wert_falsch signatur_falsch speicher_falsch; do
    if "$compiler" compile "skripte/p8/smp/runtime_provider_$fall.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/$fall.ll" >"$out/$fall.log" 2>&1; then
        echo "FAIL ungueltiger Provider akzeptiert: $fall" >&2
        exit 1
    fi
    grep -F 'Bare-Runtime-Provider:' "$out/$fall.log"
done
if "$compiler" compile skripte/p8/smp/runtime_provider_probe.moos --no-stdlib --target aarch64-bare --emit-ir -o "$out/arm.ll" >"$out/arm.log" 2>&1; then
    echo "FAIL Provider auf nicht freigegebenem Ziel" >&2
    exit 1
fi
grep -F 'zunächst nur für x86_64-bare' "$out/arm.log"
echo "PASS Provider-Typen und Zielgrenze"
