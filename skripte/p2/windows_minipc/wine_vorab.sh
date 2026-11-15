#!/usr/bin/env bash
# Lokaler Wine-Vorlauf fuer Windows-Probe-Exes (kein nativer Windows-Beweis;
# der gilt nur fuer windows_minipc/test.sh auf dem N100).
# NIE etwas auf dem Desktop des Users oeffnen: erwartete Traps wuerden sonst den
# Wine-Absturzdialog (winedbg) zeigen. Daher fest und fail-closed:
#   - kein DISPLAY/WAYLAND_DISPLAY, eigenes leeres XDG_RUNTIME_DIR (kein wayland-0)
#   - winedbg, winemenubuilder, mscoree, mshtml abgeschaltet (kein Dialog, keine
#     Mono/Gecko-Installationsfenster), WINEDEBUG=-all
#   - eigenes WINEPREFIX unter build/, nie das des Users
# Aufruf: wine_vorab.sh <exe> [Argumente]  -> Exitcode und Ausgabe der Exe.
set -euo pipefail
[ $# -ge 1 ] || { echo 'Aufruf: wine_vorab.sh <exe> [Argumente]' >&2; exit 2; }
cd "$(dirname "$0")/../../.."
prefix="$PWD/build/p2/wine-prefix"
mkdir -p "$prefix"
laufzeit="$(mktemp -d)"
trap 'rm -rf "$laufzeit"' EXIT
exe="$1"; shift
set +e
env -u DISPLAY -u WAYLAND_DISPLAY \
    XDG_RUNTIME_DIR="$laufzeit" \
    WINEPREFIX="$prefix" \
    WINEDEBUG=-all \
    WINEDLLOVERRIDES='winedbg.exe=d;winemenubuilder.exe=d;mscoree=d;mshtml=d' \
    timeout 120s wine "$exe" "$@"
rc=$?
set -e
exit "$rc"
