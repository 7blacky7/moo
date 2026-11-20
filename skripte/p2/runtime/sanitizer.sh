#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
build_dir="$project_root/build/p2-runtime-sanitize"
mkdir -p "$build_dir"
flags=(-std=c11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined)
"${CC:-cc}" "${flags[@]}" \
  "$project_root/compiler/runtime/moo_buffer.c" \
  "$project_root/compiler/runtime/moo_struct_box.c" \
  "$project_root/compiler/runtime/moo_memory.c" \
  "$project_root/compiler/runtime/tests/test_p2_buffer.c" \
  "$project_root/compiler/tests/p2/test_p2_buffer_value.c" -o "$build_dir/buffer"
"${CC:-cc}" "${flags[@]}" \
  "$project_root/compiler/runtime/moo_struct_box.c" \
  "$project_root/compiler/tests/p2/test_p2_struct_box.c" -o "$build_dir/struct_box"
"${CC:-cc}" "${flags[@]}" \
  "$project_root/compiler/runtime/moo_error.c" \
  "$project_root/compiler/runtime/tests/test_p2_try_take.c" -o "$build_dir/try_take"
# P2-T44: Ausgabeabbruch nach Konvertierungsfehler (moo_print.c + moo_error.c).
"${CC:-cc}" "${flags[@]}" \
  "$project_root/compiler/runtime/moo_error.c" \
  "$project_root/compiler/runtime/moo_print.c" \
  "$project_root/compiler/runtime/tests/test_p2_print_abbruch.c" -o "$build_dir/print_abbruch"
"${CC:-cc}" "${flags[@]}" -c "$project_root/compiler/runtime/moo_buffer.c" -o "$build_dir/buffer_cpp.o"
"${CC:-cc}" "${flags[@]}" -c "$project_root/compiler/runtime/moo_struct_box.c" -o "$build_dir/struct_box_cpp.o"
"${CC:-cc}" "${flags[@]}" -c "$project_root/compiler/runtime/moo_defer_stack.c" -o "$build_dir/defer_stack_cpp.o"
"${CC:-cc}" "${flags[@]}" -c "$project_root/compiler/runtime/moo_defer_error.c" -o "$build_dir/defer_error_cpp.o"
"${CC:-cc}" "${flags[@]}" "$project_root/compiler/runtime/moo_defer_stack.c" \
  "$project_root/compiler/tests/p2/test_p2_defer_stack.c" -Wl,--wrap=malloc -Wl,--wrap=realloc -o "$build_dir/defer_stack"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined \
  "$project_root/compiler/runtime/tests/test_p2_buffer_cpp.cpp" "$build_dir/buffer_cpp.o" "$build_dir/struct_box_cpp.o" "$build_dir/defer_stack_cpp.o" "$build_dir/defer_error_cpp.o" -o "$build_dir/buffer_cpp"
"${CC:-cc}" "${flags[@]}" -D_GNU_SOURCE -ffunction-sections -fdata-sections \
  "$project_root/compiler/runtime/moo_buffer.c" \
  "$project_root/compiler/runtime/moo_buffer_index.c" \
  "$project_root/compiler/runtime/moo_struct_box.c" \
  "$project_root/compiler/runtime/moo_struct_dyn.c" \
  "$project_root/compiler/runtime/moo_stdlib.c" \
  "$project_root/compiler/tests/p2/test_p2_buffer_index.c" -Wl,--gc-sections -lm -o "$build_dir/buffer_index"
# P2-T83 C1: valid-Masken der nativen Tensor-Leihe gegen die echte Host-Sicherung
# (moo_tensor.c; bf16 store->f32, @liest unveraendert, schreibbar DATA).
"${CC:-cc}" "${flags[@]}" -D_GNU_SOURCE -ffunction-sections -fdata-sections \
  "$project_root/compiler/runtime/moo_buffer.c" \
  "$project_root/compiler/runtime/moo_buffer_index.c" \
  "$project_root/compiler/runtime/moo_struct_box.c" \
  "$project_root/compiler/runtime/moo_struct_dyn.c" \
  "$project_root/compiler/runtime/moo_tensor.c" \
  "$project_root/compiler/tests/p2/test_p2_native_leihe_maske.c" -Wl,--gc-sections -lm -o "$build_dir/leihe_maske"
"${CC:-cc}" "${flags[@]}" -D_GNU_SOURCE -ffunction-sections -fdata-sections \
  "$project_root/compiler/runtime/moo_value.c" "$project_root/compiler/runtime/moo_memory.c" \
  "$project_root/compiler/runtime/moo_error.c" "$project_root/compiler/runtime/moo_defer_error.c" "$project_root/compiler/tests/p2/test_p2_oom_candidate.c" \
  -Wl,--gc-sections -Wl,--wrap=malloc -Wl,--wrap=strdup -o "$build_dir/oom_candidate"
"${CC:-cc}" "${flags[@]}" -DMOO_BARE -ffunction-sections -fdata-sections \
  "$project_root/compiler/runtime/moo_defer_stack.c" "$project_root/compiler/runtime/moo_bare_alloc.c" \
  "$project_root/compiler/tests/p2/test_p2_defer_bare.c" -Wl,--gc-sections -o "$build_dir/defer_bare"
"${CC:-cc}" "${flags[@]}" "$project_root/compiler/runtime/moo_buffer.c" \
  "$project_root/compiler/tests/p2/test_p2_buffer_append_oom.c" -Wl,--wrap=calloc -o "$build_dir/buffer_append_oom"
# P2-T58 G1: moo_native_math (fmod/pow ohne libm) gegen libm, unter ASan/UBSan.
"${CC:-cc}" "${flags[@]}" "$project_root/compiler/runtime/moo_native_math.c" \
  "$project_root/compiler/runtime/tests/test_p2_native_math.c" -lm -o "$build_dir/native_math"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/native_math"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/buffer_append_oom"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/defer_bare"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/oom_candidate"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/defer_stack"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/buffer_index"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/leihe_maske"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/buffer_cpp"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/try_take"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/print_abbruch"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/buffer"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/struct_box"
