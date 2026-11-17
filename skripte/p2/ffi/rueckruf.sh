#!/usr/bin/env bash
# P2-T21/T57 Batch E (Plan f4655d20 E-2/E-3/E-5): Moo-Funktionen als
# C-Funktionszeiger (@rückruf) end-to-end auf Linux hosted x86_64.
# 1. Positiv gegen .expected: rueckruf_sicher (fehlerfreie Thunks: libc-qsort,
#    user_data, void, Strukturwert, gespeichert, fremder C-Thread) und
#    rueckruf_fehler (fehlbare Thunks in versuche:, kein Koerperlauf nach dem
#    Fehler, Reentranz Tiefe 3).
# 2. Ungefangener Rueckruf-Fehler: RC 1 + Diagnose; fehlbarer Rueckruf auf
#    fremdem Thread: Diagnose + abort (kein Koerperlauf, kein moo_throw).
# 3. C-Seite unter ASan/UBSan (sicher + fehler) und der fehlerfreie
#    Fremdthread-Rueckruf unter TSan. Das Moo-Programm linkt ueber 'cc'; ein
#    Wrapper im PATH ergaenzt nur die Sanitizer-Linkflags.
# 4. Thunk-IR: interne Linkage, fehlerfreie Thunks ohne moo_native_*, fehlbare
#    mit moo_native_rueckruf_thread + moo_native_fehler_offen.
# 5. Negativ: Compilefehler mit der jeweiligen Meldung.
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
compiler="$(realpath "$compiler")"
out=build/p2-ffi/rueckruf
tests=compiler/tests/p2
mkdir -p "$out/lib" "$out/san" "$out/tsan"

# --- 1. Positiv ---
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -pthread -c skripte/p2/ffi/rueckruf_gegenstelle.c -o "$out/lib/rr.o"
ar rcs "$out/lib/libp2_rueckruf.a" "$out/lib/rr.o"
for name in rueckruf_sicher rueckruf_fehler rueckruf_ungefangen rueckruf_fremdthread; do
    "$compiler" compile "$tests/$name.moos" --lib-pfad "$out/lib" -o "$out/$name" > "$out/$name.compile.log" 2>&1 \
        || { cat "$out/$name.compile.log" >&2; echo "FEHLER: $name kompiliert nicht" >&2; exit 1; }
done
for name in rueckruf_sicher rueckruf_fehler; do
    "$out/$name" > "$out/$name.out"
    diff -u "$tests/$name.expected" "$out/$name.out" || { echo "FEHLER: $name weicht ab" >&2; exit 1; }
done
echo 'PASS Rueckruf Linux: qsort/user_data/void/Struktur/gespeichert/Fremdthread (fehlerfrei), Fehler gefangen ohne Fortlauf, Reentranz'

# --- 2. Ungefangen und fehlbar auf fremdem Thread ---
set +e
MOO_STRENG=1 "$out/rueckruf_ungefangen" > "$out/ungefangen.out" 2> "$out/ungefangen.err"; rc_u=$?  # P2-T95: Abbruch nur mit MOO_STRENG=1
"$out/rueckruf_fremdthread" > "$out/fremdthread.out" 2> "$out/fremdthread.err"; rc_f=$?
set -e
[ "$rc_u" -eq 1 ] || { cat "$out/ungefangen.out" "$out/ungefangen.err" >&2; echo "FEHLER: ungefangen RC $rc_u statt 1" >&2; exit 1; }
grep -qx 'vor' "$out/ungefangen.out" && ! grep -q 'NICHT ERREICHT' "$out/ungefangen.out" \
    || { cat "$out/ungefangen.out" >&2; echo 'FEHLER: ungefangen Ausgabe' >&2; exit 1; }
# Unbehandelte Fehler samt Fehlertext stehen auf stderr (moo_error.c).
grep -qF 'Unbehandelter Fehler' "$out/ungefangen.err" \
    && grep -qF "Nativer Laufzeitfehler (Division, Shift, Index oder Konversion) in 'teile'" "$out/ungefangen.err" \
    || { cat "$out/ungefangen.out" "$out/ungefangen.err" >&2; echo 'FEHLER: ungefangen ohne Diagnose' >&2; exit 1; }
[ "$rc_f" -ge 128 ] || { cat "$out/fremdthread.err" >&2; echo "FEHLER: Fremdthread RC $rc_f statt abort" >&2; exit 1; }
grep -qF "Rückruf 'teile' auf fremdem Thread: fehlbare Rückrufe sind nur auf Moo-Threads erlaubt" "$out/fremdthread.err" \
    || { cat "$out/fremdthread.err" >&2; echo 'FEHLER: Fremdthread ohne Diagnose' >&2; exit 1; }
! grep -q 'NICHT ERREICHT' "$out/fremdthread.out" || { echo 'FEHLER: Fremdthread lief weiter' >&2; exit 1; }
echo "PASS Rueckruf ungefangen RC 1 mit Diagnose; fehlbar auf fremdem Thread abgelehnt (RC $rc_f)"

# --- 3. Sanitizer ---
mise exec -- clang -std=c11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -pthread -c skripte/p2/ffi/rueckruf_gegenstelle.c -o "$out/san/rr.o"
ar rcs "$out/san/libp2_rueckruf.a" "$out/san/rr.o"
clang_bin="$(mise exec -- which clang)"
printf '#!/bin/sh\nexec "%s" -fsanitize=address,undefined "$@"\n' "$clang_bin" > "$out/san/cc"
chmod +x "$out/san/cc"
for name in rueckruf_sicher rueckruf_fehler; do
    PATH="$PWD/$out/san:$PATH" "$compiler" compile "$tests/$name.moos" --lib-pfad "$out/san" -o "$out/san/$name" \
        > "$out/san/$name.compile.log" 2>&1 || { cat "$out/san/$name.compile.log" >&2; exit 1; }
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$out/san/$name" > "$out/san/$name.out"
    diff -u "$tests/$name.expected" "$out/san/$name.out"
