#!/usr/bin/env bash
# P7-T25. Echte Laeufe ausschliesslich durch den Koordinator.
# --trocken aendert keinen Laufzustand. Mit --ping werden die lokalen Vertraege im Speicher geprueft.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EFI="$ROOT/build/p7-t25/stick/tftp/moo.efi"
KERN="$ROOT/build/p7-t25/stick/tftp/moo.kern"
RUECKFALL="$ROOT/build/p7-t25/rueckkehr/RUECKFALL.ELF"
RUECKFALL_SHA=
IFACE=enp60s0
SEKUNDEN=90
ZEITLIMIT=600
SYNAPSE_LIMIT=600
USB_SERIE=
PARTITION=
STICK_SHA=
TROCKEN=0
KOORDINATOR=0
DIAGNOSE=0
INVENTAR=0
MODUS=1
WATCHDOG_S=10
PING_IP=
TRANSPORT=0
KRYPTO=0
SSH=0
SSH_KEY_VERZ=
SSH_KEY=
KONFIG_QUELLE=
KONFIG_ZIEL=
KONFIG_LOADER_QUELLE=
KONFIG_LOADER_ZIEL=
RESTORE=
STOPP_SK="${MOO_N100_STOPP_SKRIPT:-$ROOT/build/koord/sanft_aus.ps1}"
NOTIZ="${MOO_N100_NOTIZ:-$ROOT/build/notizen/synapse-offline.md}"
while (($#)); do
  case "$1" in
    --trocken) TROCKEN=1; shift;;
    --inventar) INVENTAR=1; shift;;
    --koordinator) KOORDINATOR=1; shift;;
    --diagnose) DIAGNOSE=1; MODUS=0; SEKUNDEN=20; shift;;
    --it8613-messreihe) DIAGNOSE=0; MODUS=6; SEKUNDEN=70; shift;;
    --watchdog-test) DIAGNOSE=0; MODUS=7; SEKUNDEN=45; shift;;
    --ping) PING_IP=192.168.77.2; shift;;
    --udp-tcp) PING_IP=192.168.77.2; TRANSPORT=$((TRANSPORT | 1)); shift;;
    --tcp-echo) PING_IP=192.168.77.2; TRANSPORT=3; shift;;
    --krypto-selbsttest) KRYPTO=1; shift;;
    --ssh-kex) PING_IP=192.168.77.2; TRANSPORT=$((TRANSPORT | 1)); SSH=1; shift;;
    --ssh-info) PING_IP=192.168.77.2; TRANSPORT=$((TRANSPORT | 1)); SSH=2; shift;;
    --modus) MODUS="$2"; shift 2;;
    --watchdog-sekunden) WATCHDOG_S="$2"; shift 2;;
    --efi) EFI="$2"; shift 2;;
    --kern) KERN="$2"; shift 2;;
    --rueckfall-kern) RUECKFALL="$2"; shift 2;;
    --interface) IFACE="$2"; shift 2;;
    --sekunden) SEKUNDEN="$2"; shift 2;;
    --zeitlimit) ZEITLIMIT="$2"; shift 2;;
    --synapse-zeitlimit) SYNAPSE_LIMIT="$2"; shift 2;;
    --usb-serie) USB_SERIE="$2"; shift 2;;
    --partition) PARTITION="$2"; shift 2;;
    --stick-sha256) STICK_SHA="$2"; shift 2;;
    --kern-konfigurieren) KONFIG_QUELLE="$2"; KONFIG_ZIEL="$3"; shift 3;;
    --loader-konfigurieren) KONFIG_LOADER_QUELLE="$2"; KONFIG_LOADER_ZIEL="$3"; shift 3;;
    --aufraeumen) RESTORE="$2"; shift 2;;
    --hilfe) printf '%s\n' 'n100-lauf.sh --trocken | --koordinator --diagnose --efi DATEI --kern DATEI --usb-serie SERIE --partition {GUID} --stick-sha256 SHA256' \
      '--rueckfall-kern ELF: IT8613, exakt 20s/10s/Modus 3; SHA muss EFI/MOO/KERN.ELF auf dem Stick entsprechen.' \
      '--synapse-zeitlimit N: eigenes Startlimit nach Windows-Rueckkehr (Standard600s).' \
      '--inventar: Helfer hochladen und nur USB-/Firmware-/Waechter-Inventar lesen; kein Boot.' \
      'Normaler Lauf: 90s Kern mit 10s IT8613-Safety. --watchdog-test: 10s IT8613, 45s Softwarefrist.' \
      '--ping: Im regulaeren Kernlauf IPv4 aktivieren, nach Kernelmarker ARP pruefen und RTT in ping.log messen.' \
      '--udp-tcp: ARP/Ping plus UDP-Echo an Port 7 und HTTP an TCP-Port 8080 pruefen.' \
      '--tcp-echo: Zusaetzlich zwei TCP-Echo-Verbindungen an Port 2222 mit 1 MiB und 64 KiB pruefen.' \
      '--ssh-kex: OpenSSH auf TCP 2223 bis NEWKEYS und geschuetzter Service-Runde pruefen.' \
      '--ssh-info: Ed25519-Laufkey erzeugen und moo-info auf TCP 2223 pruefen.' \
      '--krypto-selbsttest: KATs und CPU-Entropie im Sammelkern pruefen.' \
      '--trocken mit Netz- oder Kryptooption: Gemeinsame lokale ELF-/PE-Vorbereitung im Speicher.' \
      '--diagnose: TCO und IT8613 lesen, dann Software-Reset.' \
      '--modus 5 --sekunden 25 --watchdog-sekunden 10: IT8613 bedienen und vor Software-Reset abschalten.' \
      '--it8613-messreihe: IT8613 lesend bei 0, 2 und 70 s, dann Software-Reset.' \
      '--modus 7 --sekunden 45 --watchdog-sekunden 10: IT8613-Safety ohne Bedienung, Software-Rueckfall nach 45 s.' \
      '--loader-konfigurieren QUELLE ZIEL: sichere Rueckkehr nur in privater PE-Kopie aktivieren.' \
      '--kern-konfigurieren QUELLE ZIEL [--sekunden N]: nur private ELF-Kopie konfigurieren.' \
      '--aufraeumen build/n100-laeufe/ZEIT --koordinator: Wiederherstellung erneut versuchen.'; exit 0;;
    *) printf 'Unbekanntes Argument: %s\n' "$1" >&2; exit 2;;
  esac
