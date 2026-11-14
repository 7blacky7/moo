#!/usr/bin/env bash
# P2-T65: natives Moo (effekt_mathe_nativ.moos) gegen
# compiler/runtime/moo_compositor_effects_math.c. Gleicher Weg wie
# test_mat4_nativ.sh: IR-Verify, Objekt vom moo-Compiler (x86_64-bare),
# main lokalisieren, Export-/Offene-Symbole-Gate, Link in hosted C-Probe (SysV).
# Argumente gehen an die Probe: [runden] [bench_n] [bench_laeufe]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-effekt"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
funktionen="moo_n_q16_mul moo_n_q16_div moo_n_rect_union moo_n_rect_intersect
moo_n_affine_transform_point moo_n_affine_inverse moo_n_transform_rect_aabb
moo_n_rect_expand moo_n_corners_normalize moo_n_rounded_coverage_a8 moo_n_compute_bounds"
mkdir -p -- "$out"

"$compiler" compile "$grafik_dir/effekt_mathe_nativ.moos" --no-stdlib --emit-ir \
    --target x86_64-bare -o "$out/effekt_mathe_nativ.ll"
mise exec -- opt -passes=verify -disable-output "$out/effekt_mathe_nativ.ll"
"$compiler" compile "$grafik_dir/effekt_mathe_nativ.moos" --no-stdlib \
    --target x86_64-bare -o "$out/effekt_mathe_nativ.roh.o"
mise exec -- llvm-objcopy --localize-symbol=main "$out/effekt_mathe_nativ.roh.o" "$out/effekt_mathe_nativ.o"
exporte="$(mise exec -- llvm-nm --defined-only -g "$out/effekt_mathe_nativ.o")"
for f in $funktionen; do
    grep -q " T $f\$" <<<"$exporte" || { echo "FEHLER: Export $f fehlt" >&2; exit 1; }
done
offen="$(mise exec -- llvm-nm -u "$out/effekt_mathe_nativ.o")"
[ -z "$offen" ] || { echo "FEHLER: unerwartete offene Symbole: $offen" >&2; exit 1; }

# Bare-Objekt ist nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_effekt_mathe_nativ.c" \
    "$project_dir/compiler/runtime/moo_compositor_effects_math.c" \
    "$out/effekt_mathe_nativ.o" -o "$out/test_effekt_mathe_nativ"
"$out/test_effekt_mathe_nativ" "$@"
