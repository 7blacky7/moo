#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-m1-cpp
mkdir -p "$out"
timeout 30s "${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -c compiler/tests/p2/m1_cpp_adapter.cpp -o "$out/adapter.o"
timeout 30s ar rcs "$out/libp2_m1_cpp.a" "$out/adapter.o"
case "${1:-c}" in
  sanitize)
    san="-fsanitize=address,undefined"
    timeout 30s clang++ -std=c++17 -O1 -g "$san" -fno-omit-frame-pointer -Wall -Wextra -Werror -c compiler/tests/p2/m1_cpp_adapter.cpp -o "$out/adapter-san.o"
    timeout 30s clang -std=c11 -O1 -g "$san" -fno-omit-frame-pointer -Wall -Wextra -Werror -c compiler/tests/p2/m1_cpp_adapter_test.c -o "$out/test-san.o"
    timeout 30s clang++ "$san" "$out/test-san.o" "$out/adapter-san.o" -o "$out/test-san"
    timeout 30s env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$out/test-san"
    printf 'C/C++ adapter ASan/UBSan: PASS\n'
    ;;
  c)
    timeout 30s "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -c compiler/tests/p2/m1_cpp_adapter_test.c -o "$out/test.o"
    timeout 30s "${CXX:-c++}" "$out/test.o" "$out/libp2_m1_cpp.a" -o "$out/test"
    timeout 30s "$out/test"
    printf 'C/C++ adapter: PASS\n'
    ;;
  bindgen)
    compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
    timeout 30s "$compiler" bindgen compiler/tests/p2/m1_cpp_adapter.h --prefix p2_cpp_example > "$out/binding.moos"
    timeout 30s "$compiler" bindgen compiler/tests/p2/m1_cpp_adapter.h --prefix p2_cpp_example > "$out/binding-repeat.moos"
    cmp "$out/binding.moos" "$out/binding-repeat.moos"
    rg -q -x 'extern "C" funktion p2_cpp_example\(arg0: i32\) -> i32' "$out/binding.moos"
    printf 'Real adapter header bindgen: PASS\n'
    ;;
  handle-zukunft)
    compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
    timeout 60s "$compiler" compile compiler/tests/p2/m1_cpp_handle.moos --emit-ir -o "$out/handle.ll"
    timeout 30s mise exec -- opt -passes=verify -disable-output "$out/handle.ll"
    timeout 30s mise exec -- llvm-extract -S --func=__moo_native.p2_handle_runde "$out/handle.ll" -o "$out/handle-extract.ll"
    timeout 30s clang -c -x ir "$out/handle-extract.ll" -o "$out/handle-moo.o"
    san="-fsanitize=address,undefined"
    timeout 30s clang++ -std=c++17 -O1 -g "$san" -fno-omit-frame-pointer -Wall -Wextra -Werror -c compiler/tests/p2/m1_cpp_adapter.cpp -o "$out/handle-adapter.o"
    timeout 30s clang++ -std=c++17 -O1 -g "$san" -fno-omit-frame-pointer -Wall -Wextra -Werror compiler/tests/p2/m1_cpp_handle.cpp "$out/handle-moo.o" "$out/handle-adapter.o" -o "$out/handle-test"
    timeout 30s env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$out/handle-test"
    ;;
  throw)
    timeout 120s "${P2_COMPILER:-compiler/target/release/moo-compiler}" compile compiler/tests/p2/m1_cpp_throw.moos --lib-pfad "$out" -o "$out/m1_cpp_throw"
    result=$(timeout 30s "$out/m1_cpp_throw")
    expected=$(printf '42\ncpp fehler\nweiter')
    [[ $result == "$expected" ]] || { printf 'Unexpected output: %s\n' "$result" >&2; exit 1; }
    printf 'C++ exception to Moo throw: PASS\n'
    ;;
  moo)
    timeout 120s "${P2_COMPILER:-compiler/target/release/moo-compiler}" compile compiler/tests/p2/m1_cpp.moos --lib-pfad "$out" -o "$out/m1_cpp"
    timeout 120s "${P2_COMPILER:-compiler/target/release/moo-compiler}" compile compiler/tests/p2/m1_cpp.moos --emit-ir -o "$out/m1_cpp.ll"
    awk '
        /^define .*@("?__moo_native[.]cpp_probe"?)/ { inside=1; signature=($0 ~ /i32.*i32/); next }
        inside && /^}/ { inside=0 }
        inside && /call i32 @p2_cpp_example\(i32 / { foreign_call=1 }
        END { if (!signature || !foreign_call) exit 1 }
    ' "$out/m1_cpp.ll"
    result=$(timeout 30s "$out/m1_cpp")
    expected=$(printf '42\n-1')
    [[ $result == "$expected" ]] || { printf 'Unexpected output: %s\n' "$result" >&2; exit 1; }
    printf 'Moo/C++ adapter: PASS\n'
    ;;
  *) printf 'Unknown mode\n' >&2; exit 2 ;;
esac