done
[[ "$SYNAPSE_LIMIT" =~ ^[0-9]+$ && "$SEKUNDEN" =~ ^[0-9]+$ && "$ZEITLIMIT" =~ ^[0-9]+$ && "$WATCHDOG_S" =~ ^[0-9]+$ && "$MODUS" =~ ^(0|1|2|3|4|5|6|7|100|101|103|104)$ ]] || { echo 'Zeiten muessen ganze Sekunden sein' >&2; exit 2; }
((SYNAPSE_LIMIT>=5 && SYNAPSE_LIMIT<=1800 && SEKUNDEN>=1 && SEKUNDEN<=600 && WATCHDOG_S>=6 && WATCHDOG_S<=600 && ZEITLIMIT>=30 && ZEITLIMIT<=1800)) || { echo 'Ungueltiges Zeitlimit' >&2; exit 2; }
if ((MODUS==2 || MODUS==4)); then
  echo 'TCO am N100 ist eine Lesediagnose (--diagnose). Fuer den Hardware-Reset --watchdog-test verwenden.' >&2
  exit 2
fi
if ((MODUS==104 && SEKUNDEN<WATCHDOG_S+5)); then
  echo 'ICH9-TCO-Beobachtung braucht mindestens Watchdog-Dauer plus 5 Sekunden' >&2
  exit 2
fi
if (((MODUS==1 || MODUS==3 || MODUS==5 || MODUS==7) && WATCHDOG_S>30)); then
  echo 'IT8613 braucht 6–30 Watchdog-Sekunden' >&2
  exit 2
fi
if (((MODUS==5 || MODUS==7) && (SEKUNDEN<=WATCHDOG_S || SEKUNDEN>60))); then
  echo 'IT8613 braucht einen laengeren Kurzlauf bis 60 Sekunden' >&2
  exit 2
fi
if ((MODUS==7 && SEKUNDEN<WATCHDOG_S+5)); then
  echo 'IT8613-Safety braucht mindestens Watchdog-Dauer plus 5 Sekunden' >&2
  exit 2
fi
if ((MODUS==6 && SEKUNDEN!=70)); then
  echo 'IT8613-Messreihe braucht 70 Sekunden' >&2
  exit 2
fi
[[ "$IFACE" == enp60s0 ]] || { echo 'Nur das belegte Direktkabel enp60s0 ist erlaubt' >&2; exit 2; }
[[ -z "$PING_IP" || "$MODUS" == 1 ]] || { echo 'Netzoptionen brauchen den regulaeren Kernlauf (Modus 1)' >&2; exit 2; }

((KRYPTO==0 || MODUS==1)) || { echo '--krypto-selbsttest braucht den regulaeren Sammellauf (Modus 1)' >&2; exit 2; }

((SSH==0)) || command -v ssh >/dev/null || { echo 'SSH-Probe braucht OpenSSH auf dem Linux-PC' >&2; exit 2; }
((SSH!=2)) || command -v ssh-keygen >/dev/null || { echo '--ssh-info braucht ssh-keygen auf dem Linux-PC' >&2; exit 2; }

kern_kopie() {
  uv run --no-project --offline python -B - "$1" "$2" "$3" "$4" "$5" "${6:-}" "${7:-0}" "${8:-0}" "${9:-0}" "${10:-0}" "$ROOT/skripte/p7" "${11:-}" <<'PY'
import ipaddress, pathlib, struct, sys
src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2]) if sys.argv[2] else None
seconds, watchdog, mode = map(int, sys.argv[3:6])
ping_ip = sys.argv[6]
unveraendert = sys.argv[7] == "1"
transport = int(sys.argv[8])
krypto = sys.argv[9] == "1"
ssh_modus = int(sys.argv[10])
if dst is not None and src.resolve() == dst.resolve():
    raise SystemExit("Quell-Kern darf nicht ueberschrieben werden")
original = src.read_bytes()
data = bytearray(original)
if data[:7] != b"\x7fELF\x02\x01\x01" or struct.unpack_from("<HH", data, 16) != (2, 62):
    raise SystemExit("Kein ELF64 x86-64 EXEC")
magic = struct.pack("<Q", 0x353254504F4F4D21)
off = struct.unpack_from("<Q", data, 32)[0]
size, count = struct.unpack_from("<HH", data, 54)
bereiche = []
for i in range(count):
    p = off + i * size
    typ, flags, start = struct.unpack_from("<IIQ", data, p)
    length = struct.unpack_from("<Q", data, p + 32)[0]
    if typ == 1 and flags & 2:
        bereiche.append((start, start + length))

def vertrag(suche, groesse, name):
    hits = []
    for start, ende in bereiche:
        pos = data.find(suche, start, ende)
        while pos >= 0:
            if pos + groesse <= ende:
                hits.append(pos)
            pos = data.find(suche, pos + 1, ende)
    if len(hits) != 1:
        raise SystemExit(f"{src}: {name} nicht eindeutig in schreibbarem PT_LOAD (Treffer: {len(hits)})")
    return hits[0]

testvertrag = vertrag(magic, 32, "Testvertrag")
# 0: Lesen. 1: regulaerer Kernlauf mit IT8613. 3: IT8613-Rueckfall ohne USB.
# 5: kurzer IT8613-Bedienlauf mit Abschalten vor Software-Reset.
# 6: IT8613 lesend in einem Boot bei 0, 2 und 70 s. 7: IT8613-Safety.
# 100/101/103/104: ausschliesslich ICH9-QEMU.
struct.pack_into("<QQQ", data, testvertrag + 8, seconds, watchdog, mode)
if ping_ip:
    leer = struct.pack("<3Q", 0x00345650494F4F4D, 0, 0)
    pos = vertrag(leer, len(leer), "Deaktivierter IPv4-Vertrag")
    struct.pack_into("<Q", data, pos + 8, int(ipaddress.IPv4Address(ping_ip)))
