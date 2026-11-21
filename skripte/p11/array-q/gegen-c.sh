#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out="$PWD/build/p11/array-q/t26"
deps="$out/deps"
mkdir -p "$out"
if [ ! -f "$deps/usr/lib/libisal.so.2" ]; then
  mkdir -p "$deps"
  curl --fail --location --retry 2 https://cdn77.cachyos.org/repo/x86_64_v3/cachyos-extra-v3/isa-l-2.32.1-1.1-x86_64_v3.pkg.tar.zst -o "$out/isa-l-2.32.1.pkg.tar.zst"
  printf '%s  %s\n' cac55cded79d6b83455f7e3d35bacd9b8ab9d88a180c4b874dfb30052365b235 "$out/isa-l-2.32.1.pkg.tar.zst" | sha256sum --check -
  bsdtar -xf "$out/isa-l-2.32.1.pkg.tar.zst" -C "$deps"
fi
printf '%s  %s\n' 0e95c577003b384e9471a135c02354fab10f19a4259b4faa5ee002e8ae50ddec "$deps/usr/lib/libisal.so.2" | sha256sum --check -
"$compiler" compile compiler/tests/p11/array_gegen_c.moos --no-stdlib --target x86_64-unknown-linux-gnu --emit-ir -o "$out/moo.ll"
mise exec -- opt -passes='default<O2>' -S "$out/moo.ll" -o "$out/moo.opt.ll"
mise exec -- llvm-extract --delete --func=main "$out/moo.opt.ll" -o "$out/moo.bc"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/moo.bc" -o "$out/moo.o"
mise exec -- llc -O2 -relocation-model=pic -filetype=asm "$out/moo.bc" -o "$out/moo.s"

# Messkontrolle vor Optimierung: lediglich CRC-Helfer loeschen (=extern).
# Alle definierten Symbole umbenennen, damit beide vollstaendigen Pruefer
# im selben Prozess mit identischen Daten und unabhaengigem Zustand laufen.
mise exec -- llvm-extract --delete --func=__moo_crc32c_update --func=main -S "$out/moo.ll" -o "$out/ref.ll"
mise exec -- opt -passes='default<O2>,verify' "$out/ref.ll" -o "$out/ref.bc"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/ref.bc" -o "$out/ref.o"
mise exec -- llvm-nm --defined-only --format=posix "$out/ref.o" | awk '{ print $1, "ref_" $1 }' > "$out/ref-symbols.txt"
mise exec -- llvm-objcopy --redefine-syms="$out/ref-symbols.txt" "$out/ref.o" "$out/ref-prefixed.o"


# Nur Messkontrolle: identischer CRC-Hardwarekoerper ohne inneren Dispatch.
# C-Treiber prueft SSE4.2; taskset haelt die Messung auf derselben CPU.
mise exec -- llvm-extract --recursive --func=__moo_crc32c_fast --glob=__moo_crc32c_table --glob=__moo_crc32c_shift256 -S "$out/moo.ll" -o "$out/crc-control-source.ll"
sed -e 's/@__moo_crc32c_fast(/@crc32c_ohne_dispatch(/g' \
    -e 's/define internal i32 @crc32c_ohne_dispatch/define i32 @crc32c_ohne_dispatch/' \
    -e 's/@__moo_crc32c_/@__moo_crc32c_control_/g' \
    "$out/crc-control-source.ll" > "$out/crc-control.ll"
mise exec -- opt -passes='default<O2>,verify' "$out/crc-control.ll" -o "$out/crc-control.bc"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/crc-control.bc" -o "$out/crc-control.o"

gcc -std=c11 -O3 -Wall -Wextra -I"$deps/usr/include" compiler/tests/p11/array_gegen_c.c "$out/moo.o" "$out/ref-prefixed.o" "$out/crc-control.o" \
  -L"$deps/usr/lib" -Wl,-rpath,'$ORIGIN/deps/usr/lib:$ORIGIN' -lisal -o "$out/moo-p11-gegen-c"
sha256sum "$compiler" "$out/moo-p11-gegen-c" "$deps/usr/lib/libisal.so.2" > "$out/stand.sha256"
taskset -c "${MOO_BENCH_CPU:-28}" "$out/moo-p11-gegen-c" | tee "$out/host.jsonl"
