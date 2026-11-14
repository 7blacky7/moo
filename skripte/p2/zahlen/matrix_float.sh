#!/usr/bin/env bash
# P2-T9/T58 Batch B2 (Plan 4500717b): erzeugt das Moo-Programm der Float-Matrix
# (f32/f64: + - * /, unaeres -, 6 Vergleiche, Pure->Pure-Ketten) nach stdout.
# Das C-Orakel matrix_ref.c rechnet jede Zeile '<typ> <op> <a> [<b>] = <erg>'
# nach und formatiert wie moo_to_string (ganzzahlig: %lld, sonst %g).
# IEEE ohne Fast-Math: x/0 = +-Inf ohne Fehlerblock, NaN-Vergleiche falsch
# (ausser !=), -0 == 0. Was %g nicht sichtbar macht, pruefen bool-Faelle:
#   rund   (f32): f32-Summe != f64-Summe der f32-Operanden (0.1 + 0.2)
#   negkehr     : 1 / (-a), fuer a = 0 also -Inf (Vorzeichen von -0)
#   negeq       : -a == a (fuer a = 0 wahr: -0 == 0)
# NaN erscheint nur als Vergleichsoperand, nie als ausgegebenes Ergebnis
# (Vorzeichen eines erzeugten NaN ist plattformabhaengig).
# P2-T58 D2c (Plan f40b5980 G3): % wie C-fmod, ** wie C99-pow (moo_native_math.c):
#   remkehr     : 1 / (a % b), zeigt das Vorzeichen der Null (fmod(-4, 2) = -0)
#   fnan        : a % b oder a ** b ist NaN (x % 0, Inf % y, (-2) ** 0.5)
set -euo pipefail

for t in f32 f64; do
    for paar in add:+ sub:- mul:'*' div:/ rem:% pow:'**'; do
        printf '@nativ\nfunktion %s_%s(a: %s, b: %s) -> %s:\n    gib_zurück a %s b\n' "$t" "${paar%%:*}" "$t" "$t" "$t" "${paar#*:}"
    done
    for paar in eq:== ne:'!=' lt:'<' le:'<=' gt:'>' ge:'>='; do
        printf '@nativ\nfunktion %s_%s(a: %s, b: %s) -> bool:\n    gib_zurück a %s b\n' "$t" "${paar%%:*}" "$t" "$t" "${paar#*:}"
    done
    printf '@nativ\nfunktion %s_neg(a: %s) -> %s:\n    gib_zurück -a\n' "$t" "$t" "$t"
    printf '@nativ\nfunktion %s_kette(a: %s, b: %s) -> %s:\n    gib_zurück %s_add(%s_mul(a, b), b)\n' "$t" "$t" "$t" "$t" "$t" "$t"
    printf '@nativ\nfunktion %s_negkehr(a: %s) -> %s:\n    gib_zurück %s_div(1, %s_neg(a))\n' "$t" "$t" "$t" "$t" "$t"
    printf '@nativ\nfunktion %s_negeq(a: %s) -> bool:\n    gib_zurück %s_eq(%s_neg(a), a)\n' "$t" "$t" "$t" "$t"
    printf '@nativ\nfunktion %s_remkehr(a: %s, b: %s) -> %s:\n    gib_zurück %s_div(1, %s_rem(a, b))\n' "$t" "$t" "$t" "$t" "$t" "$t"
    printf '@nativ\nfunktion %s_fnan(a: %s, b: %s) -> bool:\n    gib_zurück %s_ne(%s_rem(a, b), %s_rem(a, b)) oder %s_ne(%s_pow(a, b), %s_pow(a, b))\n' "$t" "$t" "$t" "$t" "$t" "$t" "$t" "$t" "$t"
done
cat <<'EOF'
@nativ
funktion f32_rund(a: f32, b: f32) -> bool:
    gib_zurück als(f64, a + b) != als(f64, a) + als(f64, b)
setze inf auf f64_div(1, 0)
setze ninf auf f64_div(-1, 0)
setze nan auf f64_div(0, 0)
EOF

schleife() {  # typ op stelligkeit liste
    local t=$1 op=$2 n=$3 liste=$4
    printf 'für p in %s:\n' "$liste"
    if (( n == 1 )); then
        printf '    zeige "%s %s " + p[0] + " = " + %s_%s(p[0])\n' "$t" "$op" "$t" "$op"
    else
        printf '    zeige "%s %s " + p[0] + " " + p[1] + " = " + %s_%s(p[0], p[1])\n' "$t" "$op" "$t" "$op"
    fi
}
arith='[[0.1, 0.2], [1.5, -2.25], [3, 0.1], [inf, 1], [1, ninf], [0.5, 0.25], [0.000001, 3]]'
teil='[[1, 3], [1, 0], [-1, 0], [0, 1], [inf, 2], [1.5, -2.25], [0.1, 0.2], [1, ninf]]'
vgl='[[0.1, 0.2], [nan, nan], [nan, 1], [1, nan], [inf, ninf], [1, 1], [0.1, 0.1]]'
for t in f32 f64; do
    for op in add sub mul; do schleife "$t" "$op" 2 "$arith"; done
    schleife "$t" div 2 "$teil"
    schleife "$t" neg 1 '[[0.1], [inf], [3], [-2.25]]'
    schleife "$t" kette 2 '[[0.1, 0.2], [1.5, -2.25], [3, 0.5]]'
    schleife "$t" negkehr 1 '[[0], [2]]'
    schleife "$t" negeq 1 '[[0], [1]]'
    schleife "$t" rem 2 '[[5.5, 2], [-5.5, 2], [5.5, -2], [7, -3], [3, inf], [0.1, 0.03], [7, 7], [0, 1]]'
    schleife "$t" pow 2 '[[2, 10], [2, 0.5], [-2, 3], [10, -2], [1.5, 2.5], [0, -1], [1, nan], [nan, 0], [-1, inf], [0.5, ninf], [2, ninf], [ninf, 3], [inf, 0.5], [0.1, 0.2]]'
    schleife "$t" remkehr 2 '[[-4, 2], [4, 2], [-0.5, 0.25]]'
    schleife "$t" fnan 2 '[[1, 0], [inf, 2], [nan, 1], [-2, 0.5], [5, 3], [2, 0.5]]'
    for op in eq ne lt le gt ge; do schleife "$t" "$op" 2 "$vgl"; done
done
schleife f32 rund 2 '[[0.1, 0.2], [0.5, 0.25], [1, 3]]'
