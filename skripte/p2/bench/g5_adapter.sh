#!/usr/bin/env bash
# Explicit adapter runner; never starts an adapter without arguments.
set -euo pipefail
cd "$(dirname "$0")/../../.."
[[ $# -ge 4 && $3 == -- ]] || { echo "usage: KIND MANIFEST -- COMMAND [ARG...]" >&2; exit 2; }
kind=$1
manifest=$2
shift 3
[[ $kind == native_transition || $kind == gpu_transfer ]] || { echo "invalid kind" >&2; exit 2; }
[[ -f $manifest ]] || { echo "manifest not found" >&2; exit 2; }
out="${P2_G5_ADAPTER_OUT:-build/p2-bench/g5-shell}"
mkdir -p "$out"
tmp=$(mktemp -d "$out/adapter.XXXXXXXX")
result="$out/adapter-$kind.json"
failed() {
    local rc=$?
    if (( rc != 0 )); then
        jq -n --arg kind "$kind" --argjson rc "$rc" --arg stage "${stage:-initialization}" \
          '{status:"failed",kind:$kind,exit_code:$rc,stage:$stage}' > "$result.tmp"
        mv "$result.tmp" "$result"
    fi
}
trap failed EXIT
stage=manifest
before=$(sha256sum -- "$manifest")
allowed=$(taskset -pc $$)
allowed=${allowed##*: }
first=${allowed%%,*}
cpu=${first%%-*}
[[ $cpu =~ ^[0-9]+$ ]]
: > "$tmp/runs.ndjson"
stage=measure
identity=
for (( i=0; i<5; i++ )); do
    capture=$(timeout 120s taskset -c "$cpu" "$@")
    if [[ $kind == gpu_transfer ]]; then
        filter='.schema=="p2-g5-adapter-v1" and .kind=="gpu_transfer" and
          (.work_items|type=="number" and .>0 and floor==.) and
          (.checksum|type=="number" and isfinite) and
          (.backend|type=="string" and length>0) and (.device|type=="string" and length>0) and
          (.bytes|type=="number" and .>0 and floor==.) and .synchronized==true and
          (.upload_s|type=="number" and isfinite and .>0) and
          (.download_s|type=="number" and isfinite and .>0)'
        identity_filter='[.backend,.device,.bytes,.work_items,.checksum]'
    else
        filter='.schema=="p2-g5-adapter-v1" and .kind=="native_transition" and
          (.work_items|type=="number" and .>0 and floor==.) and
          (.checksum|type=="number" and isfinite) and
          (.adapter|type=="string" and length>0) and
          (.native_core_s|type=="number" and isfinite and .>0) and
          (.transition_s|type=="number" and isfinite and .>0)'
        identity_filter='[.adapter,.work_items,.checksum]'
    fi
    jq -s -e "length == 1 and (.[0] | ($filter))" <<< "$capture" >/dev/null
    current=$(jq -s -c ".[0] | ($identity_filter)" <<< "$capture")
    if [[ -z $identity ]]; then identity=$current; else [[ $identity == "$current" ]]; fi
    jq -s -c ".[0]" <<< "$capture" >> "$tmp/runs.ndjson"
done
stage=drift
[[ $before == "$(sha256sum -- "$manifest")" ]]
stage=report
if [[ $kind == gpu_transfer ]]; then
    medians='{"upload_s":([.[].upload_s]|sort|.[2]),"download_s":([.[].download_s]|sort|.[2])}'
else
    medians='{"native_core_s":([.[].native_core_s]|sort|.[2]),"transition_s":([.[].transition_s]|sort|.[2])}'
fi
jq -s --arg kind "$kind" --arg manifest "$manifest" --arg hash "$before" --arg cpu "$cpu" \
   --arg utc "$(date -u +%FT%TZ)" --argjson command "$(jq -n --args '$ARGS.positional' -- "$@")" \
   "{status:\"ok\",kind:\$kind,manifest:\$manifest,manifest_sha256:\$hash,cpu:\$cpu,utc:\$utc,command:\$command,runs:.,median_s:$medians,note:\"Adapter device, synchronization and checksum are producer claims; verify independently.\"}" \
   "$tmp/runs.ndjson" > "$result.tmp"
mv "$result.tmp" "$result"
trap - EXIT
echo "G5 Adapter: 5 Läufe validiert; $result"
