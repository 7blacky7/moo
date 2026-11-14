#!/usr/bin/env bash
# P2-T9/T58 Batch B1 (Plan 4500717b): erzeugt das Moo-Programm der Int-Matrix
# (Operation x Typ fuer i8..i64, u8..u64) nach stdout. test.sh uebersetzt es,
# und das C-Orakel matrix_ref.c rechnet jede Ausgabezeile
# '<typ> <op> <a> [<b>] = <ergebnis>' unabhaengig nach.
# 8-32 Bit: Operanden direkt aus dynamischem Code (exakt in f64).
# 64 Bit: Operanden als u32-Haelften 'hi:lo' (Bitmuster), nativ zusammengesetzt
# (i64_mk/u64_mk), Ergebnis als Haelften 'hi lo': nichts Nicht-Exaktes passiert
# die dyn-Grenze (box_native_wide). Nutzt Batch A: Pure->Pure-Aufrufe fuer
# i64/u64/u32/bool-Funktionen. Bash rechnet mit 64-Bit-Bitmustern (Wrap).
# Unaeres Minus auch fuer u8..u64 (Wrap 0 - a). Op 'lokal' belegt Batch A1:
# Locals anderer Typen als die Rueckgabe (u64 bzw. i64, bool, f64) rechnen
# a + b und wickeln zurueck; das Orakel erwartet dasselbe wie 'add'.
set -euo pipefail

binaer=(add:+ sub:- mul:'*' div:/ rem:% and:'&' or:'|' xor:'^' shl:'<<' shr:'>>' pow:'**'
        eq:== ne:'!=' lt:'<' le:'<=' gt:'>' ge:'>=')
typen=(i8 i16 i32 i64 u8 u16 u32 u64)

ist_vergleich() { [[ $1 =~ ^(eq|ne|lt|le|gt|ge)$ ]]; }

