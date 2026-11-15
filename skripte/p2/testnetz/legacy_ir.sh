#!/usr/bin/env bash
# Capture LLVM IR using the existing compiler.
set -euo pipefail
export LC_ALL=C
export TZ=UTC
ROOT="$(cd "$(dirname "$BASH_SOURCE")/../../.." && pwd)"
cd "$ROOT"
compiler="$ROOT/compiler/target/release/moo-compiler"
output=""; baseline=""; profiles_file=""; save_dir=""; snapshot_ref=""; batch_ref=""; limit=30
allow_baseline_failures=false
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
      --allow-baseline-failures) allow_baseline_failures=true; shift;;
      *) echo "Unknown option: $1" >&2; exit 2;;
    esac
done
[[ -n "$output" && "$limit" =~ ^[1-9][0-9]*$ ]] || { echo "Missing output or invalid timeout" >&2; exit 2; }
[[ -z "$snapshot_ref" || "$snapshot_ref" =~ ^[0-9a-f]{40}$ ]] || { echo "Snapshot reference must be a 40-character lowercase hex SHA" >&2; exit 2; }
[[ -z "$batch_ref" || "$batch_ref" =~ ^[0-9]+$ ]] || { echo "Batch reference must be numeric" >&2; exit 2; }
[[ "$allow_baseline_failures" == false || -n "$baseline" ]] || { echo "--allow-baseline-failures requires --baseline" >&2; exit 2; }
for cmd in jq sha256sum timeout find sort awk realpath mktemp cmp grep xargs uname; do
    command -v "$cmd" >/dev/null || { echo "Missing tool: $cmd" >&2; exit 2; }
