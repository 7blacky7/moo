#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C MOO_KI_GPU=0
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "Usage: legacy-output OUTPUT_DIR SYNAPSE_BATCH [BASELINE_DIR]" >&2
  exit 2
fi
out="$(realpath -m "$1")"
case "$out" in "$root"/build/p2-ir/*) ;; *) echo "Output must be under build/p2-ir" >&2; exit 2;; esac
[[ "$2" =~ ^[0-9]+$ ]] || { echo "Batch must be numeric" >&2; exit 2; }
[[ ! -e "$out" ]] || { echo "Output already exists: $out" >&2; exit 2; }
baseline=""
if [[ $# == 3 ]]; then baseline="$(realpath -e "$3")"; [[ -f "$baseline/success" ]] || { echo "Baseline incomplete" >&2; exit 2; }; fi
compiler="$root/compiler/target/release/moo-compiler"
[[ -x "$compiler" ]] || { echo "Compiler binary missing" >&2; exit 2; }
sources=(beispiele/p2_bench/{summe,matrix,fuellen,objekte,builtin,rauschen}.moos)
mkdir -p "$out"
inventory() {
  local paths=(compiler/src compiler/runtime)
  [[ ! -d stdlib ]] || paths+=(stdlib)
  [[ ! -d compiler/stdlib ]] || paths+=(compiler/stdlib)
  {
    find "${paths[@]}" -type f -print0
    printf '%s\0' compiler/build.rs "${sources[@]}"
    find compiler/target/release/build -type f -name libmoo_runtime.a -print0
    [[ ! -f compiler/target/release/libmoo_runtime.a ]] || printf '%s\0' compiler/target/release/libmoo_runtime.a
  } | sort -zu | xargs -0 sha256sum
}
sha256sum "$compiler" > "$out/compiler.sha256"
inventory > "$out/inputs.sha256"
printf '%s\n' "$2" > "$out/synapse-batch.txt"
cc --version > "$out/cc-version.txt"
ld --version > "$out/ld-version.txt"
printf '%s\n' "${sources[@]}" > "$out/cases.txt"
sha256sum "${sources[@]}" > "$out/case-inputs.sha256"
failed=0
for i in "${!sources[@]}"; do
  source="${sources[$i]}"
  name="$(basename "$source" .moos)"
  if timeout 60 "$compiler" compile "$source" -o "$out/$name.bin" >"$out/$name.compile.stdout" 2>"$out/$name.compile.stderr"; then cr=0; else cr=$?; fi
  printf '%s\n' "$cr" > "$out/$name.compile.exit"
  if [[ $cr -eq 0 ]]; then
    if timeout 60 "$out/$name.bin" </dev/null >"$out/$name.stdout" 2>"$out/$name.stderr"; then rc=0; else rc=$?; fi
  else rc=125; fi
  printf '%s\n' "$rc" > "$out/$name.exit"
  if [[ $cr -ne 0 || $rc -ne 0 ]]; then echo "FAIL $source compile=$cr run=$rc" >&2; failed=1; continue; fi
  if [[ -n "$baseline" ]]; then
    for suffix in stdout stderr exit; do
      if ! cmp -s "$baseline/$name.$suffix" "$out/$name.$suffix"; then echo "DIFFERENT $source $suffix" >&2; failed=1; fi
    done
  fi
  echo "OK $source"
done
inventory > "$out/inputs-after.sha256"
sha256sum "$compiler" > "$out/compiler-after.sha256"
cmp -s "$out/inputs.sha256" "$out/inputs-after.sha256" || { echo "Inputs changed during capture" >&2; failed=1; }
cmp -s "$out/compiler.sha256" "$out/compiler-after.sha256" || { echo "Compiler changed during capture" >&2; failed=1; }
if [[ -n "$baseline" ]]; then
  for file in cases.txt cc-version.txt ld-version.txt; do
    cmp -s "$baseline/$file" "$out/$file" || { echo "Environment or case input differs: $file" >&2; failed=1; }
  done
  while IFS= read -r line; do
    grep -Fxq -- "$line" "$baseline/inputs.sha256" || { echo "Case input differs: $line" >&2; failed=1; }
  done < "$out/case-inputs.sha256"
fi
(( failed == 0 )) || exit 1
touch "$out/success"
