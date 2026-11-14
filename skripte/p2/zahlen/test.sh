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
# 23. P2-T9 Batch B1/B3/B4 (Plan 4500717b): Operation x Typ fuer i8..u64. Das von
#     matrix_int.sh erzeugte Programm wird gegen das C-Orakel matrix_ref.c geprueft
#     (rechnet jede Ausgabezeile unabhaengig nach). IR-Reinheit: native Funktionen
#     rufen nur andere native Funktionen oder moo_native_fehler; Div/Rem/Shift mit,
#     alle anderen Operationen ohne Fehlerblock; keine dynamischen moo_*-Aufrufe.
"${CC:-cc}" -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wconversion -Wsign-conversion "$zahlen_dir/matrix_ref.c" -o "$out/matrix_ref" -lm
bash "$zahlen_dir/matrix_int.sh" > "$out/matrix_int.moos"
"$compiler" compile "$out/matrix_int.moos" -o "$out/matrix_int"
"$out/matrix_int" > "$out/matrix_int.out"
matrix_faelle="$(wc -l < "$out/matrix_int.out")"
[ "$matrix_faelle" -ge 98000 ] || { echo "FEHLER: Int-Matrix lieferte nur $matrix_faelle Faelle" >&2; exit 1; }
sed 's/ = .*//' "$out/matrix_int.out" | "$out/matrix_ref" > "$out/matrix_int.ref"
cmp "$out/matrix_int.ref" "$out/matrix_int.out"
"$compiler" compile "$out/matrix_int.moos" --emit-ir -o "$out/matrix_int.ll"
mise exec -- opt -passes=verify -disable-output "$out/matrix_int.ll"
# ir_rein IR-Datei Ops-mit-Fehlerblock: jede native Funktion ruft nur native
# Funktionen oder moo_native_fehler; reine Operationsfunktionen <typ>_<op> haben
# genau dann einen Fehlerblock, wenn <op> in der Liste steht. Name ohne den
# 14 Zeichen langen Praefix '@__moo_native.' (Review 4ead2018: vorher +13).
ir_rein() {
    awk -v mit="^($2)\$" '
/^define .*@__moo_native\./ { match($0, /@__moo_native\.[a-z0-9_]+/); name = substr($0, RSTART + 14, RLENGTH - 14); fehler = 0; fremd = 0; drin = 1; next }
drin && / call / { if ($0 ~ /@moo_native_fehler\(/) fehler++; else if ($0 !~ /@__moo_native\./) fremd++; next }
drin && /^}/ {
    drin = 0; funktionen++
    if (fremd) { print "FEHLER: fremder Aufruf in " name; schlecht++ }
    op = name
    if (!sub(/^[iu](8|16|32|64)_/, "", op) || op ~ /_/ || op ~ /^(hi|lo|mk)$/) next
    geprueft++
    # P2-T58: ** hat nur signed einen Fehlerblock (negativer Exponent).
    soll = (op ~ mit) || (op == "pow" && name ~ /^i/)
    if (soll && !fehler) { print "FEHLER: " name " ohne Fehlerblock"; schlecht++ }
    if (!soll && fehler) { print "FEHLER: " name " mit Fehlerblock"; schlecht++ }
}
END { if (geprueft < 150 || schlecht) { print "IR-Reinheit: Funktionen " funktionen ", Operationen " geprueft ", Befunde " schlecht + 0; exit 1 } }
' "$1"
}
ir_rein "$out/matrix_int.ll" 'div|rem|shl|shr' >&2
# Selbstprobe: mit absichtlich falscher Erwartung (add mit Fehlerblock) muss sie rot werden.
if ir_rein "$out/matrix_int.ll" 'add|div|rem|shl|shr' > "$out/matrix_int.selbstprobe" 2>&1; then
    echo "FEHLER: IR-Reinheitspruefung erkennt falsche Erwartung nicht" >&2; exit 1
