#!/usr/bin/env bash
# Native/Moo vs C benchmark; fails closed while native lowering is incomplete.
# Last 'suite' (P2-T54): gepaarter Vergleich der sieben nativen Lasten.
# Last 'summe' (Standard, P2-T30): i64-Schleife nativ vs C -> result.json.
# Last 'puffer' (P2-T15/T50 C4, Plan bb3878ef): 10^7 f32 fuellen (i % 1024) und
# in f64 summieren, nativ ueber puffer[f32] vs dynamische Moo-Liste vs C
# -> build/p2-bench/native_puffer/result.json. MOO_COMPILER waehlt das Binary.
set -euo pipefail
cd "$(dirname "$0")/../../.."
last=${1:-summe}
if [[ $last == suite ]]; then
    exec bash skripte/p2/bench/native_suite.sh
fi
case $last in
    summe)
        out=build/p2-bench/native; pruef=49999995000000; seiten=(c moo)
        c_src=compiler/tests/p2/native_bench.c; moo_src=compiler/tests/p2/native_bench.moos; liste_src=""
        note="i64 loop; Moo interval includes one dynamic/native entry, C interval includes one function call; process startup excluded" ;;
    puffer)
        out=build/p2-bench/native_puffer; pruef=5114877120; seiten=(c moo liste)
        c_src=compiler/tests/p2/native_puffer_bench.c; moo_src=compiler/tests/p2/native_puffer_bench.moos
        liste_src=compiler/tests/p2/native_puffer_bench_liste.moos
        note="10^7 f32 fill+f64 sum; storage allocated outside the interval on all sides; Moo interval includes one dynamic/native entry with buffer pin, list side is purely dynamic, C interval includes one function call; process startup excluded" ;;
    *) echo "Unbekannte Last '$last' (summe|puffer|suite)" >&2; exit 2 ;;
esac
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
compiler=${MOO_COMPILER:-compiler/target/release/moo-compiler}
sources=("$moo_src" "$c_src" ${liste_src:+"$liste_src"} skripte/p2/bench/native_runner.sh "$compiler")
sha256sum -- "${sources[@]}" > "$tmp/before.sha256"
stage=build
timeout 60s taskset -c "$cpu" cc -O2 -std=c11 -ffp-contract=off -Wall -Wextra -Werror "$c_src" -o "$out/c"
timeout 120s taskset -c "$cpu" "$compiler" compile "$moo_src" -o "$out/moo"
if [[ -n $liste_src ]]; then timeout 120s taskset -c "$cpu" "$compiler" compile "$liste_src" -o "$out/liste"; fi
stage=measure
: > "$tmp/liste.ndjson"
for side in "${seiten[@]}"; do
    : > "$tmp/$side.ndjson"
    for ((i=0; i<5; i++)); do
        if [[ $side == c ]]; then capture=$(timeout 120s taskset -c "$cpu" "$out/c" 10000000)
        else capture=$(timeout 120s taskset -c "$cpu" "$out/$side"); fi
        mapfile -t values <<< "$capture"
        [[ ${#values[@]} -eq 2 ]]
        jq -ne --arg seconds "${values[0]}" --arg checksum "${values[1]}" --argjson pruef "$pruef" \
            '{seconds:($seconds|tonumber),checksum:($checksum|tonumber)} | select(.seconds>0 and .checksum==$pruef)' >> "$tmp/$side.ndjson"
        [[ $(jq -s length "$tmp/$side.ndjson") -eq $((i+1)) ]]
    done
done
stage=drift
sha256sum -- "${sources[@]}" > "$tmp/after.sha256"
cmp -s "$tmp/before.sha256" "$tmp/after.sha256"
stage=report
model=$(awk -F: '$1 ~ /^model name/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)
[[ -n $model ]]
jq -n --arg last "$last" --arg cpu "$cpu" --arg model "$model" --arg platform "$(uname -sm)" --arg cc "$(cc --version)" --arg utc "$(date -u +%FT%TZ)" --arg note "$note" --rawfile hashes "$tmp/before.sha256" \
  --slurpfile c "$tmp/c.ndjson" --slurpfile moo "$tmp/moo.ndjson" --slurpfile liste "$tmp/liste.ndjson" '
  ([$c[].seconds]|sort|.[2]) as $cm |
  ([$moo[].seconds]|sort|.[2]) as $mm |
  {status:"ok",last:$last,utc:$utc,cpu:$cpu,cpu_model:$model,platform:$platform,cc:$cc,flags:"-O2 -std=c11 -ffp-contract=off",sha256:$hashes,iterations:5,
   c:{runs:$c,median_s:$cm},native:{runs:$moo,median_s:$mm},factor_native_to_c:($mm/$cm),note:$note}
  | if ($liste|length) > 0 then
      ([$liste[].seconds]|sort|.[2]) as $lm | .liste={runs:$liste,median_s:$lm} | .factor_liste_to_native=($lm/$mm)
    else . end' > "$result.tmp"
mv "$result.tmp" "$result"
trap - EXIT
echo "Native $last: ${#seiten[@]}x5 Laeufe bestanden; $result"
