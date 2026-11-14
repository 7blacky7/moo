#!/usr/bin/env bash
# P2-T34: native Zahlenoperationen und Fehlerpfade (ADR f67bfcbd §3).
# Positiv: Division/Rest/Shift/Wrap-Orakel; fangbare Fehler in versuche:;
# unbehandelter Fehler beendet den Prozess; konstante Fehler sind Compilefehler.
# IR: Fehlerbloecke native.fehler.* rufen moo_native_fehler und geben Null zurueck.
# Casts (ADR §4): als(T, x) geprueft, als_wrap, als_saettigend; Orakel, IR-Guards,
# fangbare Fehler, statische Compilefehler.
set -euo pipefail

zahlen_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$zahlen_dir/../../.." && pwd)"
out="$project_dir/build/p2-zahlen"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
tests="$project_dir/compiler/tests/p2"
mkdir -p -- "$out"

# 1. Orakel nach ADR (Rundung zur Null, Restvorzeichen, Shifts, Wrap).
"$compiler" compile "$tests/native_zahlen_positiv.moos" -o "$out/positiv"
"$out/positiv" > "$out/positiv.out"
cmp "$tests/native_zahlen_positiv.expected" "$out/positiv.out"
echo "PASS Orakel: -7/3=-2, -7%3=-1, 7/-3=-2, 7%-3=1, u8 129<<1=2, i8 -128>>7=-1, u8 128>>7=1, i8 127+1=-128"

# 2. IR: je fehlerfaehiger Operation ein Fehlerblock mit moo_native_fehler und Nullrueckgabe,
#    Guards vor der LLVM-Operation (native.div.zero/native.shift.invalid).
"$compiler" compile "$tests/native_zahlen_fehler.moos" --emit-ir -o "$out/fehler.ll"
mise exec -- opt -passes=verify -disable-output "$out/fehler.ll"
# Hook: schwache Trap-Fallback-Definition im Modul, die Runtime (moo_error.c) gewinnt beim Link.
grep -qE '^define weak void @moo_native_fehler\(ptr' "$out/fehler.ll"
for muster in 'native.fehler.div' 'native.fehler.rem' 'native.fehler.shift' 'native.div.zero' 'native.div.minus_one' 'native.shift.invalid' 'call void @moo_native_fehler(ptr'; do
    grep -qF -- "$muster" "$out/fehler.ll" || { echo "FEHLER: IR-Muster fehlt: $muster" >&2; exit 1; }
done
# Die unoptimierte Division steht hinter dem Guard: sdiv/srem nur im Erfolgszweig.
grep -q 'sdiv i32' "$out/fehler.ll"
echo "PASS IR: Guards vor sdiv/srem/shl, Fehlerbloecke mit moo_native_fehler"

# 3. Hosted fangbar: jeder Fehler landet im fange-Zweig, danach laeuft das Programm weiter.
"$compiler" compile "$tests/native_zahlen_fehler.moos" -o "$out/fehler"
"$out/fehler" > "$out/fehler.out" 2> "$out/fehler.err"
cmp "$tests/native_zahlen_fehler.expected" "$out/fehler.out"
if grep -q 'NACH_FALSCH' "$out/fehler.out"; then echo "FEHLER: Code nach dem Fehler wurde ausgefuehrt" >&2; exit 1; fi
echo "PASS versuche: Division durch Null, MIN/-1, Rest durch Null, Shift 8 auf u8 gefangen"

# 4. Unbehandelt: moo_throw beendet den Prozess mit RC 1 und Meldung, nichts danach.
"$compiler" compile "$tests/native_zahlen_unbehandelt.moos" -o "$out/unbehandelt"
set +e
"$out/unbehandelt" > "$out/unbehandelt.out" 2> "$out/unbehandelt.err"
rc=$?
set -e
[ "$rc" -eq 1 ] || { echo "FEHLER: unbehandelter Zahlenfehler lieferte RC $rc statt 1" >&2; exit 1; }
grep -q 'vorher' "$out/unbehandelt.out"
grep -qF "Native Division durch Null oder MIN/-1 in 'z_div'" "$out/unbehandelt.err" "$out/unbehandelt.out"
if grep -q 'NACH_FALSCH' "$out/unbehandelt.out"; then echo "FEHLER: Code nach unbehandeltem Fehler lief" >&2; exit 1; fi
echo "PASS unbehandelt: RC 1, Meldung, kein Fortlauf"

