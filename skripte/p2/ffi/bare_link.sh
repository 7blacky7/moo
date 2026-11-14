#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi-bare-link
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-bare aarch64-bare; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_bare_link.moos --target "$target" --no-stdlib --lib-pfad "$out" -o "$out/$target.o" > "$out/$target.log" 2>&1
    grep -Fq 'p2_bare_bibliothek_fehlt_72ac' "$out/$target.log"
    mise exec -- llvm-readobj --file-headers "$out/$target.o" > "$out/$target.headers"
    grep -Eq 'EM_X86_64|EM_AARCH64' "$out/$target.headers"
done
libdir=$(mktemp -d "$out/lib.XXXXXX")
trap 'rm -rf -- "$libdir"' EXIT
"${CC:-cc}" -std=c11 -fPIC -shared compiler/tests/p2/ffi_klein_roundtrip.c -o "$libdir/libp2_bare_bibliothek_fehlt_72ac.so"
set +e
timeout 180s "$compiler" compile compiler/tests/p2/ffi_bare_link.moos --target x86_64-bare --no-stdlib --kernel --lib-pfad "$libdir" -o "$out/kernel.elf" > "$out/kernel.log" 2>&1
rc=$?
set -e
if (( rc == 0 || rc == 124 || rc >= 128 )) || ! grep -Fq -- '-lp2_bare_bibliothek_fehlt_72ac' "$out/kernel.log"; then
    printf 'Bare-Kernel-Archivdiagnose: Exit %s oder Bibliotheksname fehlt\n' "$rc" >&2
    exit 1
fi
"${CC:-cc}" -std=c11 -ffreestanding -c compiler/tests/p2/ffi_klein_roundtrip.c -o "$libdir/reference.o"
ar rcs "$libdir/libp2_bare_bibliothek_fehlt_72ac.a" "$libdir/reference.o"
set +e
timeout 180s "$compiler" compile compiler/tests/p2/ffi_bare_link.moos --target x86_64-bare --no-stdlib --kernel --lib-pfad "$libdir" -o "$out/kernel.elf" > "$out/kernel-archiv.log" 2>&1
archive_rc=$?
set -e
if (( archive_rc == 124 || archive_rc >= 128 )) || grep -Fq -- '-lp2_bare_bibliothek_fehlt_72ac' "$out/kernel-archiv.log"; then
    printf 'Bare-Archivsuche fehlgeschlagen: Exit %s\n' "$archive_rc" >&2
    exit 1
fi
if (( archive_rc != 0 )) && ! grep -Fq 'ld (x86_64-unknown-none) fehlgeschlagen' "$out/kernel-archiv.log"; then
    printf 'Kernel scheiterte vor dem Linker: Exit %s\n' "$archive_rc" >&2
    exit 1
fi
printf 'Bare-FFI-Metadaten und statische Archivsuche: PASS (Kernel-Link Exit %s)\n' "$archive_rc"

# P2-T43: echter Bare-FFI-Aufruf. Archiv freestanding/ohne PIC/Red-Zone fuer den
# Kernel; main ruft unsicher:-Systemkoerper mit Literalen, diese die C-Funktionen.
ffi_dir="$out/aufruf-lib"
mkdir -p "$ffi_dir"
"${CC:-cc}" -std=c11 -O2 -ffreestanding -fno-pic -mno-red-zone -c compiler/tests/p2/ffi_klein_roundtrip.c -o "$ffi_dir/klein.o"
rm -f "$ffi_dir/libp2_bare_ffi_43.a"
ar rcs "$ffi_dir/libp2_bare_ffi_43.a" "$ffi_dir/klein.o"
if timeout 120s "$compiler" compile compiler/tests/p2/ffi_bare_aufruf_dynamisch.moos --target x86_64-bare --no-stdlib --emit-ir -o "$out/dyn.ll" >"$out/dyn.log" 2>&1; then
    echo 'Bare: dynamisches Argument wurde ungewollt freigegeben' >&2; exit 1
fi
grep -Fq 'Dynamische native Aufrufe sind im Bare-Profil noch nicht unterstützt' "$out/dyn.log"
timeout 180s "$compiler" compile compiler/tests/p2/ffi_bare_aufruf.moos --target x86_64-bare --no-stdlib --kernel --lib-pfad "$ffi_dir" -o "$out/aufruf.elf" >"$out/aufruf.log" 2>&1 \
    || { cat "$out/aufruf.log" >&2; echo 'Bare-FFI-Kernel nicht gelinkt' >&2; exit 1; }
mise exec -- llvm-nm "$out/aufruf.elf" > "$out/aufruf.nm"
grep -Eq ' T p2_ffi_i16_identity$' "$out/aufruf.nm"
grep -Eq ' T p2_ffi_u8_identity$' "$out/aufruf.nm"
MOO_BIN="$compiler" KERNEL_ARGS="--lib-pfad $ffi_dir" KERNEL_MARKER='-12345 213' \
    bash scripts/kernel-smoke.sh compiler/tests/p2/ffi_bare_aufruf.moos
printf 'Bare-FFI-Aufruf im Kernel (statisches Archiv, QEMU seriell): PASS\n'
