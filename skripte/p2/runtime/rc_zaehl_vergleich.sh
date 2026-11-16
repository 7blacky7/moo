#!/usr/bin/env bash
# P2-T112: Vergleich nach User-Instruktionen (lastunabhaengig) und minimalen
# User-Takten. Erwartet die von rc_bench_vergleich.sh gebauten Paare
# ARBEITSVERZ/<prog>_a und <prog>_b.
# Aufruf: rc_zaehl_vergleich.sh ZAEHLE ARBEITSVERZ [RUNDEN] [CPU]
set -uo pipefail
z="$(realpath "$1")"; work="$(realpath "$2")"; runden="${3:-9}"; cpu="${4:-6}"
printf "%-16s %14s %14s %8s %12s %12s %8s\n" programm instr_a instr_b d_instr takte_min_a takte_min_b d_takte
for a in "$work"/*_a; do
  p="$(basename "$a" _a)"; b="$work/${p}_b"; [[ -x "$b" ]] || continue
  : >"$work/$p.za"; : >"$work/$p.zb"
  for _ in $(seq "$runden"); do
    taskset -c "$cpu" "$z" "$a" 2>>"$work/$p.za" >/dev/null
    taskset -c "$cpu" "$z" "$b" 2>>"$work/$p.zb" >/dev/null
  done
  wert() { grep -o "$1=[0-9]*" "$2" | cut -d= -f2 | sort -n | awk -v w="$3" '{v[NR]=$1} END {print (w=="min") ? v[1] : v[int((NR+1)/2)]}'; }
  ia=$(wert instr "$work/$p.za" med); ib=$(wert instr "$work/$p.zb" med)
  ta=$(wert takte "$work/$p.za" min); tb=$(wert takte "$work/$p.zb" min)
  di=$(awk -v x="$ia" -v y="$ib" 'BEGIN{printf "%+.2f%%", (y-x)/x*100}')
  dt=$(awk -v x="$ta" -v y="$tb" 'BEGIN{printf "%+.2f%%", (y-x)/x*100}')
  printf "%-16s %14s %14s %8s %12s %12s %8s\n" "$p" "$ia" "$ib" "$di" "$ta" "$tb" "$dt"
done
