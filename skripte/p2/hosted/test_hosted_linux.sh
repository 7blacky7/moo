#!/usr/bin/env bash
# P2-T66: hosted x86_64-Linux Systemkoerper (Zeiger, Strukturwerte, mehrere
# Anweisungen) ohne Bare-Triple. Positiv: IR-Verify ueber die hosted-Route,
# echtes Executable, C-gelinkte Exporte, Mat4-Pilot bitgleich gegen
# moo_3d_math.c. Negativ: Usermode-Sperren (Port-I/O, volatile), Zeigerfunktion
# ohne unsicher, dyn->native-Aufruf (Stufe 2). FFI-Einzelaufruf bleibt FFI-Pfad.
# Der Mat4-Differenzlauf (7) isoliert die Exporte per llvm-extract aus der
# --emit-ir-Ausgabe (vor den Paessen) und optimiert sie selbst mit opt -O2.
# Schritt 8 prueft das echte Produktobjekt aus write_object: hosted x86_64 mit
# Systemkoerpern faehrt seit P2-T66 selektiv IR-O2 (vektorisierte Mat4-
# Multiplikation), waehrend ein hosted Programm ohne Systemkoerper (FFI-Einzel-
# aufruf) weiter ohne IR-Paesse uebersetzt wird.
# Argumente gehen an die Mat4-Probe: [runden] [bench_n] [bench_laeufe]
set -euo pipefail

hosted_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$hosted_dir/../../.." && pwd)"
out="$project_dir/build/p2-hosted"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
grafik="$project_dir/skripte/p2/grafik"
mkdir -p -- "$out"

# 1. hosted-Route: Triple linux-gnu, Systemkoerper mit sret-Signatur, Zeiger-Export.
"$compiler" compile "$hosted_dir/probe_hosted.moos" --target x86_64 --emit-ir -o "$out/probe.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.ll"
grep -q '^target triple = "x86_64-unknown-linux-gnu"' "$out/probe.ll"
grep -qE '^define internal void @"?__moo_native\.vertauscht"?\(ptr noalias sret' "$out/probe.ll"
grep -qE '^define dso_local i32 @hosted_summe\(ptr' "$out/probe.ll"
# Einaufruf-extern-C mit Zeigern: Systemkoerper (sys.extern), nicht FFI-Pfad.
grep -qE '^define dso_local ptr @hosted_fuellen\(ptr' "$out/probe.ll"
sed -n '/^define dso_local ptr @hosted_fuellen(/,/^}/p' "$out/probe.ll" | grep -q 'call ptr @memset' \
    || { echo "FEHLER: hosted_fuellen ruft memset nicht im Systemkoerper" >&2; exit 1; }
# 8/16-Bit-extern-Parameter/-Rueckgaben: Aufrufstelle traegt signext/zeroext (SysV).
klein="$(sed -n '/^define dso_local i32 @hosted_klein(/,/^}/p' "$out/probe.ll")"
for muster in 'call signext i8 @p2_ffi_i8_identity(i8 signext' 'call zeroext i8 @p2_ffi_u8_identity(i8 zeroext' \
              'call signext i16 @p2_ffi_i16_identity(i16 signext' 'call zeroext i16 @p2_ffi_u16_identity(i16 zeroext'; do
    grep -qF -- "$muster" <<<"$klein" || { echo "FEHLER: Extension-Attribut fehlt: $muster" >&2; exit 1; }
done
echo "PASS hosted-Route: IR verify, Triple, sret-Systemkoerper, Zeiger-Export, extern-C-Zeiger-Einaufruf, signext/zeroext"

# 2. Echtes hosted Executable: Moo-main und Systemkoerper im selben Programm.
#    C-Orakel fuer die 8/16-Bit-Roundtrips (read-only aus compiler/tests/p2).
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -c "$project_dir/compiler/tests/p2/ffi_klein_roundtrip.c" -o "$out/ffi_klein.o"
ar rcs "$out/libp2_ffi_klein.a" "$out/ffi_klein.o"
"$compiler" compile "$hosted_dir/probe_hosted.moos" --lib-pfad "$out" -o "$out/probe_exe"
"$out/probe_exe" > "$out/probe_exe.out"
grep -qx 'hosted ok' "$out/probe_exe.out"
# Stufe 2a: dyn-Aufruf des skalaren Systemkoerpers hosted_klein (-5+200-300+60000).
grep -qx '59895' "$out/probe_exe.out" || { cat "$out/probe_exe.out" >&2; echo "FEHLER: dyn-Aufruf hosted_klein fehlt" >&2; exit 1; }
# Variante (b): Aufruf aus benannter Funktion mit Parameter-Argument (5+200-300+60000).
grep -qx '59905' "$out/probe_exe.out" || { cat "$out/probe_exe.out" >&2; echo "FEHLER: dyn-Aufruf aus benannter Funktion fehlt" >&2; exit 1; }
echo "PASS hosted Executable gelinkt und ausgefuehrt, dyn-Aufruf skalarer Systemkoerper aus main und benannter Funktion"

