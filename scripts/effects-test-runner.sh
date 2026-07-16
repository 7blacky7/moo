#!/usr/bin/env bash
# P016-O5 hermetic headless effects runner. No UI or wall-clock assertions.
set -u

MODE="${1:-green}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="$ROOT/compiler/runtime"
TESTS="$RUNTIME/tests"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo-effects-red.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

CC="${CC:-cc}"
BASE_FLAGS=(-std=c11 -Wall -Wextra -Werror -pedantic -I"$RUNTIME")
SANITIZER="${EFFECTS_SANITIZER:-combined}"
OPT_LEVEL="${EFFECTS_OPT_LEVEL:-O1}"
REPEAT="${EFFECTS_REPEAT:-1}"
case "$REPEAT" in
    ''|*[!0-9]*)
        echo "P016-O5 INVALID REPEAT: $REPEAT" >&2
        exit 2
        ;;
esac
if [[ "$REPEAT" -lt 1 || "$REPEAT" -gt 50 ]]; then
    echo "P016-O5 INVALID REPEAT: $REPEAT (expected 1..50)" >&2
    exit 2
fi
case "$OPT_LEVEL" in
    O0|O1|O2|O3) ;;
    *)
        echo "P016-O5 UNKNOWN OPT LEVEL: $OPT_LEVEL" >&2
        exit 2
        ;;
esac
OPT_FLAG="-$OPT_LEVEL"
case "$SANITIZER" in
    none)
        SAN_FLAGS=("$OPT_FLAG")
        ;;
    asan)
        SAN_FLAGS=("$OPT_FLAG" -g -fno-omit-frame-pointer -fsanitize=address)
        ;;
    ubsan)
        SAN_FLAGS=("$OPT_FLAG" -g -fno-omit-frame-pointer
                   -fsanitize=undefined -fno-sanitize-recover=undefined)
        ;;
    combined)
        SAN_FLAGS=("$OPT_FLAG" -g -fno-omit-frame-pointer
                   -fsanitize=address,undefined
                   -fno-sanitize-recover=undefined)
        ;;
    *)
        echo "P016-O5 UNKNOWN SANITIZER: $SANITIZER (expected none|asan|ubsan|combined)" >&2
        exit 2
        ;;
esac

build_one() {
    local name="$1"
    shift
    if ! "$CC" "${BASE_FLAGS[@]}" "${SAN_FLAGS[@]}" "$@" -o "$OUT/$name"; then
        echo "COMPILE/LINK FAILED: $name" >&2
        exit 3
    fi
}

HARNESSES=(
    "test_effects_asan|$TESTS/test_effects_asan.c $RUNTIME/moo_compositor_effects_state.c $RUNTIME/moo_compositor_animation.c $RUNTIME/moo_compositor_effects_math.c $RUNTIME/moo_compositor_effects_cpu.c"
    "test_effects_damage|$TESTS/test_effects_damage.c $RUNTIME/moo_compositor_effects_math.c $RUNTIME/moo_compositor_effects_damage.c"
    "test_effects_determinism|$TESTS/test_effects_determinism.c $RUNTIME/moo_compositor_effects_state.c $RUNTIME/moo_compositor_animation.c $RUNTIME/moo_compositor_effects_math.c $RUNTIME/moo_compositor_effects_cpu.c"
    "bench_effects|$TESTS/bench_effects.c $RUNTIME/moo_compositor_effects_state.c $RUNTIME/moo_compositor_effects_math.c $RUNTIME/moo_compositor_effects_cpu.c"
    "test_effects_integration|$TESTS/test_effects_integration.c $RUNTIME/moo_compositor_core.c $RUNTIME/moo_compositor_raster.c $RUNTIME/moo_compositor_effects_state.c $RUNTIME/moo_compositor_animation.c $RUNTIME/moo_compositor_effects_math.c $RUNTIME/moo_compositor_effects_cpu.c $RUNTIME/moo_compositor_effects_damage.c $RUNTIME/moo_compositor_effects_gpu.c"
)

for entry in "${HARNESSES[@]}"; do
    IFS='|' read -r name sources <<< "$entry"
    read -r -a source_argv <<< "$sources"
    build_one "$name" "${source_argv[@]}"
done

if [[ "$MODE" == "compile" ]]; then
    echo "P016-O5 EFFECTS COMPILE GATE PASS sanitizer=$SANITIZER cc=$CC opt=$OPT_LEVEL repeat=$REPEAT"
    exit 0
fi

unexpected=0
red_count=0
green_count=0
for entry in "${HARNESSES[@]}"; do
    IFS='|' read -r exe _ <<< "$entry"
    baseline="$OUT/$exe.baseline.log"
    attempt=1
    exe_ok=1
    while [[ "$attempt" -le "$REPEAT" ]]; do
        run_log="$OUT/$exe.run-$attempt.log"
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
            UBSAN_OPTIONS=halt_on_error=1 \
            "$OUT/$exe" >"$run_log" 2>&1
        rc=$?
        if [[ $rc -ne 0 ]]; then
            echo "UNEXPECTED RESULT: $exe attempt=$attempt rc=$rc" >&2
            cat "$run_log" >&2
            exe_ok=0
            break
        fi
        if [[ "$attempt" -eq 1 ]]; then
            cp "$run_log" "$baseline"
        elif ! cmp -s "$baseline" "$run_log"; then
            echo "NONDETERMINISTIC OUTPUT: $exe attempt=$attempt" >&2
            diff -u "$baseline" "$run_log" >&2 || true
            exe_ok=0
            break
        fi
        attempt=$((attempt + 1))
    done
    if [[ $exe_ok -eq 1 ]]; then
        cat "$baseline"
        echo "EXPECTED GREEN: $exe repeat=$REPEAT output-stable"
        green_count=$((green_count + 1))
    else
        unexpected=1
    fi
done

if [[ $unexpected -ne 0 || $red_count -ne 0 || $green_count -ne 5 ]]; then
    echo "P016-O5 MATRIX INVALID red=$red_count green=$green_count unexpected=$unexpected" >&2
    exit 2
fi
if [[ "$MODE" == "verify-green" ]]; then
    echo "P016-O5 MATRIX VERIFIED: sanitizer=$SANITIZER cc=$CC opt=$OPT_LEVEL repeat=$REPEAT red=$red_count green=$green_count"
    exit 0
fi
if [[ "$MODE" != "green" ]]; then
    echo "P016-O5 UNKNOWN MODE: $MODE" >&2
    exit 2
fi

echo "P016-O5 EFFECTS GREEN: sanitizer=$SANITIZER cc=$CC opt=$OPT_LEVEL repeat=$REPEAT real I1 integration semantics"
exit 0
