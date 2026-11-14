#!/usr/bin/env bash
# P2-T69: native Animations-Validierung (animation_pruefung_nativ.moos) gegen
# compiler/runtime/moo_compositor_animation.c. Weg wie test_effekt_damage_nativ.sh:
# beide Moo-Module bare bauen, IR-Verify, main lokalisieren, Export-Gate;
# animation_pruefung_nativ.o darf genau moo_n_anim_decode_signed offen haben
# (kommt aus animation_nativ.o). Die Probe #includet animation.c als Orakel.
# Argumente gehen an die Probe: [runden]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-animation-pruefung"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
mkdir -p -- "$out"

baue() { # $1 = Modulname, Rest = erwartete Exporte
    local name="$1"; shift
    "$compiler" compile "$grafik_dir/$name.moos" --no-stdlib --emit-ir \
        --target x86_64-bare -o "$out/$name.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$name.ll"
    "$compiler" compile "$grafik_dir/$name.moos" --no-stdlib \
        --target x86_64-bare -o "$out/$name.roh.o"
    mise exec -- llvm-objcopy --localize-symbol=main "$out/$name.roh.o" "$out/$name.o"
    local exporte
    exporte="$(mise exec -- llvm-nm --defined-only -g "$out/$name.o")"
    for f in "$@"; do
        grep -q " T $f\$" <<<"$exporte" || { echo "FEHLER: Export $f fehlt in $name" >&2; exit 1; }
    done
}

baue animation_nativ moo_n_anim_decode_signed
baue animation_pruefung_nativ moo_n_anim_limits_valid moo_n_anim_validate_value moo_n_anim_validate_desc

offen="$(mise exec -- llvm-nm -u "$out/animation_nativ.o")"
[ -z "$offen" ] || { echo "FEHLER: animation_nativ offen: $offen" >&2; exit 1; }
offen="$(mise exec -- llvm-nm -u "$out/animation_pruefung_nativ.o" | awk '{print $NF}' | sort | tr '\n' ' ')"
[ "$offen" = "moo_n_anim_decode_signed " ] || {
    echo "FEHLER: animation_pruefung_nativ offen: '$offen' (erwartet: moo_n_anim_decode_signed)" >&2; exit 1; }

# Bare-Objekte sind nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_animation_pruefung_nativ.c" \
    "$out/animation_pruefung_nativ.o" "$out/animation_nativ.o" -o "$out/test_animation_pruefung_nativ"
"$out/test_animation_pruefung_nativ" "$@"
