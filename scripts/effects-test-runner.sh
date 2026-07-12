#!/usr/bin/env bash
# P016-O5 hermetic headless effects runner. No UI or wall-clock assertions.
set -u

MODE="${1:-red}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="$ROOT/compiler/runtime"
TESTS="$RUNTIME/tests"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo-effects-red.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

CC="${CC:-cc}"
BASE_FLAGS=(-std=c11 -Wall -Wextra -Werror -pedantic -I"$RUNTIME")
SAN_FLAGS=(-O1 -g -fno-omit-frame-pointer
           -fsanitize=address,undefined
           -fno-sanitize-recover=undefined)

build_one() {
    local name="$1"
    shift
    "$CC" "${BASE_FLAGS[@]}" "${SAN_FLAGS[@]}" "$@" -o "$OUT/$name"
}

build_one test_effects_asan     "$TESTS/test_effects_asan.c"     "$RUNTIME/moo_compositor_effects_state.c"     "$RUNTIME/moo_compositor_animation.c"     "$RUNTIME/moo_compositor_effects_math.c"     "$RUNTIME/moo_compositor_effects_cpu.c"
build_one test_effects_damage     "$TESTS/test_effects_damage.c"     "$RUNTIME/moo_compositor_effects_math.c"     "$RUNTIME/moo_compositor_effects_damage.c"
build_one test_effects_determinism     "$TESTS/test_effects_determinism.c"     "$RUNTIME/moo_compositor_effects_state.c"     "$RUNTIME/moo_compositor_animation.c"     "$RUNTIME/moo_compositor_effects_math.c"     "$RUNTIME/moo_compositor_effects_cpu.c"
build_one bench_effects     "$TESTS/bench_effects.c"     "$RUNTIME/moo_compositor_effects_state.c"     "$RUNTIME/moo_compositor_effects_math.c"     "$RUNTIME/moo_compositor_effects_cpu.c"

if [[ "$MODE" == "compile" ]]; then
    echo "P016-O5 EFFECTS SCAFFOLD COMPILES"
    exit 0
fi

unexpected=0
red_count=0
green_count=0
for exe in test_effects_asan test_effects_damage            test_effects_determinism bench_effects; do
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1     UBSAN_OPTIONS=halt_on_error=1         "$OUT/$exe"
    rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "EXPECTED GREEN: $exe"
        green_count=$((green_count + 1))
    elif [[ $rc -eq 1 ]]; then
        echo "EXPECTED RED: $exe"
        red_count=$((red_count + 1))
    else
        echo "UNEXPECTED RESULT: $exe rc=$rc" >&2
        unexpected=1
    fi
done

if [[ $unexpected -ne 0 || $red_count -ne 0 || $green_count -ne 4 ]]; then
    echo "P016-O5 MATRIX INVALID red=$red_count green=$green_count unexpected=$unexpected" >&2
    exit 2
fi
if [[ "$MODE" == "verify-red" ]]; then
    echo "P016-O5 MATRIX VERIFIED: red=$red_count green=$green_count"
    exit 0
fi

echo "P016-O5 EFFECTS GREEN: red=$red_count green=$green_count"
exit 0