if transport & 1:
    leer = struct.pack("<2Q", 0x474643344C4F4F4D, 0)
    pos = vertrag(leer, len(leer), "Deaktivierter UDP-/TCP-Vertrag")
    struct.pack_into("<Q", data, pos + 8, 1)
if transport & 2:
    leer = struct.pack("<2Q", 0x004F4843454F4F4D, 0)
    pos = vertrag(leer, len(leer), "Deaktivierter TCP-Echo-Vertrag")
    struct.pack_into("<Q", data, pos + 8, 1)
if krypto:
    sammel = struct.pack("<2Q", 0x4C4D4D41534F4F4D, 1)
    vertrag(sammel, 24, "Aktiver Sammelvertrag")
    leer = struct.pack("<2Q", 0x315059524B4F4F4D, 0)
    pos = vertrag(leer, len(leer), "Deaktivierter Krypto-Vertrag")
    struct.pack_into("<Q", data, pos + 8, 1)
if ssh_modus:
    leer = struct.pack("<2Q", 0x31304853534F4F4D, 0)
    pos = vertrag(leer, len(leer), "Deaktivierter SSH-Vertrag")
    struct.pack_into("<Q", data, pos + 8, 1)
if ssh_modus == 2:
    sys.path.insert(0, sys.argv[11])
    from ssh_laufprofil import auth_konfigurieren

    public_key = sys.argv[12] or None
    if dst is not None and public_key is None:
        raise SystemExit("SSH-Info-Kopie braucht einen erzeugten Laufkey")
    auth_konfigurieren(data, public_key)
if unveraendert and data != original:
    print("IT8613-Rueckfallkern muss exakt 20s/10s/Modus 3 haben", file=sys.stderr)
    sys.exit(2)
if dst is None:
    print(f"Kern-Vorbereitung {src}: {seconds}s, Watchdog {watchdog}s, Modus {mode}")
else:
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    print(f"Kern-Kopie {dst}: {seconds}s, Watchdog {watchdog}s, Modus {mode}")
if ping_ip:
    print(f"IPv4 der Kern-Kopie: {ping_ip}")
if transport & 1:
    print("UDP 7 und TCP 8080 der Kern-Kopie aktiviert")
if transport & 2:
    print("TCP-Echo 2222 der Kern-Kopie aktiviert")
if krypto:
    print("Krypto-Selbsttest der Kern-Kopie aktiviert")
if ssh_modus:
    print("SSH-Transport 2223 der Kern-Kopie aktiviert")
if ssh_modus == 2:
    print("SSH-Auth-Vertrag im schreibbaren PT_LOAD geprüft")
    if public_key is not None:
        print("Öffentlicher Ed25519-Laufkey in der Kern-Kopie hinterlegt")
PY
}
loader_kopie() {
  uv run --no-project --offline python - "$1" "$2" "${3:-0}" <<'PY'
import pathlib, struct, sys
src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2]) if sys.argv[2] else None
unveraendert = sys.argv[3] == "1"
if dst is not None and src.resolve() == dst.resolve():
    raise SystemExit("Quell-Loader darf nicht ueberschrieben werden")
original = src.read_bytes()
data = bytearray(original)
if data[:2] != b"MZ" or len(data) < 64:
    raise SystemExit("Kein PE-Loader")
pe = struct.unpack_from("<I", data, 60)[0]
if pe + 24 > len(data) or data[pe:pe+4] != b"PE\0\0":
    raise SystemExit("Kein PE-Kopf")
machine, count = struct.unpack_from("<HH", data, pe+4)
optlen = struct.unpack_from("<H", data, pe+20)[0]
opt = pe + 24
if machine != 0x8664 or optlen < 152 or opt + optlen > len(data):
    raise SystemExit("Kein PE64 x86-64")
if struct.unpack_from("<H", data, opt)[0] != 0x20b or struct.unpack_from("<H", data, opt+68)[0] != 10:
    raise SystemExit("Kein EFI-Application PE32+")
if struct.unpack_from("<I", data, opt+64)[0] or any(data[opt+144:opt+152]):
    raise SystemExit("Nur eigener unsignierter Loader ohne PE-Pruefsumme")
magic = struct.pack("<Q", 0x353254524F4F4D21)
hits = []
for i in range(count):
    h = opt + optlen + i*40
    if h+40 > len(data):
        raise SystemExit("Abgeschnittener Abschnittskopf")
    length, start = struct.unpack_from("<II", data, h+16)
    flags = struct.unpack_from("<I", data, h+36)[0]
    if start + length > len(data):
        raise SystemExit("Abgeschnittener Abschnitt")
    if not flags & 0x80000000:
        continue
    pos = data.find(magic, start, start+length)
    while pos >= 0:
        if pos+16 <= start+length:
            hits.append(pos)
        pos = data.find(magic, pos+1, start+length)
if len(hits) != 1:
    raise SystemExit("Rueckkehrvertrag nicht eindeutig im schreibbaren PE-Abschnitt")
if struct.unpack_from("<Q", data, hits[0]+8)[0] not in (0, 1):
    raise SystemExit("Unbekannte Rueckkehrkonfiguration")
struct.pack_into("<Q", data, hits[0]+8, 1)
if unveraendert and data != original:
    print("Testloader muss sichere Windows-BootNext-Rueckkehr aktiv haben (--loader-konfigurieren)", file=sys.stderr)
    sys.exit(2)
if dst is None:
    print(f"Loader-Vorbereitung {src}: Windows-BootNext vor Kernstart, BootOrder unveraendert")
else:
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    print(f"Loader-Kopie {dst}: Windows-BootNext vor Kernstart, BootOrder unveraendert")
PY
}

