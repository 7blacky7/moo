#!/usr/bin/env bash
# P2-T26: natives Moo-mat4 (mat4_nativ.moos) gegen compiler/runtime/moo_3d_math.c.
# Standard (MOO_PFAD=objekt): das Objekt, das der moo-Compiler selbst schreibt
# (x86_64-bare, Systemkoerper, @einstieg-Exporte, eigene Optimierung), wird in
# die hosted C-Probe gelinkt (SysV). Diagnose (MOO_PFAD=ir): --emit-ir,
# llvm-extract, optional opt -$MOO_IR_OPT, llc -O2 (Weg aus skripte/p7/lader/mise.toml).
# Argumente gehen an die Probe: [runden] [bench_n] [bench_laeufe]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-nativ"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
pfad="${MOO_PFAD:-objekt}"
funktionen="moo_n_mat4_identity moo_n_mat4_multiply moo_n_mat4_translate moo_n_mat4_scale moo_n_mat4_ortho
moo_n_mat4_perspective moo_n_mat4_lookat moo_n_mat4_rotate moo_n_mat4_multiply_skalar
moo_n_mat4_translate_skalar moo_n_mat4_scale_skalar moo_n_mat4_rotate_skalar
moo_n_matrix_stack_init moo_n_matrix_stack_push moo_n_matrix_stack_pop"
mkdir -p -- "$out"

# IR-Gate fuer beide Pfade: verifizierbar, strikte IEEE-Semantik.
"$compiler" compile "$grafik_dir/mat4_nativ.moos" --no-stdlib --emit-ir \
    --target x86_64-bare -o "$out/mat4_nativ.ll"
mise exec -- opt -passes=verify -disable-output "$out/mat4_nativ.ll"
if grep -qE 'f(mul|add|sub|div|neg) (fast|contract|reassoc|nnan|ninf|nsz|arcp|afn)|fmuladd' "$out/mat4_nativ.ll"; then
    echo "FEHLER: Fast-Math/Kontraktion im Moo-IR" >&2; exit 1
fi

case "$pfad" in
objekt)
    "$compiler" compile "$grafik_dir/mat4_nativ.moos" --no-stdlib \
        --target x86_64-bare -o "$out/mat4_nativ.roh.o"
    # Das Bare-Objekt bringt ein eigenes main mit; fuer die C-Probe lokal machen.
    mise exec -- llvm-objcopy --localize-symbol=main "$out/mat4_nativ.roh.o" "$out/mat4_nativ.o"
    exporte="$(mise exec -- llvm-nm --defined-only -g "$out/mat4_nativ.o")"
    for f in $funktionen; do
        grep -q " T $f\$" <<<"$exporte" \
            || { echo "FEHLER: Export $f fehlt im Compiler-Objekt" >&2; exit 1; }
    done
    # Nur libm darf offen bleiben (gleiche Funktionen wie die C-Version).
    offen="$(mise exec -- llvm-nm -u "$out/mat4_nativ.o" | awk '{print $2}' | grep -vxE 'sinf|cosf|tanf|sqrtf' || true)"
    [ -z "$offen" ] || { echo "FEHLER: unerwartete offene Symbole: $offen" >&2; exit 1; }
    echo "PFAD: moo-Compiler-Objekt (x86_64-bare)"
    ;;
ir)
    sel=""
    for f in $funktionen; do sel="$sel --func=$f"; done
    mise exec -- llvm-extract -S $sel --recursive "$out/mat4_nativ.ll" -o "$out/mat4_nativ.extract.ll"
    ir="$out/mat4_nativ.extract.ll"
    if [ -n "${MOO_IR_OPT:-}" ]; then
        mise exec -- opt "-${MOO_IR_OPT}" -S "$ir" -o "$out/mat4_nativ.opt.ll"
        ir="$out/mat4_nativ.opt.ll"
    fi
    mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$ir" -o "$out/mat4_nativ.o"
    echo "PFAD: Diagnose IR -> llc${MOO_IR_OPT:+ (opt -$MOO_IR_OPT)}"
    ;;
*) echo "FEHLER: MOO_PFAD=$pfad unbekannt (objekt|ir)" >&2; exit 1 ;;
esac

# Bare-Objekt ist nicht PIC (RelocMode::Static fuer *-none, codegen.rs) -> -no-pie.
${CC:-cc} -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_mat4_nativ.c" "$project_dir/compiler/runtime/moo_3d_math.c" \
    "$out/mat4_nativ.o" -lm -o "$out/test_mat4_nativ"
"$out/test_mat4_nativ" "$@"

