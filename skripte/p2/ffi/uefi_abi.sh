#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/uefi-abi
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-unknown-uefi x86_64-pc-windows-msvc x86_64-unknown-none; do
    "$compiler" compile compiler/tests/p2/uefi_moovalue_abi.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.o"
    if [[ "$target" == *uefi || "$target" == *windows* ]]; then
        grep -Fqx 'declare void @moo_number(ptr, double)' "$out/$target.ll"
        grep -Fqx 'declare void @moo_io_outw(ptr, ptr)' "$out/$target.ll"
        grep -Eq 'call void @moo_number\(ptr .*double ' "$out/$target.ll"
        grep -Eq 'call void @moo_io_outw\(ptr .*ptr ' "$out/$target.ll"
    else
        grep -Fqx 'declare { i64, i64 } @moo_number(double)' "$out/$target.ll"
        grep -Fqx 'declare void @moo_io_outw({ i64, i64 }, { i64, i64 })' "$out/$target.ll"
    fi
done
printf 'UEFI/Windows MooValue-IR und Objekte PASS; ELF-Kontrolle PASS (kein Firmwarelauf)\n'