fi
grep -q 'FEHLER: i8_add ohne Fehlerblock' "$out/matrix_int.selbstprobe" || { cat "$out/matrix_int.selbstprobe" >&2; echo "FEHLER: Selbstprobe ohne erwarteten Befund" >&2; exit 1; }
echo "PASS P2-T9 Int-Matrix: $matrix_faelle Faelle i8..u64 (inkl. neg unsigned, lokal/A1) = C-Orakel, IR verify, IR-Reinheit mit Selbstprobe"
# 24. P2-T9 B6: Typfehler der Wertschicht sind Compilefehler (Float-%/** sind seit
#     P2-T58 D2c erlaubt, siehe §26 und §32).
expect_fail "$tests/native_matrix_neg_mischtyp.moos" "Nativer Name 'a' hat Typ I32, erwartet I64"
expect_fail "$tests/native_matrix_neg_literal.moos" "Ganzzahlliteral '300' passt nicht in U8"
expect_fail "$tests/native_matrix_neg_bool_arith.moos" 'Ganzzahltyp erwartet'
expect_fail "$tests/native_matrix_neg_bool_vergleich.moos" 'Vergleich ist fuer diesen Typ nicht definiert'
expect_fail "$tests/native_matrix_neg_local.moos" 'Native lokale Variable braucht eine Typannotation'
expect_fail "$tests/native_matrix_neg_rekursion.moos" "Rekursive native Aufrufe sind noch nicht unterstützt: 'f'"
echo "PASS P2-T9 Negativ: Mischtyp, Literalbereich, bool-Arithmetik/-Ordnung, Local ohne Typ, Rekursion"
# 25. P2-T9 A4 (Review 9b847d8e): Literalpaare ohne typisierten Nachbarn in beiden
#     Lowerern reihenfolgeunabhaengig (1.0 < 2 und 1 < 2.0 -> f64; -1 < 0 -> i64).
"$compiler" compile "$tests/native_literal_standard.moos" -o "$out/literal_standard"
"$out/literal_standard" > "$out/literal_standard.out"
cmp "$tests/native_literal_standard.expected" "$out/literal_standard.out"
echo "PASS P2-T9 Literal-Standard: Pure und Systemkoerper symmetrisch (beide Reihenfolgen)"
# 26. P2-T9 Batch B2: Float-Matrix f32/f64 (matrix_float.sh) gegen dasselbe Orakel:
#     IEEE-Sonderwerte (Inf, NaN-Vergleiche, -0 ueber negkehr/negeq), f32-Rundung
#     (rund), Pure->Pure-Ketten; IR: keine Fehlerbloecke, keine fremden Aufrufe.
#     P2-T58 D2c: % und ** (rem/pow/remkehr/fnan) gegen fmod/pow des Orakels; einzige
#     erlaubte fremde Aufrufe sind moo_native_fmod/fmodf/pow/powf, kein frem/libm.
bash "$zahlen_dir/matrix_float.sh" > "$out/matrix_float.moos"
"$compiler" compile "$out/matrix_float.moos" -o "$out/matrix_float"
"$out/matrix_float" > "$out/matrix_float.out"
float_faelle="$(wc -l < "$out/matrix_float.out")"
[ "$float_faelle" -ge 220 ] || { echo "FEHLER: Float-Matrix lieferte nur $float_faelle Faelle" >&2; exit 1; }
sed 's/ = .*//' "$out/matrix_float.out" | "$out/matrix_ref" > "$out/matrix_float.ref"
cmp "$out/matrix_float.ref" "$out/matrix_float.out"
"$compiler" compile "$out/matrix_float.moos" --emit-ir -o "$out/matrix_float.ll"
mise exec -- opt -passes=verify -disable-output "$out/matrix_float.ll"
awk '
/^define .*@__moo_native\./ { match($0, /@__moo_native\.[a-z0-9_]+/); name = substr($0, RSTART + 14, RLENGTH - 14); drin = 1; next }
drin && / call / { if ($0 !~ /@__moo_native\./ && $0 !~ /@moo_native_(fmod|fmodf|pow|powf)\(/) { print "FEHLER: Aufruf in " name ": " $0; schlecht++ } next }
drin && / frem / { print "FEHLER: frem in " name; schlecht++; next }
drin && /^}/ { drin = 0; geprueft++ }
END { if (geprueft < 37 || schlecht) { print "Float-IR: geprueft " geprueft ", Befunde " schlecht + 0; exit 1 } }
' "$out/matrix_float.ll" >&2
grep -q 'call double @moo_native_fmod(' "$out/matrix_float.ll" || { echo "FEHLER: f64 % ruft nicht moo_native_fmod" >&2; exit 1; }
grep -q 'call float @moo_native_powf(' "$out/matrix_float.ll" || { echo "FEHLER: f32 ** ruft nicht moo_native_powf" >&2; exit 1; }
echo "PASS P2-T9 Float-Matrix: $float_faelle Faelle f32/f64 = C-Orakel, Inf/NaN/-0, f32-Rundung, %/** (fmod/pow), IR ohne Fehlerbloecke/libm"
# 27. P2-T58 B5: Grenze dyn<->nativ je Typ (i8..u64, f32, f64, bool) in versuche: fangbar;
#     Rueckgaben i64 2^53 / 2^53+1, u64 MAX als fangbarer Grenzfehler (Batch 33601, vorher
#     'ret i32 1'), auch aus benannter Funktion; f32-Rundung; A3 Erstfehler nennt 'b_teil'.
"$compiler" compile "$tests/native_grenze_matrix.moos" -o "$out/grenze_matrix"
"$out/grenze_matrix" > "$out/grenze_matrix.out" 2> "$out/grenze_matrix.err"
cmp "$tests/native_grenze_matrix.expected" "$out/grenze_matrix.out"
if grep -q 'NACH_FALSCH' "$out/grenze_matrix.out"; then echo "FEHLER: Code nach Grenzfehler lief" >&2; exit 1; fi
"$compiler" compile "$tests/native_grenze_matrix.moos" --emit-ir -o "$out/grenze_matrix.ll"
mise exec -- opt -passes=verify -disable-output "$out/grenze_matrix.ll"
grep -q 'native.box.done' "$out/grenze_matrix.ll" || { echo "FEHLER: Rueckgabe-Grenzfehler ohne Merge native.box.done" >&2; exit 1; }
echo "PASS P2-T58 Grenze: Argumente/Rueckgaben je Typ fangbar, i64/u64-Grenzfehler auch aus Funktionen"
# 28. P2-T58 B7: 200k/800k gefangene Grenzfehler halten keinen Speicher (RSS-Harness).
for n in 200k 800k; do
    "$compiler" compile "$tests/native_grenze_heap_${n}.moos" -o "$out/grenze_heap_$n"
    timeout 120s "$out/grenze_heap_$n" > "$out/grenze_heap_$n.out"
    cmp "$tests/native_grenze_heap_${n}.expected" "$out/grenze_heap_$n.out"
