#!/usr/bin/env bash
# P2-T51 E5 (Plan f5b52a0b C1): Fluchtpruefung geliehener Zeiger. Positiv gegen
# .expected; je Verstossart ein Negativfall mit Meldung (Quelle und Senke).
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
t=compiler/tests/p2
out=build/p2-hosted/flucht
mkdir -p "$out"
"$compiler" compile "$t/flucht_positiv.moos" -o "$out/positiv" > "$out/positiv.log" 2>&1 \
    || { cat "$out/positiv.log" >&2; echo 'FEHLER: flucht_positiv kompiliert nicht' >&2; exit 1; }
"$out/positiv" > "$out/positiv.out"
diff -u "$t/flucht_positiv.expected" "$out/positiv.out" || { echo 'FEHLER: flucht_positiv weicht ab' >&2; exit 1; }
echo 'PASS flucht_positiv'
for fall in "flucht_neg_rueckgabe:entkommt per gib_zurück (adresse_von(x)" \
            "flucht_neg_alias:entkommt per gib_zurück (adresse_von(x)" \
            "flucht_neg_puffer:entkommt per gib_zurück (puffer_zeiger(p)" \
            "flucht_neg_speicher:in Speicher hinter 'h' geschrieben" \
            "flucht_neg_index:in Speicher hinter 'tabelle' geschrieben" \
            "flucht_neg_struktur:entkommt per gib_zurück (adresse_von(b)"; do
    n="${fall%%:*}"; grund="${fall#*:}"
    if "$compiler" compile "$t/$n.moos" --emit-ir -o "$out/$n.ll" > "$out/$n.err" 2>&1; then
        echo "FEHLER: $n: Compilefehler erwartet" >&2; exit 1
    fi
    grep -qF -- "$grund" "$out/$n.err" || { cat "$out/$n.err" >&2; echo "FEHLER: $n: Grund fehlt" >&2; exit 1; }
    echo "PASS $n (Compilefehler)"
done
# C3: Pin nur waehrend des Aufrufs - nach Koerper- und Rueckruf-Fehler (qsort)
# ist der Puffer entpinnt: anhängen gelingt, Daten bleiben erhalten.
"$compiler" compile "$t/pin_nach_fehler.moos" -o "$out/pin" > "$out/pin.log" 2>&1 \
    || { cat "$out/pin.log" >&2; echo 'FEHLER: pin_nach_fehler kompiliert nicht' >&2; exit 1; }
"$out/pin" > "$out/pin.out"
diff -u "$t/pin_nach_fehler.expected" "$out/pin.out" || { echo 'FEHLER: pin_nach_fehler weicht ab' >&2; exit 1; }
echo 'PASS pin_nach_fehler'
echo 'PASS P2-T51 Fluchtpruefung: 1 Positivfall (4 Funktionen), 6 Negativfaelle, Pin-Abbau nach Fehler'
