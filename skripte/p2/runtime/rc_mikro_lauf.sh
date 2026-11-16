#!/usr/bin/env bash
# P2-T112: Mikrobenchmark retain+release, vorher (RC_V) gegen nachher (RC_N).
# Aufruf: rc_mikro_lauf.sh RC_V RC_N ZAEHLE [RUNDEN] [CPU-LISTE]
set -uo pipefail
v="$(realpath "$1")"; n="$(realpath "$2")"; z="$(realpath "$3")"; r="${4:-7}"; cpus="${5:-28-31}"
min() { sort -n | head -1; }
fall() {  # name bin args...
  local name="$1" bin="$2"; shift 2
  local ns="" ins=""
  for _ in $(seq "$r"); do
    out="$(taskset -c "$cpus" "$z" "$bin" "$@" 2>&1)"
    ns="$ns $(echo "$out" | grep -o '^[0-9.]* ns' | cut -d' ' -f1)"
    ins="$ins $(echo "$out" | grep -o 'instr=[0-9]*' | cut -d= -f2)"
  done
  printf "%-34s min_ns/Paar=%s instr=%s\n" "$name" "$(tr ' ' '\n' <<<"$ns" | grep . | min)" "$(tr ' ' '\n' <<<"$ins" | grep . | min)"
}
for _ in 1; do
  fall "vorher 1T ungeteilt"               "$v" 200000000 1 0 0
  fall "nachher 1T ungeteilt"              "$n" 200000000 1 0 0
  fall "nachher 1T geteilt-markiert"       "$n" 200000000 1 0 1
  fall "vorher 4T getrennte Objekte"       "$v" 100000000 4 0 0
  fall "nachher 4T getrennte Objekte"      "$n" 100000000 4 0 0
  fall "nachher 4T dasselbe Objekt (geteilt)" "$n" 20000000 4 1 1
done
