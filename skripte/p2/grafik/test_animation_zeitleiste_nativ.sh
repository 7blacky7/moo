#!/usr/bin/env bash
# P2-T71: native Animations-Timeline (animation_zeitleiste_nativ.moos) gegen
# compiler/runtime/moo_compositor_animation.c. Baut animation_nativ (eval_slot),
# animation_pruefung_nativ (limits_valid/validate_desc) und
# animation_zeitleiste_nativ bare, IR-Verify, main lokalisieren, Export-Gate;
# die Timeline darf genau ihre drei extern-"C"-Abhaengigkeiten offen haben,
# animation_pruefung_nativ genau moo_n_anim_decode_signed. Die Probe
# #includet animation.c (laut code_intel calls ohne Fremdaufrufe).
# Argumente gehen an die Probe: [ablaeufe] [schritte]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-zeitleiste"
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

offen_genau() { # $1 = Modul, $2 = erwartete offene Symbole (sortiert, Leerzeichen-getrennt, mit Endleerzeichen)
    local offen
    offen="$(mise exec -- llvm-nm -u "$out/$1.o" | awk '{print $NF}' | sort | tr '\n' ' ')"
    [ "$offen" = "$2" ] || { echo "FEHLER: $1 offen: '$offen' (erwartet: '$2')" >&2; exit 1; }
}

baue animation_nativ moo_n_anim_eval_slot moo_n_anim_decode_signed
baue animation_pruefung_nativ moo_n_anim_limits_valid moo_n_anim_validate_desc
baue animation_zeitleiste_nativ moo_n_anim_timeline_init moo_n_anim_start moo_n_anim_evaluate \
    moo_n_anim_cancel moo_n_anim_destroy_surface moo_n_anim_active_count moo_n_anim_state_hash

offen_genau animation_nativ ""
offen_genau animation_pruefung_nativ "moo_n_anim_decode_signed "
offen_genau animation_zeitleiste_nativ "moo_n_anim_eval_slot moo_n_anim_limits_valid moo_n_anim_validate_desc "

# Bare-Objekte sind nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_animation_zeitleiste_nativ.c" \
    "$project_dir/compiler/runtime/moo_compositor_effects_state.c" \
    "$out/animation_zeitleiste_nativ.o" "$out/animation_pruefung_nativ.o" "$out/animation_nativ.o" \
    -o "$out/test_animation_zeitleiste_nativ"
"$out/test_animation_zeitleiste_nativ" "$@"