lauf_vorbereiten() {
  local ziel="$1"
  [[ -f "$RUECKFALL" ]] || { echo 'Befristeter lokaler Rueckfallkern ist Pflicht (--rueckfall-kern)' >&2; exit 2; }
  kern_kopie "$RUECKFALL" "${ziel:+$ziel/rueckfall-geprueft.elf}" 20 10 3 "" 1
  RUECKFALL_SHA="$(sha256sum "$RUECKFALL" | cut -d ' ' -f1)"
  ((INVENTAR==0)) || return 0
  loader_kopie "$EFI" "${ziel:+$ziel/loader-geprueft.efi}" 1
  [[ -f "$EFI" && -f "$KERN" && -s "$STOPP_SK" ]] || { echo 'Loader/Kern oder vorhandenes sanft_aus.ps1 nicht gefunden' >&2; exit 2; }
  [[ "$USB_SERIE" =~ ^[a-zA-Z0-9_-]+$ && "$PARTITION" =~ ^\{[0-9a-fA-F-]{36}\}$ && "$STICK_SHA" =~ ^[0-9a-fA-F]{64}$ ]] || { echo 'Belegte Stick-Identitaet ist Pflicht' >&2; exit 2; }
  kern_kopie "$KERN" "${ziel:+$ziel/moo.kern}" "$SEKUNDEN" "$WATCHDOG_S" "$MODUS" "$PING_IP" 0 "$TRANSPORT" "$KRYPTO" "$SSH" "${SSH_KEY:+$SSH_KEY.pub}"
  if [[ -n "$ziel" ]]; then
    sha256sum "$EFI" "$KERN" "$ziel/moo.kern" >"$ziel/sha256.txt"
  fi
}

if ((TROCKEN)); then
  printf '%s\n' 'TROCKEN: keine WinRM-/sudo-/Netzwerkbefehle, keine Laufdateien.'
  if [[ -n "$PING_IP" ]] || ((KRYPTO)); then
    lauf_vorbereiten ""
  fi
  printf '1. USB-ESP per Seriennummer+GPT-GUID+NT-Geraetepfad+SHA256 belegen: %s %s %s\n' "$USB_SERIE" "$PARTITION" "$STICK_SHA"
  printf 'Lokalen IT8613-Rueckfallkern (20s,10s,Modus3) und ESP-SHA pruefen: %s\n' "$RUECKFALL"
  if ((MODUS==6)); then
    printf '%s\n' '2. Private Kern-Kopie: Modus 6, lesende IT8613-Messreihe bei 0, 2 und 70 s.'
  else
    printf '2. Private Kern-Kopie: %ss, Watchdog %ss; Modus=%s (0=Lesen,1=IT8613-Kernlauf,5=IT8613-Bedienlauf,7=IT8613-Safety)\n' "$SEKUNDEN" "$WATCHDOG_S" "$MODUS"
  fi
  printf '3. Bestehenden netz-server starten: %s %s; tcpdump auf %s\n' "$EFI" "$KERN" "$IFACE"
  if [[ -n "$PING_IP" ]]; then
    printf '   Nach Kernelmarker ARP/Ping an %s auf %s; arp.log, ping.log (RTT) und ICMP in netz.pcap.\n' "$PING_IP" "$IFACE"
  fi
  if ((TRANSPORT)); then
    printf '%s\n' '   UDP-Echo und HTTP vom Linux-PC, Protokolle udp.log, tcp.log, netz.pcap.'
  fi
  if ((TRANSPORT & 2)); then
    printf '%s\n' '   Zwei TCP-Echo-Streams vom Linux-PC, Protokoll tcp-echo.log und Port 2222 in netz.pcap.'
  fi
  if ((KRYPTO)); then
    printf '%s\n' '   Krypto- und Entropie-Status im Netzlog, Nachweis in krypto.log.'
  fi
  if ((SSH==2)); then
    printf '%s\n' '   Neuen Ed25519-Laufkey erzeugen, OpenSSH moo-info pruefen und Laufkey loeschen.'
  elif ((SSH)); then
    printf '%s\n' '   OpenSSH auf TCP 2223 mit gepinntem Boot-Hostkey, Protokoll ssh-kex.log.'
  fi
  printf '%s\n' '4. Synapse-Waechter-Zustand sichern, pausieren; build/koord/sanft_aus.ps1 per WinRM.'
  printf '%s\n' '5. Genau einen USB-Eintrag als {fwbootmgr} bootsequence setzen, danach shutdown /r /t 5.'
  if ((MODUS==1 || MODUS==5)); then
    printf '6. IT8613 VOR START AUS, CTRL-Readback, SCHARF, AUS, NEUSTART und neue Windows-Bootzeit (Limit %ss).\n' "$ZEITLIMIT"
  elif ((MODUS==6)); then
    printf '6. IT8613 MESSREIHE FERTIG, NEUSTART und neue Windows-Bootzeit abwarten (Limit %ss).\n' "$ZEITLIMIT"
  elif ((MODUS==7)); then
    printf '6. IT8613-Messproben bis Netzlog-Abriss und neue Windows-Bootzeit vor Softwarefrist (Limit %ss).\n' "$ZEITLIMIT"
  else
    printf '6. Netz-Abschluss MOO-KERNEL NEUSTART und neue Windows-Bootzeit abwarten (Limit %ss).\n' "$ZEITLIMIT"
  fi
  printf '%s\n' '7. Eigene Bootsequence, Waechter, Synapse, tcpdump und Server aufraeumen; netz-server restauriert IP/NM/Firewall.'
  printf '%s\n' 'Ergebnisse: build/n100-laeufe/<UTC-Zeit>/; Offline-Notizen: build/notizen/synapse-offline.md.'
  if ((MODUS==1 || MODUS==5 || MODUS==7)); then
    printf '%s\n' 'IT8613-Vertrag: Port/Chip-ID belegen, Vorstopp und CTRL-Quittierung lesen, dann Scharfschaltung.'
  elif ((MODUS==6)); then
    printf '%s\n' 'IT8613-Messreihe: gleiche Register im selben Boot lesen, danach Software-Rueckkehr.'
  fi
  exit 0
fi
if [[ -n "$KONFIG_LOADER_QUELLE" ]]; then
  loader_kopie "$KONFIG_LOADER_QUELLE" "$KONFIG_LOADER_ZIEL"
  exit 0
fi
if [[ -n "$KONFIG_QUELLE" ]]; then
  kern_kopie "$KONFIG_QUELLE" "$KONFIG_ZIEL" "$SEKUNDEN" "$WATCHDOG_S" "$MODUS" "$PING_IP" 0 "$TRANSPORT" "$KRYPTO" "$SSH"
  exit 0