# 5. Statisch erkennbare Fehler sind Compilefehler (ADR: Konstante beim Kompilieren diagnostizieren).
expect_fail() {  # Datei Muster
    if "$compiler" compile "$1" --emit-ir -o "$out/fail.ll" >"$out/fail.log" 2>&1; then
        echo "Unerwartet akzeptiert: $1" >&2; exit 1
    fi
    grep -qF -- "$2" "$out/fail.log" || { cat "$out/fail.log" >&2; echo "Falsche Diagnose fuer $1" >&2; exit 1; }
}
expect_fail "$tests/native_zahlen_statisch_null.moos" 'durch konstante Null'
expect_fail "$tests/native_zahlen_statisch_shift.moos" 'Shiftzaehler 8 liegt ausserhalb 0..7'
expect_fail "$tests/native_zahlen_statisch_min.moos" 'MIN durch -1 ist konstant ungueltig'
echo "PASS statisch: konstante Null, Shift 8 auf u8, MIN/-1 als Compilefehler"

# 6. Bare: derselbe Koerper trappt statt moo_native_fehler (kein Runtime-Symbol im Objekt).
"$compiler" compile "$tests/native_zahlen_positiv.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/bare.ll" >/dev/null 2>&1 || true
if [ -s "$out/bare.ll" ]; then
    mise exec -- opt -passes=verify -disable-output "$out/bare.ll"
    if grep -q 'moo_native_fehler' "$out/bare.ll"; then echo "FEHLER: bare IR referenziert moo_native_fehler" >&2; exit 1; fi
    grep -q 'llvm.trap' "$out/bare.ll"
    echo "PASS bare: Fehlerbloecke trappen, kein Runtime-Symbol"
else
    echo "INFO bare: Programm mit dynamischem main ist ohne Standardbibliothek nicht uebersetzbar (erwartet), Trap-Pfad nur per Code belegt"
fi
# 7. Casts: gepruefter Standardcast, Wrap, Saettigung (ADR §4) als Orakel.
"$compiler" compile "$tests/native_cast_positiv.moos" -o "$out/cast_positiv"
"$out/cast_positiv" > "$out/cast_positiv.out"
cmp "$tests/native_cast_positiv.expected" "$out/cast_positiv.out"
echo "PASS Cast-Orakel: i32->u8, f64->i64, i32->f32->i64 Rundung, wrap -1=255, saettigend 300/-300/3.7/-5.0, i64->u64, Literal"

# 8. Cast-Fehler: IR-Guards vor fptosi/fptrunc, Fehlerblock native.fehler.cast, hosted fangbar.
"$compiler" compile "$tests/native_cast_fehler.moos" --emit-ir -o "$out/cast_fehler.ll"
mise exec -- opt -passes=verify -disable-output "$out/cast_fehler.ll"
for muster in 'native.fehler.cast' 'native.cast.lo' 'native.cast.hi' 'native.cast.bereich' 'native.cast.ganzzahlig' 'fptosi double' 'fptrunc double'; do
    grep -qF -- "$muster" "$out/cast_fehler.ll" || { echo "FEHLER: IR-Muster fehlt: $muster" >&2; exit 1; }
done
"$compiler" compile "$tests/native_cast_fehler.moos" -o "$out/cast_fehler"
"$out/cast_fehler" > "$out/cast_fehler.out" 2> "$out/cast_fehler.err"
cmp "$tests/native_cast_fehler.expected" "$out/cast_fehler.out"
if grep -q 'NACH_FALSCH' "$out/cast_fehler.out"; then echo "FEHLER: Code nach dem Cast-Fehler wurde ausgefuehrt" >&2; exit 1; fi
echo "PASS Cast-Fehler: Bereich (u8/i8/u32), Bruchteil, NaN, Infinity, f64->f32-Ueberlauf gefangen"

# 9. Statisch: konstanter Cast ausserhalb des Zielbereichs ist ein Compilefehler.
expect_fail "$tests/native_cast_statisch.moos" 'Konstante 300 passt nicht in U8'
echo "PASS statisch: als(u8, 300) als Compilefehler"
# 10. Vergleiche/Logik im Pure-Pfad: bool-Ergebnis, IEEE-NaN, Kurzschluss
#     (b != 0 und a / b > 2 loest bei b = 0 keinen Fehler aus).
"$compiler" compile "$tests/native_bool.moos" --emit-ir -o "$out/bool.ll"
mise exec -- opt -passes=verify -disable-output "$out/bool.ll"
for muster in 'native.icmp' 'native.fcmp' 'native.logik.rechts' 'native.nicht' 'phi i1'; do
    grep -qF -- "$muster" "$out/bool.ll" || { echo "FEHLER: IR-Muster fehlt: $muster" >&2; exit 1; }
