#!/usr/bin/env bash
# P2-T51 C2 (Plan f5b52a0b): Zeigertyp-Identitaet an Aufrufgrenzen (extern C,
# @nativ, indirekter Aufruf). Jede Abweichung in Signedness, Float/Int,
# Zeigertiefe, Strukturidentitaet oder Funktionssignatur ist ein Compilefehler;
# ptr[T] -> ptr[void] als Argument bleibt still erlaubt (E4).
set -uo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
t=compiler/tests/p2
out=build/p2-hosted/typid
mkdir -p "$out"
fehler=0
for n in typid_extern_vorzeichen typid_extern_float typid_extern_tiefe typid_extern_struktur \
         typid_extern_void_zu_t typid_nativ_vorzeichen typid_nativ_struktur \
         typid_indirekt_signatur typid_indirekt_argument; do
    if "$compiler" compile "$t/$n.moos" --emit-ir -o "$out/$n.ll" > "$out/$n.err" 2>&1; then
        echo "OFFEN $n: akzeptiert (Compilefehler erwartet)"; fehler=1
    elif grep -qE "Argument 1 von '[a-z_0-9]+' erwartet Pointer" "$out/$n.err"; then
        echo "PASS $n: $(tail -n 1 "$out/$n.err")"
    else
        cat "$out/$n.err"; echo "FEHLER $n: Typdiagnose fehlt"; fehler=1
    fi
done
"$compiler" compile "$t/typid_void_positiv.moos" -o "$out/void_positiv" > "$out/void_positiv.log" 2>&1 \
    || { cat "$out/void_positiv.log"; echo 'FEHLER: typid_void_positiv kompiliert nicht'; exit 1; }
[ "$("$out/void_positiv")" = 0 ] || { echo 'FEHLER: typid_void_positiv Ausgabe'; exit 1; }
echo 'PASS typid_void_positiv (ptr[T] -> ptr[void] als Argument, Rueckweg ueber zeiger(p))'
exit "$fehler"
