#!/usr/bin/env bash
# P2-T112: Vorher/Nachher-Vergleich der Refcount-Kosten.
# Aufruf: rc_bench_vergleich.sh COMPILER_A COMPILER_B PROGVERZ ARBEITSVERZ [RUNDEN] [CPU]
# Kompiliert jedes PROGVERZ/*.moos mit beiden Compilern, laeuft die Paare
# verschraenkt (A,B,A,B,...) auf einer festen CPU und meldet Median/Minimum in ms
# sowie die Abweichung B gegen A. Ausgaben beider Varianten muessen gleich sein.
set -uo pipefail
a="$(realpath "$1")"; b="$(realpath "$2")"; progs="$(realpath "$3")"; work="$(realpath -m "$4")"
runden="${5:-15}"; cpu="${6:-6}"
mkdir -p "$work"
ms() { local s e; s=$(date +%s%N); taskset -c "$cpu" "$1" >"$2" 2>/dev/null; e=$(date +%s%N); echo $(( (e - s) / 100000 )); }
stat() { sort -n | awk '{v[NR]=$1} END {m=(NR%2)?v[(NR+1)/2]:(v[NR/2]+v[NR/2+1])/2; printf "%.1f %.1f", m/10, v[1]/10}'; }
printf "%-16s %10s %8s %10s %8s %8s %8s\n" programm a_median a_min b_median b_min delta_med delta_min
for src in "$progs"/*.moos; do
  p="$(basename "$src" .moos)"
  "$a" compile "$src" -o "$work/${p}_a" >/dev/null 2>&1 || { echo "$p: A kompiliert nicht"; continue; }
  "$b" compile "$src" -o "$work/${p}_b" >/dev/null 2>&1 || { echo "$p: B kompiliert nicht"; continue; }
  : >"$work/${p}_a.t"; : >"$work/${p}_b.t"
  for _ in $(seq "$runden"); do
    ms "$work/${p}_a" "$work/${p}_a.out" >>"$work/${p}_a.t"
    ms "$work/${p}_b" "$work/${p}_b.out" >>"$work/${p}_b.t"
  done
  cmp -s "$work/${p}_a.out" "$work/${p}_b.out" || echo "$p: AUSGABEN VERSCHIEDEN"
  read -r am amin < <(stat <"$work/${p}_a.t")
  read -r bm bmin < <(stat <"$work/${p}_b.t")
  dm=$(awk -v x="$am" -v y="$bm" 'BEGIN{printf "%+.1f%%", (y-x)/x*100}')
  dn=$(awk -v x="$amin" -v y="$bmin" 'BEGIN{printf "%+.1f%%", (y-x)/x*100}')
  printf "%-16s %10s %8s %10s %8s %8s %8s\n" "$p" "$am" "$amin" "$bm" "$bmin" "$dm" "$dn"
done
