#!/usr/bin/env bash
# P2-T56: erster echter Moo-C-ABI-Crossbeleg fuer AArch64/AAPCS64.
# Auf AArch64 wird hier NICHT ausgefuehrt; Clang-C und Moo-LLVM/COFF
# werden gegen dieselbe Sourceklasse geprueft und relocatable gelinkt.
set -euo pipefail
cd "$(dirname "$0")/../../.."

compiler="${1:-build/p2-t48-51-cargo/release/moo-compiler}"
quelle=skripte/p7/lader/system_ffi_struct.moos
c_ref=skripte/p2/abi/struct_by_value.c
out=build/p2/aarch64-ffi
mkdir -p "$out"

# Getrennte Clang-Referenz ohne Host-C-ABI.
clang --target=aarch64-unknown-none -ffreestanding -std=c11 -O0 \
    -S -emit-llvm -I skripte/p2/abi "$c_ref" -o "$out/c-reference.ll"

# Dieselbe Moo-Quelle wie fuer SysV und Win64.
"$compiler" compile "$quelle" --target aarch64-bare --emit-ir \
    -o "$out/moo.ll"
opt -passes=verify -disable-output "$out/moo.ll"
llvm-extract -S --func=sys_c_abi4 --func=sys_c_abi8 \
    --func=sys_c_abi12 --func=sys_c_abi16 --func=sys_c_abi24 \
    --recursive "$out/moo.ll" -o "$out/moo-only.ll"
opt -passes=verify -disable-output "$out/moo-only.ll"

# Alle Typ-Signaturen sind Clang-bestaetigt, kein pauschales
# "LLVM kennt die Zielplattform"-Argument.
signatures=(
    "i32 @moo_p2_struct1(i64)"
    "i64 @moo_p2_struct2(i64)"
    "[2 x i64] @moo_p2_struct3([2 x i64])"
    "[2 x i64] @moo_p2_struct4([2 x i64])"
)
for signature in "${signatures[@]}"; do
    grep -Fq "declare $signature" "$out/moo-only.ll"
    # Clang-Definitionen enthalten Parameternamen/Attribute (z. B. i64 %0),
    # die Deklaration dagegen nicht. Der Typ-Praefix ist der ABI-Vertrag.
    grep -Fq "define dso_local ${signature%)} " "$out/c-reference.ll"
done
grep -Fq 'declare void @moo_p2_struct6(ptr sret(' "$out/moo-only.ll"
grep -Fq 'define dso_local void @moo_p2_struct6(ptr ' "$out/c-reference.ll"
if grep -Fq 'byval(' "$out/moo-only.ll"; then
    echo "AArch64-ABI: unerwartetes x86-SysV-byval" >&2
    exit 1
fi

# Zwei separat erzeugte AArch64-Objekte zu einem relocatable ELF
# kombinieren. Das ist ein Link-/Objektbeleg, KEIN nativer ARM64-Lauf.
llc -mtriple=aarch64-unknown-none -O2 -filetype=obj \
    "$out/moo-only.ll" -o "$out/moo.o"
clang --target=aarch64-unknown-none -ffreestanding -std=c11 -O2 \
    -I skripte/p2/abi -c "$c_ref" -o "$out/c-reference.o"
ld.lld -m aarch64elf -r "$out/moo.o" "$out/c-reference.o" \
    -o "$out/combined.o"
llvm-readobj --file-headers "$out/combined.o" > "$out/file-headers.txt"
grep -Fq 'Arch: aarch64' "$out/file-headers.txt" || \
    grep -Fq 'Machine: EM_AARCH64' "$out/file-headers.txt"
llvm-nm "$out/combined.o" > "$out/symbols.txt"
for n in 1 2 3 4 6; do
    grep -Eq " T moo_p2_struct$n$" "$out/symbols.txt"
    if grep -Eq " U moo_p2_struct$n$" "$out/symbols.txt"; then
        echo "AArch64: C-Symbol moo_p2_struct$n blieb undefiniert" >&2
        exit 1
    fi
done
echo "PASS AArch64 Moo->C Struct4/8/12/16/24: LLVM-ABI, COFF/ELF-AArch64-Objekt und relocatable C-Link (KEIN AArch64-Ziellauf)"