# ---- P2-T16 (Plan 3f03704a D1-D4): eingebaute vec2/vec3/vec4/mat4 ----
# D1: hosted Fixtures (positiv byteidentisch zu .expected, negativ
# Compilefehler mit Grund), Warnung fuer eine eigene struktur vec2.
# `importiere vektor` sucht ./stdlib relativ zum Arbeitsverzeichnis.
cd "$project_dir"
t="$project_dir/compiler/tests/p2"
for n in vecmat_typen vecmat_bibliothek vecmat_eigene vecmat_spaltenindex; do
    "$compiler" compile "$t/$n.moos" -o "$out/$n" >"$out/$n.compile" 2>&1 \
        || { cat "$out/$n.compile" >&2; echo "FEHLER: $n kompiliert nicht" >&2; exit 1; }
    "$out/$n" >"$out/$n.out"
    diff -u "$t/$n.expected" "$out/$n.out" || { echo "FEHLER: $n weicht ab" >&2; exit 1; }
    echo "PASS $n"
done
grep -qF "eigene struktur 'vec2' ueberdeckt" "$out/vecmat_eigene.compile" \
    || { echo "FEHLER: Warnung fuer eigene struktur vec2 fehlt" >&2; exit 1; }
for fall in "vecmat_neg_mischung:vec3 + vec4 ist nicht definiert" \
            "vecmat_neg_ganzzahl:Skalar einer Vektorrechnung erwartet f32, erhalten i32" \
            "vecmat_neg_plus_skalar:vec2 + Zahl ist nicht definiert" \
            "vecmat_neg_ordnung:vec3 nur mit == oder != vergleichen" \
            "vecmat_neg_mat_vec3:mat4 * vec3 ist nicht definiert" \
            "vecmat_neg_kreuz:kreuz ist nur fuer vec3/dvec3 definiert" \
            "vecmat_neg_spalte_konstant:M-M001" \
            "vecmat_neg_komponente_konstant:konstanter Komponentenindex 3 liegt ausserhalb von vec3" \
            "vecmat_neg_spalte_mischung:erwartet vec4, erhalten dvec4" \
            "vecmat_neg_komponente_mischung:erwartet f32, erhalten f64" \
            "vecmat_neg_komponente_feld:eine Komponente ist kein Vektor" \
            "vecmat_neg_spalte_negativ:konstanter Spaltenindex -1 liegt ausserhalb von mat4" \
            "vecmat_neg_komponente_negativ:konstanter Komponentenindex -1 liegt ausserhalb von vec3" \
            "native_array_neg_negativ:konstanter Index -1 liegt ausserhalb von [T; 3]"; do
    n="${fall%%:*}"; grund="${fall#*:}"
    if "$compiler" compile "$t/$n.moos" -o "$out/$n" >"$out/$n.err" 2>&1; then
        echo "FEHLER: $n: Compilefehler erwartet" >&2; exit 1
    fi
    grep -qF -- "$grund" "$out/$n.err" || { cat "$out/$n.err" >&2; echo "FEHLER: $n: Grund fehlt" >&2; exit 1; }
    echo "PASS $n (Compilefehler)"
done

# D3: IR-Gate - verifizierbar, keine Fast-Math/Kontraktion, SIMD im Rechenweg.
"$compiler" compile "$grafik_dir/vecmat_nativ.moos" --no-stdlib --emit-ir \
    --target x86_64-bare -o "$out/vecmat_nativ.ll"
mise exec -- opt -passes=verify -disable-output "$out/vecmat_nativ.ll"
if grep -qE 'f(mul|add|sub|div|neg) (fast|contract|reassoc|nnan|ninf|nsz|arcp|afn)|fmuladd|llvm\.fma' "$out/vecmat_nativ.ll"; then
    echo "FEHLER: Fast-Math/Kontraktion im vecmat-IR" >&2; exit 1
fi
for muster in 'fmul <4 x float>' 'fadd <4 x float>' 'shufflevector <3 x float>' 'llvm.sqrt.f32'; do
    grep -qF -- "$muster" "$out/vecmat_nativ.ll" || { echo "FEHLER: '$muster' fehlt im vecmat-IR" >&2; exit 1; }
