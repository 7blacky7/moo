#!/usr/bin/env bash
# P7-T25. Echte Laeufe ausschliesslich durch den Koordinator.
# --trocken fuehrt keine externen Befehle aus und aendert keinen Zustand.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EFI="$ROOT/build/p7-t25/stick/tftp/moo.efi"
KERN="$ROOT/build/p7-t25/stick/tftp/moo.kern"
IFACE=enp60s0
SEKUNDEN=90
ZEITLIMIT=600
USB_SERIE=
PARTITION=
STICK_SHA=
TROCKEN=0
KOORDINATOR=0
DIAGNOSE=0
INVENTAR=0
MODUS=1
WATCHDOG_S=180
KONFIG_QUELLE=
KONFIG_ZIEL=
RESTORE=
STOPP_SK="${MOO_N100_STOPP_SKRIPT:-$ROOT/build/koord/sanft_aus.ps1}"
NOTIZ="${MOO_N100_NOTIZ:-$ROOT/build/notizen/synapse-offline.md}"
while (($#)); do
  case "$1" in
    --trocken) TROCKEN=1; shift;;
    --inventar) INVENTAR=1; shift;;
    --koordinator) KOORDINATOR=1; shift;;
    --diagnose) DIAGNOSE=1; MODUS=0; SEKUNDEN=20; shift;;
    --watchdog-test) DIAGNOSE=0; MODUS=2; shift;;
    --modus) MODUS="$2"; shift 2;;
    --watchdog-sekunden) WATCHDOG_S="$2"; shift 2;;
    --efi) EFI="$2"; shift 2;;
    --kern) KERN="$2"; shift 2;;
    --interface) IFACE="$2"; shift 2;;
    --sekunden) SEKUNDEN="$2"; shift 2;;
    --zeitlimit) ZEITLIMIT="$2"; shift 2;;
    --usb-serie) USB_SERIE="$2"; shift 2;;
    --partition) PARTITION="$2"; shift 2;;
    --stick-sha256) STICK_SHA="$2"; shift 2;;
    --kern-konfigurieren) KONFIG_QUELLE="$2"; KONFIG_ZIEL="$3"; shift 3;;
    --aufraeumen) RESTORE="$2"; shift 2;;
    --hilfe) printf '%s\n' 'n100-lauf.sh --trocken | --koordinator --diagnose --efi DATEI --kern DATEI --usb-serie SERIE --partition {GUID} --stick-sha256 SHA256' \
      '--inventar: Helfer hochladen und nur USB-/Firmware-/Waechter-Inventar lesen; kein Boot.' \
      'Normaler Lauf: 90s Kern + 180s Hardware-Watchdog; --watchdog-test weist den Reset bei Stillstand nach.' \
      '--kern-konfigurieren QUELLE ZIEL [--sekunden N]: nur private ELF-Kopie konfigurieren.' \
      '--aufraeumen build/n100-laeufe/ZEIT --koordinator: Wiederherstellung erneut versuchen.'; exit 0;;
    *) printf 'Unbekanntes Argument: %s\n' "$1" >&2; exit 2;;
  esac
done
[[ "$SEKUNDEN" =~ ^[0-9]+$ && "$ZEITLIMIT" =~ ^[0-9]+$ && "$WATCHDOG_S" =~ ^[0-9]+$ && "$MODUS" =~ ^(0|1|2|100|101)$ ]] || { echo 'Zeiten muessen ganze Sekunden sein' >&2; exit 2; }
((SEKUNDEN>=1 && SEKUNDEN<=600 && WATCHDOG_S>=6 && WATCHDOG_S<=600 && ZEITLIMIT>=30 && ZEITLIMIT<=1800)) || { echo 'Ungueltiges Zeitlimit' >&2; exit 2; }
[[ "$IFACE" == enp60s0 ]] || { echo 'Nur das belegte Direktkabel enp60s0 ist erlaubt' >&2; exit 2; }

