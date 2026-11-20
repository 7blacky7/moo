#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
out=build/p2-t24/generator
src=skripte/p2/cpp_adapter/generator
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
mkdir -p "$out"
rust_flags=()
cpp_link_flags=()
case "$(uname -s)" in
 Linux) target=x86_64-unknown-linux-gnu; exe=; cpp_flags=(); archive="$out/libp2_cpp_generated.a" ;;
 MINGW*|MSYS*) target=x86_64-pc-windows-msvc; exe=.exe; cpp_flags=(-fms-runtime-lib=dll); cpp_link_flags=(-fuse-ld=lld); rust_flags=(-C "linker=${CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER:-lld-link}"); archive="$out/p2_cpp_generated.lib" ;;
 *) echo "Dieser Lauf verlangt Linux x64 oder Windows x64"; exit 2 ;;
esac
rustc "${rust_flags[@]}" --edition=2024 --test -A dead_code "$src/pfad_test.rs" -o "$out/pfad-test$exe"
"$out/pfad-test$exe"
args=(--class Zaehler --class Fremd --prefix tg --library p2_cpp_generated --target "$target" -DMOO_CPP_PROBE=1)
for emit in cpp moo; do
 "$compiler" bindgen-cpp "$src/probe.hpp" "${args[@]}" --emit "$emit" > "$out/bindung.$emit"
 "$compiler" bindgen-cpp "$src/probe.hpp" "${args[@]}" --emit "$emit" > "$out/wiederholt.$emit"
 cmp "$out/bindung.$emit" "$out/wiederholt.$emit"
done
cp "$out/bindung.cpp" "$out/adapter.cpp"
clang++ "${cpp_flags[@]}" "${cpp_link_flags[@]}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$out" "$src/referenz.cpp" -o "$out/referenz$exe"
"$out/referenz$exe"
clang++ "${cpp_flags[@]}" -std=c++17 -O2 -Wall -Wextra -Werror -c "$out/adapter.cpp" -o "$out/adapter.o"
llvm-ar rcs "$archive" "$out/adapter.o"
cat "$out/bindung.moo" "$src/probe.moos" > "$out/program.moos"
"$compiler" compile "$out/program.moos" --lib-pfad "$out" -o "$out/program$exe" > "$out/compile.log" 2>&1
"$out/program$exe" > "$out/program.out"
sed 's/\r$//' "$src/probe.expected" > "$out/expected.normalized"
sed 's/\r$//' "$out/program.out" > "$out/program.normalized"
diff -u "$out/expected.normalized" "$out/program.normalized"
"$compiler" compile "$out/program.moos" --emit-ir -o "$out/program.ll" > "$out/ir.log" 2>&1
opt -passes=verify -disable-output "$out/program.ll"
for name in Funktionswert Funktionsreferenz Vorlage Erbe Referenz Zeiger BoolTyp Variadisch Virtuell Standardarg Statisch OhneKonstruktor Privat; do
 rc=0
 "$compiler" bindgen-cpp "$src/negativ.hpp" --class "$name" --prefix tg --library p2_cpp_generated --target "$target" --emit moo > "$out/neg-$name.out" 2> "$out/neg-$name.err" || rc=$?
 test "$rc" -eq 2
 test ! -s "$out/neg-$name.out"
 grep -Eq "negativ.hpp:[1-9][0-9]*:[0-9]+:.*$name" "$out/neg-$name.err"
done
rc=0
"$compiler" bindgen-cpp "$src/indirekt.hpp" --class Referenz --prefix tg --library p2_cpp_generated --emit moo > "$out/indirekt.out" 2> "$out/indirekt.err" || rc=$?
test "$rc" -eq 2 && test ! -s "$out/indirekt.out"
grep -Eq "negativ.hpp:8:[0-9]+:.*Referenz" "$out/indirekt.err"
rc=0
"$compiler" bindgen-cpp "$src/negativ.hpp" --class Referenz --class Zeiger --prefix tg --library p2_cpp_generated --emit cpp > "$out/teil.out" 2> "$out/teil.err" || rc=$?
test "$rc" -eq 2 && test ! -s "$out/teil.out"
rc=0
"$compiler" bindgen-cpp "$src/negativ.hpp" --class A --class A_m_b --prefix tg --library p2_cpp_generated --emit cpp > "$out/kollision.out" 2> "$out/kollision.err" || rc=$?
test "$rc" -eq 2 && test ! -s "$out/kollision.out"
grep -Eq "Symbolkollision.*tg_A_m_b_m_c_leer" "$out/kollision.err"
if [[ "$target" == x86_64-unknown-linux-gnu ]]; then
 mkdir -p "$out/gcc-bin"
 gcc_path="$(command -v gcc)"
 printf '#!/usr/bin/env bash\nexec "%s" -Wl,--as-needed "$@"\n' "$gcc_path" > "$out/gcc-bin/cc"
 chmod +x "$out/gcc-bin/cc"
 PATH="$PWD/$out/gcc-bin:$PATH" "$compiler" compile "$out/program.moos" --lib-pfad "$out" -o "$out/program-gcc" > "$out/compile-gcc.log" 2>&1
 "$out/program-gcc" > "$out/program-gcc.out"
 diff -u "$out/expected.normalized" "$out/program-gcc.out"
 clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I"$out" "$src/referenz.cpp" -o "$out/referenz-san"
 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$out/referenz-san"
 "$compiler" bindgen-cpp "$src/probe.hpp" --class Zaehler --prefix tg --library p2_cpp_generated --target x86_64-pc-windows-msvc --emit moo > "$out/windows.moo"
fi
echo "PASS C++-Generator: deterministisch, C++/Moo-Roundtrip, Exceptions, Griffschutz, 13 Positionsdiagnosen, kein Teilentwurf ($target)"