done
# Plan-Risiko/D3 (Review #38135): der Rueckfallpfad in struct_place (Strukturwert
# als Aggregat ueber insertvalue-Ketten, Ursache der 3x im Pilot) darf fuer
# vecmat-Ausdruecke nicht erreicht werden. Je Exportfunktion samt aller Callees
# (llvm-extract --recursive, z. B. mat4_einheit/mat4_drehung aus stdlib/vektor):
# kein insertvalue; Rechenfunktionen muessen ueber <N x float> laufen.
for f in moo_n_vm_mat4_vec4 moo_n_vm_mat4_mul moo_n_vm_mat4_vec4_viele moo_n_vm_identity \
         moo_n_vm_translate moo_n_vm_scale moo_n_vm_rotate moo_n_vm_perspective moo_n_vm_lookat \
         moo_n_vm_ortho moo_n_vm_ops moo_n_vm_punkt moo_n_vm_kreuz moo_n_vm_betrag \
         moo_n_vm_normalisiere moo_n_vm_gleich; do
    mise exec -- llvm-extract -S --func="$f" --recursive "$out/vecmat_nativ.ll" -o "$out/vecmat_fn_$f.ll"
    grep -qE "^define .*@$f\(" "$out/vecmat_fn_$f.ll" || { echo "FEHLER: $f fehlt im vecmat-IR" >&2; exit 1; }
    if grep -qE '(^|[^a-z_])insertvalue ' "$out/vecmat_fn_$f.ll"; then
        grep -nE '(^|[^a-z_])insertvalue ' "$out/vecmat_fn_$f.ll" >&2
        echo "FEHLER: insertvalue-Rueckfall in $f" >&2; exit 1
    fi
    # Reine Konstruktoren (mat4_einheit/-perspektive/-orthografisch) rechnen
    # keine Vektorausdruecke; fuer sie gilt nur der insertvalue-Ausschluss.
    case "$f" in moo_n_vm_identity|moo_n_vm_perspective|moo_n_vm_ortho) continue ;; esac
    grep -qE '<[234] x float>' "$out/vecmat_fn_$f.ll" \
        || { echo "FEHLER: $f rechnet nicht ueber <N x float>" >&2; exit 1; }
done
echo "PASS vecmat IR-Gate (inkl. insertvalue-Ausschluss je Funktion)"

# D2/D4: Compiler-Objekt (x86_64-bare) gegen moo_3d_math.c und mat_ref.c,
# bitgleich inkl. Alias/Sonderwerten; Benchmark 10^6 mat4*vec4 (Median aus 7).
"$compiler" compile "$grafik_dir/vecmat_nativ.moos" --no-stdlib \
    --target x86_64-bare -o "$out/vecmat_nativ.roh.o"
mise exec -- llvm-objcopy --localize-symbol=main "$out/vecmat_nativ.roh.o" "$out/vecmat_nativ.o"
# P2-T89: stdlib/vektor rechnet sin/cos/tan ueber moo_native_* (ohne libm);
# das Objekt referenziert sie, die C-Gegenprobe nutzt dieselben Funktionen.
${CC:-cc} -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -no-pie -I"$grafik_dir" \
    -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_vecmat_nativ.c" "$grafik_dir/mat_ref.c" "$project_dir/compiler/runtime/moo_3d_math.c" \
    "$project_dir/compiler/runtime/moo_native_math.c" \
    "$out/vecmat_nativ.o" -lm -o "$out/test_vecmat_nativ"
"$out/test_vecmat_nativ" "${MOO_VECMAT_RUNDEN:-20000}" "${MOO_VECMAT_BENCH_N:-1000000}" 7

# D4 hosted (Review #38135): dynamischer @nativ-Aufruf ueber puffer[vec4] mit
# 10^6 Elementen (mat4 als Wert aus dynamischem Moo; Zeit inkl. Pin/Adapter)
# gegen die C-Basis mit identischer Eingabe; Median aus 7, Pruefsumme gleich.
"$compiler" compile "$grafik_dir/vecmat_hosted_bench.moos" -o "$out/vecmat_hosted_bench"
"$out/vecmat_hosted_bench" >"$out/vecmat_hosted_bench.out"
${CC:-cc} -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror \
    "$grafik_dir/bench_vecmat_hosted.c" -o "$out/bench_vecmat_hosted"
"$out/bench_vecmat_hosted" >"$out/bench_vecmat_hosted.out"
[ "$(wc -l <"$out/vecmat_hosted_bench.out")" -eq 8 ] \
    || { cat "$out/vecmat_hosted_bench.out" >&2; echo "FEHLER: Hosted-Bench-Ausgabe unerwartet" >&2; exit 1; }
moo_ms="$(head -n 7 "$out/vecmat_hosted_bench.out" | sort -g | sed -n 4p)"
moo_summe="$(sed -n 8p "$out/vecmat_hosted_bench.out")"
c_ms="$(awk '$1 == "C_MS" {print $2}' "$out/bench_vecmat_hosted.out")"
c_summe="$(awk '$1 == "PRUEFSUMME" {print $2}' "$out/bench_vecmat_hosted.out")"
awk -v a="$moo_summe" -v b="$c_summe" 'BEGIN { exit !(a + 0 == b + 0 && b + 0 > 0) }' \
    || { echo "FEHLER: Hosted-Pruefsumme moo $moo_summe != C $c_summe" >&2; exit 1; }
faktor="$(awk -v a="$moo_ms" -v b="$c_ms" 'BEGIN { printf "%.2f", a / b }')"
echo "BENCH hosted dyn puffer[vec4] n=1000000: moo $moo_ms ms, C $c_ms ms, Faktor $faktor, Pruefsumme $c_summe (gleich)"