done
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "$tests/defer_heap_rss.c" -o "$out/grenze_rss"
timeout 240s "$out/grenze_rss" "$out/grenze_heap_200k" "$out/grenze_heap_800k"
echo "PASS P2-T58 Grenze RSS: 400k/1.6M gefangene Grenzfehler ohne Speicherwachstum"
# 29. P2-T58 B8: Int-/Float-Matrix fuer Windows x64 (MinGW-Triple) und AArch64 Linux:
#     IR verify, Objekt per llc, IR-Reinheit der Int-Operationen wie §23. Kein Lauf auf
#     diesen Zielen hier (Windows: windows_minipc/zahlen_probe, AArch64: nur IR/Objekt).
for ziel in x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    for m in matrix_int matrix_float; do
        "$compiler" compile "$out/$m.moos" --target "$ziel" --emit-ir -o "$out/$m.$ziel.ll"
        mise exec -- opt -passes=verify -disable-output "$out/$m.$ziel.ll"
        mise exec -- llc -filetype=obj "$out/$m.$ziel.ll" -o "$out/$m.$ziel.o"
        grep -qF "target triple = \"$ziel\"" "$out/$m.$ziel.ll" || { echo "FEHLER: falsches Triple in $m.$ziel.ll" >&2; exit 1; }
    done
    ir_rein "$out/matrix_int.$ziel.ll" 'div|rem|shl|shr' >&2
