#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# scripts/native-ui-beweis.sh — NATIVE-UI-5 Beweis-Harness
#
# Beweist fuer ein UI-Binary EHRLICH und SICHTBAR:
#   (1) ldd-Negativbeweis: kein libgtk/libgdk im Prozess
#   (2) Backend-Name: Binary druckt "HOST-BACKEND <name>" und der Name
#       entspricht MOO_BEWEIS_BACKEND (falls gesetzt)
#   (3) EXTERNER Screenshot des offenen Fensters (kein Offscreen-Golden!
#       Das war der Abnahmefehler bei b79ab344.)
#
# Screenshot-Wege pro Umgebung:
#   Wayland (kwin): spectacle -b -n (Portal/KWin — kwin hat KEIN
#                   wlr-screencopy), Fallback: KWin-DBus Screenshot
#   X11/Xvfb:       import -window root (ImageMagick), Fallback xwd
#   kein Display:   HARTER ABBRUCH — dieser Harness ersetzt sichtbare
#                   Beweise niemals durch Offscreen-Hashes.
#
# Nutzung:
#   scripts/native-ui-beweis.sh <binary> [args...]
#   MOO_BEWEIS_BACKEND=wayland   erwarteter Backend-Name (optional)
#   MOO_BEWEIS_OUT=/pfad         Ausgabeverzeichnis (Default /tmp/...)
#   MOO_BEWEIS_WARTE_S=3         Wartezeit bis Screenshot
#
# Exit 0 nur wenn ALLE Beweise PASS sind. Mit dem heutigen GTK-Backend
# ist (1) by-design ROT — gruen wird der Harness erst mit einem echten
# nativen Backend (NATIVE-UI-1/2/7).
# ============================================================

BIN="${1:?Nutzung: native-ui-beweis.sh <binary> [args...]}"
shift || true
OUT="${MOO_BEWEIS_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/native-ui-beweis.XXXXXX")}"
ERWARTET="${MOO_BEWEIS_BACKEND:-}"
WARTE="${MOO_BEWEIS_WARTE_S:-3}"
mkdir -p "$OUT"

fail_gesamt=0

echo "== [1/3] ldd-Negativbeweis (kein GTK im Prozess) =="
if ldd "$BIN" | grep -Ei 'libgtk|libgdk' > "$OUT/ldd-gtk-treffer.txt"; then
    echo "NATIVE-UI-BEWEIS-LDD FAIL — GTK-Libs im Binary:"
    cat "$OUT/ldd-gtk-treffer.txt"
    fail_gesamt=1
else
    echo "NATIVE-UI-BEWEIS-LDD PASS — kein libgtk/libgdk"
fi

echo "== [2/3] Fenster starten + Backend-Name =="
if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ]; then
    echo "NATIVE-UI-BEWEIS-ABBRUCH: kein WAYLAND_DISPLAY/DISPLAY — dieser"
    echo "Harness braucht ein ECHTES Display; Offscreen ist kein Beweis."
    exit 2
fi
if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL -eL "$BIN" "$@" > "$OUT/prozess.log" 2>&1 &
else
    "$BIN" "$@" > "$OUT/prozess.log" 2>&1 &
fi
PID=$!
sleep "$WARTE"
if ! kill -0 "$PID" 2>/dev/null; then
    echo "NATIVE-UI-BEWEIS-START FAIL — Prozess beendet vor Screenshot:"
    tail -5 "$OUT/prozess.log"
    fail_gesamt=1
fi
BACKEND="$(grep -m1 '^HOST-BACKEND ' "$OUT/prozess.log" | cut -d' ' -f2- || true)"
if [ -n "$BACKEND" ]; then
    echo "NATIVE-UI-BEWEIS-BACKEND $BACKEND"
    if [ -n "$ERWARTET" ] && [ "$BACKEND" != "$ERWARTET" ]; then
        echo "NATIVE-UI-BEWEIS-BACKEND FAIL — erwartet '$ERWARTET', ist '$BACKEND'"
        fail_gesamt=1
    fi
else
    echo "NATIVE-UI-BEWEIS-BACKEND FAIL — kein 'HOST-BACKEND <name>' im Output"
    fail_gesamt=1
fi

echo "== [3/3] Externer Screenshot =="
SHOT="$OUT/beweis.png"
shot_ok=0
if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    if command -v spectacle >/dev/null 2>&1; then
        spectacle -b -n -o "$SHOT" >/dev/null 2>&1 || true
    fi
    if [ ! -s "$SHOT" ] && command -v qdbus >/dev/null 2>&1; then
        qdbus org.kde.KWin /Screenshot org.kde.kwin.Screenshot.screenshotFullscreen \
            > "$OUT/kwin-dbus.txt" 2>&1 || true
        SRC="$(cat "$OUT/kwin-dbus.txt" 2>/dev/null || true)"
        [ -f "$SRC" ] && cp "$SRC" "$SHOT" || true
    fi
else
    if command -v import >/dev/null 2>&1; then
        import -window root "$SHOT" >/dev/null 2>&1 || true
    fi
    if [ ! -s "$SHOT" ] && command -v xwd >/dev/null 2>&1; then
        xwd -root -out "$OUT/beweis.xwd" >/dev/null 2>&1 || true
        [ -s "$OUT/beweis.xwd" ] && SHOT="$OUT/beweis.xwd"
    fi
fi
if [ -s "$SHOT" ]; then
    echo "NATIVE-UI-BEWEIS-SCREENSHOT PASS $SHOT"
else
    echo "NATIVE-UI-BEWEIS-SCREENSHOT FAIL — kein Screenshot-Werkzeug erfolgreich"
    fail_gesamt=1
fi

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

if [ "$fail_gesamt" -eq 0 ]; then
    echo "NATIVE-UI-BEWEIS GESAMT PASS ($OUT)"
else
    echo "NATIVE-UI-BEWEIS GESAMT FAIL ($OUT)"
fi
exit "$fail_gesamt"
