#!/usr/bin/env bash
# P2-T68: natives Moo (animation_nativ.moos) gegen
# compiler/runtime/moo_compositor_animation.c. Gleicher Weg wie
# test_effekt_mathe_nativ.sh: IR-Verify, Objekt vom moo-Compiler
# (x86_64-bare), main lokalisieren, Export-/Offene-Symbole-Gate, Link in
# hosted C-Probe (SysV). Die Probe #includet animation.c (statische Helfer
# als Orakel); animation.c ruft keine Funktionen anderer Runtime-Dateien.
# Argumente gehen an die Probe: [runden] [bench_n] [bench_laeufe]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-animation"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
funktionen="moo_n_anim_decode_signed moo_n_anim_encode_signed moo_n_anim_round_signed_div
moo_n_anim_round_unsigned_div moo_n_anim_ease moo_n_anim_interpolate
moo_n_anim_direction_forward moo_n_anim_eval_slot"
mkdir -p -- "$out"

"$compiler" compile "$grafik_dir/animation_nativ.moos" --no-stdlib --emit-ir \
    --target x86_64-bare -o "$out/animation_nativ.ll"
mise exec -- opt -passes=verify -disable-output "$out/animation_nativ.ll"
"$compiler" compile "$grafik_dir/animation_nativ.moos" --no-stdlib \
    --target x86_64-bare -o "$out/animation_nativ.roh.o"
mise exec -- llvm-objcopy --localize-symbol=main "$out/animation_nativ.roh.o" "$out/animation_nativ.o"
exporte="$(mise exec -- llvm-nm --defined-only -g "$out/animation_nativ.o")"
for f in $funktionen; do
    grep -q " T $f\$" <<<"$exporte" || { echo "FEHLER: Export $f fehlt" >&2; exit 1; }
done
offen="$(mise exec -- llvm-nm -u "$out/animation_nativ.o")"
[ -z "$offen" ] || { echo "FEHLER: unerwartete offene Symbole: $offen" >&2; exit 1; }

# Bare-Objekt ist nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_animation_nativ.c" \
    "$out/animation_nativ.o" -o "$out/test_animation_nativ"
"$out/test_animation_nativ" "$@"