done
compiler="$(realpath -m -- "$compiler")"; output="$(realpath -m -- "$output")"
[[ -x "$compiler" && -f "$compiler" ]] || { echo "Compiler missing" >&2; exit 2; }
compiler_dir="$(dirname "$compiler")"
package_root="${HOME:-}/.moo/packages"
compare_script="$ROOT/skripte/p2/testnetz/legacy_ir_compare.sh"
[[ -f "$compare_script" ]] || { echo "Comparison helper missing" >&2; exit 2; }
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
    } | sort -z
}
list_import_candidates() {
    local root
    for root in compiler/tests beispiele stdlib compiler/stdlib "$compiler_dir/stdlib"; do
        [[ ! -d "$root" ]] || find "$root" -type f \( -name '*.moos' -o -name '*.moo' \) -print0
    done
    [[ ! -d "$package_root" ]] || find "$package_root" -type f \( -name '*.moos' -o -name '*.moo' \) -print0
}
snapshot_label() {
    local file="$1"
    case "$file" in
      "$ROOT"/*) printf '%s' "${file#"$ROOT"/}";;
      "$compiler_dir/stdlib"/*) printf '<compiler-stdlib>/%s' "${file#"$compiler_dir/stdlib"/}";;
      "$package_root"/*) printf '<packages>/%s' "${file#"$package_root"/}";;
      *) printf '%s' "$file";;
    esac
}
write_hash_inventory() {
    local list_file="$1" output_file="$2" file label sum
    : > "$output_file"
    while IFS= read -r -d '' file; do
        [[ -f "$file" ]] || return 1
        label="$(snapshot_label "$file")"
        [[ "$label" != *$'\t'* && "$label" != *$'\n'* ]] || { echo "Unsupported path in snapshot" >&2; return 1; }
        sum="$(sha256sum -- "$file" | awk '{print $1}')"
        printf '%s\t%s\n' "$sum" "$label" >> "$output_file"
    done < "$list_file"
}
product_inventory() {
    { find compiler/src compiler/runtime -type f -print0
      [[ ! -d stdlib ]] || find stdlib -type f -print0
      [[ ! -d compiler/stdlib ]] || find compiler/stdlib -type f -print0
      printf "%s\0" compiler/build.rs
      find compiler/target/release/build -type f -name libmoo_runtime.a -print0
      [[ ! -f compiler/target/release/libmoo_runtime.a ]] || printf "%s\0" compiler/target/release/libmoo_runtime.a
    } | sort -zu | xargs -0 sha256sum
}
tool_info() {
    local name="$1" path resolved sha version
    if path="$(command -v "$name" 2>/dev/null)"; then
        resolved="$(realpath -m -- "$path")"
        if [[ -f "$resolved" ]]; then sha="$(sha256sum -- "$resolved" | awk '{print $1}')"; else sha="not-a-file"; fi
        version="$("$name" --version 2>/dev/null || true)"
        version="${version%%$'\n'*}"
        printf 'tool=%s|sha256=%s|version=%s\n' "$name" "$sha" "$version"
    else
        printf 'tool=%s|missing\n' "$name"
    fi
}
toolchain_snapshot() {
    local cpu_model cpu_flags
    cpu_model="$(awk -F: '/^model name/{sub(/^[ \t]+/,"",$2); print $2; exit}' /proc/cpuinfo 2>/dev/null || true)"
    cpu_flags="$(awk -F: '/^flags/{sub(/^[ \t]+/,"",$2); print $2; exit}' /proc/cpuinfo 2>/dev/null || true)"
    printf 'contract=legacy-ir-toolchain-v1\n'
    printf 'uname=%s\n' "$(uname -srm)"
    printf 'cpu_model=%s\n' "$cpu_model"
    printf 'cpu_flags=%s\n' "$cpu_flags"
    local tool
    for tool in bash jq sha256sum timeout find sort awk realpath cmp xargs rustc cargo cc ld llvm-config mise; do
        tool_info "$tool"
    done
}

product_inventory > "$work/product-inventory"
product_inventory_sha="$(sha256sum "$work/product-inventory" | awk '{print $1}')"
list_sources > "$work/sources"
[[ -s "$work/sources" ]] || { echo "No source files" >&2; exit 2; }
write_hash_inventory "$work/sources" "$work/source-inventory"
input_sha="$(sha256sum "$work/source-inventory" | awk '{print $1}')"
list_import_candidates | sort -zu > "$work/import-candidates"
write_hash_inventory "$work/import-candidates" "$work/import-inventory"
import_sha="$(sha256sum "$work/import-inventory" | awk '{print $1}')"
toolchain_snapshot > "$work/toolchain"
toolchain_sha="$(sha256sum "$work/toolchain" | awk '{print $1}')"
compiler_sha="$(sha256sum -- "$compiler" | awk '{print $1}')"
sources_json="$(jq -Rn '[inputs | select(length > 0) | split("\t") | {(.[1]): .[0]}] | add // {}' < "$work/source-inventory")"
imports_json="$(jq -Rn '[inputs | select(length > 0) | split("\t") | {(.[1]): .[0]}] | add // {}' < "$work/import-inventory")"
toolchain_json="$(jq -Rsc 'split("\n") | map(select(length > 0))' < "$work/toolchain")"

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
      .format == 2 and .normalization == "module-header-root-v2" and
      ((.profiles // {}) == $profiles) and ((.snapshot_drift // false) == false) and
      (.files | type == "object") and (.source_snapshot.files | type == "object") and
      (.import_snapshot.files | type == "object") and (.toolchain_snapshot.entries | type == "array")
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
            awk -v root="$ROOT/" -v packages="$package_root/" '{
              sub(/\r$/, ""); gsub(root, "<root>/"); gsub(packages, "<packages>/"); print
            }' "$work/compile.log" > "$work/diagnostic.log"
            diagnostic_sha="$(sha256sum "$work/diagnostic.log" | awk '{print $1}')"
            entry="$(jq -nc --arg key "$source" --argjson rc "$rc" --arg sha "$diagnostic_sha" \
              '{key:$key,value:{status:"compile_error",exit_code:$rc,diagnostic_sha256:$sha}}')"
        fi
        echo "FAIL $source: $status"
    elif [[ ! -f "$ir" ]]; then
        fail=$((fail + 1)); echo "FAIL $source: missing_ir"
        entry="$(jq -nc --arg key "$source" '{key:$key,value:{status:"missing_ir"}}')"
    else
        raw_sha="$(sha256sum "$ir" | awk '{print $1}')"
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
            entry="$(jq -nc --arg key "$source" --arg sha "$raw_sha" \
              '{key:$key,value:{status:"unsafe_path_in_ir",raw_sha256:$sha}}')"
        fi
    fi
    printf '%s\n' "$entry" >> "$work/entries"
done < "$work/sources"

list_sources > "$work/sources-after"
write_hash_inventory "$work/sources-after" "$work/source-inventory-after"
list_import_candidates | sort -zu > "$work/import-candidates-after"
write_hash_inventory "$work/import-candidates-after" "$work/import-inventory-after"
product_inventory > "$work/product-inventory-after"
toolchain_snapshot > "$work/toolchain-after"
after_compiler="$(sha256sum -- "$compiler" | awk '{print $1}')"
drift=false
if ! cmp -s "$work/product-inventory" "$work/product-inventory-after" ||
   ! cmp -s "$work/source-inventory" "$work/source-inventory-after" ||
   ! cmp -s "$work/import-inventory" "$work/import-inventory-after" ||
   ! cmp -s "$work/toolchain" "$work/toolchain-after" ||
   [[ "$after_compiler" != "$compiler_sha" ]]; then
    drift=true
    echo "FAIL: compiler, source/import snapshot or toolchain changed during capture"
fi

# Grosse Listen als Datei: ein einzelnes Argument darf 128 KiB nicht ueberschreiten
# (MAX_ARG_STRLEN), sonst "Argument list too long".
printf '%s' "$sources_json" > "$work/sources.json"
printf '%s' "$imports_json" > "$work/imports.json"
jq -s --arg compiler "$compiler_sha" --arg input "$input_sha" --arg imports "$import_sha" \
  --arg tools "$toolchain_sha" --arg snapshot "$snapshot_ref" --arg batch "$batch_ref" \
  --arg inventory "$product_inventory_sha" --argjson profiles "$profiles" --argjson drift "$drift" \
  --slurpfile sources_d "$work/sources.json" --slurpfile import_d "$work/imports.json" \
  --argjson tool_entries "$toolchain_json" --argjson failures "$fail" '
  {format:2,compiler_sha256:$compiler,normalization:"module-header-root-v2",
   synapse_batch:(if $batch == "" then null else $batch end),
   provenance_attestation:"caller-provided-unverified",
   compiler_runtime_inventory_sha256:$inventory,
   snapshot_ref:(if $snapshot == "" then null else $snapshot end),
   snapshot_ref_attestation:"caller-provided-unverified",
   profiles:$profiles,snapshot_drift:$drift,failure_count:$failures,
   source_snapshot:{contract:"legacy-ir-sources-v1",sha256:$input,files:$sources_d[0]},
   import_snapshot:{contract:"moo-find-module-candidates-v1",sha256:$imports,files:$import_d[0]},
   toolchain_snapshot:{contract:"legacy-ir-toolchain-v1",sha256:$tools,entries:$tool_entries},
   files:from_entries}
' "$work/entries" > "$output"
printf 'Sources: %d, IR: %d, failures: %d\n' "$total" "$((total-fail))" "$fail"

different=false
if [[ -n "$baseline" ]]; then
    if ! bash "$compare_script" "$output" "$baseline"; then different=true; fi
    [[ "$(jq -r '.compiler_sha256' "$baseline")" == "$compiler_sha" ]] || echo "Compiler binary hash changed (informational)"
    [[ "$(jq -r '.compiler_runtime_inventory_sha256' "$baseline")" == "$product_inventory_sha" ]] ||
        echo "Compiler/runtime product inventory changed (informational)"
fi
unacceptable="$(jq '[.files[] | select(.status != "ok" and .status != "compile_error" and .status != "unsafe_path_in_ir")] | length' "$output")"
if [[ "$drift" == true || "$different" == true ]] || ((unacceptable > 0)); then exit 1; fi
if ((fail > 0)); then
    if [[ "$allow_baseline_failures" == true && -n "$baseline" ]]; then
        echo "Compatibility gate IDENTICAL with $fail pinned open failures; this is not compile-success."
        exit 0
    fi
    exit 1
fi