fi
((KOORDINATOR || INVENTAR)) || { echo 'Echte Laeufe/Wiederherstellung sind nur mit --koordinator erlaubt' >&2; exit 2; }
if [[ -z "$RESTORE" ]] && ((INVENTAR==0)); then
  ((MODUS<=1 || MODUS==5 || MODUS==6 || MODUS==7)) || { echo 'Modi 3 und 100+ sind nur fuer private Kernkopien vorgesehen' >&2; exit 2; }
  if ((MODUS==0)); then
    ((DIAGNOSE && SEKUNDEN<=30)) || { echo 'Lesediagnose ohne Watchdog ist auf 30s begrenzt' >&2; exit 2; }
  fi
fi
for tool in uv timeout setsid flock sudo tcpdump curl ss; do
  command -v "$tool" >/dev/null || { echo "Werkzeug nicht gefunden: $tool" >&2; exit 2; }
done
if [[ -n "$PING_IP" ]]; then
  for tool in arping ping; do
    command -v "$tool" >/dev/null || { echo "Werkzeug nicht gefunden: $tool" >&2; exit 2; }
  done
fi
OUT="${RESTORE:-${MOO_N100_AUSGABE:-$ROOT/build/n100-laeufe/$(date -u +%Y%m%dT%H%M%S)-${BASHPID}}}"
mkdir -p "$OUT" "$(dirname "$NOTIZ")" "$ROOT/build/n100-laeufe"
exec 9>"$ROOT/build/n100-laeufe/.lauf.lock"
flock -n 9 || { echo 'Ein N100-Lauf ist bereits aktiv' >&2; exit 2; }
SERVER_PID=
CAPTURE_PID=
REMOTE_ANGEFASST=0
SYNAPSE_STOPP=0
WAECHE_AN=true
EINTRAG=
json_wert() { uv run --no-project python -c 'import json,sys; v=json.load(open(sys.argv[1]))[sys.argv[2]]; print(str(v).lower() if isinstance(v,bool) else v)' "$OUT/vorher.json" "$1"; }
ps_quote() { local v="$1"; printf "'%s'" "${v//\'/\'\'}"; }
REMOTE_BEREIT=0
remote() {
  local aktion="$1"
  if ((REMOTE_BEREIT==0)); then
    # Gechunkter Upload mit SHA-Pruefung; kein langer -EncodedCommand-Inhalt.
    timeout --foreground 90s uv run --with pywinrm python -I "$ROOT/skripte/p2/minipc_winrm.py" upload \
      "$ROOT/skripte/p7/n100-boot.ps1" >"$OUT/upload.log" 2>>"$OUT/winrm.log" || return $?
    REMOTE_BEREIT=1
  fi
  # Scriptblock aus der Datei vermeidet auch eine dauerhafte ExecutionPolicy-Aenderung.
  timeout --foreground 50s uv run --with pywinrm python -I "$ROOT/skripte/p2/minipc_winrm.py" run \
    "& ([scriptblock]::Create([IO.File]::ReadAllText('D:\MooTest\n100-boot.ps1'))) -Aktion $(ps_quote "$aktion") -UsbSerial $(ps_quote "$USB_SERIE") -PartitionGuid $(ps_quote "$PARTITION") -StickSha256 $(ps_quote "$STICK_SHA") -RueckfallSha256 $(ps_quote "$RUECKFALL_SHA") -Eintrag $(ps_quote "$EINTRAG") -WaechterWarAn ([bool]::Parse('$WAECHE_AN')) -SynapseGestoppt ($SYNAPSE_STOPP -eq 1)"
}
notiz() { printf '\n[%s] N100 %s: %s [noch zu uebertragen]\n' "$(date -u +%FT%TZ)" "$OUT" "$*" >>"$NOTIZ"; }
beenden_gruppe() {
  local pid="$1"
  [[ -n "$pid" ]] || return 0
  kill -TERM -- "-$pid" 2>/dev/null || true
  for ((j=0;j<50;j++)); do
    kill -0 -- "-$pid" 2>/dev/null || break
    sleep 0.1
  done
  if kill -0 -- "-$pid" 2>/dev/null; then
    sudo -n kill -KILL -- "-$pid" 2>/dev/null || true
  fi
  wait "$pid" 2>/dev/null || true
}
synapse_warten() {
  local bis=$((SECONDS+SYNAPSE_LIMIT)) code
  while ((SECONDS<bis)); do
    code="$(curl -s -o /dev/null --max-time 5 -w '%{http_code}' https://synapse.moosynapse.org/ || true)"
    printf 'Synapse HTTP %s; verbleibendes Startlimit %ss\n' "$code" "$((bis-SECONDS))"
    [[ "$code" != 200 ]] || return 0
    # Auch 502 ist waehrend des Starts ein Wartezustand, bis das eigene Limit endet.
    sleep 5
  done
  return 1
}
ssh_laufkey_loeschen() {
  if [[ -n "$SSH_KEY_VERZ" ]]; then
    rm -f -- "$SSH_KEY_VERZ/id" "$SSH_KEY_VERZ/id.pub" && rmdir -- "$SSH_KEY_VERZ"
  fi
}
aufraeumen() {
  local rc=$? wieder=0 bis=$((SECONDS+120))
  trap - EXIT INT TERM HUP
  set +e
  if ! ssh_laufkey_loeschen; then
    echo 'FEHLER: Temporaerer SSH-Laufkey konnte nicht geloescht werden' >&2
    rc=1
  fi
  if ((REMOTE_ANGEFASST)); then
    while ((SECONDS<bis)); do
      if remote zurueck >"$OUT/wiederherstellung.json" 2>>"$OUT/winrm.log"; then wieder=1; break; fi
      sleep 5
    done
    if ((!wieder)); then
      notiz "Wiederherstellung ausstehend; KEIN weiterer Neustart. Koordinator: bash skripte/p7/n100-lauf.sh --koordinator --aufraeumen '$OUT'"
      echo "FEHLER: N100 nicht erreichbar/Wiederherstellung offen; erneut --aufraeumen '$OUT'. Nichts erzwingen." >&2
      rc=1
    fi
  fi
  beenden_gruppe "$CAPTURE_PID"
  beenden_gruppe "$SERVER_PID"
  if ((SYNAPSE_STOPP && wieder)); then
    if ! synapse_warten; then
      echo 'FEHLER: Windows zurueck, aber Synapse HTTP 200 nach eigenem Startlimit nicht belegt' >&2
      rc=1
    fi
  fi
  printf 'Exit=%s Wiederhergestellt=%s\n' "$rc" "$wieder" >"$OUT/ergebnis.txt"
  notiz "Exit=$rc; Register/Reset/Windows siehe netz.log und WinRM-Artefakte; HTTP-Rueckkehr siehe lauf.log"
  exit "$rc"
}
it8613_start_belegt() {
  local hex
  printf -v hex '%016X' "$WATCHDOG_S"
  grep -aFq 'MOO-KERNEL IT8613 VOR START AUS 0000000000000000' "$OUT/netz.log" &&
    grep -aEq 'MOO-KERNEL IT8613 SAFETY CTRL QUITTIERUNG READBACK 00000000000000[0-9A-F][02468ACE]' "$OUT/netz.log" &&
    grep -aFq "MOO-KERNEL IT8613 SCHARF SEKUNDEN $hex" "$OUT/netz.log" &&
    ! grep -aEq 'MOO-KERNEL IT8613 (SCHARFSCHALTUNG VERWEIGERT|BEDIENUNG FEHLER|ABSCHALTEN FEHLER)' "$OUT/netz.log"
}

