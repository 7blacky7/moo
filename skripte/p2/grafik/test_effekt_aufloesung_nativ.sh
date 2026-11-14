#!/usr/bin/env bash
# P2-T70: native Effekt-Aufloesung + Flaechen-Effektzustand
# (effekt_aufloesung_nativ.moos) gegen compiler/runtime/moo_compositor_effects_state.c.
# Baut effekt_zustand_nativ (P2-T69) und effekt_aufloesung_nativ bare, IR-Verify,
# main lokalisieren, Export-Gate; effekt_aufloesung_nativ.o darf genau die vier
# Pruefer aus effekt_zustand_nativ.o offen haben. Die Probe #includet
# effects_state.c als Orakel (laut code_intel calls ohne Fremdaufrufe).
# Argumente gehen an die Probe: [runden]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-aufloesung"
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

baue effekt_zustand_nativ moo_n_effect_over_mask moo_n_effect_limits_valid moo_n_effect_state_valid moo_n_effect_estimate
baue effekt_aufloesung_nativ moo_n_effect_disable moo_n_effect_resolve_one moo_n_effect_resolve \
    moo_n_effect_access moo_n_effect_surface_init moo_n_effect_surface_destroy \
    moo_n_effect_state_preflight moo_n_effect_state_apply moo_n_effect_surface_status \
    moo_n_effect_state_hash moo_n_effect_surface_hash

offen="$(mise exec -- llvm-nm -u "$out/effekt_zustand_nativ.o")"
[ -z "$offen" ] || { echo "FEHLER: effekt_zustand_nativ offen: $offen" >&2; exit 1; }
offen="$(mise exec -- llvm-nm -u "$out/effekt_aufloesung_nativ.o" | awk '{print $NF}' | sort | tr '\n' ' ')"
erlaubt="moo_n_effect_estimate moo_n_effect_limits_valid moo_n_effect_over_mask moo_n_effect_state_valid "
[ "$offen" = "$erlaubt" ] || { echo "FEHLER: effekt_aufloesung_nativ offen: '$offen' (erwartet: '$erlaubt')" >&2; exit 1; }

# Bare-Objekte sind nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_effekt_aufloesung_nativ.c" \
    "$out/effekt_aufloesung_nativ.o" "$out/effekt_zustand_nativ.o" -o "$out/test_effekt_aufloesung_nativ"
"$out/test_effekt_aufloesung_nativ" "$@"
