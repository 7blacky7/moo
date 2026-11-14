#!/usr/bin/env bash
# P2-T72: nativer CPU-Effekt-Unterbau (effekt_cpu_{pixel,geometrie,blur,render}_nativ.moos
# + effekt_mathe_nativ.moos) gegen compiler/runtime/moo_compositor_effects_cpu.c.
# Alle fuenf Moo-Module bare bauen, IR-Verify, main lokalisieren, Export-Gate.
# Fremdsymbol-Gate: offene Symbole muessen ausnahmslos moo_n_* sein (keine
# libc wie memset/memcpy im Bare-Objekt, vgl. QA_ROT #37258). Die Probe
# #includet effects_cpu.c und linkt effects_math.c (C-Orakel).
# Argumente gehen an die Probe: [einzelfaelle] [szenen]
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
out="$project_dir/build/p2-grafik-cpu"
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
    local fremd
    fremd="$(mise exec -- llvm-nm -u "$out/$name.o" | awk '{print $NF}' | grep -v '^moo_n_' || true)"
    [ -z "$fremd" ] || { echo "FEHLER: $name hat Nicht-moo_n-Fremdsymbole: $fremd" >&2; exit 1; }
}

baue effekt_mathe_nativ moo_n_affine_inverse moo_n_affine_transform_point moo_n_corners_normalize \
    moo_n_rect_expand moo_n_rounded_coverage_a8
baue effekt_cpu_pixel_nativ moo_n_fx_clamp_u8 moo_n_fx_round_u64 moo_n_fx_round_signed moo_n_fx_view_valid \
    moo_n_fx_ranges_overlap moo_n_fx_rect_valid moo_n_fx_effect_valid moo_n_fx_over moo_n_comp_noise_hash \
    moo_n_fx_hash_rgba
baue effekt_cpu_geometrie_nativ moo_n_fx_q16_floor moo_n_fx_local_sample_checked \
    moo_n_fx_geometry_preflight moo_n_fx_requirements
baue effekt_cpu_blur_nativ moo_n_fx_box_mask_h moo_n_fx_box_mask_v moo_n_fx_clamp_coord \
    moo_n_fx_box_rgba_h moo_n_fx_box_rgba_v moo_n_fx_capture
baue effekt_cpu_render_nativ moo_n_fx_colorize moo_n_fx_coverage moo_n_fx_render_content \
    moo_n_fx_render_shadow moo_n_fx_render_backdrop moo_n_fx_validate_scratch moo_n_fx_render

# Bare-Objekte sind nicht PIC (RelocMode::Static fuer *-none) -> -no-pie.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I"$project_dir/compiler/runtime" \
    "$grafik_dir/test_effekt_cpu_nativ.c" \
    "$project_dir/compiler/runtime/moo_compositor_effects_math.c" \
    "$out/effekt_cpu_render_nativ.o" "$out/effekt_cpu_blur_nativ.o" "$out/effekt_cpu_geometrie_nativ.o" \
    "$out/effekt_cpu_pixel_nativ.o" "$out/effekt_mathe_nativ.o" \
    -o "$out/test_effekt_cpu_nativ"
"$out/test_effekt_cpu_nativ" "$@"
