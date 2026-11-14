#!/usr/bin/env bash
set -euo pipefail
[[ $# -eq 2 ]] || { echo "Usage: legacy_ir_compare.sh NEW BASELINE" >&2; exit 2; }
new="$1"; old="$2"
for file in "$new" "$old"; do
    [[ -f "$file" ]] || { echo "Missing comparison input: $file" >&2; exit 2; }
    jq -e '
      .format == 2 and .normalization == "module-header-root-v2" and
      (.snapshot_drift == false) and
      (.profiles | type == "object") and (.files | type == "object") and
      (.source_snapshot.contract == "legacy-ir-sources-v1") and
      (.source_snapshot.sha256 | type == "string") and (.source_snapshot.files | type == "object") and
      (.import_snapshot.contract == "moo-find-module-candidates-v1") and
      (.import_snapshot.sha256 | type == "string") and (.import_snapshot.files | type == "object") and
      (.toolchain_snapshot.contract == "legacy-ir-toolchain-v1") and
      (.toolchain_snapshot.sha256 | type == "string") and (.toolchain_snapshot.entries | type == "array")
    ' "$file" >/dev/null || { echo "Incompatible comparison input: $file" >&2; exit 2; }
done
diff_output="$(jq -r --slurpfile old "$old" '
  . as $new | $old[0] as $old |
  (if $new.normalization != $old.normalization then "DIFF normalization contract" else empty end),
  (if $new.profiles != $old.profiles then "DIFF profiles" else empty end),
  (if $new.source_snapshot != $old.source_snapshot then "DIFF source snapshot" else empty end),
  (if $new.import_snapshot != $old.import_snapshot then "DIFF import snapshot" else empty end),
  (if $new.toolchain_snapshot != $old.toolchain_snapshot then "DIFF toolchain snapshot" else empty end),
  ((($new.files | keys) + ($old.files | keys) | unique[]) as $path |
    select($new.files[$path] != $old.files[$path]) | "DIFF file \($path)")
' "$new")"
if [[ -n "$diff_output" ]]; then
    printf '%s\n' "$diff_output"
    echo "Comparison: DIFFERENT"
    exit 1
fi
echo "Comparison: IDENTICAL"
