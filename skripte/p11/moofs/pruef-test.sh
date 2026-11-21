#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/moofs
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/moofs_pruefen.moos -o "$out/pruefen-test"
"$out/pruefen-test" > "$out/pruefen-test.txt"
diff -u compiler/tests/p11/moofs_pruefen.expected "$out/pruefen-test.txt"
"$compiler" compile compiler/tests/p11/moofs_pruef_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/pruefen-bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/pruefen-bare.ll"
# Interner Bare-Slottraeger muss semantisch ptr[ptr[void]] liefern.
if "$compiler" compile compiler/tests/p11/cleanup_kontext_falsch.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/cleanup-falsch.ll" > "$out/cleanup-falsch.log" 2>&1; then
    echo 'FAIL: falscher Cleanup-Pointee akzeptiert' >&2
    exit 1
fi
rg -q -F 'ptr[ptr[void]]' "$out/cleanup-falsch.log"
echo 'MooFS Prüfung: beide Generationen und Online-Neustart grün'