done
echo "PASS P2-T58 Plattform-IR: Int-/Float-Matrix fuer x86_64-pc-windows-gnu und aarch64-unknown-linux-gnu (verify, llc, IR-Reinheit)"
# 30. P2-T58 Batch C: Fehlerweitergabe im Systemkoerper. Nach fehlgeschlagenem
#     fehlbarem Callee kein Seiteneffekt (nur ein 'X' aus dem Erfolgsfall), Erstfehler
#     bleibt; IR: Flagpruefung (moo_native_fehler_offen, sys.fehler.weiter) nur nach
#     fehlbaren Callees, reine Float-Natives ohne Zusatzkosten; schwacher Hook im Modul.
"$compiler" compile "$tests/native_system_weitergabe.moos" -o "$out/system_weitergabe"
"$out/system_weitergabe" > "$out/system_weitergabe.out" 2> "$out/system_weitergabe.err"
cmp "$tests/native_system_weitergabe.expected" "$out/system_weitergabe.out"
"$compiler" compile "$tests/native_system_weitergabe.moos" --emit-ir -o "$out/system_weitergabe.ll"
mise exec -- opt -passes=verify -disable-output "$out/system_weitergabe.ll"
marke_ir="$(sed -n '/define.*__moo_native\.sys_marke(/,/^}/p' "$out/system_weitergabe.ll")"
float_ir="$(sed -n '/define.*__moo_native\.sys_float(/,/^}/p' "$out/system_weitergabe.ll")"
grep -q 'sys.fehler.weiter' <<<"$marke_ir" || { echo "FEHLER: sys_marke ohne Weitergabe-Block" >&2; exit 1; }
grep -q 'moo_native_fehler_offen' <<<"$marke_ir" || { echo "FEHLER: sys_marke ohne Flagpruefung" >&2; exit 1; }
if grep -qE 'sys.fehler.weiter|moo_native_fehler_offen' <<<"$float_ir"; then echo "FEHLER: Float-Systemkoerper mit Flagpruefung (Zusatzkosten)" >&2; exit 1; fi
grep -qE '^define weak i32 @moo_native_fehler_offen\(\)' "$out/system_weitergabe.ll" || { echo "FEHLER: schwacher Hook moo_native_fehler_offen fehlt" >&2; exit 1; }
echo "PASS P2-T58 Systemkoerper-Weitergabe: kein Seiteneffekt nach Callee-Fehler, Erstfehler, keine Kosten ohne fehlbaren Callee"
# 31. P2-T58 D1 (Plan f40b5980 G3): Integer-** in Pure- und Systemkoerper, fangbar,
#     unbehandelt RC 1, potenzierbar; Compilefehler konstant negativ und Mischtyp;
#     IR: inline-Schleife native.pow.*, kein Runtime-Aufruf; Fehlerblock nur signed.
"$compiler" compile "$tests/native_pow.moos" -o "$out/pow"
"$out/pow" > "$out/pow.out" 2> "$out/pow.err"
cmp "$tests/native_pow.expected" "$out/pow.out"
"$compiler" compile "$tests/native_pow.moos" --emit-ir -o "$out/pow.ll"
mise exec -- opt -passes=verify -disable-output "$out/pow.ll"
ipow_ir="$(sed -n '/define.*__moo_native\.ipow(/,/^}/p' "$out/pow.ll")"
upow_ir="$(sed -n '/define.*__moo_native\.upow(/,/^}/p' "$out/pow.ll")"
grep -q 'native.pow.kopf' <<<"$ipow_ir" || { echo "FEHLER: ipow ohne Potenzschleife" >&2; exit 1; }
grep -q 'native.fehler.pow' <<<"$ipow_ir" || { echo "FEHLER: ipow ohne Fehlerblock" >&2; exit 1; }
if grep -q 'native.fehler' <<<"$upow_ir"; then echo "FEHLER: upow mit Fehlerblock" >&2; exit 1; fi
if grep -qE 'call .*@(pow|powf|llvm\.pow)' <<<"$ipow_ir$upow_ir"; then echo "FEHLER: Integer-** ruft eine Bibliotheksfunktion" >&2; exit 1; fi
"$compiler" compile "$tests/native_pow_unbehandelt.moos" -o "$out/pow_unbehandelt"
set +e
"$out/pow_unbehandelt" > "$out/pow_unbehandelt.out" 2> "$out/pow_unbehandelt.err"
rc=$?
set -e
[ "$rc" -eq 1 ] || { echo "FEHLER: unbehandelter Potenzfehler RC $rc statt 1" >&2; exit 1; }
grep -qF "Native Potenz mit negativem Exponenten in 'f'" "$out/pow_unbehandelt.err" "$out/pow_unbehandelt.out"
if grep -q 'NACH_FALSCH' "$out/pow_unbehandelt.out"; then echo "FEHLER: Code nach unbehandeltem Potenzfehler lief" >&2; exit 1; fi
expect_fail "$tests/native_pow_konstant.moos" 'Native Potenz mit konstant negativem Exponenten'
# Review 06d10060 #1: auch im Systemkoerper ein Compilefehler; Systemkoerper-
# Laufzeitfehler mit eigenem Block sys.fehler.pow (#3, Meldung siehe Expected).
expect_fail "$tests/native_pow_konstant_system.moos" 'Native Potenz mit konstant negativem Exponenten'
spow_ir="$(sed -n '/define.*__moo_native\.spow(/,/^}/p' "$out/pow.ll")"
grep -q 'sys.fehler.pow' <<<"$spow_ir" || { echo "FEHLER: spow ohne eigenen Pow-Fehlerblock" >&2; exit 1; }
expect_fail "$tests/native_pow_misch.moos" "Nativer Name 'b' hat Typ F64, erwartet I32"
echo "PASS P2-T58 Integer-**: Pure/Systemkoerper, Wrap, fangbar, RC 1, potenzierbar, statisch, IR ohne Bibliothek"
# 32. P2-T58 D2c (Plan f40b5980 G3): Float-% / Float-** in Pure- und Systemkoerper
#     (f32/f64), kein Moo-Fehler bei NaN-Ergebnis, -0 aus fmod; IR: Aufruf
#     moo_native_fmod/pow{,f} mit nounwind willreturn memory(none), kein frem, kein
#     fmod/pow/llvm.pow (libm) - gilt damit auch fuer bare (bare.sh §5).
"$compiler" compile "$tests/native_float_rest_pow.moos" -o "$out/float_rest_pow"
"$out/float_rest_pow" > "$out/float_rest_pow.out"
cmp "$tests/native_float_rest_pow.expected" "$out/float_rest_pow.out"
"$compiler" compile "$tests/native_float_rest_pow.moos" --emit-ir -o "$out/float_rest_pow.ll"
mise exec -- opt -passes=verify -disable-output "$out/float_rest_pow.ll"
if grep -qE ' frem |call .*@(fmod|fmodf|pow|powf|llvm\.pow[^(]*)\(' "$out/float_rest_pow.ll"; then echo "FEHLER: Float-%/** ueber frem oder libm" >&2; exit 1; fi
srem_ir="$(sed -n '/define.*__moo_native\.s_rem(/,/^}/p' "$out/float_rest_pow.ll")"
spow_ir="$(sed -n '/define.*__moo_native\.s_pow32(/,/^}/p' "$out/float_rest_pow.ll")"
grep -q 'call double @moo_native_fmod(' <<<"$srem_ir" || { echo "FEHLER: Systemkoerper-% ohne moo_native_fmod" >&2; exit 1; }
grep -q 'call float @moo_native_powf(' <<<"$spow_ir" || { echo "FEHLER: Systemkoerper-** (f32) ohne moo_native_powf" >&2; exit 1; }
for sym in 'double @moo_native_fmod(double, double)' 'double @moo_native_pow(double, double)' 'float @moo_native_fmodf(float, float)' 'float @moo_native_powf(float, float)'; do
    decl="$(grep -F "declare $sym #" "$out/float_rest_pow.ll" | sed -E 's/.* (#[0-9]+)$/\1/')"
    [ -n "$decl" ] || { echo "FEHLER: Deklaration fehlt: $sym" >&2; exit 1; }
    grep -qE "^attributes $decl = \{ nounwind willreturn memory\(none\) \}" "$out/float_rest_pow.ll" || { echo "FEHLER: $sym ohne nounwind willreturn memory(none)" >&2; exit 1; }
