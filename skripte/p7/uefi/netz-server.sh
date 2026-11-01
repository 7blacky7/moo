#!/usr/bin/env bash
# P7-T10: TFTP-Server fuer den Moo-Netz-Lader (N100 am Direktkabel).
#
# Nutzung:  mise run netz-server [<efi-datei>]       (in skripte/p7/uefi)
#   Standard-Image: build/p7-netz/run5-hallo/BOOTX64.EFI (hallo_uefi mit I226-Treiber, TX-Fix).
#
# Ablauf:
#   1. <efi> nach build/p7-netz/tftp/moo.efi kopieren, moo.efi.pruef schreiben:
#      "<laenge> <crc32hex>" (CRC32 aus dem gzip-Trailer, RFC 1952 - keine
#      Zusatzpakete noetig). Der Lader prueft beides plus den PE-Kopf.
#   2. VORUEBERGEHEND 192.168.77.1/24 auf enp60s0 setzen (sudo ip addr add);
#      beim Beenden (Strg+C) wird die Adresse wieder entfernt.
#   3. Vorhandenes dnsmasq im Vordergrund NUR als TFTP-Server starten:
#      --port=0 (kein DNS), keine dhcp-range (kein DHCP), an enp60s0 gebunden,
#      nach dem Binden von Port 69 als aktueller Benutzer.
# Der N100 nutzt fest 192.168.77.2. Neues Image: Skript erneut starten oder nur
# Schritt 1 wiederholen (MOO_NETZ_NUR_KOPIEREN=1), auf dem N100 "R" druecken.
#
# Umgebung: MOO_NETZ_IF (enp60s0), MOO_NETZ_ADR (192.168.77.1/24),
#           MOO_NETZ_NUR_KOPIEREN=1 (nur moo.efi/.pruef erneuern, kein Server).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
IFACE="${MOO_NETZ_IF:-enp60s0}"
ADR="${MOO_NETZ_ADR:-192.168.77.1/24}"
IP="${ADR%/*}"
EFI="${1:-$ROOT/build/p7-netz/run5-hallo/BOOTX64.EFI}"
TFTP="$ROOT/build/p7-netz/tftp"

[ "$IFACE" != "enp59s0" ] || { echo "enp59s0 ist das Heimnetz - verweigert." >&2; exit 2; }
[ -f "$EFI" ] || { echo "EFI-Datei fehlt: $EFI" >&2; exit 2; }
file -b "$EFI" | grep -q 'PE32+ executable for EFI' || { echo "Kein PE32+-EFI-Image: $EFI" >&2; exit 2; }
ip link show "$IFACE" >/dev/null 2>&1 || { echo "Schnittstelle fehlt: $IFACE" >&2; exit 2; }

mkdir -p "$TFTP"
cp "$EFI" "$TFTP/moo.efi.neu" && mv "$TFTP/moo.efi.neu" "$TFTP/moo.efi"
printf '%s %s\n' "$(stat -c %s "$TFTP/moo.efi")" \
  "$(gzip -c -n "$TFTP/moo.efi" | tail -c8 | od -An -tx4 -N4 | tr -d ' \n')" >"$TFTP/moo.efi.pruef"
echo "moo.efi:       $(sha256sum "$TFTP/moo.efi" | cut -c1-64)  (aus $EFI)"
echo "moo.efi.pruef: $(cat "$TFTP/moo.efi.pruef")"
[ -z "${MOO_NETZ_NUR_KOPIEREN:-}" ] || exit 0

command -v dnsmasq >/dev/null || { echo "dnsmasq fehlt" >&2; exit 2; }
sudo -n true 2>/dev/null || { echo "sudo ohne Passwort noetig (Port 69, Adresse)" >&2; exit 2; }
netz="${IP%.*}."
if ip -4 -o addr show | grep -F " inet $netz" | grep -vq "inet $IP/"; then
  echo "Andere IPv4 aus ${netz}0/24 schon gesetzt - abgebrochen:" >&2
  ip -4 -o addr show | grep -F " inet $netz" >&2
  exit 2
fi
KLIENT="${MOO_NETZ_KLIENT:-${netz}2}"
hinzugefuegt=0
nm_aus=0
fw_regel=0
aufraeumen() {
  if [ "$fw_regel" = 1 ]; then
    sudo -n iptables -D INPUT -i "$IFACE" -s "$KLIENT" -d "$IP" -p udp -j ACCEPT || true
  fi
  if [ "$hinzugefuegt" = 1 ]; then sudo -n ip addr del "$ADR" dev "$IFACE" 2>/dev/null || true; fi
  if [ "$nm_aus" = 1 ]; then sudo -n nmcli device set "$IFACE" managed yes || true; fi
  echo "Aufgeraeumt: Adresse $ADR entfernt=$hinzugefuegt, Firewall-Regel entfernt=$fw_regel, NM wieder verwaltet=$nm_aus"
}
trap aufraeumen EXIT
# Auch bei Abbruch (Strg+C, kill, timeout) ueber EXIT aufraeumen.
trap 'exit 130' INT TERM HUP
# NetworkManager entfernt fremde Adressen beim DHCP-Zyklus seines Profils: nur
# zur Laufzeit die Verwaltung von $IFACE abschalten (nicht im Profil gespeichert).
if command -v nmcli >/dev/null 2>&1 && nmcli -t -f DEVICE,STATE device 2>/dev/null | grep -q "^$IFACE:" \
   && ! nmcli -t -f DEVICE,STATE device | grep -q "^$IFACE:unmanaged"; then
  sudo -n nmcli device set "$IFACE" managed no
  nm_aus=1
fi
if ! ip -4 addr show dev "$IFACE" | grep -q "inet $IP/"; then
  sudo -n ip addr add "$ADR" dev "$IFACE"
  hinzugefuegt=1
fi
# Host-Firewall (UFW) verwirft ankommendes TFTP: Laufzeit-Regel nur fuer den
# Lader ($KLIENT -> $IP, UDP, $IFACE); die UFW-Regeldateien bleiben unveraendert.
if command -v iptables >/dev/null 2>&1; then
  sudo -n iptables -I INPUT 1 -i "$IFACE" -s "$KLIENT" -d "$IP" -p udp -j ACCEPT
  fw_regel=1
fi
echo "TFTP-Server: $IP:69 auf $IFACE, Wurzel $TFTP (Strg+C beendet und entfernt die Adresse)"
sudo -n dnsmasq --no-daemon --conf-file=/dev/null --port=0 --pid-file= \
  --interface="$IFACE" --except-interface=lo --bind-interfaces --listen-address="$IP" \
  --enable-tftp --tftp-root="$TFTP" --user="$(id -un)" --group="$(id -gn)" \
  --log-facility=- || true