# 3. Exporte aus der hosted IR mit C-Aufrufer linken und ausfuehren (Zeiger + Strukturwert).
# --keep-const-init: referenzierte konstante Globals (Fehlertexte) behalten ihre Initialisierer,
# llvm-extract macht sie sonst zu offenen Deklarationen.
mise exec -- llvm-extract -S --func=hosted_summe --func=hosted_tausche --func=hosted_fuellen --func=hosted_klein --recursive --keep-const-init "$out/probe.ll" -o "$out/probe.extract.ll"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/probe.extract.ll" -o "$out/probe.o"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror "$hosted_dir/probe_hosted_main.c" "$out/probe.o" "$out/libp2_ffi_klein.a" -o "$out/probe_c"
"$out/probe_c"

# 4. Ein einzelner extern-C-Aufruf bleibt im FFI-Pfad (kein Systemkoerper) und laeuft.
"$compiler" compile "$hosted_dir/ffi_einzel.moos" --emit-ir -o "$out/ffi.ll"
if sed -n '/@"\?__moo_native\.betrag"\?(/,/^}/p' "$out/ffi.ll" | grep -q 'sys\.start'; then
    echo "FEHLER: FFI-Einzelaufruf im Systemkoerper statt FFI-Pfad" >&2; exit 1
fi
"$compiler" compile "$hosted_dir/ffi_einzel.moos" -o "$out/ffi_exe"
"$out/ffi_exe" | grep -qx '5'
echo "PASS FFI-Einzelaufruf bleibt FFI-Pfad"

# 5. Negativfaelle: Usermode-Sperren und nicht freigegebene Formen.
expect_fail() {  # Datei Muster [weitere compile-Argumente]
    local src="$1" muster="$2"; shift 2
    if "$compiler" compile "$src" --emit-ir -o "$out/fail.ll" "$@" >"$out/fail.log" 2>&1; then
        echo "Unerwartet akzeptiert: $src" >&2; exit 1
    fi
    grep -q -F -- "$muster" "$out/fail.log" || { cat "$out/fail.log" >&2; echo "Falsche Diagnose fuer $src" >&2; exit 1; }
}
expect_fail "$hosted_dir/negativ_port.moos" 'nur im Bare-Profil' --target x86_64
expect_fail "$hosted_dir/negativ_volatile.moos" 'nur im Bare-Profil' --target x86_64
expect_fail "$hosted_dir/negativ_zeiger_ohne_unsicher.moos" 'nur innerhalb von unsicher:' --target x86_64
expect_fail "$hosted_dir/negativ_dyn_aufruf.moos" 'Nativer Zeigerfunktionsaufruf'
# Stufe 2a: skalarer Systemkoerper aus dyn nur in unsicher:, in main oder benannter Funktion.
expect_fail "$hosted_dir/negativ_dyn_ohne_unsicher.moos" 'nur innerhalb von unsicher:'
# Variante (b): benannte Funktionen ja, nicht aus Methoden/Lambdas.
expect_fail "$hosted_dir/negativ_dyn_in_lambda.moos" 'keine Lambdas/Methoden'
echo "PASS Negativfaelle: Port-I/O, volatile, Zeiger ohne unsicher, dyn->native ptr, dyn ohne unsicher, aus Methode"

