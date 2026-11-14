#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/literal_cross
compiler=compiler/target/release/moo-compiler
src=compiler/tests/p2/literal_roh_positiv.moos
for target in x86_64-pc-windows-msvc aarch64-unknown-linux-gnu; do
  out="build/p2-frontend/literal_cross/$target"
  timeout 30s "$compiler" compile "$src" --emit-ir --target "$target" -o "$out.ll"
  rg -q -F "target triple = \"$target\"" "$out.ll"
  awk '
    /^define .*@("?__moo_native[.]binaer"?)/ {inside=1; next}
    inside && /^}/ {inside=0}
    inside && /ret i32 42/ {ok=1}
    END {if (!ok) exit 1}
  ' "$out.ll"
  rg -q 'call .*@moo_range\(' "$out.ll"
  timeout 30s mise exec -- opt -passes=verify -disable-output "$out.ll"
  timeout 30s mise exec -- llc -filetype=obj "$out.ll" -o "$out.o"
  timeout 30s mise exec -- llvm-readobj --file-headers "$out.o" > "$out.headers"
  case "$target" in
    x86_64-*) rg -q -F 'Format: COFF-x86-64' "$out.headers" ;;
    aarch64-*) rg -q -F 'Format: elf64-littleaarch64' "$out.headers" ;;
  esac
  echo "PASS $target: Binärliteral, Range-IR, Verify und Objektformat"
done
for target in x86_64-bare aarch64-bare; do
  out="build/p2-frontend/literal_cross/$target"
  timeout 30s "$compiler" compile compiler/tests/p2/literal_bare_probe.moos --no-stdlib --emit-ir --target "$target" -o "$out.ll"
  case "$target" in
    x86_64-*) triple=x86_64-unknown-none; format=elf64-x86-64 ;;
    aarch64-*) triple=aarch64-unknown-none; format=elf64-littleaarch64 ;;
  esac
  rg -q -F "target triple = \"$triple\"" "$out.ll"
  awk '
    /^define .*@("?__moo_native[.]binaer"?)/ {inside=1; next}
    inside && /^}/ {inside=0}
    inside && /ret i32 42/ {ok=1}
    END {if (!ok) exit 1}
  ' "$out.ll"
  timeout 30s mise exec -- opt -passes=verify -disable-output "$out.ll"
  timeout 30s mise exec -- llc -filetype=obj "$out.ll" -o "$out.o"
  timeout 30s mise exec -- llvm-readobj --file-headers "$out.o" > "$out.headers"
  rg -q -F "Format: $format" "$out.headers"
  echo "PASS $target: Binärliteral, Verify und ELF-Objekt"
done
