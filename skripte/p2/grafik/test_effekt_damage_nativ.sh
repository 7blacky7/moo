#!/usr/bin/env bash
# P2-T65 Damage-Paket: natives Moo (effekt_damage_nativ.moos) gegen
# compiler/runtime/moo_compositor_effects_damage.c. Gleicher Weg wie
# test_effekt_mathe_nativ.sh: IR-Verify, Objekte vom moo-Compiler
# (x86_64-bare), main lokalisieren, Export-/Offene-Symbole-Gate, Link in
# hosted C-Probe (SysV). Das Damage-Objekt ruft die nativen rect_* aus
# effekt_mathe_nativ.o per extern "C" - offen sein duerfen genau diese drei.
# Argumente gehen an die Probe: [runden] [bench_n] [bench_laeufe]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-damage"
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

baue effekt_mathe_nativ moo_n_rect_expand moo_n_rect_intersect moo_n_rect_union
baue effekt_damage_nativ moo_n_damage_propagate_backdrop moo_n_damage_build moo_n_damage_hash

offen="$(mise exec -- llvm-nm -u "$out/effekt_mathe_nativ.o")"
[ -z "$offen" ] || { echo "FEHLER: effekt_mathe_nativ offen: $offen" >&2; exit 1; }
offen="$(mise exec -- llvm-nm -u "$out/effekt_damage_nativ.o" | awk '{print $NF}' | sort | tr '\n' ' ')"
erlaubt="moo_n_rect_expand moo_n_rect_intersect moo_n_rect_union "
[ "$offen" = "$erlaubt" ] || { echo "FEHLER: effekt_damage_nativ offen: '$offen' (erwartet: '$erlaubt')" >&2; exit 1; }

# Bare-Objekte sind nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_effekt_damage_nativ.c" \
    "$project_dir/compiler/runtime/moo_compositor_effects_damage.c" \
    "$project_dir/compiler/runtime/moo_compositor_effects_math.c" \
    "$out/effekt_damage_nativ.o" "$out/effekt_mathe_nativ.o" -o "$out/test_effekt_damage_nativ"
"$out/test_effekt_damage_nativ" "$@"