# 5a. P2-T74: Aufruf in versuche: (main und benannte Funktion) ist erlaubt, der
#     Fehler im Koerper (moo_native_fehler) landet im fange-Zweig.
"$compiler" compile "$hosted_dir/positiv_dyn_in_try.moos" -o "$out/dyn_try_exe"
"$out/dyn_try_exe" > "$out/dyn_try.out" 2> "$out/dyn_try.err"
printf '3\ngefangen\nweiter\n' | cmp - "$out/dyn_try.out" || { cat "$out/dyn_try.out" "$out/dyn_try.err" >&2; echo "FEHLER: Systemkoerper in versuche: (main)" >&2; exit 1; }
"$compiler" compile "$hosted_dir/positiv_dyn_in_funktion.moos" -o "$out/dyn_funktion_exe"
"$out/dyn_funktion_exe" > "$out/dyn_funktion.out" 2> "$out/dyn_funktion.err"
printf '3\n-1\n' | cmp - "$out/dyn_funktion.out" || { cat "$out/dyn_funktion.out" "$out/dyn_funktion.err" >&2; echo "FEHLER: Systemkoerper in versuche: (benannte Funktion)" >&2; exit 1; }
echo "PASS P2-T74 Systemkoerper in versuche: Fehler gefangen in main und benannter Funktion"

# 5b. Fehler im hosted Systemkoerper (P2-T34, ADR §3): moo_native_fehler statt
#     llvm.trap; ohne Handler beendet moo_throw den Prozess mit RC 1 und
#     Diagnose ("Unbehandelter Fehler: ..."), kein 'NICHT ERREICHT'.
"$compiler" compile "$hosted_dir/probe_trap.moos" -o "$out/trap_exe"
set +e
"$out/trap_exe" > "$out/trap.out" 2> "$out/trap.err"
trap_rc=$?
set -e
[ "$trap_rc" -eq 1 ] || { echo "FEHLER: Fehler im Systemkoerper lieferte RC $trap_rc statt 1" >&2; exit 1; }
grep -qF "Nativer Laufzeitfehler (Division, Shift, Index oder Konversion) in 'teile'" "$out/trap.out" "$out/trap.err" \
    || { cat "$out/trap.out" "$out/trap.err" >&2; echo "FEHLER: Diagnose des Systemkoerper-Fehlers fehlt" >&2; exit 1; }
if grep -q 'NICHT ERREICHT' "$out/trap.out"; then
    cat "$out/trap.out" >&2; echo "FEHLER: Code nach dem Systemkoerper-Fehler lief" >&2; exit 1
fi
echo "PASS Fehler im hosted Systemkoerper: RC 1, Diagnose, kein Fortlauf (statt Trap)"

# 6. Windows hosted ueber dieselbe Route (nur IR-Verify, kein Lauf).
"$compiler" compile "$hosted_dir/probe_hosted.moos" --target x86_64-pc-windows-gnu --emit-ir -o "$out/probe.win.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.win.ll"
echo "PASS Windows-hosted IR verify"

# 7. Mat4-Pilot als hosted Linux-Objekt bitgleich gegen die C-Referenz.
"$compiler" compile "$grafik/mat4_nativ.moos" --target x86_64 --emit-ir -o "$out/mat4.ll"
mise exec -- opt -passes=verify -disable-output "$out/mat4.ll"
if grep -qE 'f(mul|add|sub|div|neg) (fast|contract|reassoc|nnan|ninf|nsz|arcp|afn)|fmuladd' "$out/mat4.ll"; then
    echo "FEHLER: Fast-Math/Kontraktion im Moo-IR" >&2; exit 1
fi
sel=""
for f in moo_n_mat4_identity moo_n_mat4_multiply moo_n_mat4_translate moo_n_mat4_scale moo_n_mat4_ortho \
         moo_n_mat4_perspective moo_n_mat4_lookat moo_n_mat4_rotate moo_n_mat4_multiply_skalar \
         moo_n_mat4_translate_skalar moo_n_mat4_scale_skalar moo_n_mat4_rotate_skalar \
         moo_n_matrix_stack_init moo_n_matrix_stack_push moo_n_matrix_stack_pop; do
    sel="$sel --func=$f"
done
mise exec -- llvm-extract -S $sel --recursive --keep-const-init "$out/mat4.ll" -o "$out/mat4.extract.ll"
mise exec -- opt -passes='default<O2>' -S "$out/mat4.extract.ll" -o "$out/mat4.o2.ll"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/mat4.o2.ll" -o "$out/mat4.o"
offen="$(mise exec -- llvm-nm -u "$out/mat4.o" | awk '{print $2}' | grep -vxE 'sinf|cosf|tanf|sqrtf' || true)"
[ -z "$offen" ] || { echo "FEHLER: unerwartete offene Symbole: $offen" >&2; exit 1; }
${CC:-cc} -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -I"$project_dir/compiler/runtime" \
    "$grafik/test_mat4_nativ.c" "$project_dir/compiler/runtime/moo_3d_math.c" \
    "$out/mat4.o" -lm -o "$out/test_mat4_hosted"
