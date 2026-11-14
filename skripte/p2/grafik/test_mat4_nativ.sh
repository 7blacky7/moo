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