kern_kopie() {
  uv run --no-project python - "$1" "$2" "$SEKUNDEN" "$WATCHDOG_S" "$MODUS" <<'PY'
import pathlib, struct, sys
src, dst = map(pathlib.Path, sys.argv[1:3])
seconds, watchdog, mode = map(int, sys.argv[3:6])
if src.resolve() == dst.resolve():
    raise SystemExit("Quell-Kern darf nicht ueberschrieben werden")
data = bytearray(src.read_bytes())
if data[:7] != b"\x7fELF\x02\x01\x01" or struct.unpack_from("<HH", data, 16) != (2, 62):
    raise SystemExit("Kein ELF64 x86-64 EXEC")
magic = struct.pack("<Q", 0x353254504F4F4D21)
off = struct.unpack_from("<Q", data, 32)[0]
size, count = struct.unpack_from("<HH", data, 54)
hits = []
for i in range(count):
    p = off + i * size
    typ, flags, start = struct.unpack_from("<IIQ", data, p)
    length = struct.unpack_from("<Q", data, p + 32)[0]
    if typ != 1 or not flags & 2:
        continue
    pos = data.find(magic, start, start + length)
    while pos >= 0:
        if pos + 32 <= start + length:
            hits.append(pos)
        pos = data.find(magic, pos + 1, start + length)
if len(hits) != 1:
    raise SystemExit("Testvertrag nicht eindeutig in schreibbarem PT_LOAD")
# 0: Lesen; 1: N100+Pings; 2: N100-Stillstand; 100/101: nur ICH9-QEMU.
struct.pack_into("<QQQ", data, hits[0] + 8, seconds, watchdog, mode)
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_bytes(data)
print(f"Kern-Kopie {dst}: {seconds}s, iTCO {watchdog}s, Modus {mode}")
PY
}
if ((TROCKEN)); then
  printf '%s\n' 'TROCKEN: keine WinRM-/sudo-/Netzwerkbefehle, keine Dateien.'
  printf '1. USB-ESP per Seriennummer+GPT-GUID+NT-Geraetepfad+SHA256 belegen: %s %s %s\n' "$USB_SERIE" "$PARTITION" "$STICK_SHA"
  printf '2. Private Kern-Kopie: %ss, Watchdog %ss; Modus=%s (0=Lesen,1=Bedienen,2=Stillstand)\n' "$SEKUNDEN" "$WATCHDOG_S" "$MODUS"
  printf '3. Bestehenden netz-server starten: %s %s; tcpdump auf %s\n' "$EFI" "$KERN" "$IFACE"
  printf '%s\n' '4. Synapse-Waechter-Zustand sichern, pausieren; build/koord/sanft_aus.ps1 per WinRM.'
  printf '%s\n' '5. Genau einen USB-Eintrag als {fwbootmgr} bootsequence setzen, danach shutdown /r /t 5.'
  printf '6. Netz-Abschluss MOO-KERNEL NEUSTART und neue Windows-Bootzeit abwarten (Limit %ss).\n' "$ZEITLIMIT"
  printf '%s\n' '7. Eigene Bootsequence, Waechter, Synapse, tcpdump und Server aufraeumen; netz-server restauriert IP/NM/Firewall.'
  printf '%s\n' 'Ergebnisse: build/n100-laeufe/<UTC-Zeit>/; Offline-Notizen: build/notizen/synapse-offline.md.'
  printf '%s\n' 'Safety-Vertrag: N100 8086:54A3, Basis0x400, TCOCTL0x101, CNT0x1800, Strap0; sonst vor USB abbrechen und zuruecksetzen.'
  exit 0
fi
if [[ -n "$KONFIG_QUELLE" ]]; then
  kern_kopie "$KONFIG_QUELLE" "$KONFIG_ZIEL"
  exit 0
fi
((KOORDINATOR || INVENTAR)) || { echo 'Echte Laeufe/Wiederherstellung sind nur mit --koordinator erlaubt' >&2; exit 2; }
if [[ -z "$RESTORE" ]] && ((INVENTAR==0)); then
  ((MODUS<=2)) || { echo 'ICH9-Testmodi sind nur fuer private QEMU-Kernkopien erlaubt' >&2; exit 2; }
  if ((MODUS==0)); then
    ((DIAGNOSE && SEKUNDEN<=30)) || { echo 'Lesediagnose ohne Watchdog ist auf30s begrenzt' >&2; exit 2; }
  elif ((MODUS==1 && SEKUNDEN+5>=WATCHDOG_S)); then
    echo 'Watchdog muss laenger als der befristete Kernlauf sein' >&2; exit 2
  fi
fi
for tool in uv timeout setsid flock sudo tcpdump curl ss; do
  command -v "$tool" >/dev/null || { echo "Werkzeug nicht gefunden: $tool" >&2; exit 2; }