done
mise exec -- clang -std=c11 -O1 -g -fsanitize=thread -Wall -Wextra -Werror -pthread \
    -c skripte/p2/ffi/rueckruf_gegenstelle.c -o "$out/tsan/rr.o"
ar rcs "$out/tsan/libp2_rueckruf.a" "$out/tsan/rr.o"
printf '#!/bin/sh\nexec "%s" -fsanitize=thread "$@"\n' "$clang_bin" > "$out/tsan/cc"
chmod +x "$out/tsan/cc"
PATH="$PWD/$out/tsan:$PATH" "$compiler" compile "$tests/rueckruf_sicher.moos" --lib-pfad "$out/tsan" -o "$out/tsan/sicher" \
    > "$out/tsan/compile.log" 2>&1 || { cat "$out/tsan/compile.log" >&2; exit 1; }
TSAN_OPTIONS=halt_on_error=1 "$out/tsan/sicher" > "$out/tsan/sicher.out"
diff -u "$tests/rueckruf_sicher.expected" "$out/tsan/sicher.out"
echo 'PASS Rueckruf C-Seite unter ASan/UBSan (sicher, fehler) und TSan (fehlerfreier Fremdthread-Rueckruf)'

# --- 4. Thunk-IR ---
thunk() { awk -v n="@__moo_rueckruf.$2(" 'index($0, "define") == 1 && index($0, n) { d = 1 } d { print } d && /^}/ { exit }' "$1"; }
"$compiler" compile "$tests/rueckruf_sicher.moos" --emit-ir -o "$out/sicher.ll"
"$compiler" compile "$tests/rueckruf_fehler.moos" --emit-ir -o "$out/fehler.ll"
mise exec -- opt -passes=verify -disable-output "$out/sicher.ll"
mise exec -- opt -passes=verify -disable-output "$out/fehler.ll"
for f in vergleiche besuche melde tausche verdopple; do
    t="$(thunk "$out/sicher.ll" "$f")"
    head -n1 <<< "$t" | grep -q '^define internal ' || { echo "FEHLER: Thunk $f fehlt/nicht intern" >&2; exit 1; }
    ! grep -q 'moo_native_' <<< "$t" || { echo "FEHLER: fehlerfreier Thunk $f beruehrt Runtime" >&2; exit 1; }
done
for f in vergleiche vergleiche_teilend zaehle melde tiefer; do
    t="$(thunk "$out/fehler.ll" "$f")"
    head -n1 <<< "$t" | grep -q '^define internal ' || { echo "FEHLER: Thunk $f fehlt/nicht intern" >&2; exit 1; }
    grep -q 'call void @moo_native_rueckruf_thread' <<< "$t" && grep -q 'call i32 @moo_native_fehler_offen' <<< "$t" \
        || { echo "FEHLER: fehlbarer Thunk $f ohne Thread-/Fehlerpruefung" >&2; exit 1; }
done
! grep -q 'moo_native_rueckruf_thread' "$out/sicher.ll" || { echo 'FEHLER: Modul ohne fehlbare Rueckrufe deklariert den Thread-Hook' >&2; exit 1; }
echo 'PASS Rueckruf-Thunks: intern; fehlerfrei ohne Runtime; fehlbar mit Thread- und Fehlerpruefung'

# --- 5. Negativ ---
neg() {
    local name="$1" text="$2" rc
    set +e
    timeout 30s "$compiler" compile "$tests/rueckruf_neg_$name.moos" --emit-ir -o "$out/neg_$name.ll" > "$out/neg_$name.log" 2>&1
    rc=$?
    set -e
    if (( rc == 0 || rc == 124 || rc >= 128 )); then
        cat "$out/neg_$name.log" >&2; echo "FEHLER: Negativfall $name Exit $rc" >&2; exit 1
    fi
    grep -qF "$text" "$out/neg_$name.log" || { cat "$out/neg_$name.log" >&2; echo "FEHLER: Negativfall $name ohne Meldung" >&2; exit 1; }
}
neg ohne_deko "'f' ist kein C-Rückruf - die Funktion braucht @rückruf"
neg signatur "Rückruf 'f' hat die Signatur fn(i64, ptr[void]) -> i32, erwartet wird fn(i32, ptr[void]) -> i32"
neg pointee "Rückruf 'f' hat die Signatur fn(i32, ptr[i32]) -> i32, erwartet wird fn(i32, ptr[void]) -> i32"
neg konvention "Rückruf 'f' hat die Signatur fn[win64](i32, ptr[void]) -> i32, erwartet wird fn(i32, ptr[void]) -> i32"
neg fehlerwert_fehlt "Rückruf 'f' kann fehlschlagen; @rückruf(fehlerwert) angeben"
neg fehlerwert_bereich "Fehlerwert 300 liegt ausserhalb"
neg struktur_fehlbar "kann fehlschlagen und liefert eine Struktur"
neg einstieg "@einstieg-Exporte haben bereits die C-ABI"
neg dynamisch "nur für Top-Level-@nativ-Funktionen"
neg puffer "Signatur ist kein C-Rückruf"
neg adresse_von "als C-Funktionszeiger nur ueber @rückruf"
echo 'PASS Rueckruf negativ: 11 Compilefehler mit Meldung'
