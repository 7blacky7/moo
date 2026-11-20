#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t14/kontext"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile skripte/p8/smp/laufzeit_kontext_probe.moos --no-stdlib --emit-ir -o "$out/probe.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.ll"
mise exec -- llvm-extract -S --recursive --func=__moo_native.t14_kontext_slot \
    --func=__moo_native.t14_kontext_groesse --func=__moo_native.t14_kontext_start \
    --glob=__moo_prokern_vorlage --rglob='sys[.]fehler[.]text.*' "$out/probe.ll" -o "$out/kontext.ll"
mise exec -- llc -relocation-model=pic -filetype=obj "$out/kontext.ll" -o "$out/kontext.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -pthread skripte/p8/smp/laufzeit_kontext_orakel.cpp "$out/kontext.o" -o "$out/kontext-orakel"
"$out/kontext-orakel"
"$compiler" compile beispiele/kernel/kern_laufzeit_faden.moos --no-stdlib --emit-ir --target x86_64-bare -o "$out/traeger.ll"
mise exec -- opt -passes=verify -disable-output "$out/traeger.ll"
if "$compiler" compile skripte/p8/smp/runtime_export_kollision.moos --no-stdlib --emit-ir --target x86_64-bare -o "$out/kollision.ll" >"$out/kollision.log" 2>&1; then
    echo "FAIL: allgemeine C-Export-Kollision wurde akzeptiert" >&2
    exit 1
fi
grep -F "Native Funktion 'moo_thread_done': Signatur, Defaults, Decorators oder Name nicht unterstützt" "$out/kollision.log"
echo "PASS bestehende Runtime-/C-Export-Kollisionsdiagnose"
