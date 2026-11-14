#!/usr/bin/env bash
# P2 bindgen contract; executed by mise run p2-bindgen-test.
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-bindgen
mkdir -p "$out"
base=compiler/tests/p2/bindgen_fixture.h
extra=compiler/tests/p2/bindgen_fixture_extra.h
standard=compiler/tests/p2/bindgen_fixture_standard.h
timeout 30s "$compiler" bindgen "$base" --prefix p2_add > "$out/first.moos"
timeout 30s "$compiler" bindgen "$base" --prefix p2_add > "$out/second.moos"
cmp "$out/first.moos" "$out/second.moos"
grep -Fx 'extern "C" funktion p2_add(arg0: i32, arg1: i32) -> i32' "$out/first.moos"
grep -F '# dependency sha256 ' "$out/first.moos" >/dev/null
timeout 30s "$compiler" bindgen "$extra" --prefix p2_echo > "$out/pointer.moos"
grep -Fx 'extern "C" funktion p2_echo(arg0: zeiger[i32]) -> zeiger[i32]' "$out/pointer.moos"
timeout 30s "$compiler" bindgen "$standard" --prefix p2_narrow > "$out/stdint.moos"
grep -Fx 'extern "C" funktion p2_narrow(arg0: u64) -> u32' "$out/stdint.moos"
timeout 30s "$compiler" bindgen "$base" --prefix P2 > "$out/records.moos"
grep -Fx 'struktur P2Pair:' "$out/records.moos"
grep -Fx '    x: i32' "$out/records.moos"
grep -Fx '    y: f32' "$out/records.moos"
grep -Fx 'konstante P2_ZERO auf 0' "$out/records.moos"
grep -Fx 'konstante P2_THREE auf 3' "$out/records.moos"
grep -Fx 'konstante P2_LIMIT auf 42' "$out/records.moos"
grep -Fx 'konstante P2_FLOATING auf 1.25' "$out/records.moos"
timeout 30s "$compiler" bindgen compiler/tests/p2/bindgen_fixture_include.h --prefix p2_twice > "$out/include.moos"
grep -Fx 'extern "C" funktion p2_twice(arg0: u32) -> u32' "$out/include.moos"
grep -F 'bindgen_dependency.h' "$out/include.moos" >/dev/null
timeout 30s "$compiler" bindgen skripte/p2/abi/ffi_reference.h --prefix moo_p2_echo_ > "$out/abi.moos"
for typ in i8 u8 i16 u16 i32 u32 i64 u64 f32 f64; do
    grep -Fx "extern \"C\" funktion moo_p2_echo_${typ}(arg0: ${typ}) -> ${typ}" "$out/abi.moos"
done
grep -Fx 'extern "C" funktion moo_p2_echo_pointer(arg0: zeiger[void]) -> zeiger[void]' "$out/abi.moos"
[[ $(grep -c '^extern "C" funktion ' "$out/abi.moos") -eq 11 ]]
timeout 30s "$compiler" bindgen "$extra" --prefix P2Array > "$out/array.moos"
grep -Fx '    values: [[i32; 3]; 2]' "$out/array.moos"
timeout 30s "$compiler" bindgen compiler/tests/p2/bindgen_fixture_packed.h --prefix P2BindgenPacked > "$out/packed.moos"
grep -Fx '@gepackt' "$out/packed.moos"
grep -Fx 'struktur P2BindgenPacked:' "$out/packed.moos"
grep -Fx '    array: [u16; 3]' "$out/packed.moos"
for prefix in P2BindgenPragma P2BindgenBoth P2BindgenAliasAttr P2BindgenUsesAlias P2BindgenUsesChain P2BindgenFieldAttr P2BindgenInherited p2_bindgen_aligned_arg; do
    rc=0
    timeout 30s "$compiler" bindgen compiler/tests/p2/bindgen_fixture_packed.h --prefix "$prefix" > "$out/pragma.out" 2> "$out/pragma.err" || rc=$?
    [[ $rc -eq 2 && ! -s "$out/pragma.out" && -s "$out/pragma.err" ]] || { echo "FAIL packed attribute rejection $prefix rc=$rc" >&2; exit 1; }
