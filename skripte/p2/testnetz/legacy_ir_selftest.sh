#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$BASH_SOURCE")/../../.." && pwd)"
baseline="${1:-$ROOT/skripte/p2/testnetz/legacy-ir-baseline.json}"
compare="$ROOT/skripte/p2/testnetz/legacy_ir_compare.sh"
[[ -f "$baseline" ]] || { echo "Baseline missing: $baseline" >&2; exit 2; }
mkdir -p "$ROOT/build"
work="$(mktemp -d "$ROOT/build/.legacy-ir-selftest.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT
bash "$compare" "$baseline" "$baseline" >/dev/null
first_ok="$(jq -r 'first(.files | to_entries[] | select(.value.status == "ok") | .key) // empty' "$baseline")"
[[ -n "$first_ok" ]] || { echo "Baseline has no successful IR entry" >&2; exit 2; }
jq --arg key "$first_ok" '.files[$key].sha256 = "0000000000000000000000000000000000000000000000000000000000000000"' \
  "$baseline" > "$work/ir-mutated.json"
set +e
ir_output="$(bash "$compare" "$work/ir-mutated.json" "$baseline" 2>&1)"
ir_rc=$?
set -e
[[ $ir_rc -eq 1 ]] || { echo "IR mutation was not rejected" >&2; exit 1; }
case "$ir_output" in *"DIFF file $first_ok"*) ;; *) echo "IR mutation diagnostic missing" >&2; exit 1;; esac
jq '.import_snapshot.sha256 = "0000000000000000000000000000000000000000000000000000000000000000"' \
  "$baseline" > "$work/import-mutated.json"
set +e
import_output="$(bash "$compare" "$work/import-mutated.json" "$baseline" 2>&1)"
import_rc=$?
set -e
[[ $import_rc -eq 1 ]] || { echo "Import snapshot mutation was not rejected" >&2; exit 1; }
case "$import_output" in *"DIFF import snapshot"*) ;; *) echo "Import mutation diagnostic missing" >&2; exit 1;; esac
echo "PASS legacy IR mutation self-test"
