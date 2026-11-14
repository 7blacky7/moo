#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/pointer-rekursion-zukunft
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-bare x86_64-unknown-uefi aarch64-bare; do
    "$compiler" compile compiler/tests/p2/struktur_pointer_rekursiv.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.o"
    "$compiler" compile compiler/tests/p2/struktur_pointer_primitive_kollision.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target-primitive.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-primitive.ll"
    "$compiler" compile compiler/tests/p2/struktur_pointer_void_return.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target-void-return.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-void-return.ll"
done
for test_case in unbekannt wertzyklus fn_unbekannt duplikat void_parameter; do
    if "$compiler" compile "compiler/tests/p2/struktur_pointer_${test_case}.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/$test_case.ll" >"$out/$test_case.log" 2>&1; then
        echo "Ungültige Struktur akzeptiert: $test_case" >&2; exit 1
    fi
done
grep -Eqi 'unbekannt|nicht definiert|nicht gefunden' "$out/unbekannt.log"
grep -Eqi 'rekurs|zykl|kreis' "$out/wertzyklus.log"
grep -Eqi 'unbekannt|nicht definiert|nicht gefunden' "$out/fn_unbekannt.log"
grep -Eqi 'doppelt|bereits definiert|mehrfach' "$out/duplikat.log"
grep -Eqi 'void.*parameter|parameter.*void' "$out/void_parameter.log"
printf 'Pointer-Selbst-/Vorwärtsreferenz und Negativdiagnosen PASS; kein Moo/C-Layoutvergleich\n'
