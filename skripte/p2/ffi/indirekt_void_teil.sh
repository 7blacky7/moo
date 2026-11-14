#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/indirekt-void-teil
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-bare x86_64-unknown-uefi aarch64-bare; do
    "$compiler" compile compiler/tests/p2/ffi_indirekt_void_teil.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llvm-extract -S --recursive --func=__moo_native.p2_indirekt_void "$out/$target.ll" -o "$out/$target.extract.ll"
    grep -Eq 'call void %[^ (]+\(i32 ' "$out/$target.extract.ll"
    mise exec -- llc -filetype=obj "$out/$target.extract.ll" -o "$out/$target.o"
done
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror compiler/tests/p2/ffi_indirekt_void_teil.cpp "$out/x86_64-bare.o" -o "$out/probe"
"$out/probe"
for case_name in verworfen wert; do
    if "$compiler" compile "compiler/tests/p2/ffi_indirekt_void_${case_name}.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/$case_name.ll" >"$out/$case_name.log" 2>&1; then
        echo "Ungültiger void-Aufruf akzeptiert: $case_name" >&2; exit 1
    fi
    grep -Fq 'Rückgabetyp stimmt nicht' "$out/$case_name.log"
done
printf 'Indirekter void-Teilaufruf C++ und Cross-IR PASS; kein UEFI-Lauf\n'
