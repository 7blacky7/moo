#!/usr/bin/env bash
# P2-T19/T56 Batch E (Plan 91033131 E1-E3, Nachzug Review 1e51cd93): Struktur-ABI-Matrix.
# Formen: {f32x3} 12 B, {u64,u64}, 24 B, {u8,u16}, 8 B {u32,u32}, {ptr,u32}, {f32,u32},
# verschachtelt+Arrayfeld, 3/5/6/7 Byte, HFA {f64}/{f64,f64}/{f32x4}/{f64x4}; je Form
# Rueckgabe+Einzelargument in beide Richtungen, dazu verschiedene Strukturen unter
# gemischtem u64/f64/ptr-Druck, Skalarrueckgabe mit Strukturargument, bool, void.
# 1. Linux hosted (x86_64-unknown-linux-gnu): Moo -> C und C -> Moo echt ausgefuehrt,
#    C-Seite zusaetzlich unter ASan/UBSan.
# 2. Clang-Signaturorakel je Ziel (linux-gnu, windows-msvc/-gnu, aarch64-linux, UEFI):
#    Moo-declare (Moo -> C) und Export-define (C -> Moo) gegen Clangs Definitionen
#    derselben C-Signaturen. Normalisiert werden nur Wertnamen, noundef/dead_on_*/
#    writable und %struct.NAME (durch den Typkoerper ersetzt).
# 3. AArch64-Objekt mit allen Exporten (kein Ziellauf), Windows-Lauf: windows_minipc.
# 4. Negativ (E3): ueberausgerichtet, Array by value, variadisch -> Diagnose.
#    Unions und Vektortypen gibt es in Moo nicht (nicht ausdrueckbar).
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
src=skripte/p2/abi
out=build/p2-ffi/abi-matrix
mkdir -p "$out"
exporte='^(probe_|moo_(id_|v3_|g24_|druck|k4|misch|setze))'

"$compiler" compile "$src/abi_matrix.moos" --target x86_64 --emit-ir -o "$out/linux.ll"
mise exec -- opt -passes=verify -disable-output "$out/linux.ll"
mise exec -- llvm-extract -S --rfunc="$exporte" --recursive --keep-const-init "$out/linux.ll" -o "$out/linux.x.ll"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/linux.x.ll" -o "$out/moo.o"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror "$src/abi_matrix.c" "$out/moo.o" -o "$out/matrix"
"$out/matrix"
mise exec -- clang -std=c11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
    -fno-sanitize-recover=all -Wall -Wextra -Werror "$src/abi_matrix.c" "$out/moo.o" -o "$out/matrix_san"
"$out/matrix_san"
echo 'PASS ABI-Matrix Linux unter ASan/UBSan (C-Seite)'

norm() {
    sed -E -e 's/^(declare|define) (dso_local )?//' -e 's/ #[0-9]+//' -e 's/ \{$//' \
        -e 's/ %[0-9]+//g' -e 's/ (noundef|dead_on_unwind|writable|dead_on_return)//g'
}
for ziel in x86_64-unknown-linux-gnu x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu x86_64-unknown-uefi; do
    extra=()
    [[ "$ziel" == *uefi ]] && extra=(--no-stdlib)
    "$compiler" compile "$src/abi_matrix.moos" --target "$ziel" "${extra[@]}" --emit-ir -o "$out/$ziel.moo.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$ziel.moo.ll"
    mise exec -- clang --target="$ziel" -ffreestanding -std=c11 -O0 -DABI_MATRIX_ORAKEL \
        -S -emit-llvm "$src/abi_matrix.c" -o "$out/$ziel.c.ll"
    sed -nE 's/^%struct\.([A-Za-z0-9_]+) = type (.*)$/s@%struct[.]\1([^A-Za-z0-9_])@\2\\1@g/p' \
        "$out/$ziel.c.ll" > "$out/$ziel.struct.sed"
    grep -E '^define .*@(abi_|probe_|moo_)' "$out/$ziel.c.ll" \
        | sed -E -f "$out/$ziel.struct.sed" | norm | sort > "$out/$ziel.c.sig"
    { grep -E '^declare .*@abi_' "$out/$ziel.moo.ll"; grep -E '^define [^@]*@(probe_|moo_)' "$out/$ziel.moo.ll" | grep -vE '@moo_native_'; } \
        | norm | sort > "$out/$ziel.moo.sig"
    if ! diff -u "$out/$ziel.c.sig" "$out/$ziel.moo.sig"; then
        echo "FAIL Clang-Orakel $ziel" >&2
        exit 1
    fi
    printf 'PASS Clang-Orakel %s: %s Signaturen\n' "$ziel" "$(wc -l < "$out/$ziel.c.sig")"
done

mise exec -- llc -O2 -filetype=obj "$out/aarch64-unknown-linux-gnu.moo.ll" -o "$out/aarch64.o"
mise exec -- llvm-readobj --file-headers "$out/aarch64.o" | grep -Fq 'EM_AARCH64'
mise exec -- llvm-nm --defined-only "$out/aarch64.o" > "$out/aarch64.nm"
anzahl=0
for f in $(grep -oE '^define [^@]*@(probe|moo)_[A-Za-z0-9_]+' "$out/aarch64-unknown-linux-gnu.moo.ll" | sed 's/.*@//' | grep -v '^moo_native_'); do
    grep -Eq " T $f\$" "$out/aarch64.nm" || { echo "FAIL AArch64-Objekt ohne Export $f" >&2; exit 1; }
    anzahl=$((anzahl + 1))
done
echo "PASS AArch64-Objekt EM_AARCH64 mit $anzahl Exporten (kein Ziellauf)"

negativ() {  # Datei erwartete-Diagnose
    if "$compiler" compile "$1" --emit-ir -o "$out/negativ.ll" > "$out/negativ.log" 2>&1; then
        echo "FAIL unerwartet akzeptiert: $1" >&2; exit 1
    fi
    grep -qF "$2" "$out/negativ.log" || { cat "$out/negativ.log" >&2; echo "FAIL falsche Diagnose: $1" >&2; exit 1; }
}
negativ compiler/tests/p2/ffi_struktur_ueberausgerichtet.moos 'ueberausgerichtete Strukturen'
negativ compiler/tests/p2/ffi_aggregat.moos 'Aggregate und Funktionstypen by value'
negativ compiler/tests/p2/ffi_varargs.moos 'Variadische extern-C-Signaturen'
echo 'PASS E3-Negativfaelle: ueberausgerichtet, Array by value, variadisch'
echo 'PASS Struktur-ABI-Matrix: Linux-Lauf (+ASan/UBSan) + Clang-Orakel (linux-gnu, windows-msvc/-gnu, aarch64-linux, UEFI) + AArch64-Objekt + Negativ'
