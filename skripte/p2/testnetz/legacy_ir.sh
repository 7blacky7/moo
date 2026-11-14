#!/usr/bin/env bash
# Capture LLVM IR using the existing compiler.
set -euo pipefail
ROOT="$(cd "$(dirname "$BASH_SOURCE")/../../.." && pwd)"
cd "$ROOT"
compiler="$ROOT/compiler/target/release/moo-compiler"
output=""; baseline=""; profiles_file=""; save_dir=""; snapshot_ref=""; batch_ref=""; limit=30
while (($#)); do
    case "$1" in
      --output|--baseline|--profiles|--compiler|--timeout|--save-ir|--snapshot-ref|--batch-ref)
        (($# >= 2)) || exit 2
        case "$1" in
          --output) output="$2";; --baseline) baseline="$2";;
          --profiles) profiles_file="$2";; --compiler) compiler="$2";;
          --timeout) limit="$2";; --save-ir) save_dir="$2";;
          --snapshot-ref) snapshot_ref="$2";; --batch-ref) batch_ref="$2";;
        esac
        shift 2;;
      *) echo "Unknown option: $1" >&2; exit 2;;
    esac
done
[[ -n "$output" && "$limit" =~ ^[1-9][0-9]*$ ]] || { echo "Missing output or invalid timeout" >&2; exit 2; }
[[ -z "$snapshot_ref" || "$snapshot_ref" =~ ^[0-9a-f]{40}$ ]] || { echo "Snapshot reference must be a 40-character lowercase hex SHA" >&2; exit 2; }
[[ -z "$batch_ref" || "$batch_ref" =~ ^[0-9]+$ ]] || { echo "Batch reference must be numeric" >&2; exit 2; }
for cmd in jq sha256sum timeout find sort awk realpath mktemp cmp grep xargs; do
    command -v "$cmd" >/dev/null || { echo "Missing tool: $cmd" >&2; exit 2; }
done
compiler="$(realpath -m -- "$compiler")"; output="$(realpath -m -- "$output")"
[[ -x "$compiler" && -f "$compiler" ]] || { echo "Compiler missing" >&2; exit 2; }
if [[ -n "$baseline" ]]; then
    baseline="$(realpath -m -- "$baseline")"
    [[ "$baseline" != "$output" && -f "$baseline" ]] || { echo "Invalid baseline" >&2; exit 2; }
fi
if [[ -n "$save_dir" ]]; then
    save_dir="$(realpath -m -- "$save_dir")"
    case "$save_dir/" in "$ROOT/build/p2-ir/"*) ;; *) echo "IR archive must be under build/p2-ir/" >&2; exit 2;; esac
    [[ ! -e "$save_dir" ]] || { echo "IR archive already exists" >&2; exit 2; }
    mkdir -p -- "$save_dir"
fi
mkdir -p -- "$(dirname "$output")"
work="$(mktemp -d "$(dirname "$output")/.legacy-ir.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT
list_sources() {
    { find compiler/tests -maxdepth 1 -type f -name '*.moos' -print0
      find beispiele -type f -name '*.moos' -print0
    } | LC_ALL=C sort -z
}
source_hash() {
    while IFS= read -r -d '' file; do
        [[ -f "$file" ]] || return 1
        sum="$(sha256sum -- "$file" | awk '{print $1}')"
        printf '%s\0%s\0' "$file" "$sum"
    done < "$1" | sha256sum | awk '{print $1}'
}
inventory() {
    { find compiler/src compiler/runtime -type f -print0
      [[ ! -d stdlib ]] || find stdlib -type f -print0
      [[ ! -d compiler/stdlib ]] || find compiler/stdlib -type f -print0
      printf "%s\0" compiler/build.rs
      find compiler/target/release/build -type f -name libmoo_runtime.a -print0
      [[ ! -f compiler/target/release/libmoo_runtime.a ]] || printf "%s\0" compiler/target/release/libmoo_runtime.a
    } | LC_ALL=C sort -zu | xargs -0 sha256sum
}
inventory > "$work/inventory"
inventory_sha="$(sha256sum "$work/inventory" | awk '{print $1}')"
list_sources > "$work/sources"
[[ -s "$work/sources" ]] || { echo "No source files" >&2; exit 2; }
input_sha="$(source_hash "$work/sources")"
compiler_sha="$(sha256sum -- "$compiler" | awk '{print $1}')"
profiles='{}'
if [[ -n "$profiles_file" ]]; then
    jq -e '
      .schema_version == 1 and (.cases | type == "array") and
      ([.cases[].path] | all(type == "string")) and
      ([.cases[].path] | length == (unique | length)) and
      ([.cases[] | select(.category == "profile")] |
        all(.status == "ir_only_observed_success" and
            (.args | type == "array" and length == 3 and .[0] == "--no-stdlib"
             and .[1] == "--target" and (.[2] | type == "string" and length > 0 and (startswith("-") | not)))))
    ' "$profiles_file" >/dev/null || { echo "Invalid profile manifest" >&2; exit 2; }
    profiles="$(jq -cS '[.cases[] | select(.category == "profile") | {key:.path,value:.args}] | from_entries' "$profiles_file")"
    while IFS= read -r path; do
        [[ -f "$path" ]] && grep -Fxzq -- "$path" "$work/sources" || { echo "Profile source missing: $path" >&2; exit 2; }
    done < <(jq -r 'keys[]' <<< "$profiles")
