#!/usr/bin/env bash
# P7-T25: isolierte Orchestrierungsproben. Alle WinRM-/sudo-/Server-Aufrufe
# werden lokal ersetzt; MOO_MINIPC_HOST zusaetzlich zwingend Loopback.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$ROOT/build/p7-t25/runner-test-$$"
REAL_UV="$(command -v uv)"
mkdir -p "$T/bin"
export REAL_UV MOCK_ROOT="$T" MOO_MINIPC_HOST=127.0.0.1
cat >"$T/bin/uv" <<'MOCK'
#!/usr/bin/env bash
set -eu
remote=0
for a in "$@"; do [[ "$a" != */minipc_winrm.py ]] || remote=1; done
if ((remote==0)); then exec "$REAL_UV" "$@"; fi
for a in "$@"; do
  if ((${#a}>2000)); then echo "The command line is too long." >&2; exit 20; fi
done
case "$*" in
  *"upload"*) action=upload;;
  *"runfile"*) action=stop;;
  *"-Aktion 'pruefen'"*) action=pruefen;;
  *"-Aktion 'pausieren'"*) action=pausieren;;
  *"-Aktion 'booten'"*) action=booten;;
  *"-Aktion 'zurueck'"*) action=zurueck;;
  *"LastBootUpTime"*) action=windows;;
  *) echo 'UNBEKANNTER REMOTE-MOCK VERWEIGERT' >&2; exit 99;;
esac
echo "$action" >>"$MOCK_ROOT/trace"
if [[ "$action" == "${MOCK_FAIL:-}" ]]; then exit 17; fi
case "$action" in
  pruefen) printf '%s\n' '{"entry":"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}","watcher_enabled":true,"usb_serial":"USBTEST","partition_guid":"{11111111-2222-3333-4444-555555555555}","stick_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","last_boot":"2026-11-16T20:00:00.0000000Z"}';;
  zurueck) printf '%s\n' '{"restored":true,"watcher_enabled":true}';;
  windows)
    if [[ "${MOCK_FAIL:-}" == it8613-safety-bootzeit-alt ]]; then
      printf '%s\n' '2026-11-16T20:00:00.0000000Z' 1000
    else
      printf '%s\n' '2026-11-16T20:01:00.0000000Z' 1000
    fi
    ;;
esac
MOCK
cat >"$T/bin/sudo" <<'MOCK'
#!/usr/bin/env bash
[[ "$1" != -n ]] || shift
case "$1" in
  true) exit 0;;
  tcpdump) shift; exec tcpdump "$@";;
  kill) shift; exec /usr/bin/kill "$@";;
  *) echo 'ECHTES SUDO VERWEIGERT' >&2; exit 99;;
esac
MOCK
cat >"$T/bin/mise" <<'MOCK'
#!/usr/bin/env bash
echo "$$" >"$MOCK_ROOT/server.pid"
trap 'echo server-clean >>"$MOCK_ROOT/trace"; exit 0' TERM INT HUP
if [[ "${MOCK_FAIL:-}" == server ]]; then exit 18; fi
printf '%s\n' 'dnsmasq-tftp: TFTP root is synthetic'
while :; do sleep 1; done
MOCK
cat >"$T/bin/tcpdump" <<'MOCK'
#!/usr/bin/env bash
echo "$$" >>"$MOCK_ROOT/capture.pid"
trap 'echo capture-clean >>"$MOCK_ROOT/trace"; exit 0' TERM INT HUP
if [[ "$*" == *"-r -"* ]]; then cat; exit 0; fi
if [[ "${MOCK_FAIL:-}" == capture ]]; then exit 19; fi
printf '%s\n' 'PCAP-MOCK'
it8613_start() {
  [[ "$MOCK_MODE" == it8613-ohne-vorstopp ]] ||
    printf '%s\n' 'MOO-KERNEL IT8613 VOR START AUS 0000000000000000'
  if [[ "$MOCK_MODE" == it8613-quittierung-steht ]]; then
    printf '%s\n' 'MOO-KERNEL IT8613 SAFETY CTRL QUITTIERUNG READBACK 0000000000000009'
  elif [[ "$MOCK_MODE" != normal-ohne-quittierung ]]; then
    printf '%s\n' 'MOO-KERNEL IT8613 SAFETY CTRL QUITTIERUNG READBACK 0000000000000008'
  fi
  printf '%s\n' 'MOO-KERNEL IT8613 SCHARF SEKUNDEN 000000000000000A'
}
paket() {
  printf '%s.000000 IP 0.0.0.0.7777 > 255.255.255.255.7777: UDP, length 70\n' "$1"
  printf 'E..Z........MOO-KERNEL IT8613 %s %016X\n' "$2" "$3"
}
case "${MOCK_MODE:-diag}" in
  it8613-safety*)
    it8613_start
    start=$(( $(date +%s) - 25 ))
    if [[ "$MOCK_MODE" == it8613-safety-spaet ]]; then start=$((start - 35)); fi
    paket "$start" 'SAFETY OHNE BEDIENUNG' 10
    letzte=9
    case "$MOCK_MODE" in
      it8613-safety-ohne-proben) letzte=-1;;
      it8613-safety-frueh) letzte=1;;
    esac
    for ((i=0;i<=letzte;i++)); do
      paket "$((start + i))" 'SAFETY MESSZEIT MS' "$((i * 1000 + 10))"
      paket "$((start + i))" 'WDT CFG' 0xD0
    done
    if [[ "$MOCK_MODE" == it8613-safety-rueckfall ]]; then
      paket "$((start + 45))" 'SAFETY SOFTWARE FRIST SEKUNDEN' 45
    fi
    ;;
  normal*|it8613*)
    it8613_start
    printf '%s\n' 'MOO-KERNEL IT8613 BEDIENT 0000000000000001'
    [[ "$MOCK_MODE" == it8613-ohne-aus ]] ||
      printf '%s\n' 'MOO-KERNEL IT8613 AUS 0000000000000000'
    printf '%s\n' 'MOO-KERNEL NEUSTART 0000000000000019'
    ;;
  messreihe) printf '%s\n' 'MOO-KERNEL IT8613 MESSREIHE FERTIG 0000000000011170' 'MOO-KERNEL NEUSTART 0000000000000046';;
  messreihe-unvollstaendig) printf '%s\n' 'MOO-KERNEL IT8613 MESSZEIT MS 0000000000000000' 'MOO-KERNEL NEUSTART 0000000000000046';;
  *) printf '%s\n' 'MOO-KERNEL NEUSTART 0000000000000014';;
