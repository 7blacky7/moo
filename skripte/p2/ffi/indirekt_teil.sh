#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/indirekt-teil
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-bare x86_64-unknown-uefi aarch64-bare; do
    "$compiler" compile compiler/tests/p2/ffi_indirekt_teil.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llvm-extract -S --recursive --func=__moo_native.p2_indirekt --func=__moo_native.p2_indirekt_zehn --func=__moo_native.p2_indirekt_i8 --func=__moo_native.p2_indirekt_u8 --func=__moo_native.p2_indirekt_i16 --func=__moo_native.p2_indirekt_u16 "$out/$target.ll" -o "$out/$target.extract.ll"
    grep -Eq 'call i32 %[^ (]+\(i32 ' "$out/$target.extract.ll"
    grep -Eq 'call i64 %[^ (]+\(ptr [^,]+, ptr [^,]+, i32 [^,]+, i64 .*' "$out/$target.extract.ll"
    if [[ "$target" == x86_64-bare ]]; then
        grep -Eq 'call signext i8 %[^ (]+\(i8 signext ' "$out/$target.extract.ll"
        grep -Eq 'call zeroext i8 %[^ (]+\(i8 zeroext ' "$out/$target.extract.ll"
        grep -Eq 'call signext i16 %[^ (]+\(i16 signext ' "$out/$target.extract.ll"
        grep -Eq 'call zeroext i16 %[^ (]+\(i16 zeroext ' "$out/$target.extract.ll"
    else
        if grep -Eq 'call (signext|zeroext) i8 %' "$out/$target.extract.ll"; then
            echo "Unerwartetes Narrow-Attribut für $target" >&2; exit 1
        fi
        [[ $(grep -Ec 'call i8 %[^ (]+\(i8 ' "$out/$target.extract.ll") -eq 2 ]]
        if grep -Eq 'call (signext|zeroext) i16 %' "$out/$target.extract.ll"; then
            echo "Unerwartetes 16-Bit-Attribut für $target" >&2; exit 1
        fi
        [[ $(grep -Ec 'call i16 %[^ (]+\(i16 ' "$out/$target.extract.ll") -eq 2 ]]
    fi
    mise exec -- llc -filetype=obj "$out/$target.extract.ll" -o "$out/$target.o"
done
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror compiler/tests/p2/ffi_indirekt_teil.cpp "$out/x86_64-bare.o" -o "$out/probe"
"$out/probe"
for test_case in unsicher_fehlt typ_falsch pointer_falsch return_falsch anzahl_falsch; do
    if "$compiler" compile "compiler/tests/p2/ffi_indirekt_${test_case}.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/$test_case.ll" >"$out/$test_case.log" 2>&1; then
        echo "Ungültiger indirekter Aufruf akzeptiert: $test_case" >&2; exit 1
    fi
done
grep -Fq 'erfordert unsicher' "$out/unsicher_fehlt.log"
grep -Eq 'Argumenttyp stimmt nicht|falscher Typ' "$out/typ_falsch.log"
grep -Eq 'Argumenttyp stimmt nicht|falscher Typ' "$out/pointer_falsch.log"
grep -Fq 'Rückgabetyp stimmt nicht' "$out/return_falsch.log"
grep -Fq 'falsche Argumentzahl' "$out/anzahl_falsch.log"
printf 'Teilaufruf C++ und Cross-IR PASS; keine GOP- oder Firmwareausführung\n'
