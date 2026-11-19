#!/usr/bin/env bash
# P2-T54: C-O2 und natives Moo mit demselben gepinnten Messrahmen.
set -euo pipefail
cd "$(dirname "$0")/../../.."

basis=build/p2-bench/native-suite
mkdir -p "$basis"
out=$(mktemp -d "$basis/run.XXXXXXXX")
result="$out/result.json"
stage=init
failed() {
    local rc=$?
    if ((rc != 0)); then
        jq -n --arg stage "$stage" --argjson rc "$rc" \
            '{status:"failed",stage:$stage,exit_code:$rc}' > "$result.tmp"
        mv "$result.tmp" "$result"
        printf 'Native Suite fehlgeschlagen: %s\n' "$result" >&2
    fi
}
trap failed EXIT

allowed=$(taskset -pc $$)
allowed=${allowed##*: }
first=${allowed%%,*}
cpu=${MOO_BENCH_CPU:-${first%%-*}}
[[ $cpu =~ ^[0-9]+$ ]]
taskset -c "$cpu" true
[[ $(uname -m) == x86_64 ]]
compiler=${MOO_COMPILER:-compiler/target/release/moo-compiler}
test -x "$compiler"
cc=${CC:-cc}
cc_flags=(-O2 -std=c11 -ffp-contract=off -fno-lto -Wall -Wextra -Werror)
moo_flags=(--no-stdlib --target x86_64-bare)
quellen=(
    .mise.toml
    skripte/p2/bench/native_runner.sh
    skripte/p2/bench/native_suite.sh
    skripte/p2/bench/native_suite.moos
    skripte/p2/bench/native_suite.c
    skripte/p2/bench/native_suite_ref.c
    skripte/p2/bench/native_suite.h
    compiler/runtime/moo_noise.c
    compiler/runtime/moo_noise.h
)
snapshot="$out/snapshot"
mkdir "$snapshot"
sha256sum -- "${quellen[@]}" > "$out/quellen.sha256"
sha256sum -- "$compiler" > "$out/compiler.sha256"
cp --parents -- "${quellen[@]}" "$snapshot"
(cd "$snapshot" && sha256sum --check ../quellen.sha256 >/dev/null)
kern="$snapshot/skripte/p2/bench"
runtime="$snapshot/compiler/runtime"

stage=build
timeout 120s taskset -c "$cpu" "$compiler" compile "$kern/native_suite.moos" \
    "${moo_flags[@]}" --emit-ir -o "$out/moo.ll"
mise exec -- opt -passes=verify -disable-output "$out/moo.ll"
timeout 120s taskset -c "$cpu" "$compiler" compile "$kern/native_suite.moos" \
    "${moo_flags[@]}" -o "$out/moo-roh.o"
mise exec -- llvm-objcopy --localize-symbol=main "$out/moo-roh.o" "$out/moo.o"
timeout 60s taskset -c "$cpu" "$cc" "${cc_flags[@]}" -I"$runtime" -no-pie \
    "$kern/native_suite.c" "$kern/native_suite_ref.c" \
    "$runtime/moo_noise.c" "$out/moo.o" -lm -o "$out/vergleich"
sha256sum -- "$out/moo.ll" "$out/moo.o" "$out/vergleich" > "$out/artefakte.sha256"

stage=measure
timeout 180s taskset -c "$cpu" "$out/vergleich" > "$out/rohwerte.ndjson"
jq -es '
    ([.[].last] == ["summe","vektor","matrix","fuellen","objekte","builtin","rauschen"])
    and all(.[]; [.c_ns, .moo_ns] | all(.[]; length == 5 and all(.[]; . > 0)))
' "$out/rohwerte.ndjson" >/dev/null

stage=drift
sha256sum --check "$out/quellen.sha256" >/dev/null
sha256sum --check "$out/compiler.sha256" >/dev/null
sha256sum --check "$out/artefakte.sha256" >/dev/null
(cd "$snapshot" && sha256sum --check ../quellen.sha256 >/dev/null)

stage=report
model=$(awk -F: '$1 ~ /^model name/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)
[[ -n $model ]]
jq -n \
    --arg utc "$(date -u +%FT%TZ)" --arg platform "$(uname -sm)" \
    --argjson cpu "$cpu" --arg model "$model" \
    --arg cc "$("$cc" --version)" --arg cc_flags "${cc_flags[*]} -no-pie" \
    --arg llvm "$("$compiler" llvm-version)" --arg moo_flags "${moo_flags[*]} default<O2>" \
    --arg snapshot_ref "${MOO_BENCH_SNAPSHOT_REF:-}" \
    --rawfile quellen "$out/quellen.sha256" --rawfile compiler "$out/compiler.sha256" \
    --rawfile artefakte "$out/artefakte.sha256" --slurpfile roh "$out/rohwerte.ndjson" '
    def statistik:
        . as $werte | sort |
        {runs_ns:$werte, min_ns:.[0], median_ns:.[2], max_ns:.[-1]};
    {status:"ok", utc:$utc, platform:$platform, cpu:$cpu, cpu_model:$model,
     cc:$cc, c_flags:$cc_flags, llvm:$llvm, moo_flags:$moo_flags,
     snapshot_ref:(if $snapshot_ref == "" then null else $snapshot_ref end),
     sha256:{quellen:$quellen, compiler:$compiler, artefakte:$artefakte},
     clock:"CLOCK_MONOTONIC", warmups:1, iterations:5,
     order:["c/moo","moo/c","c/moo","moo/c","c/moo"],
     pending_proposed_limit:1.3,
     faelle:[$roh[] |
         . + {c:(.c_ns|statistik), native:(.moo_ns|statistik)} |
         del(.c_ns, .moo_ns) |
         .factor_native_to_c = (.native.median_ns / .c.median_ns) |
         .meets_proposed_limit = (.factor_native_to_c <= 1.3)]}
' > "$result.tmp"
mv "$result.tmp" "$result"
trap - EXIT
printf 'Native Suite: 7 × 2 × 5 Läufe und Ergebnisvergleich bestanden. %s\n' "$result"