esac
while :; do sleep 1; done
MOCK
cat >"$T/bin/curl" <<'MOCK'
#!/usr/bin/env bash
if [[ "${MOCK_FAIL:-}" == http-timeout ]]; then printf 502
elif [[ "${MOCK_FAIL:-}" == http502 && ! -e "$MOCK_ROOT/http-first" ]]; then
  touch "$MOCK_ROOT/http-first"; printf 502
else printf 200
fi
MOCK
cat >"$T/bin/ss" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK
chmod +x "$T/bin/"*
export PATH="$T/bin:$PATH"
printf '%s\n' 'Write-Output "synthetischer Stop"' >"$T/stopp.ps1"
export MOO_N100_STOPP_SKRIPT="$T/stopp.ps1" MOO_N100_NOTIZ="$T/offline-test.md"
testkern="${MOO_N100_TEST_KERN:-$ROOT/build/p7-t25/stick/KERN.ELF}"
bash "$ROOT/skripte/p7/n100-lauf.sh" --kern-konfigurieren "$testkern" "$T/RUECKFALL.ELF" \
  --modus 3 --sekunden 20 --watchdog-sekunden 10
args=(--koordinator --zeitlimit 30
  --efi "$ROOT/build/p7-t25/rueckkehr/LADER-AUTO.EFI" --kern "$testkern"
  --rueckfall-kern "$T/RUECKFALL.ELF"
  --usb-serie USBTEST --partition '{11111111-2222-3333-4444-555555555555}'
  --stick-sha256 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa)
bash "$ROOT/skripte/p7/n100-lauf.sh" --trocken >"$T/trocken.log"
grep -Fq 'Private Kern-Kopie: 90s, Watchdog 10s; Modus=1' "$T/trocken.log"
bash "$ROOT/skripte/p7/n100-lauf.sh" --trocken --watchdog-test >"$T/trocken-safety.log"
grep -Fq 'Private Kern-Kopie: 45s, Watchdog 10s; Modus=7' "$T/trocken-safety.log"
test ! -e "$T/trace"
test ! -e "$T/offline-test.md"
echo 'PASS: --trocken ruft weder WinRM noch Dienste auf und schreibt keine Laufdaten'
if bash "$ROOT/skripte/p7/n100-lauf.sh" --koordinator --diagnose --sekunden 90 >"$T/normal-gesperrt.log" 2>&1; then
  echo 'FAIL: unbelegter Watchdog wurde uebergangen' >&2; exit 1
fi
test ! -e "$T/trace"
echo 'PASS: Ungeschuetzter Langlauf wird verweigert'
for tco_modus in 2 4; do
  if bash "$ROOT/skripte/p7/n100-lauf.sh" --koordinator --modus "$tco_modus" >"$T/tco-gesperrt.log" 2>&1; then
    echo 'FAIL: Ehemaliger TCO-Scharfmodus wurde akzeptiert' >&2; exit 1
  fi
  grep -Fq 'TCO am N100 ist eine Lesediagnose' "$T/tco-gesperrt.log"
  test ! -e "$T/trace"
done
echo 'PASS: N100-TCO-Scharfmodi werden vor WinRM verweigert'
if bash "$ROOT/skripte/p7/n100-lauf.sh" --koordinator --modus 5 --sekunden 10 --watchdog-sekunden 10 >"$T/it8613-zu-kurz.log" 2>&1; then
  echo 'FAIL: IT8613-Lauf muss laenger als ein Watchdog-Intervall sein' >&2; exit 1
