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
  pruefen) printf '%s\n' '{"entry":"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}","watcher_enabled":true,"usb_serial":"USBTEST","partition_guid":"{11111111-2222-3333-4444-555555555555}","stick_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","last_boot":"2026-11-16T20:00:00Z"}';;
  zurueck) printf '%s\n' '{"restored":true,"watcher_enabled":true}';;
  windows) printf '%s\n' '2026-11-16T20:01:00Z';;
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
case "${MOCK_MODE:-diag}" in
  normal) printf '%s\n' 'MOO-KERNEL TCO SCHARF GESAMT-SEKUNDEN 00000000000000B4' 'MOO-KERNEL NEUSTART 0000000000000014';;
  safety) printf '%s\n' 'MOO-KERNEL TCO SCHARF GESAMT-SEKUNDEN 00000000000000B4' 'MOO-KERNEL TCO SAFETY OHNE BEDIENUNG 00000000000000B4';;
  beobachten) printf '%s\n' 'MOO-KERNEL TCO BEOBACHTUNG START 00000000000000B4';;
  beobachten-frist) printf '%s\n' 'MOO-KERNEL TCO BEOBACHTUNG START 00000000000000B4' 'MOO-KERNEL TCO BEOBACHTUNG FRIST 000000000002E630' 'MOO-KERNEL NEUSTART 00000000000000BE';;
  it8613) printf '%s\n' 'MOO-KERNEL IT8613 SCHARF SEKUNDEN 000000000000000A' 'MOO-KERNEL IT8613 BEDIENT 0000000000000001' 'MOO-KERNEL IT8613 AUS 0000000000000000' 'MOO-KERNEL NEUSTART 0000000000000019';;
  it8613-ohne-aus) printf '%s\n' 'MOO-KERNEL IT8613 SCHARF SEKUNDEN 000000000000000A' 'MOO-KERNEL IT8613 BEDIENT 0000000000000001' 'MOO-KERNEL NEUSTART 0000000000000019';;
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
args=(--koordinator --diagnose --zeitlimit 30
  --efi "$ROOT/build/p7-t25/rueckkehr/LADER-AUTO.EFI" --kern "$ROOT/build/p7-t25/stick/KERN.ELF"
  --rueckfall-kern "$ROOT/build/p7-t25/rueckkehr/RUECKFALL.ELF"
  --usb-serie USBTEST --partition '{11111111-2222-3333-4444-555555555555}'
  --stick-sha256 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa)
bash "$ROOT/skripte/p7/n100-lauf.sh" --trocken >"$T/trocken.log"
test ! -e "$T/trace"
test ! -e "$T/offline-test.md"
echo 'PASS: --trocken ruft weder WinRM noch Dienste auf und schreibt keine Laufdaten'
if bash "$ROOT/skripte/p7/n100-lauf.sh" --koordinator --diagnose --sekunden 90 >"$T/normal-gesperrt.log" 2>&1; then
  echo 'FAIL: unbelegter Watchdog wurde uebergangen' >&2; exit 1
fi
test ! -e "$T/trace"
echo 'PASS: Ungeschuetzter Langlauf wird verweigert'
if bash "$ROOT/skripte/p7/n100-lauf.sh" --koordinator --modus 4 --sekunden 184 >"$T/beobachtung-zu-kurz.log" 2>&1; then
  echo 'FAIL: TCO-Beobachtung mit verfruehtem Software-Reset wurde akzeptiert' >&2; exit 1
fi
test ! -e "$T/trace"
grep -q 'mindestens Watchdog-Dauer plus 5 Sekunden' "$T/beobachtung-zu-kurz.log"
echo 'PASS: Zu kurze TCO-Beobachtungsfrist wird vor WinRM verweigert'
if bash "$ROOT/skripte/p7/n100-lauf.sh" --koordinator --modus 5 --sekunden 10 --watchdog-sekunden 10 >"$T/it8613-zu-kurz.log" 2>&1; then
  echo 'FAIL: IT8613-Lauf muss laenger als ein Watchdog-Intervall sein' >&2; exit 1
fi
test ! -e "$T/trace"
grep -Fq 'IT8613 braucht' "$T/it8613-zu-kurz.log"
echo 'PASS: IT8613-Zeitvertrag wird vor WinRM geprueft'
for phase in ok normal safety beobachten beobachten-frist it8613 it8613-ohne-aus http502 http-timeout rueckfall loader upload pruefen server capture pausieren stop booten; do
  extra=(--synapse-zeitlimit 5)
  if [[ "$phase" == http502 ]]; then extra=(--synapse-zeitlimit 15); fi
  if [[ "$phase" == rueckfall ]]; then extra+=(--rueckfall-kern "$ROOT/build/p7-t25/stick/KERN.ELF"); fi
  if [[ "$phase" == loader ]]; then extra+=(--efi "$ROOT/build/p7-t25/rueckkehr/LADER.EFI"); fi
  export MOCK_MODE=diag
  if [[ "$phase" == normal ]]; then extra=(--modus 1); export MOCK_MODE=normal; fi
  if [[ "$phase" == safety ]]; then extra=(--watchdog-test); export MOCK_MODE=safety; fi
  if [[ "$phase" == beobachten || "$phase" == beobachten-frist ]]; then
    extra=(--modus 4 --sekunden 190); export MOCK_MODE="$phase"
  fi
  if [[ "$phase" == it8613* ]]; then
    extra+=(--modus 5 --sekunden 25 --watchdog-sekunden 10); export MOCK_MODE="$phase"
  fi
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
    ok|normal|safety|beobachten|beobachten-frist|it8613|http502) test "$rc" -eq 0;;
    *) test "$rc" -ne 0;;
  esac
  case "$phase" in
    beobachten) grep -q 'ohne protokollierte TCO-Beobachtungsfrist' "$T/$phase-konsole.log";;
    beobachten-frist) grep -q 'Software-Reset eingeleitet' "$T/$phase-konsole.log";;
    it8613) grep -Fq 'IT8613-Bedienung, Abschalten und neue Windows-Bootzeit belegt' "$T/$phase-konsole.log";;
    it8613-ohne-aus) grep -Fq 'Windows zurueck, aber IT8613-Bedienung oder Abschalten nicht belegt' "$T/$phase-konsole.log";;
  esac
  case "$phase" in
    ok|normal|safety|beobachten|beobachten-frist|it8613|it8613-ohne-aus|http502|http-timeout|pausieren|stop|booten) grep -qx zurueck "$T/trace";;
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
