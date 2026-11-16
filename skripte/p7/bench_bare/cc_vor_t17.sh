#!/usr/bin/env bash
# P7-T20: cc-Huelle fuer den Kernel-Build "vor T17". Liegt als "cc" vorn im
# PATH. Bei moo_bare.c werden moo_add/sub/mul/div/lt/gt/lte/gte/is_truthy
# umbenannt und durch vor_t17_ops.c (Stand files-Version 207403) ersetzt.
# Alle anderen Aufrufe gehen unveraendert an /usr/bin/cc.
set -eu
HIER="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
REAL=/usr/bin/cc
case " $* " in
  *"/moo_bare.c "*) ;;
  *) exec "$REAL" "$@" ;;
esac
args=(); out=""; prev=""
for a in "$@"; do
  if [ "$prev" = "-o" ]; then out="$a"; else [ "$a" = "-o" ] || args+=("$a"); fi
  prev="$a"
done
flags=(); for a in "${args[@]}"; do case "$a" in *.c) ;; *) flags+=("$a");; esac; done
ren=""
for f in moo_add moo_sub moo_mul moo_div moo_lt moo_gt moo_lte moo_gte moo_is_truthy; do
  ren="$ren -D$f=${f}_t19"
done
# shellcheck disable=SC2086
"$REAL" "${args[@]}" $ren -o "$out.t19.o"
"$REAL" "${flags[@]}" "$HIER/vor_t17_ops.c" -o "$out.vor.o"
ld -r "$out.t19.o" "$out.vor.o" -o "$out"
