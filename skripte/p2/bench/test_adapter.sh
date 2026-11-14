#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-bench/adapter-test
mkdir -p "$out"
export P2_G5_ADAPTER_OUT="$out/results"
cc -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p2/bindgen_adapter_test.c -o "$out/fixture"
printf 'synthetic adapter fixture for collector test\n' > "$out/manifest.txt"
bash skripte/p2/bench/g5_adapter.sh native_transition "$out/manifest.txt" -- "$out/fixture"
jq -e '.status=="ok" and .kind=="native_transition" and (.runs|length==5) and
  .median_s.native_core_s==0.25 and .median_s.transition_s==0.5 and
  all(.runs[]; .adapter=="synthetic-fixture" and .checksum==6)' \
  "$P2_G5_ADAPTER_OUT/adapter-native_transition.json" >/dev/null
bash skripte/p2/bench/g5_adapter.sh gpu_transfer "$out/manifest.txt" -- "$out/fixture" gpu
jq -e '.status=="ok" and .kind=="gpu_transfer" and (.runs|length==5) and
  .median_s.upload_s==0.125 and .median_s.download_s==0.75 and
  all(.runs[]; .backend=="synthetic" and .device=="fixture-only")' \
  "$P2_G5_ADAPTER_OUT/adapter-gpu_transfer.json" >/dev/null
rc=0
bash skripte/p2/bench/g5_adapter.sh native_transition "$out/manifest.txt" -- "$out/fixture" multi || rc=$?
[[ $rc -ne 0 ]]
jq -e '.status=="failed" and .kind=="native_transition"' "$P2_G5_ADAPTER_OUT/adapter-native_transition.json" >/dev/null
rc=0
bash skripte/p2/bench/g5_adapter.sh native_transition "$out/manifest.txt" -- /bin/false || rc=$?
[[ $rc -ne 0 ]]
jq -e '.status=="failed" and .kind=="native_transition"' \
  "$P2_G5_ADAPTER_OUT/adapter-native_transition.json" >/dev/null
echo "P2 adapter collector: PASS (synthetic contract only)"