it8613_safety_nachweis() {
  uv run --no-project python - "$OUT/netz.log" "$WATCHDOG_S" "$SEKUNDEN" "$1" "$2" <<'PY'
import re
import sys

pfad, *werte = sys.argv[1:]
watchdog, softwarefrist, antwort_ms, boot_alter_ms = map(int, werte)
paketzeit = re.compile(rb"^([0-9]+)\.([0-9]{6}) IP ")
meldung = re.compile(rb"MOO-KERNEL (IT8613 [A-Z0-9 _-]+?) ([A-F0-9]{16})(?![A-F0-9])")
probenmarken = {
    b"IT8613 SAFETY MESSZEIT MS", b"IT8613 PORT", b"IT8613 CHIP ID", b"IT8613 CHIP REV",
    b"IT8613 WDT CTRL", b"IT8613 WDT CFG", b"IT8613 WDT TIMEOUT LSB",
    b"IT8613 WDT TIMEOUT MSB", b"IT8613 REG 23H", b"IT8613 REG 2DH",
}
zeit_ms = None
begonnen = False
letzte_marke = None
proben = []
with open(pfad, "rb") as log:
    for zeile in log:
        paket = paketzeit.match(zeile)
        if paket:
            zeit_ms = int(paket[1]) * 1000 + int(paket[2]) // 1000
        if b"MOO-KERNEL " not in zeile:
            continue
        treffer = meldung.search(zeile)
        letzte_marke = treffer[1] if treffer else None
        if letzte_marke == b"IT8613 SAFETY OHNE BEDIENUNG":
            begonnen = zeit_ms is not None and int(treffer[2], 16) == watchdog
            proben.clear()
        if begonnen and letzte_marke == b"IT8613 SAFETY MESSZEIT MS":
            proben.append((int(treffer[2], 16), zeit_ms))

if letzte_marke not in probenmarken or len(proben) < 2:
    sys.exit("FEHLER: IT8613-Safety-Netzlog endet ohne belegte Messreihe.")
erste_ms, erste_paket_ms = proben[0]
letzte_ms, letzte_paket_ms = proben[-1]
if any(a[0] >= b[0] for a, b in zip(proben, proben[1:])):
    sys.exit("FEHLER: IT8613-Safety-Messzeiten sind nicht fortlaufend.")
# Eine Probe pro Sekunde plus Spielraum fuer eine verlorene Probe.
if not (watchdog - 2) * 1000 <= letzte_ms <= (watchdog + 2) * 1000 or letzte_ms >= softwarefrist * 1000:
    sys.exit(f"FEHLER: Netzlog-Abriss bei {letzte_ms} ms passt nicht zum IT8613-Ablauf.")

# Das Windows-Alter wird innerhalb des WinRM-Aufrufs gemessen. Empfangszeit
# minus Alter ist eine Obergrenze fuer den Windows-Start auf der Linux-Uhr.
# So kann ein verlorener Frist-Marker allein keinen HW-Reset vortaeuschen.
boot_spaetestens_ms = antwort_ms - boot_alter_ms
frist_ms = erste_paket_ms - erste_ms + softwarefrist * 1000
if not letzte_paket_ms <= boot_spaetestens_ms < frist_ms:
    sys.exit("FEHLER: Neue Windows-Bootzeit vor der IT8613-Softwarefrist nicht belegt.")
print(f"IT8613-Hardware-Reset belegt. Netzlog-Abriss nach {letzte_ms} ms, neue Windows-Bootzeit vor Softwarefrist.")
PY
}

if [[ -z "$RESTORE" ]]; then
  if ((SSH==2 && INVENTAR==0)); then
    trap ssh_laufkey_loeschen EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
    SSH_KEY_VERZ="$(mktemp -d -- "$OUT/.ssh-laufkey.XXXXXX")"
    SSH_KEY="$SSH_KEY_VERZ/id"
    ssh-keygen -q -t ed25519 -N '' -f "$SSH_KEY"
  fi
  lauf_vorbereiten "$OUT"
fi
if ((INVENTAR)); then
  remote pruefen >"$OUT/vorher.json" 2>>"$OUT/winrm.log"
  printf 'Laufordner: %s\n' "$OUT"
  cat "$OUT/vorher.json"
  exit 0
fi
if [[ -n "$RESTORE" ]]; then
  [[ -f "$OUT/vorher.json" ]] || { echo 'Snapshot fuer Wiederherstellung nicht gefunden' >&2; exit 2; }
  EINTRAG="$(json_wert entry)"
  WAECHE_AN="$(json_wert watcher_enabled)"
  USB_SERIE="$(json_wert usb_serial)"
  PARTITION="$(json_wert partition_guid)"
  STICK_SHA="$(json_wert stick_sha256)"
  REMOTE_ANGEFASST=1
  SYNAPSE_STOPP=1
  trap aufraeumen EXIT
  exit 0
