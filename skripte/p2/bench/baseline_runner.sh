#!/usr/bin/env bash
# Seven-case dynamic Moo/C process-time baseline. Run via mise.
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-bench/baseline-shell
mkdir -p "$out"
tmp=$(mktemp -d "$out/run.XXXXXXXX")
result="$out/result.json"
stage=init
failed() {
    local rc=$?
    if (( rc != 0 )); then
        jq -n --arg stage "$stage" --argjson rc "$rc" '{status:"failed",stage:$stage,exit_code:$rc}' > "$result.tmp"
        mv "$result.tmp" "$result"
    fi
}
trap failed EXIT
allowed=$(taskset -pc $$); allowed=${allowed##*: }; first=${allowed%%,*}; cpu=${first%%-*}
[[ $cpu =~ ^[0-9]+$ ]]
compiler=compiler/target/release/moo-compiler
names=(summe vektor matrix fuellen objekte builtin rauschen)
sources=(skripte/p2/bench/baseline_runner.sh skripte/p2/bench/walltime.c
         skripte/p2/bench/referenz.c compiler/runtime/moo_noise.c compiler/runtime/moo_noise.h "$compiler")
for name in "${names[@]}"; do sources+=("beispiele/p2_bench/$name.moos"); done
sha256sum -- "${sources[@]}" > "$tmp/before.sha256"
stage=build
timeout 60s taskset -c "$cpu" cc -O2 -std=c11 -Wall -Wextra -Werror \
  skripte/p2/bench/walltime.c -o "$out/walltime"
timeout 60s taskset -c "$cpu" cc -O2 -std=c11 -ffp-contract=off -Icompiler/runtime \
  skripte/p2/bench/referenz.c compiler/runtime/moo_noise.c -lm -o "$out/c-reference"
for name in "${names[@]}"; do
    timeout 120s taskset -c "$cpu" "$compiler" compile "beispiele/p2_bench/$name.moos" -o "$out/moo-$name"
done
stage=measure
: > "$tmp/cases.ndjson"
for name in "${names[@]}"; do
    for side in c moo; do
        : > "$tmp/$side.ndjson"
        for ((i=0; i<5; i++)); do
            if [[ $side == c ]]; then cmd=("$out/c-reference" "$name"); else cmd=("$out/moo-$name"); fi
            timeout 120s taskset -c "$cpu" "$out/walltime" "$tmp/ns" "${cmd[@]}" > "$tmp/stdout"
            value=$(<"$tmp/stdout")
            ns=$(<"$tmp/ns")
            [[ $ns =~ ^[0-9]+$ && $ns -gt 0 ]]
            jq -ne --arg value "$value" --argjson ns "$ns" \
               '{seconds:($ns/1000000000),value:($value|tonumber)}|select(.value|isfinite)' >> "$tmp/$side.ndjson"
            [[ $(jq -s length "$tmp/$side.ndjson") -eq $((i+1)) ]]
        done
    done
    jq -ne --arg name "$name" --slurpfile c "$tmp/c.ndjson" --slurpfile m "$tmp/moo.ndjson" '
      def abs: if . < 0 then -. else . end;
      ($c[0].value) as $want |
      select(all($c[]; .value == $want)) |
      select(all($m[]; ((.value-$want)|abs) <= ([0.01, (($want|abs)*0.00001)]|max))) |
      ([ $c[].seconds ]|sort|.[2]) as $cm |
      ([ $m[].seconds ]|sort|.[2]) as $mm |
      {name:$name,c:{runs:$c,median_s:$cm},dynamisch:{runs:$m,median_s:$mm},faktor_dynamisch_zu_c:($mm/$cm)}' >> "$tmp/cases.ndjson"
    echo "$name: fünf Läufe je Variante und Prüfsumme bestanden"
done
stage=drift
sha256sum -- "${sources[@]}" > "$tmp/after.sha256"
cmp -s "$tmp/before.sha256" "$tmp/after.sha256"
stage=report
[[ $(jq -s length "$tmp/cases.ndjson") -eq 7 ]]
model=$(awk -F: '$1 ~ /^model name/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)
[[ -n $model ]]
jq -s --arg utc "$(date -u +%FT%TZ)" --arg cpu "$cpu" --arg model "$model" \
  --arg platform "$(uname -sm)" --arg cc "$(cc --version)" --rawfile hashes "$tmp/before.sha256" \
  '{status:"ok",utc:$utc,cpu:$cpu,cpu_model:$model,platform:$platform,cc:$cc,
    flags:"-O2 -std=c11 -ffp-contract=off",sha256:$hashes,iterations:5,cases:.,
    note:"Process wall time includes allocation, checksum and teardown; Moo numbers are dynamic doubles."}' \
   "$tmp/cases.ndjson" > "$result.tmp"
mv "$result.tmp" "$result"
trap - EXIT
echo "Baseline: 70 Läufe bestanden; $result"