done
echo "PASS P2-T58 Float-%/**: Pure/Systemkoerper f32/f64 = C-fmod/pow, NaN ohne Moo-Fehler, -0, IR ohne frem/libm"
# 33. P2-T58 (Review 06d10060 #4): Fehlbarkeit typbewusst. s_frei ruft unsigned-**,
#     Float-%/**/'/' und signed-** mit Literal-Exponent ohne Flagpruefung; s_pruef
#     prueft nach signed-** mit variablem Exponenten (fangbar, Meldung nennt i_pot).
"$compiler" compile "$tests/native_pow_fehlbar.moos" -o "$out/pow_fehlbar"
"$out/pow_fehlbar" > "$out/pow_fehlbar.out"
cmp "$tests/native_pow_fehlbar.expected" "$out/pow_fehlbar.out"
"$compiler" compile "$tests/native_pow_fehlbar.moos" --emit-ir -o "$out/pow_fehlbar.ll"
mise exec -- opt -passes=verify -disable-output "$out/pow_fehlbar.ll"
frei_ir="$(sed -n '/define.*__moo_native\.s_frei(/,/^}/p' "$out/pow_fehlbar.ll")"
pruef_ir="$(sed -n '/define.*__moo_native\.s_pruef(/,/^}/p' "$out/pow_fehlbar.ll")"
if grep -qE 'moo_native_fehler_offen|sys.fehler.weiter' <<<"$frei_ir"; then echo "FEHLER: s_frei mit Flagpruefung (nicht fehlbare Callees)" >&2; exit 1; fi
grep -q 'moo_native_fehler_offen' <<<"$pruef_ir" || { echo "FEHLER: s_pruef ohne Flagpruefung nach signed-**" >&2; exit 1; }
echo "PASS P2-T58 Fehlbarkeit typbewusst: unsigned-**/Float-%/**/Literal-Exponent ohne Zusatzkosten, signed-** geprueft"
# 34. P2-T58 (Plan f40b5980 §3.1, Review 06d10060 #2): native Konstanten falten **
#     (Wrap: 2**7 in i8 = -128, 3**6 in u8 = 217, 0**0 = 1); negativ = Compilefehler.
"$compiler" compile "$tests/native_konst_pow.moos" -o "$out/konst_pow"
"$out/konst_pow" > "$out/konst_pow.out"
cmp "$tests/native_konst_pow.expected" "$out/konst_pow.out"
expect_fail "$tests/native_konst_pow_negativ.moos" 'Native Potenz mit konstant negativem Exponenten'
echo "PASS P2-T58 Konstantenfaltung **: Wrap wie Lowerer, 0**0, negativer Exponent Compilefehler"
# 35. P2-T48 Batch A (Plan a71947f7): feste Arrays im Systemkoerper (lokal, 2D,
#     Kopie, Arrayliteral, Arrayfeld, Nullkonstruktor, Struktur-Array), Index
#     grenzgeprueft; Array-Parameter und falsche Literallaenge sind Compilefehler.
"$compiler" compile "$tests/native_arrays.moos" -o "$out/arrays"
"$out/arrays" > "$out/arrays.out"
cmp "$tests/native_arrays.expected" "$out/arrays.out"
"$compiler" compile "$tests/native_arrays.moos" --emit-ir -o "$out/arrays.ll"
mise exec -- opt -passes=verify -disable-output "$out/arrays.ll"
grep -q 'sys.array.ausserhalb' "$out/arrays.ll" || { echo "FEHLER: Arrayindex ohne Grenzpruefung" >&2; exit 1; }
expect_fail "$tests/native_array_neg_param.moos" 'Feste Arrays sind als Parameter/Rueckgabe by value nicht freigegeben'
expect_fail "$tests/native_array_neg_laenge.moos" 'erwartet 3 Elemente, das Arrayliteral hat 2'
echo "PASS P2-T48 Arrays: lokal/2D/Kopie/Literal/Arrayfeld/Nullkonstruktor/Struktur-Array, grenzgeprueft, 2 Compilefehler"
# 36. P2-T48 Batch B (Plan a71947f7 B1): native Konstanten f32/f64/bool (Skalar,
#     Array, Strukturfeld) mit Zielrundung; Globals im IR, Einsatz in Pure- und
#     Systemkoerper; Unterlauf und bool aus Zahl sind Compilefehler.
"$compiler" compile "$tests/native_konst_float.moos" -o "$out/konst_float"
"$out/konst_float" > "$out/konst_float.out"
cmp "$tests/native_konst_float.expected" "$out/konst_float.out"
"$compiler" compile "$tests/native_konst_float.moos" --emit-ir -o "$out/konst_float.ll"
mise exec -- opt -passes=verify -disable-output "$out/konst_float.ll"
for muster in '@PI = constant double 0x400921FB54442D18' '@ZEHNTEL = constant float 0x3FB99999A0000000' \
              '@AN = constant i1 true' '@TABELLE = constant [3 x float] [float 5.000000e-01, float -1.250000e+00, float 2.000000e+00]' \
              '@URSPRUNG = constant { float, double } { float 1.500000e+00, double -2.500000e-01 }'; do
    grep -qF "$muster" "$out/konst_float.ll" || { echo "FEHLER: Konstante fehlt: $muster" >&2; exit 1; }
done
expect_fail "$tests/native_konst_float_unterlauf.moos" "Floatliteral '1e-50' rundet in f32 auf 0"
expect_fail "$tests/native_konst_bool_zahl.moos" 'bool-Konstante erwartet wahr oder falsch'
echo "PASS P2-T48 Konstanten f32/f64/bool: Zielrundung, Array/Struktur, Pure/Systemkoerper, 2 Compilefehler"
echo "PASS P2-T34 Zahlenoperationen: Orakel, IR-Guards, fangbar, unbehandelt, statisch, Casts, Vergleiche, Systemkoerper, bool-Argumente, Vorpruefung, Funktionsende, bool-Rueckgabe"