fi
[[ -z "$(ss -H -lun 'sport = :69')" ]] || { echo 'Port69 ist schon belegt; vorhandenen Server nicht uebernehmen' >&2; exit 2; }
sudo -n true
remote pruefen >"$OUT/vorher.json" 2>"$OUT/winrm.log"
EINTRAG="$(json_wert entry)"
WAECHE_AN="$(json_wert watcher_enabled)"
boot_alt="$(json_wert last_boot)"
boot_muster='^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{7}Z$'
[[ "$boot_alt" =~ $boot_muster ]] || { echo 'FEHLER: Windows-Bootzeit vor dem Lauf fehlt oder ist ungueltig' >&2; exit 2; }
trap aufraeumen EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
exec > >(exec 9>&-; tee -a "$OUT/lauf.log") 2>&1
printf 'Laufordner: %s\n' "$OUT"
capture_filter='arp or udp'
[[ -z "$PING_IP" ]] || capture_filter='arp or udp or icmp'
((TRANSPORT==0)) || capture_filter='arp or udp or icmp or (tcp port 8080)'
(( (TRANSPORT & 2) == 0 )) || capture_filter+=' or (tcp port 2222)'
((SSH==0)) || capture_filter+=' or (tcp port 2223)'
setsid bash -c 'sudo -n tcpdump -ni "$1" -s0 -U -w - "$3" 2>"$2/tcpdump.log" | tee "$2/netz.pcap" | tcpdump -n -tt -l -A -r - "udp dst port 7777" >"$2/netz.log" 2>"$2/decode.log"; exit "${PIPESTATUS[0]}"' _ "$IFACE" "$OUT" "$capture_filter" &
CAPTURE_PID=$!
MOO_NETZ_TFTP="$OUT/tftp" setsid mise run -C "$ROOT/skripte/p7/uefi" netz-server "$EFI" "$OUT/moo.kern" >"$OUT/server.log" 2>&1 &
SERVER_PID=$!
for ((i=0;i<30;i++)); do
  kill -0 "$CAPTURE_PID" && kill -0 "$SERVER_PID" || { echo 'Server/Mitschnitt vorzeitig beendet' >&2; exit 1; }
  if grep -q 'dnsmasq-tftp.*TFTP root' "$OUT/server.log" && [[ -s "$OUT/netz.pcap" ]]; then break; fi
  sleep 1