done
timeout 30s "$compiler" bindgen "$extra" --prefix P2_ANON > "$out/enum.moos"
grep -Fx 'konstante P2_ANON auf 17' "$out/enum.moos"
timeout 30s "$compiler" bindgen "$extra" --prefix P2_FROM_NAMED > "$out/named-enum.moos"
grep -Fx 'konstante P2_FROM_NAMED auf 23' "$out/named-enum.moos"
timeout 30s "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror skripte/p2/bindgen/layout_ref.c -o "$out/layout-ref"
[[ $(timeout 30s "$out/layout-ref") == $'8 4 0 4\n24 8 0 8 16\n64 8 0 8 32 56' ]]
timeout 30s "$compiler" bindgen skripte/p2/bindgen/layout_probe.h --prefix P2Layout > "$out/layout-generated.moos"
layout_decls=$(awk '/^struktur / || /^    / { print }' "$out/layout-generated.moos")
[[ $layout_decls == $'struktur P2LayoutProbe:\n    first: i32\n    second: i32\nstruktur P2LayoutPadded:\n    tag: u8\n    address: u64\n    flags: u16\nstruktur P2LayoutNested:\n    head: u8\n    entry: P2LayoutPadded\n    values: [[u32; 3]; 2]\n    tail: u8' ]]
timeout 60s "$compiler" compile "$out/layout-generated.moos" --emit-ir -o "$out/layout-generated.ll"
# A filtered-out by-value dependency must never produce an incomplete binding.
rc=0
timeout 30s "$compiler" bindgen skripte/p2/bindgen/layout_probe.h --prefix P2LayoutNested > "$out/layout-incomplete.out" 2> "$out/layout-incomplete.err" || rc=$?
[[ $rc -eq 2 && ! -s "$out/layout-incomplete.out" && -s "$out/layout-incomplete.err" ]] || { echo "FAIL missing layout dependency rc=$rc" >&2; exit 1; }
timeout 30s "$compiler" bindgen /usr/include/stdlib.h --prefix abs > "$out/libc-abs.moos"
grep -Fx 'extern "C" funktion abs(arg0: i32) -> i32' "$out/libc-abs.moos"
[[ $(grep -c '^extern "C" funktion abs(' "$out/libc-abs.moos") -eq 1 ]]
for prefix in p2_oldstyle p2_bool p2_fnptr p2_attr p2_global P2Aligned P2Alias P2_OCTAL P2_EXPR P2_HUGE P2_TINY guard p2_no_such_symbol; do
    rc=0
    timeout 30s "$compiler" bindgen "$extra" --prefix "$prefix" > "$out/rejected.out" 2> "$out/rejected.err" || rc=$?
    [[ $rc -eq 2 && ! -s "$out/rejected.out" && -s "$out/rejected.err" ]] || { echo "FAIL reject $prefix rc=$rc" >&2; exit 1; }
done
rc=0
timeout 30s "$compiler" bindgen skripte/p2/bindgen/unsupported_attr.h --prefix p2_annotated > "$out/attr.out" 2> "$out/attr.err" || rc=$?
[[ $rc -eq 2 && ! -s "$out/attr.out" && -s "$out/attr.err" ]] || { echo "FAIL unsupported function attribute rc=$rc" >&2; exit 1; }
rc=0
timeout 30s "$compiler" bindgen "$base" --prefix p2_ --allow-partial > "$out/partial.moos" 2> "$out/partial.err" || rc=$?
[[ $rc -eq 2 ]]
grep -F '# UNVOLLSTAENDIG:' "$out/partial.moos" >/dev/null
grep -F 'variadic' "$out/partial.err" >/dev/null
grep -F 'aggregate' "$out/partial.err" >/dev/null
rc=0
timeout 30s "$compiler" bindgen "$out/no-such-header.h" --prefix p2_ > "$out/missing.out" 2> "$out/missing.err" || rc=$?
[[ $rc -eq 1 && ! -s "$out/missing.out" && -s "$out/missing.err" ]]
echo "P2 Bindgen: PASS"