fi
test ! -e "$T/trace"
grep -Fq 'IT8613 braucht' "$T/it8613-zu-kurz.log"
echo 'PASS: IT8613-Zeitvertrag wird vor WinRM geprueft'
for phase in ok normal normal-ohne-quittierung it8613 it8613-ohne-aus it8613-ohne-vorstopp it8613-quittierung-steht \
    it8613-safety it8613-safety-rueckfall it8613-safety-ohne-proben it8613-safety-frueh it8613-safety-spaet \
    it8613-safety-bootzeit-alt messreihe messreihe-unvollstaendig http502 http-timeout rueckfall loader \
    upload pruefen server capture pausieren stop booten; do
  extra=(--synapse-zeitlimit 5)
  if [[ "$phase" == http502 ]]; then extra=(--synapse-zeitlimit 15); fi
  if [[ "$phase" == rueckfall ]]; then extra+=(--rueckfall-kern "$testkern"); fi
  if [[ "$phase" == loader ]]; then extra+=(--efi "$ROOT/build/p7-t25/rueckkehr/LADER.EFI"); fi
  export MOCK_MODE="$phase"
  case "$phase" in
    normal*) ;;
    it8613-safety*) extra+=(--watchdog-test);;
    it8613*) extra+=(--modus 5 --sekunden 25 --watchdog-sekunden 10);;
    messreihe*) extra+=(--it8613-messreihe);;
    *) extra+=(--diagnose);;
  esac
  export MOCK_FAIL="$phase" MOO_N100_AUSGABE="$T/$phase"
  : >"$T/trace"
  : >"$T/capture.pid"
  rm -f "$T/server.pid"
  set +e
  bash "$ROOT/skripte/p7/n100-lauf.sh" "${args[@]}" "${extra[@]}" >"$T/$phase-konsole.log" 2>&1
  rc=$?
  set -e
  cp "$T/trace" "$T/$phase-trace"
  case "$phase" in
    ok|normal|it8613|it8613-safety|messreihe|http502) test "$rc" -eq 0;;
    *) test "$rc" -ne 0;;
  esac
  case "$phase" in
    normal|it8613) grep -Fq 'IT8613-Start, Abschalten und neue Windows-Bootzeit belegt' "$T/$phase-konsole.log";;
    normal-ohne-quittierung|it8613-ohne-*|it8613-quittierung-steht) grep -Fq 'IT8613-Quittierung, Bedienung oder Abschalten nicht belegt' "$T/$phase-konsole.log";;
    it8613-safety) grep -Fq 'IT8613-Hardware-Reset belegt. Netzlog-Abriss nach 9010 ms' "$T/$phase-konsole.log";;
    it8613-safety-rueckfall) grep -Fq 'Software-Rueckkehr protokolliert' "$T/$phase-konsole.log";;
    it8613-safety-ohne-proben) grep -Fq 'Netzlog endet ohne belegte Messreihe' "$T/$phase-konsole.log";;
    it8613-safety-frueh) grep -Fq 'Netzlog-Abriss bei 1010 ms passt nicht' "$T/$phase-konsole.log";;
    it8613-safety-spaet) grep -Fq 'Windows-Bootzeit vor der IT8613-Softwarefrist nicht belegt' "$T/$phase-konsole.log";;
    it8613-safety-bootzeit-alt)
      grep -Fq 'Kern-Abschluss/Windows-Rueckkehr nach 30 s nicht belegt' "$T/$phase-konsole.log"
      test ! -e "$T/$phase/windows-boot.txt"
      ;;
    messreihe) grep -Fq 'IT8613-Messreihe und neue Windows-Bootzeit belegt' "$T/$phase-konsole.log";;
    messreihe-unvollstaendig) grep -Fq 'Windows zurueck, aber IT8613-Messreihe unvollstaendig' "$T/$phase-konsole.log";;
  esac
  case "$phase" in
    ok|normal*|it8613*|messreihe*|http502|http-timeout|pausieren|stop|booten) grep -qx zurueck "$T/trace";;
    *) if grep -qx zurueck "$T/trace"; then echo 'FAIL: unberuehrten Remote-Zustand restauriert' >&2; exit 1; fi;;
  esac
  if [[ -s "$T/server.pid" ]]; then
    pid="$(<"$T/server.pid")"
    if kill -0 "$pid" 2>/dev/null; then echo "FAIL: eigener Server $pid lebt" >&2; exit 1; fi
  fi
  while read -r pid; do
    if kill -0 "$pid" 2>/dev/null; then echo "FAIL: eigener Mitschnitt $pid lebt" >&2; exit 1; fi
  done <"$T/capture.pid"
  echo "PASS: Phase $phase RC$rc; nur eigene Zustaende restauriert, Prozesse beendet"
done
echo "Artefakte: $T"
echo 'N100-RUNNER MOCK OK'