# --- native Operationsfunktionen je Typ ---
for t in "${typen[@]}"; do
    for paar in "${binaer[@]}"; do
        op=${paar%%:*} sym=${paar#*:}
        erg=$t; ist_vergleich "$op" && erg=bool
        printf '@nativ\nfunktion %s_%s(a: %s, b: %s) -> %s:\n    gib_zurück a %s b\n' "$t" "$op" "$t" "$t" "$erg" "$sym"
    done
    printf '@nativ\nfunktion %s_not(a: %s) -> %s:\n    gib_zurück ~a\n' "$t" "$t" "$t"
    printf '@nativ\nfunktion %s_neg(a: %s) -> %s:\n    gib_zurück -a\n' "$t" "$t" "$t"
    if [[ $t == u64 ]]; then lt=i64; else lt=u64; fi
    printf '@nativ\nfunktion %s_lokal(a: %s, b: %s) -> %s:\n    setze x: %s auf als_wrap(%s, a) + als_wrap(%s, b)\n    setze gleich: bool auf x == x\n    setze halb: f64 auf 0.5\n    gib_zurück als_wrap(%s, x)\n' \
        "$t" "$t" "$t" "$t" "$lt" "$lt" "$lt" "$t"
done
cat <<'EOF'
@nativ
funktion i64_hi(x: i64) -> u32:
    gib_zurück als_wrap(u32, als_wrap(u64, x) >> 32)
@nativ
funktion i64_lo(x: i64) -> u32:
    gib_zurück als_wrap(u32, x)
@nativ
funktion i64_mk(h: u32, l: u32) -> i64:
    gib_zurück als_wrap(i64, (als(u64, h) << 32) | als(u64, l))
@nativ
funktion u64_hi(x: u64) -> u32:
    gib_zurück als_wrap(u32, x >> 32)
@nativ
funktion u64_lo(x: u64) -> u32:
    gib_zurück als_wrap(u32, x)
@nativ
funktion u64_mk(h: u32, l: u32) -> u64:
    gib_zurück (als(u64, h) << 32) | als(u64, l)
EOF
# 64 Bit: Haelften-Wrapper je Operation (_h/_l Ergebnis, _k Vergleich).
for t in i64 u64; do
    ops=()
    for paar in "${binaer[@]}"; do ops+=("${paar%%:*}"); done
    ops+=(not neg lokal)
    for op in "${ops[@]}"; do
        if [[ $op == not || $op == neg ]]; then
            ps='ah: u32, al: u32'; innen="${t}_${op}(${t}_mk(ah, al))"
        else
            ps='ah: u32, al: u32, bh: u32, bl: u32'; innen="${t}_${op}(${t}_mk(ah, al), ${t}_mk(bh, bl))"
        fi
        if ist_vergleich "$op"; then
            printf '@nativ\nfunktion %s_%s_k(%s) -> bool:\n    gib_zurück %s\n' "$t" "$op" "$ps" "$innen"
        else
            for h in hi lo; do
                printf '@nativ\nfunktion %s_%s_%s(%s) -> u32:\n    gib_zurück %s_%s(%s)\n' "$t" "$op" "${h:0:1}" "$ps" "$t" "$h" "$innen"
            done
        fi
    done
done

# --- Faelle: Listen + Schleifen ---
haelften() { printf '%d, %d' $(( ($1 >> 32) & 0xFFFFFFFF )) $(( $1 & 0xFFFFFFFF )); }
fallschleife() {  # typ op bits faelle... (je Fall "a" oder "a,b")
    local t=$1 op=$2 bits=$3; shift 3
    local liste="" f a b zeile
    for f in "$@"; do
        a=${f%%,*}; b=""; [[ $f == *,* ]] && b=${f#*,}
        if (( bits == 64 )); then
            zeile="$(haelften "$a")"; [[ -n $b ]] && zeile+=", $(haelften "$b")"
        else
            zeile=$a; [[ -n $b ]] && zeile+=", $b"
        fi
        liste+="${liste:+, }[$zeile]"
    done
    printf 'für p in [%s]:\n' "$liste"
    local einstellig=0; [[ $1 != *,* ]] && einstellig=1
    if (( bits < 64 )); then
        if (( einstellig )); then
            printf '    zeige "%s %s " + p[0] + " = " + %s_%s(p[0])\n' "$t" "$op" "$t" "$op"
        else
            printf '    zeige "%s %s " + p[0] + " " + p[1] + " = " + %s_%s(p[0], p[1])\n' "$t" "$op" "$t" "$op"
        fi
        return
    fi
    local args='p[0], p[1]' label='p[0] + ":" + p[1]'
    (( einstellig )) || { args='p[0], p[1], p[2], p[3]'; label+=' + " " + p[2] + ":" + p[3]'; }
    if ist_vergleich "$op"; then
        printf '    zeige "%s %s " + %s + " = " + %s_%s_k(%s)\n' "$t" "$op" "$label" "$t" "$op" "$args"
    else
        printf '    zeige "%s %s " + %s + " = " + %s_%s_h(%s) + " " + %s_%s_l(%s)\n' "$t" "$op" "$label" "$t" "$op" "$args" "$t" "$op" "$args"
    fi
}

for t in "${typen[@]}"; do
    bits=${t:1}
    if [[ $t == i* ]]; then
        lo=$(( -(1 << (bits - 1)) )); hi=$(( (1 << (bits - 1)) - 1 ))   # 64: Bash-Wrap liefert MIN/MAX
        arith=("$hi,1" "$lo,-1" "$lo,$lo" "-7,3" "7,-3" "$hi,$hi" "$lo,1")
        teil=("-7,3" "7,-3" "-7,-3" "$hi,-1" "$lo,1" "$lo,2" "$hi,$hi")
        bit=("$lo,-1" "$hi,$lo" "-7,3" "5,3")
        shl=("1,$(( bits - 1 ))" "-1,1" "$hi,1" "3,0")
        shr=("$lo,$(( bits - 1 ))" "-1,1" "$hi,$(( bits - 1 ))" "-7,1")
        nicht=(0 -1 "$lo" "$hi"); neg=("$hi" -1 0 "$lo")
        pot=("2,$(( bits - 2 ))" "2,$(( bits - 1 ))" "3,5" "-1,$hi" "$lo,1" "$lo,2" "-2,$(( bits - 1 ))" "7,0" "0,0" "1,$hi" "-3,3" "$hi,2" "$hi,$hi" "-1,0" "2,1")
        vgl=("$lo,$hi" "-1,0" "0,0" "$hi,$lo" "-1,1")
    else
        if (( bits == 64 )); then max=-1; else max=$(( (1 << bits) - 1 )); fi   # 64: Bitmuster
        halb=$(( 1 << (bits - 1) ))
        arith=("$max,1" "0,1" "$max,$max" "7,3" "$halb,2")
        teil=("7,3" "$max,2" "$max,$max" "$halb,3" "0,5")
        bit=("$max,0" "$halb,$max" "12,10" "7,3")
        shl=("1,$(( bits - 1 ))" "$max,1" "3,0" "$halb,1")
        shr=("$max,$(( bits - 1 ))" "$halb,1" "$max,1" "7,0")
        nicht=(0 "$max" "$halb"); neg=(0 1 "$max" "$halb")
        pot=("2,$(( bits - 1 ))" "3,5" "$max,2" "$max,$max" "0,0" "7,0" "$halb,2" "3,$(( bits + 1 ))")
        vgl=("0,$max" "$halb,$(( halb - 1 ))" "$max,$max" "1,0")
    fi
    for op in add sub mul lokal; do fallschleife "$t" "$op" "$bits" "${arith[@]}"; done
    for op in div rem; do fallschleife "$t" "$op" "$bits" "${teil[@]}"; done
    for op in and or xor; do fallschleife "$t" "$op" "$bits" "${bit[@]}"; done
    fallschleife "$t" shl "$bits" "${shl[@]}"
    fallschleife "$t" shr "$bits" "${shr[@]}"
    fallschleife "$t" pow "$bits" "${pot[@]}"
    fallschleife "$t" not "$bits" "${nicht[@]}"
    (( ${#neg[@]} )) && fallschleife "$t" neg "$bits" "${neg[@]}"
    for op in eq ne lt le gt ge; do fallschleife "$t" "$op" "$bits" "${vgl[@]}"; done
done
# P2-T58 G2 (Plan f40b5980, Review 06d10060 #6): ** fuer 8 Bit vollstaendig -
# i8 alle 256 Basen x alle 256 Exponenten-Bitmuster (-128..127; negativ = Fehler-
# vertrag: fangbar, Ausgabe 'fehler'), u8 alle 256 x 256. Ergebnis erst in eine
# Variable (Fehler im Teilausdruck, Gotcha try-fehler-in-teil-ausdruck). Dazu
# negative Exponenten fuer i16/i32 als Stichprobe.
cat <<'EOF'
setze a auf -128
solange a < 128:
    setze b auf -128
    solange b < 128:
        versuche:
            setze r auf i8_pow(a, b)
            zeige "i8 pow " + a + " " + b + " = " + r
        fange f:
            zeige "i8 pow " + a + " " + b + " = fehler"
        setze b auf b + 1
    setze a auf a + 1
für p in [[2, -1], [-32768, -1], [0, -32768], [1, -2]]:
    versuche:
        setze r auf i16_pow(p[0], p[1])
        zeige "i16 pow " + p[0] + " " + p[1] + " = " + r
    fange f:
        zeige "i16 pow " + p[0] + " " + p[1] + " = fehler"
für p in [[2, -1], [-2147483648, -1], [0, -2147483648], [-1, -7]]:
    versuche:
        setze r auf i32_pow(p[0], p[1])
        zeige "i32 pow " + p[0] + " " + p[1] + " = " + r
    fange f:
        zeige "i32 pow " + p[0] + " " + p[1] + " = fehler"
setze a auf 0
solange a < 256:
    setze b auf 0
    solange b < 256:
        zeige "u8 pow " + a + " " + b + " = " + u8_pow(a, b)
        setze b auf b + 1
    setze a auf a + 1
EOF
