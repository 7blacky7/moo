#!/usr/bin/env bash
# G5 CPU baseline; execute via mise run p2-bench-g5.
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-bench/g5-shell
mkdir -p "$out"
result="$out/result.json"
tmp=$(mktemp -d "$out/run.XXXXXXXX")
failed() {
    local rc=$?
    if (( rc != 0 )); then
        jq -n --arg stage "${stage:-initialization}" --argjson rc "$rc" \
            '{status:"failed",stage:$stage,exit_code:$rc}' > "$result.tmp"
        mv "$result.tmp" "$result"
    fi
}
trap failed EXIT
stage=dependencies
command -v jq >/dev/null
command -v taskset >/dev/null
command -v timeout >/dev/null
allowed=$(taskset -pc $$)
allowed=${allowed##*: }
first=${allowed%%,*}
cpu=${first%%-*}
[[ $cpu =~ ^[0-9]+$ ]]
compiler=compiler/target/release/moo-compiler
sources=(skripte/p2/bench/g5_runner.sh skripte/p2/bench/g5_reference.c
 skripte/p2/bench/g5_ffi.c skripte/p2/bench/g5_graphics.c
 beispiele/p2_bench/g5_kern.moos skripte/p2/grafik/{mat_ref.c,mat_ref.h,mesh_ref.c,mesh_ref.h}
 "$compiler")
sha256sum -- "${sources[@]}" > "$tmp/before.sha256"
stage=build
timeout 60s taskset -c "$cpu" cc -O2 -std=c11 -ffp-contract=off -fPIC -shared \
 skripte/p2/bench/g5_ffi.c -o "$out/libg5ffi.so"
timeout 60s taskset -c "$cpu" cc -O2 -std=c11 -ffp-contract=off \
 skripte/p2/bench/g5_reference.c -L"$out" -lg5ffi '-Wl,-rpath,$ORIGIN' -o "$out/reference"
timeout 60s taskset -c "$cpu" cc -O2 -std=c11 -ffp-contract=off -Iskripte/p2/grafik \
 skripte/p2/bench/g5_graphics.c skripte/p2/grafik/{mat_ref.c,mesh_ref.c} -o "$out/grafik"
timeout 120s taskset -c "$cpu" "$compiler" compile beispiele/p2_bench/g5_kern.moos -o "$out/moo"
stage=measure
for kind in c_memory moo_memory c_ffi c_graphics; do
    : > "$tmp/$kind.ndjson"
    for (( i=0; i<5; i++ )); do
        case "$kind" in
          c_memory)  capture=$(timeout 120s taskset -c "$cpu" "$out/reference" memory) ;;
          moo_memory) capture=$(timeout 120s taskset -c "$cpu" "$out/moo") ;;
          c_ffi) capture=$(timeout 120s taskset -c "$cpu" "$out/reference" ffi_c) ;;
          c_graphics) capture=$(timeout 120s taskset -c "$cpu" "$out/grafik") ;;
        esac
        mapfile -t v <<< "$capture"
        case "$kind" in
          c_memory|moo_memory)
            [[ ${#v[@]} -eq 3 ]]
            jq -n --arg a "${v[0]}" --arg b "${v[1]}" --arg c "${v[2]}" \
              '[$a,$b,$c]|map(tonumber)|select(.[0]>0 and .[1]>0 and .[2]==495000000)' >> "$tmp/$kind.ndjson" ;;
          c_ffi)
            [[ ${#v[@]} -eq 2 ]]
            jq -n --arg a "${v[0]}" --arg b "${v[1]}" \
              '[$a,$b]|map(tonumber)|select(.[0]>0 and .[1]==505000000)' >> "$tmp/$kind.ndjson" ;;
          c_graphics)
            [[ ${#v[@]} -eq 5 ]]
            jq -n --arg a "${v[0]}" --arg b "${v[1]}" --arg c "${v[2]}" --arg d "${v[3]}" --arg e "${v[4]}" \
              '[$a,$b,$c,$d,$e]|map(tonumber)|select(.[0]>0 and .[1]==118000000 and .[2]>0 and .[3]>0 and .[4]==35400000)' >> "$tmp/$kind.ndjson" ;;
        esac
        # Exactly one JSON array must have been appended on each iteration.
        [[ $(jq -s length "$tmp/$kind.ndjson") -eq $((i+1)) ]]
    done
done
stage=drift
sha256sum -- "${sources[@]}" > "$tmp/after.sha256"
cmp -s "$tmp/before.sha256" "$tmp/after.sha256"
stage=report
jq -s '{runs:.,allocation_fill_s:([.[].[0]]|sort|.[2]),core_sum_s:([.[].[1]]|sort|.[2])}' "$tmp/c_memory.ndjson" > "$tmp/c.json"
jq -s '{runs:.,allocation_fill_s:([.[].[0]]|sort|.[2]),core_sum_s:([.[].[1]]|sort|.[2])}' "$tmp/moo_memory.ndjson" > "$tmp/moo.json"
jq -s '{runs:.,call_s:([.[].[0]]|sort|.[2])}' "$tmp/c_ffi.ndjson" > "$tmp/ffi.json"
jq -s '{runs:.,mat4_vec4_s:([.[].[0]]|sort|.[2]),mesh_create_s:([.[].[2]]|sort|.[2]),mesh_transform_s:([.[].[3]]|sort|.[2])}' "$tmp/c_graphics.ndjson" > "$tmp/grafik.json"
model=$(awk -F: '$1 ~ /^model name/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)
[[ -n $model ]]
cc_version=$(cc --version)
jq -n --arg utc "$(date -u +%FT%TZ)" --arg cpu "$cpu" --arg model "$model" \
 --arg platform "$(uname -sm)" --arg cc "$cc_version" --rawfile hashes "$tmp/before.sha256" \
 --slurpfile c "$tmp/c.json" --slurpfile moo "$tmp/moo.json" \
 --slurpfile ffi "$tmp/ffi.json" --slurpfile grafik "$tmp/grafik.json" \
 '{status:"ok",utc:$utc,cpu:$cpu,cpu_model:$model,platform:$platform,cc:$cc,
   flags:"-O2 -std=c11 -ffp-contract=off",sha256:$hashes,iterations:5,
   c_memory:$c[0],moo_memory:$moo[0],c_ffi:$ffi[0],c_graphics:$grafik[0],
   unavailable:{native:"not yet available",moo_ffi:"not yet available",gpu_transfer:"not run"},
   note:"Internal monotonic intervals; allocation includes fill; layouts differ; CPU graphics only."}' > "$result.tmp"
mv "$result.tmp" "$result"
trap - EXIT
echo "G5: 20 Läufe, Prüfsummen und Input-Hashes bestanden; $result"
