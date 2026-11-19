#!/usr/bin/env bash
# OpenSSH-Probe fuer die private QEMU- oder N100-Direktverbindung.
set -euo pipefail
ziel="$1"
quelle="$2"
kernlog="$3"
ausgabe="$4"
modus="${5:-kex}"
mkdir -p -- "$ausgabe"

marker=(ROLLE CREDENTIAL TAG)
kex_optionen=()
kex_erwartet=
case "${7:-auto}" in
  auto) ;;
  hybrid)
    kex_erwartet=mlkem768x25519-sha256
    marker+=(MLKEM)
    ;;
  klassisch)
    kex_erwartet=curve25519-sha256
    kex_optionen=( -o "KexAlgorithms=$kex_erwartet" )
    ;;
  *) echo 'Unbekannte KEX-Probe' >&2; exit 2 ;;
esac
anmeldung=( -o PubkeyAuthentication=no -o PasswordAuthentication=no )
konto=moo-probe
log="$ausgabe/ssh-kex.log"
kommando=()
case "$modus" in
  kex) ;;
  info|fremd)
    marker+=(SIGNATUR METHODE)
    anmeldung=( -o PreferredAuthentications=publickey -o IdentitiesOnly=yes -i "${6:?Laufkey fehlt}" )
    konto=moo
    log="$ausgabe/ssh-info.log"
    kommando=(moo-info)
    ;;
  passwort)
    marker+=(SIGNATUR METHODE)
    anmeldung=( -o PreferredAuthentications=password -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=0 )
    konto=moo
    log="$ausgabe/ssh-info.log"
    kommando=(moo-info)
    ;;
  *) echo "Unbekannte SSH-Probe $modus" >&2; exit 2 ;;
esac
for testmarker in "${marker[@]}"; do
  grep -aFq "MOO-KERNEL NETZ SSH TEST $testmarker 0000000000000001" "$kernlog" || {
    printf 'SSH-Sicherheitsprobe fehlt fuer %s\n' "$testmarker" >&2
    exit 1
  }
done
uv run --no-project --offline python - "$kernlog" "$ziel" "$ausgabe/ssh-known-hosts" <<'PY'
import base64
import pathlib
import re
import struct
import sys

log, ziel, known = sys.argv[1:]
text = pathlib.Path(log).read_bytes()
muster = rb"MOO-KERNEL NETZ SSH HOSTKEY-([0-3]) ([0-9A-F]{16})(?![0-9A-F])"
felder = re.findall(muster, text)
if [int(i) for i, _ in felder] != list(range(4)):
    sys.exit("Vier geordnete Hostkey-Felder des laufenden Kerns fehlen")
pub = bytes.fromhex(b"".join(wert for _, wert in felder).decode("ascii"))
alg = b"ssh-ed25519"
blob = struct.pack("!I", len(alg)) + alg + struct.pack("!I", len(pub)) + pub
pin = base64.b64encode(blob).decode("ascii")
pathlib.Path(known).write_text(f"[{ziel}]:2223 ssh-ed25519 {pin}\n", encoding="ascii")
PY

status=0
LC_ALL=C timeout --foreground 22s ssh -vvv -F /dev/null -n -T -p 2223 -b "$quelle" -l "$konto" \
  -o BatchMode=yes -o ConnectTimeout=8 -o ConnectionAttempts=1 \
  -o StrictHostKeyChecking=yes -o GlobalKnownHostsFile=/dev/null \
  -o "UserKnownHostsFile=$ausgabe/ssh-known-hosts" \
  -o IdentityAgent=none -o KbdInteractiveAuthentication=no -o GSSAPIAuthentication=no \
  "${kex_optionen[@]}" "${anmeldung[@]}" "$ziel" "${kommando[@]}" >"$ausgabe/ssh-ausgabe.tmp" 2>"$log" || status=$?
if [ "$modus" = info ]; then
  mv -- "$ausgabe/ssh-ausgabe.tmp" "$ausgabe/moo-info.txt"