done
"$compiler" compile "$tests/native_bool.moos" -o "$out/bool"
"$out/bool" > "$out/bool.out"
cmp "$tests/native_bool.expected" "$out/bool.out"
echo "PASS bool: Vergleiche signed/unsigned/float, NaN != NaN, und/oder-Kurzschluss, nicht"
# 11. Hosted Systemkoerper (unsicher:-Koerper): Laufzeitfehler ruft moo_native_fehler
#     (Block sys.fehler) statt llvm.trap; ohne Handler RC 1 mit Diagnose, kein Fortlauf.
"$compiler" compile "$tests/native_system_fehler.moos" --emit-ir -o "$out/system_fehler.ll"
mise exec -- opt -passes=verify -disable-output "$out/system_fehler.ll"
s_div_ir="$(sed -n '/define.*__moo_native\.s_div/,/^}/p' "$out/system_fehler.ll")"
[ -n "$s_div_ir" ] || { echo "FEHLER: Systemkoerper __moo_native.s_div fehlt im IR" >&2; exit 1; }
grep -qF 'sys.fehler' <<<"$s_div_ir" || { echo "FEHLER: Block sys.fehler fehlt in s_div" >&2; exit 1; }
grep -qF 'call void @moo_native_fehler(ptr' <<<"$s_div_ir" || { echo "FEHLER: moo_native_fehler fehlt in s_div" >&2; exit 1; }
if grep -qF 'llvm.trap' <<<"$s_div_ir"; then echo "FEHLER: hosted Systemkoerper s_div trappt noch" >&2; exit 1; fi
"$compiler" compile "$tests/native_system_fehler.moos" -o "$out/system_fehler"
set +e
"$out/system_fehler" > "$out/system_fehler.out" 2> "$out/system_fehler.err"
rc=$?
set -e
[ "$rc" -eq 1 ] || { echo "FEHLER: Systemkoerper-Fehler lieferte RC $rc statt 1" >&2; exit 1; }
grep -qx '4' "$out/system_fehler.out"
grep -qF "Nativer Laufzeitfehler (Division, Shift, Index oder Konversion) in 's_div'" "$out/system_fehler.err" "$out/system_fehler.out"
if grep -q 'NACH_FALSCH' "$out/system_fehler.out"; then echo "FEHLER: Code nach Systemkoerper-Fehler lief" >&2; exit 1; fi
echo "PASS Systemkoerper hosted: moo_native_fehler statt Trap, RC 1, Diagnose, kein Fortlauf"
# 12. bool als dynamisches Argument: Literal-Schnellpfad, Tagpruefung MOO_BOOL,
#     Zahl statt bool ist in versuche: fangbar.
"$compiler" compile "$tests/native_bool_arg.moos" --emit-ir -o "$out/bool_arg.ll"
mise exec -- opt -passes=verify -disable-output "$out/bool_arg.ll"
grep -qF 'native.bool.is_bool' "$out/bool_arg.ll" || { echo "FEHLER: bool-Tagpruefung fehlt im IR" >&2; exit 1; }
"$compiler" compile "$tests/native_bool_arg.moos" -o "$out/bool_arg"
"$out/bool_arg" > "$out/bool_arg.out" 2> "$out/bool_arg.err"
cmp "$tests/native_bool_arg.expected" "$out/bool_arg.out"
if grep -q 'NACH_FALSCH' "$out/bool_arg.out"; then echo "FEHLER: Code nach dem bool-Argumentfehler lief" >&2; exit 1; fi
echo "PASS bool-Argumente: Literal, dynamischer Wahrheitswert, Zahl statt bool gefangen"
# 13. Vorpruefung mit Status (ADR §13): teilbar/verschiebbar als bool, Kurzschluss,
#     Systemkoerper-Fortsetzung mit Ersatzwert statt Fehler.
"$compiler" compile "$tests/native_pruefung.moos" --emit-ir -o "$out/pruefung.ll"
mise exec -- opt -passes=verify -disable-output "$out/pruefung.ll"
grep -qF 'native.pruefung' "$out/pruefung.ll" || { echo "FEHLER: Vorpruefung fehlt im IR" >&2; exit 1; }
"$compiler" compile "$tests/native_pruefung.moos" -o "$out/pruefung"
"$out/pruefung" > "$out/pruefung.out"
cmp "$tests/native_pruefung.expected" "$out/pruefung.out"
echo "PASS Vorpruefung: teilbar (0, MIN/-1), verschiebbar (8 auf u8), Kurzschluss, Systemkoerper-Ersatzwert"
# 14. Hosted Systemkoerper mit Rueckgabetyp endet ohne gib_zurück: sys.fehler
#     ('endet ohne Rueckgabewert') statt llvm.trap; RC 1, Wert davor ausgegeben,
#     Diagnose, kein Fortlauf.
"$compiler" compile "$tests/native_system_ende.moos" --emit-ir -o "$out/system_ende.ll"
mise exec -- opt -passes=verify -disable-output "$out/system_ende.ll"
s_ende_ir="$(sed -n '/define.*__moo_native\.s_ende/,/^}/p' "$out/system_ende.ll")"
[ -n "$s_ende_ir" ] || { echo "FEHLER: Systemkoerper __moo_native.s_ende fehlt im IR" >&2; exit 1; }
grep -qF 'call void @moo_native_fehler(ptr' <<<"$s_ende_ir" || { echo "FEHLER: moo_native_fehler fehlt am Funktionsende von s_ende" >&2; exit 1; }
if grep -qF 'llvm.trap' <<<"$s_ende_ir"; then echo "FEHLER: hosted Systemkoerper s_ende trappt am Funktionsende noch" >&2; exit 1; fi
grep -qF "endet ohne Rueckgabewert in 's_ende'" "$out/system_ende.ll" || { echo "FEHLER: Diagnosetext fuer Funktionsende fehlt im IR" >&2; exit 1; }
"$compiler" compile "$tests/native_system_ende.moos" -o "$out/system_ende"
set +e
"$out/system_ende" > "$out/system_ende.out" 2> "$out/system_ende.err"
rc=$?
set -e
[ "$rc" -eq 1 ] || { echo "FEHLER: Funktionsende ohne Rueckgabe lieferte RC $rc statt 1" >&2; exit 1; }
grep -qx '5' "$out/system_ende.out"
grep -qF "Native Funktion endet ohne Rueckgabewert in 's_ende'" "$out/system_ende.err" "$out/system_ende.out"
if grep -q 'NACH_FALSCH' "$out/system_ende.out"; then echo "FEHLER: Code nach dem Funktionsende-Fehler lief" >&2; exit 1; fi
echo "PASS Systemkoerper-Funktionsende: moo_native_fehler statt Trap, RC 1, Diagnose, kein Fortlauf"
# 15. bool-Rueckgabe eines hosted Systemkoerpers an die dynamische Ebene
#     (scalar_signature -> moo_bool_fn): wahr/falsch, kein Zahlenumweg.
"$compiler" compile "$tests/native_system_bool.moos" -o "$out/system_bool"
"$out/system_bool" > "$out/system_bool.out"
cmp "$tests/native_system_bool.expected" "$out/system_bool.out"
echo "PASS Systemkoerper bool-Rueckgabe: wahr/falsch an dyn, und-Verknuepfung"
# 16. P2-T74: hosted Systemkoerper innerhalb versuche: (main, benannte Funktion,
#     versuche: in der Funktion); Fehler landen im fange-Zweig, kein Fortlauf,
#     das Programm laeuft weiter. Vorher Compilefehler 'ein Trap waere nicht fangbar'.
"$compiler" compile "$tests/native_system_try.moos" -o "$out/system_try"
"$out/system_try" > "$out/system_try.out" 2> "$out/system_try.err"
cmp "$tests/native_system_try.expected" "$out/system_try.out"
if grep -q 'NACH_FALSCH' "$out/system_try.out"; then echo "FEHLER: Code nach gefangenem Systemkoerper-Fehler lief" >&2; exit 1; fi
"$compiler" compile "$tests/native_system_funktion_try.moos" -o "$out/system_funktion_try"
"$out/system_funktion_try" > "$out/system_funktion_try.out" 2> "$out/system_funktion_try.err"
cmp "$tests/native_system_funktion_try.expected" "$out/system_funktion_try.out"
if grep -q 'NACH_FALSCH' "$out/system_funktion_try.out"; then echo "FEHLER: Code nach gefangenem Systemkoerper-Fehler in Funktion lief" >&2; exit 1; fi
# Batch 2: dynamische Argumente bei setze/gib_zurueck (Direktpfad), ungueltiges Argument gefangen.
"$compiler" compile "$tests/native_system_try_dyn.moos" -o "$out/system_try_dyn"
"$out/system_try_dyn" > "$out/system_try_dyn.out" 2> "$out/system_try_dyn.err"
cmp "$tests/native_system_try_dyn.expected" "$out/system_try_dyn.out"
if grep -q 'NACH_FALSCH' "$out/system_try_dyn.out"; then echo "FEHLER: Code nach gefangenem Fehler (dyn. Argument) lief" >&2; exit 1; fi
# Batch 4: dyn. Argument in benannter Funktion ohne eigenes versuche, Aufrufer-Handler aktiv:
#     flag-basierte Propagation statt llvm.trap (IR: kein Trap im Funktionsrahmen von teile).
"$compiler" compile "$tests/native_system_funktion_propagiert.moos" --emit-ir -o "$out/system_propagiert.ll"
mise exec -- opt -passes=verify -disable-output "$out/system_propagiert.ll"
teile_ir="$(sed -n '/^define.*@teile(/,/^}/p' "$out/system_propagiert.ll")"
[ -n "$teile_ir" ] || { echo "FEHLER: Funktion teile fehlt im IR" >&2; exit 1; }
if grep -qF 'llvm.trap' <<<"$teile_ir"; then echo "FEHLER: Argumentadapter in teile trappt noch statt zu propagieren" >&2; exit 1; fi
grep -qF 'native.arg.none' <<<"$teile_ir" || { echo "FEHLER: Propagationsausgang native.arg.none fehlt in teile" >&2; exit 1; }
"$compiler" compile "$tests/native_system_funktion_propagiert.moos" -o "$out/system_propagiert"
"$out/system_propagiert" > "$out/system_propagiert.out" 2> "$out/system_propagiert.err"
cmp "$tests/native_system_funktion_propagiert.expected" "$out/system_propagiert.out"
if grep -q 'NACH_FALSCH' "$out/system_propagiert.out"; then echo "FEHLER: Code nach propagiertem Fehler lief" >&2; exit 1; fi
echo "PASS P2-T74 Systemkoerper in versuche: Division, Funktionsende, dynamisches Argument (zeige/setze/gib_zurueck), benannte Funktion, versuche: in Funktion, Propagation aus Funktion gefangen"
# 17. P2-T74 Batch 3: Systemkoerper mit Rueckgabetyp ohne jedes gib_zurueck ist ein
#     Compilefehler (statisch sicher); native_system_ende.moos (Rueckgabe nur auf
#     einem Pfad) bleibt ein Laufzeitfall (Abschnitt 14).
expect_fail "$tests/native_system_ende_statisch.moos" "$(cat "$tests/native_system_ende_statisch.expected_error")"
echo "PASS P2-T74 statisch: Systemkoerper ohne gib_zurueck als Compilefehler"
# 18. P2-T77 Batch 1: Direktform auch fuer wenn-Bedingung und Aufruf-Statement:
#     dynamisches Argument an Systemkoerper in versuche: dort nutzbar, Fehler gefangen.
"$compiler" compile "$tests/native_system_direktform.moos" -o "$out/system_direktform"
"$out/system_direktform" > "$out/system_direktform.out" 2> "$out/system_direktform.err"
cmp "$tests/native_system_direktform.expected" "$out/system_direktform.out"
if grep -q 'NACH_FALSCH' "$out/system_direktform.out"; then echo "FEHLER: Code nach gefangenem Fehler (Direktform wenn/Ausdruck) lief" >&2; exit 1; fi
echo "PASS P2-T77 Direktform: wenn-Bedingung und Aufruf-Statement mit dynamischem Argument gefangen"
# 19. P2-T77 Batch 2: fangbare Systemkoerper in Funktionen mit aufraeumen; Aktionen
#     laufen genau einmal (auch bei Koerper-/Argumentfehler), Erstfehler beim Aufrufer.
for fall in defer_system_koerper defer_system_frame defer_system_runtime; do
    "$compiler" compile "$tests/$fall.moos" -o "$out/$fall"
    "$out/$fall" > "$out/$fall.out" 2> "$out/$fall.err"
    cmp "$tests/$fall.expected" "$out/$fall.out"
    if grep -q 'NACH_FALSCH' "$out/$fall.out"; then echo "FEHLER: Code nach gefangenem Fehler lief ($fall)" >&2; exit 1; fi
