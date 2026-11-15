#!/usr/bin/env bash
# Jeder Fall muss ohne Typdiagnose kompilierbar bleiben.
set -uo pipefail
cd "$(dirname "$0")/../../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
dir=skripte/p2/typen/fehlalarme
out=build/p2-typen-fehlalarme
mkdir -p "$out"
failed=0
count=0
for file in "$dir"/*.moos; do
    [ -f "$file" ] || continue
    name="${file##*/}"
    count=$((count + 1))
    timeout 120s "$compiler" pruefe --typpruefung "$file" --diagnose=json >"$out/$name.out" 2>"$out/$name.json"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        printf 'FAIL %s: RC %s statt 0\n' "$file" "$rc"
        cat "$out/$name.json"
        failed=1
        continue
    fi
    if ! jq -e -s 'length == 1 and (.[0].diagnostics | type == "array") and ([.[0].diagnostics[] | select(.code | startswith("M-T"))] | length == 0)' "$out/$name.json" >/dev/null; then
        printf 'FAIL %s: M-T-Diagnose oder ungültiges JSON\n' "$file"
        cat "$out/$name.json"
        failed=1
        continue
    fi
    printf 'PASS %s\n' "$file"
done
if [ "$count" -eq 0 ]; then
    echo "FAIL keine Fehlalarm-Fixtures gefunden"
    exit 1
fi
if [ "$failed" -ne 0 ]; then
    echo "FAIL Fehlalarm-Gate ($count Dateien)"
    exit 1
fi
printf 'PASS Fehlalarm-Gate (%s Dateien)\n' "$count"