else
  if [ -s "$ausgabe/ssh-ausgabe.tmp" ]; then
    echo 'Unerwartete Ausgabe ohne SSH-Anmeldung' >&2
    exit 1
  fi
  rm -- "$ausgabe/ssh-ausgabe.tmp"
fi

kex="$(tr -d '\r' <"$log" | sed -n 's/^debug1: kex: algorithm: //p')"
case "$kex" in
  mlkem768x25519-sha256|curve25519-sha256) ;;
  *) echo 'Erwartetes SSH-KEX-Verfahren fehlt' >&2; exit 1 ;;
esac
if [ -n "$kex_erwartet" ] && [ "$kex" != "$kex_erwartet" ]; then
  printf 'SSH-KEX %s statt %s\n' "$kex" "$kex_erwartet" >&2
  exit 1
fi
printf 'SSH/2223 KEX %s\n' "$kex"

suchzeilen=(
  'kex: host key algorithm: ssh-ed25519'
  'kex: server->client cipher: chacha20-poly1305@openssh.com MAC: <implicit> compression: none'
  'kex: client->server cipher: chacha20-poly1305@openssh.com MAC: <implicit> compression: none'
  'will use strict KEX ordering'
  'is known and matches the ED25519 host key'
  'SSH2_MSG_NEWKEYS sent' 'SSH2_MSG_NEWKEYS received'
  'SSH2_MSG_SERVICE_ACCEPT received'
)
case "$modus" in
  kex) suchzeilen+=("Received disconnect from $ziel port 2223:11: Moo SSH transport complete") ;;
  info) suchzeilen+=('Authenticated to ' 'Sending command: moo-info' 'Exit status 0') ;;
  fremd) suchzeilen+=('Offering public key: ' 'Permission denied (publickey)') ;;
  passwort) suchzeilen+=('Authentications that can continue: publickey' 'Permission denied (publickey)') ;;
esac
erwartet=255
[ "$modus" != info ] || erwartet=0
if ((status != erwartet)); then
  tail -35 "$log"
  printf 'OpenSSH-Probe %s endete mit Status %s statt %s\n' "$modus" "$status" "$erwartet" >&2
  exit 1
fi
for zeile in "${suchzeilen[@]}"; do
  if ! grep -aF -- "$zeile" "$log"; then
    tail -30 "$log"
    printf 'OpenSSH-Nachweis fehlt für %s\n' "$zeile" >&2
    exit 1
  fi
done
if [ "$modus" = info ]; then
  uv run --no-project --offline python - "$ausgabe/moo-info.txt" <<'PY'
import pathlib
import re
import sys

zeilen = pathlib.Path(sys.argv[1]).read_text().splitlines()
if len(zeilen) != 3 or zeilen[0] != "kernel=MoOS Bare 0.1":
    sys.exit("Moo-Info-Kernversion fehlt")
uptime = re.fullmatch(r"uptime_ms=([0-9]+)", zeilen[1])
frei = re.fullmatch(r"free_bytes=([0-9]+)", zeilen[2])
if not uptime or int(uptime[1]) <= 0 or not frei or int(frei[1]) <= 0 or int(frei[1]) % 4096:
    sys.exit("Moo-Info-Uptime oder freier Seitenspeicher ungültig")
print("\n".join(zeilen))
PY
  printf '%s\n' 'SSH/2223 Ed25519-Userauth, Session, moo-info und Exit 0 belegt'
else
  if grep -aEq 'Authenticated to |send packet: type 90([^0-9]|$)' "$log"; then
    echo 'SSH-Probe erreichte einen unerwarteten Auth- oder Kanalzustand' >&2
    exit 1
  fi
  case "$modus" in
    kex) printf '%s\n' 'SSH/2223 Hostkey gepinnt, beide NEWKEYS und geschuetzte Service-Runde belegt' ;;
    fremd) printf '%s\n' 'SSH/2223 fremden Ed25519-Key abgewiesen' ;;
    passwort) printf '%s\n' 'SSH/2223 Passwortauswahl auf publickey begrenzt' ;;
  esac
fi
