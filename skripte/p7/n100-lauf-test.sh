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
case "$*" in
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
  *) printf '%s\n' 'MOO-KERNEL NEUSTART 0000000000000014';;
esac
while :; do sleep 1; done
MOCK
cat >"$T/bin/curl" <<'MOCK'
#!/usr/bin/env bash
printf 200
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
  --efi "$ROOT/build/p7-t25/stick/BOOTX64.EFI" --kern "$ROOT/build/p7-t25/stick/KERN.ELF"
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
for phase in ok normal safety pruefen server capture pausieren stop booten; do
  extra=()
  export MOCK_MODE=diag
  if [[ "$phase" == normal ]]; then extra=(--modus 1); export MOCK_MODE=normal; fi
  if [[ "$phase" == safety ]]; then extra=(--watchdog-test); export MOCK_MODE=safety; fi
  export MOCK_FAIL="$phase" MOO_N100_AUSGABE="$T/$phase"
  : >"$T/trace"
  : >"$T/capture.pid"
  rm -f "$T/server.pid"
  set +e
  bash "$ROOT/skripte/p7/n100-lauf.sh" "${args[@]}" "${extra[@]}" >"$T/$phase-konsole.log" 2>&1
  rc=$?
  set -e
  cp "$T/trace" "$T/$phase-trace"
  if [[ "$phase" == ok || "$phase" == normal || "$phase" == safety ]]; then test "$rc" -eq 0; else test "$rc" -ne 0; fi
  case "$phase" in
    ok|normal|safety|pausieren|stop|booten) grep -qx zurueck "$T/trace";;
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
