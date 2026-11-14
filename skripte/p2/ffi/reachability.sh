#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-reachability
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for name in argument cleanup catch transitiv funktionswert; do
    set +e
    timeout 60s "$compiler" compile "compiler/tests/p2/ffi_defer_reach_${name}.moos" --emit-ir -o "$out/$name.ll" >"$out/$name.log" 2>&1
    rc=$?
    set -e
    if (( rc == 0 || rc == 124 || rc >= 128 )); then
        printf '%s: unerwarteter Compilerexit %s\n' "$name" "$rc" >&2
        exit 1
    fi
    if [[ "$name" == funktionswert ]]; then
        grep -Fq 'Nativer Funktionswert' "$out/$name.log"
    else
        grep -Fq 'Native FFI-Aufrufe sind derzeit nur in main ausserhalb von try unterstützt' "$out/$name.log"
    fi
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
printf 'FFI-Reachability-Sperren und Lambda-Fallback: PASS\n'
