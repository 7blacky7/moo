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
terminal=(-n -T)
eingabe=/dev/null
ausgabe_datei="$ausgabe/ssh-ausgabe.tmp"
case "$modus" in
  kex) ;;
  info|fremd)
    marker+=(SIGNATUR METHODE)
    anmeldung=( -o PreferredAuthentications=publickey -o IdentitiesOnly=yes -i "${6:?Laufkey fehlt}" )
    konto=moo
    log="$ausgabe/ssh-info.log"
    kommando=(moo-info)
    ;;
  shell)
    marker+=(SIGNATUR METHODE)
    anmeldung=( -o PreferredAuthentications=publickey -o IdentitiesOnly=yes -i "${6:?Laufkey fehlt}" )
    konto=moo
    terminal=(-tt)
    log="$ausgabe/ssh-shell.log"
    eingabe="$ausgabe/ssh-shell.stdin"
    ausgabe_datei="$ausgabe/ssh-shell.txt"
    # Die lange, absichtlich ungueltige Zeile ueberschreitet den SSH-Empfangskredit.
    # Die letzten Befehle muessen nach einem Window Adjust weiter ankommen.
    {
      printf 'hilfx\177e\r\nmoo-info\npci\nnetz\nspeicher\nunbekannt\n'
      printf '%0700d\n' 0
      printf 'hilfxy\b\177e\n'
      printf 'gru\303\237e \303\244\303\266\303\274 \342\202\254\n'
      printf 'hilf\342\202\254\177\303\244\177e\nbeenden\n'
    } >"$eingabe"
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
LC_ALL=C TERM=vt100 timeout --foreground 22s ssh -vvv -F /dev/null "${terminal[@]}" -p 2223 -b "$quelle" -l "$konto" \
  -o BatchMode=yes -o ConnectTimeout=8 -o ConnectionAttempts=1 \
  -o StrictHostKeyChecking=yes -o GlobalKnownHostsFile=/dev/null \
  -o "UserKnownHostsFile=$ausgabe/ssh-known-hosts" \
  -o IdentityAgent=none -o KbdInteractiveAuthentication=no -o GSSAPIAuthentication=no \
  "${kex_optionen[@]}" "${anmeldung[@]}" "$ziel" "${kommando[@]}" <"$eingabe" >"$ausgabe_datei" 2>"$log" || status=$?
if [ "$modus" = info ]; then
  mv -- "$ausgabe_datei" "$ausgabe/moo-info.txt"
elif [ "$modus" != shell ]; then
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
  shell) suchzeilen+=('Authenticated to ' 'PTY allocation request accepted' 'shell request accepted' 'Exit status 0') ;;
  fremd) suchzeilen+=('Offering public key: ' 'Permission denied (publickey)') ;;
  passwort) suchzeilen+=('Authentications that can continue: publickey' 'Permission denied (publickey)') ;;
esac
erwartet=255
case "$modus" in info|shell) erwartet=0 ;; esac
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
elif [ "$modus" = shell ]; then
  uv run --no-project --offline python - "$ausgabe_datei" "$ziel" <<'PY'
import pathlib
import re
import sys

daten = pathlib.Path(sys.argv[1]).read_bytes()
daten.decode("utf-8")
zeilen = daten.split(b"\r\n")
hilfe = b"hilfe moo-info pci netz speicher beenden"
pflicht = (
    b"moo> hilfx\b \be",
    b"moo> moo-info",
    b"kernel=MoOS Bare 0.1",
    b"moo> pci",
    b"moo> netz",
    b"ipv4=" + sys.argv[2].encode("ascii"),
    b"moo> speicher",
    b"moo> unbekannt",
    b"Unbekannter Befehl",
    b"moo> " + b"0" * 96,
    b"Zeile zu lang",
    b"moo> hilfxy\b \b\b \be",
    "moo> gruße äöü €".encode(),
    b"Unbekannter Befehl",
    "moo> hilf€\b \bä\b \be".encode(),
)
if zeilen[:1] != [b"MoOS Bare SSH"] or zeilen[-3:] != [b"moo> beenden", b"MoOS Shell beendet", b""]:
    sys.exit("SSH-Shell: Start oder sauberer Abschluss fehlt")
position = 0
for zeile in pflicht:
    try:
        position = zeilen.index(zeile, position) + 1
    except ValueError:
        sys.exit(f"SSH-Shell: erwartete Ausgabe fehlt ({zeile!r})")
if zeilen.count(hilfe) != 3:
    sys.exit("SSH-Shell: Hilfe nach korrigierten ASCII-/UTF-8-Eingaben fehlt")
print("SSH/2223 UTF-8: äöüß€ bytegleich, Backspace entfernt ganze Codepunkte")

def zahl(name):
    werte = [z[len(name):] for z in zeilen if z.startswith(name)]
    if len(werte) != 1 or not werte[0].isdigit():
        sys.exit(f"SSH-Shell: Zahlenwert fehlt ({name!r})")
    return int(werte[0])

bdf = rb"[0-9A-F]{2}:[0-9A-F]{2}\.[0-7]"
pci = [z for z in zeilen if re.fullmatch(rb"pci " + bdf + rb" [0-9A-F]{4}:[0-9A-F]{4}", z)]
anzahl = zahl(b"pci_count=")
if not anzahl or len(pci) != min(anzahl, 64) or len(pci) != zahl(b"pci_listed="):
    sys.exit("SSH-Shell: PCI-Bestand ungueltig")
mac = rb"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}"
raten = [re.fullmatch(rb"netz " + bdf + rb" mac=" + mac + rb" link_mbps=(0|10|100|1000|2500)", z) for z in zeilen]
raten = [int(m[1]) for m in raten if m]
if len(raten) != zahl(b"netz_ports=") or not any(raten):
    sys.exit("SSH-Shell: Netzport-Bestand oder aktiver Link fehlt")
frei = zahl(b"free_bytes=")
seiten = zahl(b"free_pages=")
if not zahl(b"uptime_ms=") or not frei or frei % 4096 or not 0 < seiten * 4096 <= zahl(b"pfa_limit_bytes="):
    sys.exit("SSH-Shell: PFA-Werte oder Uptime ungueltig")
PY
  printf '%s\n' 'SSH/2223 PTY, Shell, Echo/Backspace, Fensterkredit, Befehle und Exit 0 belegt'
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
