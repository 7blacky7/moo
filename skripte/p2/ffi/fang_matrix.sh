#!/usr/bin/env bash
# P2-T55 (Plan f5b52a0b A6): FFI-Einzelform hosted fangbar. Matrix
# compiler/tests/p2/ffi_fang_matrix.moos gegen .expected (C-Aufrufmarken aus
# ffi_i64.c: Argumentfehler erreichen C nicht, Ergebnisfehler genau ein
# C-Aufruf; aufraeumen genau einmal; kein Folgeausdruck/keine Folgezeile).
# A3: der FFI-Wrapper prueft bei fehlbaren Rueckrufen nach dem C-Aufruf das
# Fehlerflag (IR-Probe). Dazu derselbe Lauf unter ASan/UBSan mit Leckpruefung.
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
compiler="$(realpath "$compiler")"
out=build/p2-ffi/fang_matrix
t=compiler/tests/p2
mkdir -p "$out/lib" "$out/san"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -c "$t/ffi_i64.c" -o "$out/lib/i64.o"
ar rcs "$out/lib/libp2_i64.a" "$out/lib/i64.o"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -pthread -c skripte/p2/ffi/rueckruf_gegenstelle.c -o "$out/lib/rr.o"
ar rcs "$out/lib/libp2_rueckruf.a" "$out/lib/rr.o"
"$compiler" compile "$t/ffi_fang_matrix.moos" --lib-pfad "$out/lib" -o "$out/matrix" > "$out/compile.log" 2>&1 \
    || { cat "$out/compile.log" >&2; echo 'FEHLER: Matrix kompiliert nicht' >&2; exit 1; }
"$out/matrix" > "$out/matrix.out"
diff -u "$t/ffi_fang_matrix.expected" "$out/matrix.out" || { echo 'FEHLER: Matrix weicht ab' >&2; exit 1; }
echo 'PASS FFI-Fangmatrix (13 Kontexte/Fehlerarten, Rueckgabe + void)'

# A3: Flagpruefung nach dem Fremdaufruf im Wrapper (Modul mit fehlbarem Rueckruf).
"$compiler" compile "$t/ffi_fang_matrix.moos" --lib-pfad "$out/lib" --emit-ir -o "$out/matrix.ll" > /dev/null
mise exec -- opt -passes=verify -disable-output "$out/matrix.ll"
wrapper="$(awk 'index($0, "define") == 1 && index($0, "@__moo_native.c_loese(") { d = 1 } d { print } d && /^}/ { exit }' "$out/matrix.ll")"
[ -n "$wrapper" ] || { echo 'FEHLER: Wrapper c_loese fehlt im IR' >&2; exit 1; }
grep -qF '@moo_native_fehler_offen()' <<<"$wrapper" && grep -qF 'ffi.fremd.fehler' <<<"$wrapper" \
    || { echo "$wrapper" >&2; echo 'FEHLER: Wrapper ohne Flagpruefung nach dem C-Aufruf' >&2; exit 1; }
echo 'PASS FFI-Wrapper prueft Rueckruf-Fehler nach dem C-Aufruf'

# ASan/UBSan inkl. LeakSanitizer (Programm linkt ueber cc; Wrapper im PATH).
clang_bin="$(mise exec -- which clang)"
printf '#!/bin/sh\nexec "%s" -fsanitize=address,undefined "$@"\n' "$clang_bin" > "$out/san/cc"
chmod +x "$out/san/cc"
mise exec -- clang -std=c11 -O1 -g -fsanitize=address,undefined -c "$t/ffi_i64.c" -o "$out/san/i64.o"
ar rcs "$out/san/libp2_i64.a" "$out/san/i64.o"
mise exec -- clang -std=c11 -O1 -g -fsanitize=address,undefined -pthread -c skripte/p2/ffi/rueckruf_gegenstelle.c -o "$out/san/rr.o"
ar rcs "$out/san/libp2_rueckruf.a" "$out/san/rr.o"
PATH="$PWD/$out/san:$PATH" "$compiler" compile "$t/ffi_fang_matrix.moos" --lib-pfad "$out/san" -o "$out/san/matrix" \
    > "$out/san/compile.log" 2>&1 || { cat "$out/san/compile.log" >&2; exit 1; }
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$out/san/matrix" > "$out/san/matrix.out"
diff -u "$t/ffi_fang_matrix.expected" "$out/san/matrix.out"
echo 'PASS FFI-Fangmatrix unter ASan/UBSan/LSan'