fi
if [[ -n "$baseline" ]]; then
    jq -e --argjson profiles "$profiles" '
      .format == 1 and .normalization == "module-header-root-v1" and
      ((.profiles // {}) == $profiles) and ((.snapshot_drift // false) == false) and
      (.files | type == "object")
    ' "$baseline" >/dev/null || { echo "Incompatible baseline" >&2; exit 2; }
fi
: > "$work/entries"; fail=0; total=0
while IFS= read -r -d '' source; do
    total=$((total + 1))
    target="$(jq -r --arg path "$source" '.[$path][2] // ""' <<< "$profiles")"
    ir="$work/current.ll"; rm -f -- "$ir"; rc=0
    if [[ -n "$target" ]]; then
        timeout "$limit" "$compiler" compile "$source" --emit-ir -o "$ir" --no-stdlib --target "$target" >"$work/compile.log" 2>&1 || rc=$?
    else
        timeout "$limit" "$compiler" compile "$source" --emit-ir -o "$ir" >"$work/compile.log" 2>&1 || rc=$?
    fi
    if ((rc != 0)); then
        fail=$((fail + 1))
        if ((rc == 124)); then
            status=timeout
            entry="$(jq -nc --arg key "$source" --arg status "$status" '{key:$key,value:{status:$status}}')"
        else
            status=compile_error
            entry="$(jq -nc --arg key "$source" --argjson rc "$rc" '{key:$key,value:{status:"compile_error",exit_code:$rc}}')"
        fi
        echo "FAIL $source: $status"
    elif [[ ! -f "$ir" ]]; then
        fail=$((fail + 1)); echo "FAIL $source: missing_ir"
        entry="$(jq -nc --arg key "$source" '{key:$key,value:{status:"missing_ir"}}')"
    else
        if awk -v root="$ROOT/" '
          { sub(/\r$/, "")
            if ($0 ~ /^; ModuleID = /) $0="; ModuleID = \x27<module>\x27"
            else if ($0 ~ /^source_filename = ".*"$/) $0="source_filename = \"<module>\""
            else if (index($0, root) != 0) exit 42
            print
          }
        ' "$ir" > "$work/normalized.ll"; then
            if [[ -n "$save_dir" ]]; then
                archive="$save_dir/$source.ll"
                mkdir -p -- "$(dirname "$archive")"
                cp -- "$work/normalized.ll" "$archive"
            fi
            hash="$(sha256sum "$work/normalized.ll" | awk '{print $1}')"
            entry="$(jq -nc --arg key "$source" --arg sha "$hash" '{key:$key,value:{status:"ok",sha256:$sha}}')"
        else
            fail=$((fail + 1))
            echo "FAIL $source: unsafe_path_in_ir"
            entry="$(jq -nc --arg key "$source" '{key:$key,value:{status:"unsafe_path_in_ir"}}')"
        fi
    fi
    printf '%s\n' "$entry" >> "$work/entries"
done < "$work/sources"
list_sources > "$work/sources-after"
inventory > "$work/inventory-after"
after_compiler="$(sha256sum -- "$compiler" | awk '{print $1}')"
drift=false
if ! cmp -s "$work/inventory" "$work/inventory-after" || ! cmp -s "$work/sources" "$work/sources-after" ||
   [[ "$after_compiler" != "$compiler_sha" ]] ||
   [[ "$(source_hash "$work/sources-after")" != "$input_sha" ]]; then
    drift=true
    echo "FAIL: compiler or direct sources changed during capture"
fi
jq -s --arg compiler "$compiler_sha" --arg input "$input_sha" --arg snapshot "$snapshot_ref" --arg batch "$batch_ref" --arg inventory "$inventory_sha" --argjson profiles "$profiles" --argjson drift "$drift" '
  {format:1,compiler_sha256:$compiler,normalization:"module-header-root-v1",
   synapse_batch:(if $batch == "" then null else $batch end),
   provenance_attestation:"caller-provided-unverified",
   compiler_runtime_inventory_sha256:$inventory,
   snapshot_ref:(if $snapshot == "" then null else $snapshot end),
   snapshot_ref_attestation:"caller-provided-unverified",
   profiles:$profiles,snapshot_drift:$drift,input_sha256:$input,files:from_entries}
' "$work/entries" > "$output"
printf 'Sources: %d, IR: %d, failures: %d\n' "$total" "$((total-fail))" "$fail"
different=false
if [[ -n "$baseline" ]]; then
    jq -r --slurpfile old "$baseline" '
       .files as $new | ($old[0].files) as $old |
       (($new | keys) + ($old | keys) | unique[]) as $path |
       select($new[$path] != $old[$path]) | "DIFF \($path)"
    ' "$output" > "$work/diff"
    if [[ -s "$work/diff" ]]; then different=true; cat "$work/diff"; fi
    [[ "$(jq -r '.compiler_sha256' "$baseline")" == "$compiler_sha" ]] || echo "Compiler binary hash changed (informational)"
    [[ "$(jq -r '.input_sha256 // ""' "$baseline")" == "$input_sha" ]] || echo "Direct source snapshot changed or is missing (informational)"
    if [[ "$different" == true ]]; then echo "Comparison: DIFFERENT"; else echo "Comparison: IDENTICAL"; fi
fi
if ((fail > 0)) || [[ "$different" == true || "$drift" == true ]]; then exit 1; fi
