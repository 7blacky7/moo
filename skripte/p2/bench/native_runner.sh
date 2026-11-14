#!/usr/bin/env bash
# Native/Moo vs C benchmark; fails closed while native lowering is incomplete.
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-bench/native
mkdir -p "$out"
tmp=$(mktemp -d "$out/run.XXXXXXXX")
result="$out/result.json"
stage=init
failed() {
    local rc=$?
    if ((rc != 0)); then
        jq -n --arg stage "$stage" --argjson rc "$rc" '{status:"failed",stage:$stage,exit_code:$rc}' > "$result.tmp"
        mv "$result.tmp" "$result"
    fi
}
trap failed EXIT
allowed=$(taskset -pc $$); allowed=${allowed##*: }; first=${allowed%%,*}; cpu=${first%%-*}
[[ $cpu =~ ^[0-9]+$ ]]
compiler=compiler/target/release/moo-compiler
sources=(compiler/tests/p2/native_bench.moos compiler/tests/p2/native_bench.c skripte/p2/bench/native_runner.sh "$compiler")
sha256sum -- "${sources[@]}" > "$tmp/before.sha256"
stage=build
timeout 60s taskset -c "$cpu" cc -O2 -std=c11 -ffp-contract=off -Wall -Wextra -Werror compiler/tests/p2/native_bench.c -o "$out/c"
timeout 120s taskset -c "$cpu" "$compiler" compile compiler/tests/p2/native_bench.moos -o "$out/moo"
stage=measure
for side in c moo; do
    : > "$tmp/$side.ndjson"
    for ((i=0; i<5; i++)); do
        if [[ $side == c ]]; then capture=$(timeout 120s taskset -c "$cpu" "$out/c" 10000000)
        else capture=$(timeout 120s taskset -c "$cpu" "$out/moo"); fi
        mapfile -t values <<< "$capture"
        [[ ${#values[@]} -eq 2 ]]
        jq -ne --arg seconds "${values[0]}" --arg checksum "${values[1]}" \
            '{seconds:($seconds|tonumber),checksum:($checksum|tonumber)} | select(.seconds>0 and .checksum==49999995000000)' >> "$tmp/$side.ndjson"
        [[ $(jq -s length "$tmp/$side.ndjson") -eq $((i+1)) ]]
    done
done
stage=drift
sha256sum -- "${sources[@]}" > "$tmp/after.sha256"
cmp -s "$tmp/before.sha256" "$tmp/after.sha256"
stage=report
model=$(awk -F: '$1 ~ /^model name/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)
[[ -n $model ]]
jq -n --arg cpu "$cpu" --arg model "$model" --arg platform "$(uname -sm)" --arg cc "$(cc --version)" --arg utc "$(date -u +%FT%TZ)" --rawfile hashes "$tmp/before.sha256" \
  --slurpfile c "$tmp/c.ndjson" --slurpfile moo "$tmp/moo.ndjson" '
  ([$c[].seconds]|sort|.[2]) as $cm |
  ([$moo[].seconds]|sort|.[2]) as $mm |
  {status:"ok",utc:$utc,cpu:$cpu,cpu_model:$model,platform:$platform,cc:$cc,flags:"-O2 -std=c11 -ffp-contract=off",sha256:$hashes,iterations:5,
   c:{runs:$c,median_s:$cm},native:{runs:$moo,median_s:$mm},factor_native_to_c:($mm/$cm),
   note:"i64 loop; Moo interval includes one dynamic/native entry, C interval includes one function call; process startup excluded"}' > "$result.tmp"
mv "$result.tmp" "$result"
trap - EXIT
echo "Native baseline: 10 Läufe bestanden; $result"