done
grep -q 'dnsmasq-tftp.*TFTP root' "$OUT/server.log" && [[ -s "$OUT/netz.pcap" ]] || { echo 'Server/Capture nicht betriebsbereit' >&2; exit 1; }
REMOTE_ANGEFASST=1
remote pausieren
SYNAPSE_STOPP=1
notiz "Kernlauf beginnt: Modus=$MODUS, Laufzeit=$SEKUNDEN, Watchdog=$WATCHDOG_S"
timeout --foreground 90s uv run --with pywinrm python -I "$ROOT/skripte/p2/minipc_winrm.py" runfile "$STOPP_SK"
remote booten
bis=$((SECONDS+ZEITLIMIT))
ping_versucht=0
arp_belegt=0
ping_belegt=0
transport_belegt=0
ssh_belegt=0
while ((SECONDS<bis)); do
  kill -0 "$CAPTURE_PID" && kill -0 "$SERVER_PID" || { echo 'Server/Mitschnitt waehrend Lauf ausgefallen' >&2; exit 1; }
  ende=0
  if ((MODUS==7)); then
    grep -aEq 'MOO-KERNEL (IT8613 SAFETY OHNE BEDIENUNG|NEUSTART)' "$OUT/netz.log" && ende=1
  elif grep -aFq 'MOO-KERNEL NEUSTART' "$OUT/netz.log"; then
    ende=1
  fi
  # Windows hat dieselbe IP. Nur die Probe zwischen Kernmarker und Neustart
  # zaehlt, und der regulaere Kernel-/Windows-Abschluss muss erhalten bleiben.
  if [[ -n "$PING_IP" ]] && ((ping_versucht==0 && ende==0)) &&
     grep -aFq 'MOO-KERNEL NETZ IPV4 BEREIT ' "$OUT/netz.log"; then
    ping_versucht=1
    # Frische ARP-Anfrage auch dann, wenn der Host die MAC vom Loader kennt.
    if LC_ALL=C timeout --foreground 5s sudo -n arping -I "$IFACE" -c 1 -w 3 "$PING_IP" >"$OUT/arp.log" 2>&1; then
      arp_belegt=1
    fi
    cat "$OUT/arp.log"
    if LC_ALL=C timeout --foreground 10s ping -4 -n -D -I "$IFACE" -c 5 -W 1 "$PING_IP" >"$OUT/ping.log" 2>&1 &&
       ! grep -aFq 'MOO-KERNEL NEUSTART' "$OUT/netz.log"; then
      ping_belegt=1
    fi
    cat "$OUT/ping.log"
    tcp_echo=$(( (TRANSPORT >> 1) & 1 ))
    if ((TRANSPORT)) && grep -aFq 'MOO-KERNEL NETZ UDP TCP BEREIT 0000000000000001' "$OUT/netz.log" &&
       { ((tcp_echo==0)) || grep -aFq 'MOO-KERNEL NETZ TCP ECHO BEREIT 0000000000000001' "$OUT/netz.log"; } &&
       ! grep -aFq 'MOO-KERNEL NEUSTART' "$OUT/netz.log"; then
      host_adr="${MOO_NETZ_ADR:-192.168.77.1/24}"
      if bash "$ROOT/skripte/p7/transport-probe.sh" "$PING_IP" "$IFACE" "${host_adr%/*}" "$OUT" "$tcp_echo" &&
         ! grep -aFq 'MOO-KERNEL NEUSTART' "$OUT/netz.log"; then
        transport_belegt=1
      fi
    fi
    if ((SSH)) && grep -aFq 'MOO-KERNEL NETZ SSH BEREIT 0000000000000001' "$OUT/netz.log" &&
       ! grep -aFq 'MOO-KERNEL NEUSTART' "$OUT/netz.log"; then
      host_adr="${MOO_NETZ_ADR:-192.168.77.1/24}"
      ssh_probe=()
      if ((SSH==2)); then
        ssh_probe=(info "$SSH_KEY")
      fi
      if bash "$ROOT/skripte/p7/ssh-kex-probe.sh" "$PING_IP" "${host_adr%/*}" "$OUT/netz.log" "$OUT" "${ssh_probe[@]}" &&
         ! grep -aFq 'MOO-KERNEL NEUSTART' "$OUT/netz.log"; then
        ssh_belegt=1
      fi
    fi
  fi
  if ((ende)); then
    if timeout --foreground 50s uv run --with pywinrm python -I "$ROOT/skripte/p2/minipc_winrm.py" run \
      '$os=Get-CimInstance Win32_OperatingSystem; $boot=$os.LastBootUpTime.ToUniversalTime(); $boot.ToString("o"); [int64](([DateTime]::UtcNow - $boot).TotalMilliseconds)' >"$OUT/windows-boot-probe.txt" 2>>"$OUT/winrm.log"; then
      antwort_ms="$(date +%s%3N)"
      mapfile -t boot_probe <"$OUT/windows-boot-probe.txt"
      boot_neu="${boot_probe[0]-}"
      boot_alter="${boot_probe[1]-}"
      boot_neu="${boot_neu%$'\r'}"
      boot_alter="${boot_alter%$'\r'}"
      if [[ "$boot_neu" =~ $boot_muster && "$boot_neu" > "$boot_alt" && "$boot_alter" =~ ^[0-9]{1,15}$ ]]; then
        printf '%s\n' "$boot_neu" >"$OUT/windows-boot.txt"
        if ((MODUS==1 || MODUS==5)); then
          if ! it8613_start_belegt || ! grep -aFq 'MOO-KERNEL IT8613 AUS 0000000000000000' "$OUT/netz.log" ||
             { ((MODUS==5)) && ! grep -aFq 'MOO-KERNEL IT8613 BEDIENT ' "$OUT/netz.log"; }; then
            echo 'FEHLER: Windows zurueck, aber IT8613-Quittierung, Bedienung oder Abschalten nicht belegt.' >&2
            exit 1
          fi
          echo 'IT8613-Start, Abschalten und neue Windows-Bootzeit belegt'
        elif ((MODUS==6)); then
          if ! grep -aFq 'MOO-KERNEL IT8613 MESSREIHE FERTIG ' "$OUT/netz.log"; then
            echo 'FEHLER: Windows zurueck, aber IT8613-Messreihe unvollstaendig.' >&2
            exit 1
          fi
          echo 'IT8613-Messreihe und neue Windows-Bootzeit belegt'
        elif ((MODUS==7)); then
          if grep -aEq 'MOO-KERNEL (IT8613 SAFETY SOFTWARE FRIST|IT8613 AUS |NEUSTART|RESET CF9|RESET 8042)' "$OUT/netz.log"; then
            echo 'IT8613-Safety: Software-Rueckkehr protokolliert. Hardware-Reset bleibt unbelegt.' >&2
            exit 1
          fi
          if ! it8613_start_belegt || grep -aFq 'MOO-KERNEL IT8613 BEDIENT ' "$OUT/netz.log"; then
            echo 'FEHLER: IT8613-Safety-Start ohne Reload nicht vollstaendig belegt.' >&2
            exit 1
          fi
          kill -0 "$CAPTURE_PID" && kill -0 "$SERVER_PID" || { echo 'Server/Mitschnitt waehrend Lauf ausgefallen' >&2; exit 1; }
          it8613_safety_nachweis "$antwort_ms" "$boot_alter" || exit 1
        else
          echo 'Kern-Abschluss und neue Windows-Bootzeit belegt'
        fi
        if [[ -n "$PING_IP" ]]; then
          ((arp_belegt && ping_belegt)) || { echo 'FEHLER: ARP und Ping im Kernfenster nicht vollstaendig belegt.' >&2; exit 1; }
          echo 'ARP und Ping im Kernfenster belegt; RTT in ping.log'
        fi
        if ((TRANSPORT)); then
          ((transport_belegt)) || { echo 'FEHLER: Angeforderte Transportproben im Kernfenster nicht vollstaendig belegt.' >&2; exit 1; }
          echo 'UDP-Echo und TCP/HTTP im Kernfenster belegt'
        fi
        if ((KRYPTO)); then
          uv run --no-project --offline python "$ROOT/skripte/p7/krypto-nachweis.py" "$OUT/netz.log" n100 >"$OUT/krypto.log" 2>&1 || {
            cat "$OUT/krypto.log"
            exit 1
          }
          cat "$OUT/krypto.log"
        fi
        if ((SSH)); then
          if ! ((ssh_belegt)) || ! grep -aFq 'MOO-KERNEL NETZ SSH NEWKEYS 0000000000000001' "$OUT/netz.log" ||
             ! grep -aFq 'MOO-KERNEL NETZ SSH SERVICE 0000000000000001' "$OUT/netz.log"; then
            echo 'FEHLER: SSH-Transport und geschuetzte Service-Runde im Kernfenster fehlen.' >&2
            exit 1
          fi
          if ((SSH==2)); then
            grep -aFq 'MOO-KERNEL NETZ SSH AUTH OK 0000000000000001' "$OUT/netz.log" &&
              grep -aFq 'MOO-KERNEL NETZ SSH MOO-INFO 0000000000000001' "$OUT/netz.log" || {
                echo 'FEHLER: Auth-/moo-info-Marker im Kernfenster fehlen.' >&2
                exit 1
              }
            echo 'OpenSSH-Userauth und moo-info belegt'
          else
            echo 'OpenSSH-Transport und Kernmarker belegt'
          fi
        fi
        exit 0
      fi
    fi
  fi
  sleep 5
done
echo "FEHLER: Kern-Abschluss/Windows-Rueckkehr nach $ZEITLIMIT s nicht belegt. Kein zweiter Neustart." >&2
exit 1
