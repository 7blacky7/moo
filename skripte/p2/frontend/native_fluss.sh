#!/usr/bin/env bash
# P2-T12 (Plan 31194239 D1-D4): nativer Kontrollfluss in sicheren @nativ-Koerpern
# (wenn/solange/für ohne unsicher:). Positivfall byteidentisch zu .expected,
# Negativfaelle mit Diagnose, IR: verify, Schleife roh ohne Aufruf und mit
# eigenem Schritt-Block, nach O2 ohne call/alloca.
set -eu
cd "$(dirname "$0")/../../.."
d=build/p2-frontend/native_fluss
t=compiler/tests/p2
mkdir -p "$d"
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"

n=native_fluss
timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n"
timeout 30s "$d/$n" > "$d/$n.out" 2> "$d/$n.err"
cmp "$t/$n.expected" "$d/$n.out"
if grep -q 'NACH_FALSCH' "$d/$n.out"; then echo "FAIL $n: Code nach dem Fehler lief" >&2; exit 1; fi
echo "PASS $n"

# Zusammengesetzte Zuweisung (+=, -=) an Variablen in nativen Koerpern.
n=native_verbund
timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n"
timeout 30s "$d/$n" > "$d/$n.out"
cmp "$t/$n.expected" "$d/$n.out"
echo "PASS $n"

for fall in "native_fluss_neg_zaehler:kann nicht neu gesetzt werden" \
            "native_verbund_feld:M-S005" \
            "native_verbund_index:v[i] = v[i] - 1" \
            "native_verbund_aufruf:zweimal ausgewertet" \
            "native_verbund_aufruf_tief:zweimal ausgewertet" \
            "native_verbund_typ:Ungueltiges Ganzzahlliteral" \
            "native_fluss_neg_nach:ist nicht deklariert" \
            "native_fluss_neg_float:braucht Ganzzahlgrenzen" \
            "native_fluss_neg_liste:erwartet einen Bereich a..b" \
            "native_fluss_neg_zeiger:Aufruf 'zeiger' ist nur innerhalb von unsicher:" \
            "native_fluss_neg_adresse:Aufruf 'adresse_von' ist nur innerhalb von unsicher:" \
            "native_fluss_neg_speicher:Aufruf 'speicher_lesen' ist nur innerhalb von unsicher:" \
            "native_fluss_neg_konv:Aufruf 'u8' ist nur innerhalb von unsicher:" \
            "native_fluss_neg_extern:Aufruf 'abs' ist nur innerhalb von unsicher:" \
            "native_fluss_neg_fnzeiger:Aufruf 'g' ist nur innerhalb von unsicher:"; do
  n=${fall%%:*} text=${fall#*:}
  if timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n" > "$d/$n.log" 2>&1; then
    echo "FAIL $n: Compilefehler erwartet" >&2; exit 1
  fi
  grep -qF "$text" "$d/$n.log" || { cat "$d/$n.log" >&2; exit 1; }
  echo "PASS $n (Compilefehler)"
done

# D3: IR verify; summe_bis roh mit Schritt-Block und ohne Aufruf, nach O2 ohne
# call/alloca (Zaehler und Summe in Registern).
timeout 30s "$compiler" compile "$t/native_fluss.moos" --emit-ir -o "$d/native_fluss.ll"
mise exec -- opt -passes=verify -disable-output "$d/native_fluss.ll"
mise exec -- llvm-extract -S --func=__moo_native.summe_bis "$d/native_fluss.ll" -o "$d/summe_bis.ll"
grep -q 'native.loop.step' "$d/summe_bis.ll" || { echo "FAIL summe_bis: kein Schritt-Block" >&2; exit 1; }
if grep -q ' call ' "$d/summe_bis.ll"; then echo "FAIL summe_bis: Aufruf im rohen IR" >&2; exit 1; fi
mise exec -- opt -passes='default<O2>' -S "$d/summe_bis.ll" -o "$d/summe_bis.o2.ll"
if grep -qE ' call | alloca ' "$d/summe_bis.o2.ll"; then echo "FAIL summe_bis: call/alloca nach O2" >&2; exit 1; fi
# D3 Bare (x86_64-bare): Fehlervertrag llvm.trap statt moo_native_fehler.
timeout 30s "$compiler" compile "$t/native_fluss_bare.moos" --no-stdlib --emit-ir --target x86_64-bare -o "$d/bare.ll"
mise exec -- opt -passes=verify -disable-output "$d/bare.ll"
grep -q 'llvm.trap' "$d/bare.ll" || { echo "FAIL bare: kein llvm.trap" >&2; exit 1; }
if grep -q 'moo_native_fehler' "$d/bare.ll"; then echo "FAIL bare: moo_native_fehler im Bare-IR" >&2; exit 1; fi
grep -q 'native.loop.step' "$d/bare.ll" || { echo "FAIL bare: kein Schritt-Block" >&2; exit 1; }
echo "PASS P2-T12 nativer Kontrollfluss: 19 Ausgaben, Verbundzuweisung, 15 Compilefehler, IR (verify, Schritt-Block, O2 ohne call/alloca), Bare-IR (llvm.trap)"
