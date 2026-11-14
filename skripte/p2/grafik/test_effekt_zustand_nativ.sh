#!/usr/bin/env bash
# P2-T69 Folgepaket: native Effekt-Zustandspruefung (effekt_zustand_nativ.moos)
# gegen compiler/runtime/moo_compositor_effects_state.c. Weg wie
# test_effekt_mathe_nativ.sh: IR-Verify, Objekt vom moo-Compiler (x86_64-bare),
# main lokalisieren, Export-/Offene-Symbole-Gate, Link in hosted C-Probe.
# Die Probe #includet effects_state.c (ruft laut code_intel calls nur eigene
# Funktionen) als Orakel fuer die statischen Pruefer.
# Argumente gehen an die Probe: [runden]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-zustand"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
funktionen="moo_n_effect_limits_valid moo_n_effect_state_config_init moo_n_effect_state_valid
moo_n_effect_over_mask moo_n_effect_estimate"
mkdir -p -- "$out"

"$compiler" compile "$grafik_dir/effekt_zustand_nativ.moos" --no-stdlib --emit-ir \
    --target x86_64-bare -o "$out/effekt_zustand_nativ.ll"
mise exec -- opt -passes=verify -disable-output "$out/effekt_zustand_nativ.ll"
"$compiler" compile "$grafik_dir/effekt_zustand_nativ.moos" --no-stdlib \
    --target x86_64-bare -o "$out/effekt_zustand_nativ.roh.o"
mise exec -- llvm-objcopy --localize-symbol=main "$out/effekt_zustand_nativ.roh.o" "$out/effekt_zustand_nativ.o"
exporte="$(mise exec -- llvm-nm --defined-only -g "$out/effekt_zustand_nativ.o")"
for f in $funktionen; do
    grep -q " T $f\$" <<<"$exporte" || { echo "FEHLER: Export $f fehlt" >&2; exit 1; }
done
offen="$(mise exec -- llvm-nm -u "$out/effekt_zustand_nativ.o")"
[ -z "$offen" ] || { echo "FEHLER: unerwartete offene Symbole: $offen" >&2; exit 1; }

# Bare-Objekt ist nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_effekt_zustand_nativ.c" \
    "$out/effekt_zustand_nativ.o" -o "$out/test_effekt_zustand_nativ"
"$out/test_effekt_zustand_nativ" "$@"
