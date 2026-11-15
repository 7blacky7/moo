#!/usr/bin/env bash
# P2-T17 (Plan f5b52a0b B3): Zeigerarithmetik und zeiger(p)-Umdeutung im
# System-Emitter. Positiv gegen .expected (hosted, Aufruf aus dynamischem Moo)
# plus IR: GEP ohne inbounds, Differenz als exakte Division. Negativ:
# Compilefehler mit Grund.
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
t=compiler/tests/p2
out=build/p2-hosted/zeiger_arith
mkdir -p "$out"
"$compiler" compile "$t/zeiger_arith.moos" -o "$out/zeiger_arith" > "$out/compile.log" 2>&1 \
    || { cat "$out/compile.log" >&2; echo 'FEHLER: zeiger_arith kompiliert nicht' >&2; exit 1; }
"$out/zeiger_arith" > "$out/zeiger_arith.out"
diff -u "$t/zeiger_arith.expected" "$out/zeiger_arith.out" || { echo 'FEHLER: zeiger_arith weicht ab' >&2; exit 1; }
"$compiler" compile "$t/zeiger_arith.moos" --emit-ir -o "$out/zeiger_arith.ll" > /dev/null
mise exec -- opt -passes=verify -disable-output "$out/zeiger_arith.ll"
grep -qE '%sys\.zarith[0-9]* = getelementptr (i32|%[^ ]+|\{[^}]*\}|i8)' "$out/zeiger_arith.ll" \
    || { echo 'FEHLER: Zeigerarithmetik ohne (nicht-inbounds) GEP' >&2; exit 1; }
if grep -qE 'sys\.zarith[0-9]* = getelementptr inbounds' "$out/zeiger_arith.ll"; then
    echo 'FEHLER: Zeigerarithmetik darf nicht inbounds sein' >&2; exit 1
fi
grep -qE 'sys\.zdiff[0-9]* = sdiv exact i64' "$out/zeiger_arith.ll" \
    || { echo 'FEHLER: Zeigerdifferenz ohne sdiv exact' >&2; exit 1; }
echo 'PASS zeiger_arith (positiv + IR)'
for fall in "zeiger_arith_neg_void:Zeigerarithmetik auf ptr[void] ist nicht definiert" \
            "zeiger_arith_neg_summe:nur die Differenz p - q definiert" \
            "zeiger_arith_neg_typ:Zeigerdifferenz nur bei gleichem Zieltyp" \
            "zeiger_arith_neg_ordnung:Zeiger nur mit == oder != vergleichen" \
            "zeiger_arith_neg_mal:nur p + n, n + p, p - n und p - q definiert" \
            "zeiger_arith_neg_sicher:Zeigerarithmetik ist nur innerhalb von unsicher: erlaubt" \
            "zeiger_arith_neg_umdeuten:Aufruf 'zeiger' ist nur innerhalb von unsicher: erlaubt"; do
    n="${fall%%:*}"; grund="${fall#*:}"
    if "$compiler" compile "$t/$n.moos" --emit-ir -o "$out/$n.ll" > "$out/$n.err" 2>&1; then
        echo "FEHLER: $n: Compilefehler erwartet" >&2; exit 1
    fi
    grep -qF -- "$grund" "$out/$n.err" || { cat "$out/$n.err" >&2; echo "FEHLER: $n: Grund fehlt" >&2; exit 1; }
    echo "PASS $n (Compilefehler)"
done
echo 'PASS P2-T17 Zeigerarithmetik: 1 Positivfall (5 Funktionen), 7 Negativfaelle'
