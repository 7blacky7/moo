#!/usr/bin/env bash
# P7-T10: TFTP-Server fuer den Moo-Netz-Lader (N100 am Direktkabel).
#
# Nutzung:  mise run netz-server [<efi-datei> [<kern.elf>]]   (in skripte/p7/uefi)
#   P7-T5: mit <kern.elf> (oder MOO_NETZ_KERN) zusaetzlich moo.kern fuer den Moo-UEFI-Loader.
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
#           MOO_NETZ_STEUERUNG (privates Laufverzeichnis fuer Snapshot, stopp, fertig).
#           MOO_NETZ_NUR_LINK=1 (IPv4/IPv6 vorbereiten und restaurieren).
set -euo pipefail
export LC_ALL=C
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
IFACE="${MOO_NETZ_IF:-enp60s0}"
ADR="${MOO_NETZ_ADR:-192.168.77.1/24}"
IP="${ADR%/*}"
TFTP="${MOO_NETZ_TFTP:-$ROOT/build/p7-netz/tftp}"
NUR_LINK="${MOO_NETZ_NUR_LINK:-0}"
case "$NUR_LINK" in
  0|1) ;;
  *) echo 'MOO_NETZ_NUR_LINK: 0 oder 1' >&2; exit 2 ;;
esac

[ "$IFACE" != "enp59s0" ] || { echo "enp59s0 ist das Heimnetz - verweigert." >&2; exit 2; }
ip link show "$IFACE" >/dev/null 2>&1 || { echo "Schnittstelle fehlt: $IFACE" >&2; exit 2; }
if [ "$NUR_LINK" = 0 ]; then
  EFI="${1:-$ROOT/build/p7-netz/run5-hallo/BOOTX64.EFI}"
  # Optional fuer Kern-Updates: Loader und seine Pruefdatei bleiben bytegleich.
  if [ "${MOO_NETZ_NUR_KERN:-0}" = 1 ]; then
    EFI="$TFTP/moo.efi"
    [ -s "$TFTP/moo.efi.pruef" ] || { echo "Vorhandene Loader-Pruefdatei fehlt" >&2; exit 2; }
    [ -n "${MOO_NETZ_KERN:-${2:-}}" ] || { echo "Kernel-only braucht einen Kern" >&2; exit 2; }
  fi

  [ -f "$EFI" ] || { echo "EFI-Datei fehlt: $EFI" >&2; exit 2; }
  file -b "$EFI" | grep -q 'PE32+ executable for EFI' || { echo "Kein PE32+-EFI-Image: $EFI" >&2; exit 2; }

  mkdir -p "$TFTP"
  if [ "${MOO_NETZ_NUR_KERN:-0}" != 1 ]; then
    cp "$EFI" "$TFTP/moo.efi.neu" && mv "$TFTP/moo.efi.neu" "$TFTP/moo.efi"
    printf '%s %s\n' "$(stat -c %s "$TFTP/moo.efi")" \
      "$(gzip -c -n "$TFTP/moo.efi" | tail -c8 | od -An -tx4 -N4 | tr -d ' \n')" >"$TFTP/moo.efi.pruef"
    echo "moo.efi:       $(sha256sum "$TFTP/moo.efi" | cut -c1-64)  (aus $EFI)"
    echo "moo.efi.pruef: $(cat "$TFTP/moo.efi.pruef")"
  fi
  # P7-T5: optionaler Kern fuer den Moo-UEFI-Loader (moo.efi = beispiele/uefi/moo_lader.moos),
  # 2. Argument oder MOO_NETZ_KERN. Der Netz-Lader holt ihn als moo.kern und uebergibt
  # ihn per LoadOptions. Ohne Kern werden alte moo.kern-Dateien entfernt.
  KERN="${MOO_NETZ_KERN:-${2:-}}"
  if [ -n "$KERN" ]; then
    [ -f "$KERN" ] || { echo "Kern-Datei fehlt: $KERN" >&2; exit 2; }
    [ "$(head -c4 "$KERN" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || { echo "Kein ELF: $KERN" >&2; exit 2; }
    cp "$KERN" "$TFTP/moo.kern.neu" && mv "$TFTP/moo.kern.neu" "$TFTP/moo.kern"
    printf '%s %s\n' "$(stat -c %s "$TFTP/moo.kern")" \
      "$(gzip -c -n "$TFTP/moo.kern" | tail -c8 | od -An -tx4 -N4 | tr -d ' \n')" >"$TFTP/moo.kern.pruef"
    echo "moo.kern:      $(sha256sum "$TFTP/moo.kern" | cut -c1-64)  (aus $KERN)"
    echo "moo.kern.pruef: $(cat "$TFTP/moo.kern.pruef")"
  else
    rm -f "$TFTP/moo.kern" "$TFTP/moo.kern.pruef"
  fi
  [ -z "${MOO_NETZ_NUR_KOPIEREN:-}" ] || exit 0

  command -v dnsmasq >/dev/null || { echo "dnsmasq fehlt" >&2; exit 2; }
fi
sudo -n true 2>/dev/null || { echo "sudo ohne Passwort fuer die Netzkonfiguration noetig" >&2; exit 2; }
netz="${IP%.*}."
if ip -4 -o addr show | grep -F " inet $netz" | grep -vq "inet $IP/"; then
  echo "Andere IPv4 aus ${netz}0/24 schon gesetzt - abgebrochen:" >&2
  ip -4 -o addr show | grep -F " inet $netz" >&2
  exit 2
fi
KLIENT="${MOO_NETZ_KLIENT:-${netz}2}"
STEUERUNG="${MOO_NETZ_STEUERUNG:-}"
zustand="$STEUERUNG"
if [ -z "$zustand" ]; then
  basis="$(dirname "$TFTP")"
  if [ "$NUR_LINK" = 1 ]; then
    basis="$ROOT/build"
    mkdir -p "$basis"
  fi
  zustand="$(mktemp -d "$basis/.netz-zustand.XXXXXX")"
else
  [ -d "$zustand" ] || { echo "Netz-Steuerverzeichnis fehlt: $zustand" >&2; exit 2; }
fi
hinzugefuegt=0
nm_aus=0
fw_regel=0
dns_pid=
v6_adresse=
v6_sysctl="net/ipv6/conf/$IFACE/disable_ipv6"
v6_gen_sysctl="net/ipv6/conf/$IFACE/addr_gen_mode"

nm_verwaltet() { nmcli -g GENERAL.NM-MANAGED device show "$IFACE" 2>/dev/null; }
nm_uuid() { nmcli -g GENERAL.CON-UUID device show "$IFACE" 2>/dev/null | sed '/^--$/d'; }
link_local() { ip -6 -o addr show dev "$IFACE" scope link | awk '{print $4}' | sort; }
ifindex() { ip -o link show dev "$IFACE" | cut -d: -f1; }

nm_vorher="$(nm_verwaltet || true)"
nm_uuid_vorher=
if [ "$nm_vorher" = yes ]; then nm_uuid_vorher="$(nm_uuid)"; fi
{
  printf 'interface=%s\nnm_managed=%s\nnm_uuid=%s\n' "$IFACE" "$nm_vorher" "$nm_uuid_vorher"
  if [ "${MOO_NETZ_IPV6:-0}" = 1 ]; then
    v6_vorher="$(sysctl -n "$v6_sysctl")"
    v6_gen_vorher="$(sysctl -n "$v6_gen_sysctl")"
    v6_link_vorher="$(link_local)"
    v6_index_vorher="$(ifindex)"
    if [ -n "$v6_link_vorher" ]; then
      ip -6 addr save dev "$IFACE" scope link >"$zustand/link-local.ip"
    fi
    printf 'disable_ipv6=%s\naddr_gen_mode=%s\nifindex=%s\nlink_local:\n%s\n' \
      "$v6_vorher" "$v6_gen_vorher" "$v6_index_vorher" "$v6_link_vorher"
  fi
} >"$zustand/vorher"

ipv6_vorbereiten() {
  local aktuell mac
  aktuell="$(sysctl -n "$v6_sysctl")" || return 1
  echo "MOO-NETZ IPV6 START $IFACE disable_ipv6=$v6_vorher vor_temp=$aktuell"
  if [ "$aktuell" = 1 ]; then
    sudo -n sysctl -q -w "$v6_sysctl=0" || return 1
  fi
  if ip -6 -o addr show dev "$IFACE" scope link | grep -vE ' (tentative|dadfailed)( |$)' | grep -q ' inet6 '; then
    return 0
  fi
  mac="$(ip -o link show dev "$IFACE" | awk '{ for (i=1; i<NF; i++) if ($i=="link/ether") { print $(i+1); exit } }')" || return 1
  [[ "$mac" =~ ^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$ ]] || return 1
  printf -v v6_adresse 'fe80::%02x%s:%sff:fe%s:%s%s/64' "$((16#${mac:0:2} ^ 2))" \
    "${mac:3:2}" "${mac:6:2}" "${mac:9:2}" "${mac:12:2}" "${mac:15:2}"
  if ! sudo -n ip -6 addr add "$v6_adresse" dev "$IFACE" nodad; then
    v6_adresse=
    return 1
  fi
}
ipv6_wiederherstellen() {
  local aktuell gen adressen
  [ "$(ifindex)" = "$v6_index_vorher" ] || return 1
  gen="$(sysctl -n "$v6_gen_sysctl")" || return 1
  if [ "$gen" != "$v6_gen_vorher" ]; then
    sudo -n sysctl -q -w "$v6_gen_sysctl=$v6_gen_vorher" || return 1
  fi
  aktuell="$(sysctl -n "$v6_sysctl")" || return 1
  if [ "$aktuell" != "$v6_vorher" ]; then
    sudo -n sysctl -q -w "$v6_sysctl=$v6_vorher" || return 1
  fi
  adressen="$(link_local)" || return 1
  if [ "$adressen" != "$v6_link_vorher" ] && [ -s "$zustand/link-local.ip" ]; then
    # ip route speichert hier RTNL-Adressen mit urspruenglicher ifindex/Flags.
    sudo -n ip -6 addr restore <"$zustand/link-local.ip" || return 1
    adressen="$(link_local)" || return 1
  fi
  gen="$(sysctl -n "$v6_gen_sysctl")" || return 1
  aktuell="$(sysctl -n "$v6_sysctl")" || return 1
  printf 'disable_ipv6=%s\naddr_gen_mode=%s\nlink_local:\n%s\n' "$aktuell" "$gen" "$adressen" >"$zustand/ipv6-nachher" || return 1
  [ "$aktuell" = "$v6_vorher" ] && [ "$gen" = "$v6_gen_vorher" ] && [ "$adressen" = "$v6_link_vorher" ]
}
aufraeumen() {
  local rc=$? restore=0 aktuell uuid
  # Nach Eintritt in den EXIT-Trap darf ein Folgesignal das Rollback
  # zwischen zwei Netzoperationen nicht abbrechen.
  trap '' INT TERM HUP
  trap - EXIT
  set +e
  if [ -n "$dns_pid" ]; then
    if jobs -pr | grep -Fxq "$dns_pid"; then
      kill -TERM "$dns_pid" || restore=1
    fi
    wait "$dns_pid" 2>/dev/null || true
  fi
  if [ "$fw_regel" = 1 ]; then
    sudo -n iptables -D INPUT -i "$IFACE" -s "$KLIENT" -d "$IP" -p udp -j ACCEPT || restore=1
  fi
  if [ "$hinzugefuegt" = 1 ]; then
    sudo -n ip -4 addr del "$ADR" dev "$IFACE" || restore=1
  fi
  if [ -n "$v6_adresse" ] && ip -6 -o addr show dev "$IFACE" to "${v6_adresse%/*}/128" | grep -q ' inet6 '; then
    sudo -n ip -6 addr del "$v6_adresse" dev "$IFACE" || restore=1
  fi
  if [ "$nm_aus" = 1 ]; then
    sudo -n nmcli -w 5 device set "$IFACE" managed yes || restore=1
    if [ -n "$nm_uuid_vorher" ] && [ "$(nm_uuid)" != "$nm_uuid_vorher" ]; then
      sudo -n nmcli -w 15 connection up uuid "$nm_uuid_vorher" ifname "$IFACE" || restore=1
    fi
  fi
  aktuell="$(nm_verwaltet || true)"
  uuid="$(nm_uuid || true)"
  if [[ "$nm_vorher" = yes || "$nm_vorher" = no ]]; then
    [ "$aktuell" = "$nm_vorher" ] || restore=1
    [ -z "$nm_uuid_vorher" ] || [ "$uuid" = "$nm_uuid_vorher" ] || restore=1
  fi
  printf 'nm_managed=%s\nnm_uuid=%s\n' "$aktuell" "$uuid" >"$zustand/nm-nachher" || restore=1
  if [ "${MOO_NETZ_IPV6:-0}" = 1 ]; then
    if ipv6_wiederherstellen; then
      echo "MOO-NETZ IPV6 ZURUECK $IFACE"
    else
      echo "MOO-NETZ IPV6 FEHLER bei Wiederherstellung auf $IFACE" >&2
      restore=1
    fi
  fi
  if [ "$restore" = 0 ]; then
    echo "Aufgeraeumt: Adresse $ADR entfernt=$hinzugefuegt, Firewall-Regel entfernt=$fw_regel, NM wieder verwaltet=$nm_aus"
  else
    echo "MOO-NETZ Wiederherstellung fehlgeschlagen: $zustand" >&2
    rc=1
  fi
  printf '%s\n' "$restore" >"$zustand/fertig" || rc=1
  if [ -z "$STEUERUNG" ] && [ "$restore" = 0 ]; then
    rm -f "$zustand/vorher" "$zustand/link-local.ip" "$zustand/nm-nachher" "$zustand/ipv6-nachher" "$zustand/fertig"
    rmdir "$zustand"
  fi
  exit "$rc"
}
trap aufraeumen EXIT
trap 'exit 130' INT TERM HUP
# NetworkManager kann fremde Adressen beim DHCP-Zyklus seines Profils entfernen.
if [ "$nm_vorher" = yes ]; then
  nm_aus=1
  sudo -n nmcli device set "$IFACE" managed no
fi
if ! ip -4 addr show dev "$IFACE" | grep -q "inet $IP/"; then
  sudo -n ip addr add "$ADR" dev "$IFACE"
  hinzugefuegt=1
fi
if [ "${MOO_NETZ_IPV6:-0}" = 1 ]; then
  ipv6_vorbereiten || { echo "IPv6 auf $IFACE konnte nicht vorbereitet werden" >&2; exit 2; }
  ip -6 -o addr show dev "$IFACE" scope link
fi
if [ "$NUR_LINK" = 1 ]; then
  printf 'MOO-NETZ LINK BEREIT %s %s\n' "$IFACE" "$ADR"
  while [ -z "$STEUERUNG" ] || [ ! -e "$STEUERUNG/stopp" ]; do sleep 0.1; done
  exit 0
fi
# Laufzeit-Firewallregel fuer den bekannten TFTP-Client.
if command -v iptables >/dev/null 2>&1; then
  sudo -n iptables -I INPUT 1 -i "$IFACE" -s "$KLIENT" -d "$IP" -p udp -j ACCEPT
  fw_regel=1
fi
echo "TFTP-Server: $IP:69 auf $IFACE, Wurzel $TFTP (Strg+C beendet und entfernt die Adresse)"
befehl=(sudo -n dnsmasq --no-daemon --conf-file=/dev/null --port=0 --pid-file=
  --interface="$IFACE" --except-interface=lo --bind-interfaces --listen-address="$IP"
  --enable-tftp --tftp-root="$TFTP" --user="$(id -un)" --group="$(id -gn)"
  --log-facility=-)
if [ -z "$STEUERUNG" ]; then
  "${befehl[@]}"
else
  "${befehl[@]}" &
  dns_pid=$!
  while [ ! -e "$STEUERUNG/stopp" ] && kill -0 "$dns_pid" 2>/dev/null; do sleep 0.1; done
  if [ -e "$STEUERUNG/stopp" ]; then exit 0; fi
  wait "$dns_pid"
  dns_pid=
fi