"$out/test_mat4_hosted" "$@"

# 8. Produktobjekt aus write_object (hosted Cross-Pfad, kein externes opt):
#    Mat4-Multiplikation muss vektorisiert sein (mulps), die Exporte muessen
#    erhalten bleiben. Kontrolle: das hosted Programm ohne Systemkoerper
#    (ffi_einzel) wird ohne IR-Paesse uebersetzt (main unveraendert vorhanden,
#    Laufzeitaufrufe intakt) und laeuft weiterhin.
"$compiler" compile "$grafik/mat4_nativ.moos" --target x86_64 -o "$out/mat4_produkt.o"
mulps="$(mise exec -- llvm-objdump -d --no-show-raw-insn --disassemble-symbols=moo_n_mat4_multiply "$out/mat4_produkt.o" | grep -c 'mulps' || true)"
[ "$mulps" -gt 0 ] || { echo "FEHLER: Produktobjekt ohne vektorisierte Mat4-Multiplikation (mulps=$mulps)" >&2; exit 1; }
for f in moo_n_mat4_multiply moo_n_matrix_stack_push; do
    mise exec -- llvm-nm --defined-only "$out/mat4_produkt.o" | grep -q " T $f\$" || { echo "FEHLER: Export $f fehlt im Produktobjekt" >&2; exit 1; }
done
"$compiler" compile "$hosted_dir/probe_hosted.moos" --target x86_64 -o "$out/probe_produkt.o"
mise exec -- llvm-nm --defined-only "$out/probe_produkt.o" | grep -q ' T main$'
# Im Objekt stehen Call-Ziele nur als Relocations (R_X86_64_PLT32), nicht im
# Disassembly-Text; deshalb -dr und die realen Laufzeitsymbole von main pruefen.
main_dis="$(mise exec -- llvm-objdump -dr --no-show-raw-insn --disassemble-symbols=main "$out/probe_produkt.o")"
for sym in moo_string_new moo_print moo_release; do
    grep -qE "R_X86_64_(PLT32|PC32)[[:space:]]+$sym" <<<"$main_dis" \
        || { echo "FEHLER: dynamisches main ohne Laufzeitaufruf $sym im Produktobjekt" >&2; exit 1; }
done
echo "PASS Produktobjekt: Mat4 vektorisiert (mulps=$mulps), Exporte erhalten, dynamisches main mit Laufzeitaufrufen"
# 9. Hosted aarch64-Linux ueber dieselbe Route: IR-Verify, Produktobjekt aus
#    write_object (mit O2) und Exporte; kein AArch64-Lauf (kein Zielsystem),
#    Muster skripte/p2/ffi/cross_aarch64.sh.
"$compiler" compile "$hosted_dir/probe_hosted.moos" --target aarch64-unknown-linux-gnu --emit-ir -o "$out/probe.aarch64.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.aarch64.ll"
grep -q '^target triple = "aarch64-unknown-linux-gnu"' "$out/probe.aarch64.ll"
"$compiler" compile "$hosted_dir/probe_hosted.moos" --target aarch64-unknown-linux-gnu -o "$out/probe.aarch64.o"
mise exec -- llvm-readobj --file-headers "$out/probe.aarch64.o" | grep -Fq 'EM_AARCH64'
for f in hosted_summe hosted_tausche hosted_fuellen hosted_klein main; do
    mise exec -- llvm-nm --defined-only "$out/probe.aarch64.o" | grep -q " T $f\$" || { echo "FEHLER: aarch64-Symbol $f fehlt" >&2; exit 1; }
done
echo "PASS hosted aarch64-Linux: IR verify, Produktobjekt EM_AARCH64, Exporte (kein Lauf)"
echo "PASS hosted x86_64-Linux: Systemkoerper (Zeiger/Struktur/Mehrstatement), Executable, C-Link, FFI-Pfad, Negativfaelle, Windows-IR, Mat4 bitgleich, Produktobjekt-O2, aarch64-IR/Objekt"
