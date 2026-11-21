#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-$PWD/compiler/target/release/moo-compiler}"
build="$PWD/build/p12/behaelter"
quelle="$PWD/compiler/tests/p12"
mkdir -p "$build"
lauf="$(mktemp -d "$build/lauf.XXXXXX")"
printf 'Behälterprotokoll %s\n' "$lauf"
export TMPDIR="$lauf"
"$compiler" compile "$quelle/behaelter.moos" --lib-pfad "$PWD/stdlib" \
    --target x86_64-unknown-linux-gnu --no-stdlib --emit-ir -o "$lauf/behaelter-alle.ll"
mise exec -- llvm-extract -S --func=p12_behaelter_probe --recursive --keep-const-init \
    "$lauf/behaelter-alle.ll" -o "$lauf/behaelter.ll"
mise exec -- opt -passes=verify -disable-output "$lauf/behaelter.ll"
mise exec -- clang -O2 -c "$lauf/behaelter.ll" -o "$lauf/behaelter.o"
mise exec -- clang -O2 -Wall -Wextra -Werror "$quelle/behaelter.c" "$lauf/behaelter.o" -o "$lauf/behaelter"
"$lauf/behaelter"
mise exec -- opt -passes='forceattrs,asan' -force-attribute=sanitize_address \
    -S "$lauf/behaelter.ll" -o "$lauf/behaelter-asan.ll"
rg -q '__asan_report_(load|store)' "$lauf/behaelter-asan.ll"
mise exec -- clang -O1 -fno-omit-frame-pointer -c "$lauf/behaelter-asan.ll" -o "$lauf/behaelter-asan.o"
mise exec -- clang -O1 -g -Wall -Wextra -Werror -fsanitize=address \
    -fno-omit-frame-pointer "$quelle/behaelter.c" "$lauf/behaelter-asan.o" -o "$lauf/behaelter-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$lauf/behaelter-asan"
printf '%s\n' 'ASan: Moo und Hosted-Treiber grün'
