#!/usr/bin/env bash
# P2 / P2-T55: FFI-Einzelform in Funktionen mit aufraeumen, im Catch, als
# aufraeumen-Aktion und transitiv. Bis P2-T55 Compilefehler ("nur in main
# ausserhalb von try"), seit Plan f5b52a0b A1 hosted fangbar -> Laufzeit-
# nachweis mit Ausgabe. Die @nativ-Huelle als Funktionswert (funktionswert)
# laeuft seit den nativen Funktionswerten ebenso, mit Cleanup nach der Ausgabe.
set -euo pipefail
out=build/p2-ffi-reachability
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for name in argument cleanup catch transitiv funktionswert; do
    timeout 60s "$compiler" compile "compiler/tests/p2/ffi_defer_reach_${name}.moos" -o "$out/$name" >"$out/$name.log" 2>&1 \
        || { cat "$out/$name.log" >&2; printf '%s: kompiliert nicht\n' "$name" >&2; exit 1; }
    timeout 60s "$compiler" compile "compiler/tests/p2/ffi_defer_reach_${name}.moos" --emit-ir -o "$out/$name.ll" >/dev/null
    mise exec -- opt -passes=verify -disable-output "$out/$name.ll"
    actual=$(timeout 30s "$out/$name")
    if [[ "$name" == cleanup ]]; then expected=''; else expected=$'0.997495\nCLEANUP'; fi
    [[ "$actual" == "$expected" ]] || { printf '%s: Ausgabe unerwartet: %s\n' "$name" "$actual" >&2; exit 1; }
done
timeout 60s "$compiler" compile compiler/tests/p2/ffi_defer_reach_lambda.moos --emit-ir -o "$out/lambda.ll"
mise exec -- opt -passes=verify -disable-output "$out/lambda.ll"
grep -Eq '^define .*@probe\(' "$out/lambda.ll"
if sed -n '/^define .*@probe(/,/^}/p' "$out/lambda.ll" | grep -Fq '@moo_defer_stack_push'; then
    printf 'Lambda-Alias wurde faelschlich als sicherer Direktaufruf eingestuft\n' >&2
    exit 1
fi
timeout 60s "$compiler" compile compiler/tests/p2/ffi_defer_reach_lambda.moos -o "$out/lambda"
actual=$(timeout 30s "$out/lambda")
[[ "$actual" == $'42\nCLEANUP' ]] || { printf 'Lambda-Ausgabe unerwartet: %s\n' "$actual" >&2; exit 1; }
printf 'FFI-Einzelform mit aufraeumen/Catch/transitiv (Laufzeit), @nativ-Huelle als Funktionswert (extern-C-Name als Wert bleibt gesperrt, M-N003), Lambda-Fallback: PASS\n'
