#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-codegen
compiler=compiler/target/release/moo-compiler
source=compiler/tests/p2/m1_i32_probe.moos
"$compiler" compile "$source" --emit-ir -o build/p2-codegen/m1_i32_probe.ll
"$compiler" compile "$source" -o build/p2-codegen/m1_i32_probe
actual=$(build/p2-codegen/m1_i32_probe)
expected=$(printf '42\n-2147483648\n')
if [[ "$actual" != "$expected" ]]; then
    printf 'FAIL: unerwartete Ausgabe: %s\n' "$actual" >&2
    exit 1
fi
awk '
/^define .*@("?__moo_native[.]m1_probe_summe"?)/ { inside=1; signature=($0 ~ /i32.*i32.*i32/); next }
inside && /^}/ { inside=0 }
inside && /add i32 / { add=1 }
inside && /add (nsw|nuw)/ { bad=1 }
END { if (!signature || !add || bad) exit 1 }
' build/p2-codegen/m1_i32_probe.ll
echo "PASS: i32-Ausgabe, Wrap und natives LLVM-IR"
