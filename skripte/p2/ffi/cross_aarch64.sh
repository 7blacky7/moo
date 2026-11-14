#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi-cross-aarch64
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
target=aarch64-unknown-linux-gnu
timeout 120s "$compiler" compile compiler/tests/p2/ffi_klein_roundtrip.moos --target "$target" --emit-ir -o "$out/moo.ll"
mise exec -- opt -passes=verify -disable-output "$out/moo.ll"
mise exec -- llc -filetype=obj "$out/moo.ll" -o "$out/moo.o"
mise exec -- llvm-readobj --file-headers "$out/moo.o" | grep -Fq 'EM_AARCH64'
mise exec -- llvm-nm "$out/moo.o" > "$out/moo.nm"
grep -Fq 'p2_ffi_i8_identity' "$out/moo.nm"
grep -Fq 'p2_ffi_u8_identity' "$out/moo.nm"
awk '/call i8 @p2_ffi_i8_identity\(i8 / {s=1} /call i8 @p2_ffi_u8_identity\(i8 / {u=1} END {if (!s || !u) exit 1}' "$out/moo.ll"
grep -Eq '^declare i16 @p2_ffi_i16_identity\(i16\)' "$out/moo.ll"
grep -Eq '^declare i16 @p2_ffi_u16_identity\(i16\)' "$out/moo.ll"
mise exec -- clang --target="$target" -ffreestanding -S -emit-llvm -O0 compiler/tests/p2/ffi_klein_roundtrip.c -o "$out/clang.ll"
awk '/^define .* i8 @p2_ffi_i8_identity\(i8 / {s=1} /^define .* i8 @p2_ffi_u8_identity\(i8 / {u=1} END {if (!s || !u) exit 1}' "$out/clang.ll"
grep -Eq '^define .* i16 @p2_ffi_i16_identity\(i16 ' "$out/clang.ll"
grep -Eq '^define .* i16 @p2_ffi_u16_identity\(i16 ' "$out/clang.ll"
printf 'AArch64 Linux FFI IR/ELF: PASS (kein AArch64-Lauf)\n'
