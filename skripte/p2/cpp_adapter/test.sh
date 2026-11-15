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
  griff)
    # P2-T23/T53 (Plan 36aca1f2): C++-Klasse als Griff in dynamischem Moo.
    compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
    timeout 120s "$compiler" compile compiler/tests/p2/cpp_griff.moos --lib-pfad "$out" -o "$out/cpp_griff"
    timeout 30s "$out/cpp_griff" > "$out/cpp_griff.out"
    diff -u compiler/tests/p2/cpp_griff.expected "$out/cpp_griff.out"
    # P2-T85 D6: dieselbe C++-Klasse als Moo-Klasse, native Aufrufe aus Methoden.
    timeout 120s "$compiler" compile compiler/tests/p2/cpp_griff_klasse.moos --lib-pfad "$out" -o "$out/cpp_griff_klasse"
    timeout 30s "$out/cpp_griff_klasse" > "$out/cpp_griff_klasse.out"
    diff -u compiler/tests/p2/cpp_griff_klasse.expected "$out/cpp_griff_klasse.out"
    timeout 120s "$compiler" compile compiler/tests/p2/griff_text.moos -o "$out/griff_text"
    timeout 30s "$out/griff_text" > "$out/griff_text.out"
    diff -u compiler/tests/p2/griff_text.expected "$out/griff_text.out"
    # P2-T49 C5: Strukturwert by value aus C++, dynamisch geaendert, zurueck an C++.
    timeout 120s "$compiler" compile compiler/tests/p2/cpp_struktur.moos --lib-pfad "$out" -o "$out/cpp_struktur"
    timeout 30s "$out/cpp_struktur" > "$out/cpp_struktur.out"
    diff -u compiler/tests/p2/cpp_struktur.expected "$out/cpp_struktur.out"
    # P2-T47 E4: dieselbe Strecke mit Struktur + extern C aus importiertem Modul (Alias).
    timeout 120s "$compiler" compile compiler/tests/p2/cpp_struktur_import.moos --lib-pfad "$out" -o "$out/cpp_struktur_import"
    timeout 30s "$out/cpp_struktur_import" > "$out/cpp_struktur_import.out"
    diff -u compiler/tests/p2/cpp_struktur.expected "$out/cpp_struktur_import.out"
    neg() {
        local name="$1" text="$2" rc
        set +e
        timeout 30s "$compiler" compile "compiler/tests/p2/cpp_griff_neg_$name.moos" --emit-ir -o "$out/neg_$name.ll" > "$out/neg_$name.log" 2>&1
        rc=$?
        set -e
        if (( rc == 0 || rc == 124 || rc >= 128 )); then cat "$out/neg_$name.log" >&2; echo "Negativfall $name: Exit $rc" >&2; exit 1; fi
        grep -qF -- "$text" "$out/neg_$name.log" || { cat "$out/neg_$name.log" >&2; echo "Negativfall $name ohne Meldung" >&2; exit 1; }
    }
    neg literal 'Griffargument 1 muss vorerst eine Variable oder nichts sein'
    neg opak_feld "opaker Typ 'P2Zaehler' ist nur als Zeigerziel"
    neg verbraucht 'ist kein Griffparameter'
    printf 'Moo-Griff auf C++-Objekt (Klasse, Vektor, Text, Exception, Freigabeschutz, Lebendzaehler 0) + Moo-Klasse + Text-Bruecke + 3 Negativfaelle: PASS\n'
    ;;
  griff-sanitize)
    # Adapter unter ASan/UBSan/LSan; das Moo-Programm linkt ueber 'cc' -> clang++-Wrapper
    # im PATH ergaenzt nur die Sanitizer-Linkflags (C++-Laufzeit der Sanitizer).
    compiler="$(realpath "${P2_COMPILER:-compiler/target/release/moo-compiler}")"
    san_dir="$out/griff-san"
    mkdir -p "$san_dir"
    timeout 60s mise exec -- clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
        -Wall -Wextra -Werror -c compiler/tests/p2/m1_cpp_adapter.cpp -o "$san_dir/adapter.o"
    ar rcs "$san_dir/libp2_m1_cpp.a" "$san_dir/adapter.o"
    printf '#!/bin/sh\nexec "%s" -fsanitize=address,undefined "$@"\n' "$(mise exec -- which clang++)" > "$san_dir/cc"
    chmod +x "$san_dir/cc"
    PATH="$PWD/$san_dir:$PATH" timeout 120s "$compiler" compile compiler/tests/p2/cpp_griff.moos --lib-pfad "$san_dir" -o "$san_dir/cpp_griff"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s "$san_dir/cpp_griff" > "$san_dir/cpp_griff.out"
    diff -u compiler/tests/p2/cpp_griff.expected "$san_dir/cpp_griff.out"
    # P2-T49 C5: Strukturwert-Box gegen den ASan/UBSan-Adapter, LSan ohne Leck.
    PATH="$PWD/$san_dir:$PATH" timeout 120s "$compiler" compile compiler/tests/p2/cpp_struktur.moos --lib-pfad "$san_dir" -o "$san_dir/cpp_struktur"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s "$san_dir/cpp_struktur" > "$san_dir/cpp_struktur.out"
    diff -u compiler/tests/p2/cpp_struktur.expected "$san_dir/cpp_struktur.out"
    # Plan 36aca1f2 D1: dieselbe Aufruffolge zusaetzlich als reiner C-Harness
    # (ohne Moo-Runtime), ebenfalls unter ASan/UBSan/LSan.
    timeout 60s mise exec -- clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer -Wall -Wextra -Werror -c compiler/tests/p2/cpp_griff_harness.c -o "$san_dir/harness.o"
    timeout 60s mise exec -- clang++ -fsanitize=address,undefined "$san_dir/harness.o" "$san_dir/adapter.o" -o "$san_dir/harness"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s "$san_dir/harness"
    printf 'Moo-Griff gegen ASan/UBSan-Adapter + C-Harness gleicher Aufruffolge unter ASan/UBSan/LSan: PASS\n'
    ;;
  puffer)
    # P2-T55/T17 D2 (Plan f5b52a0b): Puffer (Zeiger, Laenge) an C++ aus Funktion,
    # versuche: und aufraeumen; C++-Ausnahme im Adapter gefangen -> Fehlercode ->
    # wirf. Normal und gegen den ASan/UBSan-Adapter mit LSan.
    compiler="$(realpath "${P2_COMPILER:-compiler/target/release/moo-compiler}")"
    timeout 120s "$compiler" compile compiler/tests/p2/cpp_puffer.moos --lib-pfad "$out" -o "$out/cpp_puffer"
    timeout 30s "$out/cpp_puffer" > "$out/cpp_puffer.out"
    diff -u compiler/tests/p2/cpp_puffer.expected "$out/cpp_puffer.out"
    san_dir="$out/puffer-san"
    mkdir -p "$san_dir"
    timeout 60s mise exec -- clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
        -Wall -Wextra -Werror -c compiler/tests/p2/m1_cpp_adapter.cpp -o "$san_dir/adapter.o"
    ar rcs "$san_dir/libp2_m1_cpp.a" "$san_dir/adapter.o"
    printf '#!/bin/sh\nexec "%s" -fsanitize=address,undefined "$@"\n' "$(mise exec -- which clang++)" > "$san_dir/cc"
    chmod +x "$san_dir/cc"
    PATH="$PWD/$san_dir:$PATH" timeout 120s "$compiler" compile compiler/tests/p2/cpp_puffer.moos --lib-pfad "$san_dir" -o "$san_dir/cpp_puffer"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s "$san_dir/cpp_puffer" > "$san_dir/cpp_puffer.out"
    diff -u compiler/tests/p2/cpp_puffer.expected "$san_dir/cpp_puffer.out"
    printf 'Moo-Puffer an C++ aus Funktion/versuche:/aufraeumen, Exception -> wirf, ASan/UBSan/LSan: PASS\n'
    ;;
  *) printf 'Unknown mode\n' >&2; exit 2 ;;
esac
