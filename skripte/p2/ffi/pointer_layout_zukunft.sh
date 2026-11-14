#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/pointer-layout-zukunft
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-bare x86_64-unknown-uefi aarch64-bare; do
    "$compiler" compile compiler/tests/p2/struktur_pointer_felder.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.o"
done
printf 'Pointerfeld-Deklaration kompiliert; Groesse/Offset gegen C noch nicht verglichen\n'
