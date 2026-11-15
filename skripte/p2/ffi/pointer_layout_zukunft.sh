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
# P2-T51 C4 (Plan f5b52a0b): Moo/C-Layoutvergleich - Moo erzeugt je Ziel einen
# C11-Header mit _Static_assert fuer sizeof/_Alignof/offsetof, der Ziel-C-
# Compiler prueft ihn (wie skripte/p2/bindgen/layout_gate.sh), inkl. hosted.
clang_bin="${CLANG:-clang}"
for ziel in x86_64-unknown-linux-gnu aarch64-unknown-linux-gnu x86_64-pc-windows-msvc x86_64-unknown-none aarch64-unknown-none; do
    "$compiler" layout compiler/tests/p2/struktur_pointer_felder.moos --target "$ziel" --c-header > "$out/$ziel.h"
    grep -q '_Static_assert' "$out/$ziel.h" || { echo "FEHLER: Header $ziel ohne Layout-Asserts" >&2; exit 1; }
    mise exec -- "$clang_bin" --target="$ziel" -std=c11 -ffreestanding -fsyntax-only -Wall -Wextra -Werror -x c "$out/$ziel.h"
done
printf 'Pointerfeld-Strukturen: IR/Objekt je Bare-Ziel, Moo/C-Layout (sizeof/_Alignof/offsetof) fuer 5 Ziele inkl. hosted PASS\n'