done
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
    "& ([scriptblock]::Create([IO.File]::ReadAllText('D:\MooTest\n100-boot.ps1'))) -Aktion $(ps_quote "$aktion") -UsbSerial $(ps_quote "$USB_SERIE") -PartitionGuid $(ps_quote "$PARTITION") -StickSha256 $(ps_quote "$STICK_SHA") -Eintrag $(ps_quote "$EINTRAG") -WaechterWarAn ([bool]::Parse('$WAECHE_AN')) -SynapseGestoppt ($SYNAPSE_STOPP -eq 1)"
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
aufraeumen() {
  local rc=$? wieder=0 bis=$((SECONDS+120))
  trap - EXIT INT TERM HUP
  set +e
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
    if ! timeout --foreground 180s bash -c 'until [[ "$(curl -s -o /dev/null --max-time 5 -w "%{http_code}" https://synapse.moosynapse.org/)" == 200 ]]; do sleep 5; done'; then
      echo 'FEHLER: Windows zurueck, aber Synapse HTTP 200 nicht belegt' >&2
      rc=1
    fi
  fi
  printf 'Exit=%s Wiederhergestellt=%s\n' "$rc" "$wieder" >"$OUT/ergebnis.txt"
  notiz "Exit=$rc; Register/Reset/Windows siehe netz.log und WinRM-Artefakte; HTTP-Rueckkehr siehe lauf.log"
  exit "$rc"
}
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
[[ -f "$EFI" && -f "$KERN" && -s "$STOPP_SK" ]] || { echo 'Loader/Kern oder vorhandenes sanft_aus.ps1 nicht gefunden' >&2; exit 2; }
[[ "$USB_SERIE" =~ ^[a-zA-Z0-9_-]+$ && "$PARTITION" =~ ^\{[0-9a-fA-F-]{36}\}$ && "$STICK_SHA" =~ ^[0-9a-fA-F]{64}$ ]] || { echo 'Belegte Stick-Identitaet ist Pflicht' >&2; exit 2; }
[[ -z "$(ss -H -lun 'sport = :69')" ]] || { echo 'Port69 ist schon belegt; vorhandenen Server nicht uebernehmen' >&2; exit 2; }
sudo -n true
remote pruefen >"$OUT/vorher.json" 2>"$OUT/winrm.log"
EINTRAG="$(json_wert entry)"
WAECHE_AN="$(json_wert watcher_enabled)"
kern_kopie "$KERN" "$OUT/moo.kern"
sha256sum "$EFI" "$KERN" "$OUT/moo.kern" >"$OUT/sha256.txt"
trap aufraeumen EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
exec > >(exec 9>&-; tee -a "$OUT/lauf.log") 2>&1
printf 'Laufordner: %s\n' "$OUT"
setsid bash -c 'sudo -n tcpdump -ni "$1" -s0 -U -w - "arp or udp" 2>"$2/tcpdump.log" | tee "$2/netz.pcap" | tcpdump -n -l -A -r - "udp dst port 7777" >"$2/netz.log" 2>"$2/decode.log"; exit "${PIPESTATUS[0]}"' _ "$IFACE" "$OUT" &
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
while ((SECONDS<bis)); do
  kill -0 "$CAPTURE_PID" && kill -0 "$SERVER_PID" || { echo 'Server/Mitschnitt waehrend Lauf ausgefallen' >&2; exit 1; }
  ende=0
  if ((MODUS==2)); then
    grep -q 'MOO-KERNEL TCO SAFETY OHNE BEDIENUNG' "$OUT/netz.log" && ende=1
  elif grep -q 'MOO-KERNEL NEUSTART' "$OUT/netz.log"; then
    if ((MODUS==0)) || grep -q 'MOO-KERNEL TCO SCHARF GESAMT-SEKUNDEN' "$OUT/netz.log"; then ende=1; fi
  fi
  if ((ende)); then
    if timeout --foreground 50s uv run --with pywinrm python -I "$ROOT/skripte/p2/minipc_winrm.py" run \
      '$os=Get-CimInstance Win32_OperatingSystem; $os.LastBootUpTime.ToUniversalTime().ToString("o")' >"$OUT/windows-boot.txt" 2>>"$OUT/winrm.log"; then
      if [[ "$(tr -d '\r\n' <"$OUT/windows-boot.txt")" != "$(json_wert last_boot)" ]]; then
        echo 'Kern-Abschluss und neue Windows-Bootzeit belegt'
        exit 0
      fi
    fi
  fi
  sleep 5
done
echo "FEHLER: Kern-Abschluss/Windows-Rueckkehr nach $ZEITLIMIT s nicht belegt. Kein zweiter Neustart." >&2
exit 1
