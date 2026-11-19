#!/usr/bin/env bash
# OpenSSH-Probe fuer die private QEMU- oder N100-Direktverbindung.
set -euo pipefail
ziel="$1"
quelle="$2"
kernlog="$3"
ausgabe="$4"
mkdir -p -- "$ausgabe"
for testmarker in ROLLE CREDENTIAL TAG; do
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
LC_ALL=C timeout --foreground 22s ssh -vvv -F /dev/null -T -p 2223 -b "$quelle" -l moo-probe \
  -o BatchMode=yes -o ConnectTimeout=8 -o ConnectionAttempts=1 \
  -o StrictHostKeyChecking=yes -o GlobalKnownHostsFile=/dev/null \
  -o "UserKnownHostsFile=$ausgabe/ssh-known-hosts" \
  -o IdentityAgent=none -o PubkeyAuthentication=no -o PasswordAuthentication=no \
  -o KbdInteractiveAuthentication=no -o GSSAPIAuthentication=no \
  "$ziel" >"$ausgabe/ssh-kex.log" 2>&1 || status=$?
if ((status != 255)); then
  tail -35 "$ausgabe/ssh-kex.log"
  printf 'OpenSSH-Transportprobe endete mit Status %s\n' "$status" >&2
  exit 1
fi
for zeile in \
  'kex: algorithm: curve25519-sha256' \
  'kex: host key algorithm: ssh-ed25519' \
  'kex: server->client cipher: chacha20-poly1305@openssh.com MAC: <implicit> compression: none' \
  'kex: client->server cipher: chacha20-poly1305@openssh.com MAC: <implicit> compression: none' \
  'will use strict KEX ordering' \
  'is known and matches the ED25519 host key' \
  'SSH2_MSG_NEWKEYS sent' 'SSH2_MSG_NEWKEYS received' \
  'SSH2_MSG_SERVICE_ACCEPT received' \
  "Received disconnect from $ziel port 2223:11: Moo SSH transport complete"; do
  if ! grep -aF -- "$zeile" "$ausgabe/ssh-kex.log"; then
    tail -30 "$ausgabe/ssh-kex.log"
    printf 'OpenSSH-Nachweis fehlt für %s\n' "$zeile" >&2
    exit 1
  fi
done
if grep -aEq 'Authenticated to |send packet: type 90([^0-9]|$)' "$ausgabe/ssh-kex.log"; then
  echo 'SSH-Transportprobe erreichte einen Auth- oder Kanalzustand' >&2
  exit 1
fi
printf '%s\n' 'SSH/2223 Hostkey gepinnt, beide NEWKEYS und geschuetzte Service-Runde belegt'
