#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi-cross-windows
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_klein_roundtrip.moos --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.obj"
    mise exec -- llvm-nm "$out/$target.obj" > "$out/$target.nm"
    grep -Fq 'p2_ffi_i8_identity' "$out/$target.nm"
    grep -Fq 'p2_ffi_u8_identity' "$out/$target.nm"
    awk '/call i8 @p2_ffi_i8_identity\(i8 / {s=1} /call i8 @p2_ffi_u8_identity\(i8 / {u=1} END {if (!s || !u) exit 1}' "$out/$target.ll"
    grep -Eq '^declare i16 @p2_ffi_i16_identity\(i16\)' "$out/$target.ll"
    grep -Eq '^declare i16 @p2_ffi_u16_identity\(i16\)' "$out/$target.ll"
    mise exec -- clang --target="$target" -S -emit-llvm -O0 compiler/tests/p2/ffi_klein_roundtrip.c -o "$out/$target-clang.ll"
    awk '/^define .* i8 @p2_ffi_i8_identity\(i8 / {s=1} /^define .* i8 @p2_ffi_u8_identity\(i8 / {u=1} END {if (!s || !u) exit 1}' "$out/$target-clang.ll"
    grep -Eq '^define .* i16 @p2_ffi_i16_identity\(i16 ' "$out/$target-clang.ll"
    grep -Eq '^define .* i16 @p2_ffi_u16_identity\(i16 ' "$out/$target-clang.ll"
done
timeout 120s "$compiler" compile compiler/tests/p2/ffi_klein_roundtrip.moos --target x86_64-pc-windows-gnu --lib-pfad "$out" -o "$out/moo.obj" >"$out/cross.log" 2>&1
grep -Fq 'p2_ffi_klein' "$out/cross.log"
mise exec -- x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_klein_roundtrip.c -o "$out/ffi_ref.o"
mise exec -- x86_64-w64-mingw32-ar rcs "$out/libp2_ffi_klein.a" "$out/ffi_ref.o"
mise exec -- x86_64-w64-mingw32-ld -r -o "$out/zusammen.obj" "$out/moo.obj" -L "$out" -lp2_ffi_klein
mise exec -- llvm-nm "$out/zusammen.obj" > "$out/zusammen.nm"
grep -Eq ' T p2_ffi_i8_identity$' "$out/zusammen.nm"
grep -Eq ' T p2_ffi_u8_identity$' "$out/zusammen.nm"
if grep -Eq ' U p2_ffi_[iu]8_identity$' "$out/zusammen.nm"; then
    echo 'C-Referenzsymbole bleiben unaufgeloest' >&2
    exit 1
fi
# P2-T43: @bibliothek als eingebettete Link-Direktive. MSVC-COFF traegt
# /DEFAULTLIB:<name>.lib (einmal) in .drectve; GNU-COFF bleibt ohne, weil GNU ld
# .drectve-Bibliotheken ignoriert (dort uebergibt der Treiber -l).
msvc=x86_64-pc-windows-msvc
[ "$(grep -c '/DEFAULTLIB:p2_ffi_klein.lib' "$out/$msvc.ll")" -eq 1 ] || { echo 'MSVC-IR: Link-Direktive fehlt oder doppelt' >&2; exit 1; }
if grep -Fq 'llvm.linker.options' "$out/x86_64-pc-windows-gnu.ll"; then
    echo 'GNU-COFF darf keine Link-Direktive tragen' >&2; exit 1
fi
mise exec -- llvm-readobj --coff-directives "$out/$msvc.obj" > "$out/msvc.directives"
grep -Fq '/DEFAULTLIB:p2_ffi_klein.lib' "$out/msvc.directives"
# lld-link findet die Bibliothek allein ueber die Direktive (+ /libpath), ohne -l.
# Moo-Runtime-Symbole bleiben offen (/force:unresolved), geprueft wird nur FFI.
mise exec -- clang --target="$msvc" -c compiler/tests/p2/ffi_klein_roundtrip.c -o "$out/ffi_ref_msvc.obj"
mise exec -- llvm-lib /out:"$out/p2_ffi_klein.lib" "$out/ffi_ref_msvc.obj"
link_msvc() { # $1 = Log, Rest = Zusatzargumente
    local log="$1"; shift
    mise exec -- lld-link /force:unresolved /entry:main /subsystem:console /nodefaultlib:libcmt \
        /nodefaultlib:oldnames "$@" "$out/$msvc.obj" /out:"$out/msvc_probe.exe" >"$log" 2>&1 || true
}
link_msvc "$out/lld-mit.log" /libpath:"$out"
if grep -Eq 'undefined symbol: p2_ffi_' "$out/lld-mit.log"; then
    cat "$out/lld-mit.log" >&2; echo 'lld-link: Direktive loeste p2_ffi_klein nicht auf' >&2; exit 1
fi
grep -Eq 'undefined symbol: moo_' "$out/lld-mit.log" || { cat "$out/lld-mit.log" >&2; echo 'lld-link-Probe unplausibel' >&2; exit 1; }
link_msvc "$out/lld-ohne.log"
grep -Eq 'p2_ffi_klein.lib|undefined symbol: p2_ffi_' "$out/lld-ohne.log" || { cat "$out/lld-ohne.log" >&2; echo 'Negativkontrolle ohne Suchpfad griff nicht' >&2; exit 1; }
printf 'MSVC-COFF /DEFAULTLIB-Direktive (lld-link ohne -l): PASS\n'
printf 'Windows x64 FFI IR/COFF und GNU-Archivlink: PASS (kein Windows-Lauf)\n'