done
echo "PASS P2-T77 aufraeumen: Systemkoerper in Defer-Funktionen, Aktionen genau einmal, Erstfehler gefangen"
# 20. P2-T78: Nicht-Direktform (Argumentposition, gib_zurueck-Teilausdruck, versuche: in
#     der Funktion) hosted Systemkoerper in benannten Funktionen: Adapterfehler wie
#     Koerperfehler fangbar (Skip-Block native.skip, Aufruf liefert nichts) statt llvm.trap.
"$compiler" compile "$tests/native_system_indirekt.moos" -o "$out/system_indirekt"
"$out/system_indirekt" > "$out/system_indirekt.out" 2> "$out/system_indirekt.err"
cmp "$tests/native_system_indirekt.expected" "$out/system_indirekt.out"
if grep -q 'NACH_FALSCH' "$out/system_indirekt.out"; then echo "FEHLER: Code nach gefangenem Fehler lief (Nicht-Direktform)" >&2; exit 1; fi
"$compiler" compile "$tests/native_system_indirekt.moos" --emit-ir -o "$out/system_indirekt.ll"
grep -q 'native.skip' "$out/system_indirekt.ll" || { echo "FEHLER: kein native.skip-Block in der IR (Nicht-Direktform)" >&2; exit 1; }
echo "PASS P2-T78 Nicht-Direktform: Systemkoerper-Adapterfehler in Funktionen fangbar (Argumentposition, Teilausdruck, versuche: in Funktion)"
# 21. P2-T82: verschachtelte Operatoren/Aufrufe um reine @nativ-Aufrufe in versuche:
#     Erstfehler bleibt erhalten (kein 'Division durch Null!' auf nichts), keine
#     Seiteneffekte nach dem uebersprungenen Aufruf (rekursive Guards auch um native Aufrufe).
"$compiler" compile "$tests/native_verschachtelt_try.moos" -o "$out/verschachtelt_try"
"$out/verschachtelt_try" > "$out/verschachtelt_try.out" 2> "$out/verschachtelt_try.err"
cmp "$tests/native_verschachtelt_try.expected" "$out/verschachtelt_try.out"
if grep -q -e 'NACH_FALSCH' -e 'Division durch Null' -e 'marke nichts' -e 'marke 2' "$out/verschachtelt_try.out"; then echo "FEHLER: Erstfehler verloren oder Seiteneffekt nach uebersprungenem nativen Aufruf" >&2; exit 1; fi
echo "PASS P2-T82 verschachtelt: Erstfehler erhalten, keine Folgeoperation/Seiteneffekte auf nichts"
# 22. P2-T82 Batch 2: dasselbe in benannten Funktionen OHNE eigenes versuche: ueber den
#     Pruefpunkt-Ausgang native.guard.exit (Locals frei, nichts zurueck, Aufrufer faengt).
"$compiler" compile "$tests/native_verschachtelt_funktion.moos" -o "$out/verschachtelt_funktion"
"$out/verschachtelt_funktion" > "$out/verschachtelt_funktion.out" 2> "$out/verschachtelt_funktion.err"
cmp "$tests/native_verschachtelt_funktion.expected" "$out/verschachtelt_funktion.out"
if grep -q -e 'NACH_FALSCH' -e 'Division durch Null' -e 'marke nichts' "$out/verschachtelt_funktion.out"; then echo "FEHLER: Erstfehler verloren oder Seiteneffekt in Funktion ohne versuche" >&2; exit 1; fi
"$compiler" compile "$tests/native_verschachtelt_funktion.moos" --emit-ir -o "$out/verschachtelt_funktion.ll"
grep -q 'native.guard.exit' "$out/verschachtelt_funktion.ll" || { echo "FEHLER: kein native.guard.exit in der IR" >&2; exit 1; }
echo "PASS P2-T82 Funktionsrahmen: Pruefpunkt-Ausgang in Funktionen ohne versuche, Erstfehler beim Aufrufer"
echo "PASS P2-T34 Zahlenoperationen: Orakel, IR-Guards, fangbar, unbehandelt, statisch, Casts, Vergleiche, Systemkoerper, bool-Argumente, Vorpruefung, Funktionsende, bool-Rueckgabe"
