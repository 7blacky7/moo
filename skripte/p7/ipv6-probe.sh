#!/usr/bin/env bash
# IPv6-Probe auf dem privaten TAP oder am N100-Direktkabel.
set -euo pipefail
iface="$1"
kernlog="$2"
ausgabe="$3"
mapfile -t ziele < <(grep -aoE 'MOO-KERNEL NETZ IPV6 BEREIT [0-9A-F]{16}' "$kernlog" | awk '!gesehen[$NF]++ { print $NF }')
((${#ziele[@]} > 0)) || { echo 'Keine aktive IPv6-Link-Local-Adresse im Kernlog' >&2; exit 1; }

for iid in "${ziele[@]}"; do
  ziel="fe80::${iid:0:4}:${iid:4:4}:${iid:8:4}:${iid:12:4}%$iface"
  if LC_ALL=C timeout --foreground 10s ping -6 -n -D -c 5 -i 0.25 -W 1 -s 57 "$ziel" >"$ausgabe/ping6.log" 2>&1 &&
     grep -aqF '5 packets transmitted, 5 received' "$ausgabe/ping6.log" &&
     LC_ALL=C timeout --foreground 5s ping -6 -n -D -c 1 -W 1 -M do -s 1452 "$ziel" >>"$ausgabe/ping6.log" 2>&1 &&
     grep -aqF '1 packets transmitted, 1 received' "$ausgabe/ping6.log"; then
    printf '%s\n' "$ziel" >"$ausgabe/ipv6-ziel.txt"
    printf '%s\n' "$iid" >"$ausgabe/ipv6-iid.txt"
    cat "$ausgabe/ping6.log"
    exit 0
  fi
done
cat "$ausgabe/ping6.log" >&2
echo 'IPv6-Echo am Direktlink fehlgeschlagen' >&2
exit 1
