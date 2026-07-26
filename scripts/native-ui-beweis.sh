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
#   Wayland/KWin:    Prozessfenster per PID fokussieren, danach spectacle -a
#                   (aktives Fenster, kein Desktop/keine fremden Fenster)
#   X11/Xvfb:       Fenster-ID aus Prozess-PID, import -window <id>;
#                   Fallback spectacle -a nach windowactivate
#   kein Display:   HARTER ABBRUCH — dieser Harness ersetzt sichtbare
#                   Beweise niemals durch Offscreen-Hashes.
#
# Nutzung:
#   scripts/native-ui-beweis.sh <binary> [args...]
#   MOO_BEWEIS_BACKEND=wayland   erwarteter Backend-Name (optional)
#   MOO_BEWEIS_OUT=/pfad         Ausgabeverzeichnis (Default /tmp/...)
#   MOO_BEWEIS_WARTE_S=3         Wartezeit bis Screenshot
#   MOO_BEWEIS_ERLAUBE_TOOLKIT=1 GTK/GDK fuer bewusst native UI-Shell
#
# Exit 0 nur wenn ALLE Beweise PASS sind. Ohne explizites Toolkit-Opt-in
# bleibt der ldd-Negativbeweis fail-closed fuer native Host-Backends.
# ============================================================

BIN="${1:?Nutzung: native-ui-beweis.sh <binary> [args...]}"
shift || true
OUT="${MOO_BEWEIS_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/native-ui-beweis.XXXXXX")}"
ERWARTET="${MOO_BEWEIS_BACKEND:-}"
WARTE="${MOO_BEWEIS_WARTE_S:-3}"
TOOLKIT_ERLAUBT="${MOO_BEWEIS_ERLAUBE_TOOLKIT:-0}"
mkdir -p "$OUT"

fail_gesamt=0

echo "== [1/3] Abhaengigkeitsbeweis =="
if ldd "$BIN" | grep -Ei 'libgtk|libgdk' > "$OUT/ldd-gtk-treffer.txt"; then
    wenn_toolkit_erlaubt="$TOOLKIT_ERLAUBT"
    if [ "$wenn_toolkit_erlaubt" = "1" ]; then
        echo "NATIVE-UI-BEWEIS-LDD PASS — GTK/GDK fuer native Fenstershell explizit erlaubt"
    else
        echo "NATIVE-UI-BEWEIS-LDD FAIL — GTK-Libs im Binary:"
        cat "$OUT/ldd-gtk-treffer.txt"
        fail_gesamt=1
    fi
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

echo "== [3/3] Externer window-only Screenshot =="
SHOT="$OUT/beweis.png"
SHOT_NEU="$OUT/beweis.neu.png"
rm -f "$SHOT_NEU"

# Das Moo-Fenster vor spectacle -a gezielt aktivieren. Unter KWin/Wayland
# geschieht das per kurzlebigem KWin-Skript und PID; unter X11 per xdotool.
if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    QDBUS=""
    command -v qdbus6 >/dev/null 2>&1 && QDBUS="qdbus6"
    [ -z "$QDBUS" ] && command -v qdbus >/dev/null 2>&1 && QDBUS="qdbus"
    if [ -n "$QDBUS" ]; then
        KWIN_PLUGIN="moo-beweis-focus-$PID"
        KWIN_SCRIPT="$OUT/kwin-focus-$PID.js"
        printf '%s\n' \
            "var targetPid = $PID;" \
            'var windows = workspace.windowList ? workspace.windowList() : workspace.clientList();' \
            'for (var i = 0; i < windows.length; ++i) {' \
            '  var w = windows[i];' \
            '  if (Number(w.pid) === targetPid) {' \
            '    if (workspace.raiseWindow) workspace.raiseWindow(w);' \
            '    if (typeof workspace.activeWindow !== "undefined") workspace.activeWindow = w;' \
            '    else workspace.activeClient = w;' \
            '    break;' \
            '  }' \
            '}' > "$KWIN_SCRIPT"
        "$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript \
            "$KWIN_SCRIPT" "$KWIN_PLUGIN" > "$OUT/kwin-focus-load.txt" 2>&1 || true
        "$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.start \
            >> "$OUT/kwin-focus-load.txt" 2>&1 || true
        sleep 0.5
        "$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript \
            "$KWIN_PLUGIN" >> "$OUT/kwin-focus-load.txt" 2>&1 || true
    fi
    if command -v spectacle >/dev/null 2>&1; then
        spectacle -a -b -n -o "$SHOT_NEU" >/dev/null 2>&1 || true
    fi
else
    WINDOW_ID=""
    if command -v xdotool >/dev/null 2>&1; then
        WINDOW_ID="$(xdotool search --pid "$PID" 2>/dev/null | tail -n1 || true)"
        if [ -n "$WINDOW_ID" ]; then
            xdotool windowactivate --sync "$WINDOW_ID" >/dev/null 2>&1 || true
            sleep 0.2
        fi
    fi
    if [ -n "$WINDOW_ID" ] && command -v import >/dev/null 2>&1; then
        import -window "$WINDOW_ID" "$SHOT_NEU" >/dev/null 2>&1 || true
    fi
    if [ ! -s "$SHOT_NEU" ] && command -v spectacle >/dev/null 2>&1; then
        spectacle -a -b -n -o "$SHOT_NEU" >/dev/null 2>&1 || true
    fi
fi
if [ -s "$SHOT_NEU" ]; then
    mv -f "$SHOT_NEU" "$SHOT"
    cp "$SHOT" "$OUT/beweis-phase-a.png"
    sleep "${MOO_BEWEIS_PHASE_PAUSE_S:-0.35}"
    PHASE_B_NEU="$OUT/beweis-phase-b.neu.png"
    rm -f "$PHASE_B_NEU"
    if [ -n "${WAYLAND_DISPLAY:-}" ] && command -v spectacle >/dev/null 2>&1; then
        spectacle -a -b -n -o "$PHASE_B_NEU" >/dev/null 2>&1 || true
    elif [ -n "${WINDOW_ID:-}" ] && command -v import >/dev/null 2>&1; then
        import -window "$WINDOW_ID" "$PHASE_B_NEU" >/dev/null 2>&1 || true
    fi
    if [ -s "$PHASE_B_NEU" ]; then
        mv -f "$PHASE_B_NEU" "$OUT/beweis-phase-b.png"
        echo "NATIVE-UI-BEWEIS-PHASEN PASS $OUT/beweis-phase-a.png $OUT/beweis-phase-b.png"
    else
        echo "NATIVE-UI-BEWEIS-PHASEN FAIL — zweites window-only Bild fehlt"
        fail_gesamt=1
    fi
    if command -v identify >/dev/null 2>&1; then
        echo "NATIVE-UI-BEWEIS-FENSTER-GEOMETRIE $(identify -format '%wx%h' "$SHOT" 2>/dev/null || echo unbekannt)"
    fi
    echo "NATIVE-UI-BEWEIS-SCREENSHOT PASS $SHOT"
else
    echo "NATIVE-UI-BEWEIS-SCREENSHOT FAIL — kein window-only Screenshot-Werkzeug erfolgreich"
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
