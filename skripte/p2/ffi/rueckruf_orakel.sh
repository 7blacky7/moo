#!/usr/bin/env bash
# P2-T21/T57 Batch E (Plan f4655d20 E-4/E-5): define-Zeile jedes @rückruf-Thunks
# (__moo_rueckruf.<name>, interne Linkage) gegen Clangs Definition derselben
# C-Signatur je Ziel: x86_64-linux-gnu, x86_64-windows-msvc/-gnu,
# aarch64-linux-gnu, x86_64-uefi. Normalisierung wie abi_matrix.sh (Wertnamen,
# noundef/dead_on_*/writable, %struct.NAME -> Typkoerper) plus Thunkname/internal.
# Dazu: AArch64-Objekt (kein Ziellauf) und UEFI-Objekt ohne offene moo_*-Symbole
# (Bare/UEFI: kein Runtime-Hook, auch nicht fuer den fehlbaren r_teile).
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
src=compiler/tests/p2/rueckruf_orakel.moos
out=build/p2-ffi/rueckruf-orakel
mkdir -p "$out"

norm() {
    sed -E -e 's/^(declare|define) (dso_local |internal )?//' -e 's/ #[0-9]+//' -e 's/ \{$//' \
        -e 's/ %[0-9]+//g' -e 's/ (noundef|dead_on_unwind|writable|dead_on_return)//g' \
        -e 's/@"?__moo_rueckruf\.([A-Za-z0-9_]+)"?/@\1/'
}
for ziel in x86_64-unknown-linux-gnu x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu x86_64-unknown-uefi; do
    extra=()
    [[ "$ziel" == *uefi ]] && extra=(--no-stdlib)
    "$compiler" compile "$src" --target "$ziel" "${extra[@]}" --emit-ir -o "$out/$ziel.moo.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$ziel.moo.ll"
    mise exec -- clang --target="$ziel" -ffreestanding -std=c11 -O0 -Wall -Wextra -Werror \
        -S -emit-llvm skripte/p2/ffi/rueckruf_orakel.c -o "$out/$ziel.c.ll"
    sed -nE 's/^%struct\.([A-Za-z0-9_]+) = type (.*)$/s@%struct[.]\1([^A-Za-z0-9_])@\2\\1@g/p' \
        "$out/$ziel.c.ll" > "$out/$ziel.struct.sed"
    grep -E '^define .*@r_' "$out/$ziel.c.ll" | sed -E -f "$out/$ziel.struct.sed" | norm | sort > "$out/$ziel.c.sig"
    grep -E '^define internal .*@"?__moo_rueckruf\.' "$out/$ziel.moo.ll" | norm | sort > "$out/$ziel.moo.sig"
    [ "$(wc -l < "$out/$ziel.c.sig")" -eq 10 ] || { echo "FEHLER: Clang-Orakel $ziel unvollstaendig" >&2; exit 1; }
    if ! diff -u "$out/$ziel.c.sig" "$out/$ziel.moo.sig"; then
        echo "FAIL Rueckruf-Orakel $ziel" >&2
        exit 1
    fi
    printf 'PASS Rueckruf-Orakel %s: %s Thunks\n' "$ziel" "$(wc -l < "$out/$ziel.c.sig")"
done

mise exec -- llc -O2 -filetype=obj "$out/aarch64-unknown-linux-gnu.moo.ll" -o "$out/aarch64.o"
mise exec -- llvm-readobj --file-headers "$out/aarch64.o" | grep -Fq 'EM_AARCH64'
mise exec -- llc -O2 -filetype=obj "$out/x86_64-unknown-uefi.moo.ll" -o "$out/uefi.o"
if mise exec -- llvm-nm -u "$out/uefi.o" | grep -q 'moo_'; then
    mise exec -- llvm-nm -u "$out/uefi.o" >&2
    echo 'FEHLER: UEFI-Rueckruf-Objekt mit offenen moo_*-Symbolen' >&2
    exit 1
fi
! grep -q 'moo_native_rueckruf_thread' "$out/x86_64-unknown-uefi.moo.ll" || { echo 'FEHLER: UEFI-Thunk mit Runtime-Hook' >&2; exit 1; }
echo 'PASS Rueckruf AArch64-Objekt (nicht ausgefuehrt), UEFI-Objekt ohne offene moo_*-Symbole'
