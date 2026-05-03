#!/bin/bash
# Drag-Test in Xvfb - isoliert von User-Session.
LOG="$1"
GDK_BACKEND=x11 /tmp/list_drag_live > "$LOG.out" 2>&1 &
PID=$!
sleep 1.8

WID=$(xdotool search --name "Drag-Diag-Live" 2>/dev/null | head -1)
echo "WID=$WID PID=$PID DISPLAY=$DISPLAY" > "$LOG"

if [ -z "$WID" ]; then
    echo "FAIL: kein Window in Xvfb gefunden" >> "$LOG"
    kill $PID 2>/dev/null
    exit 1
fi

eval $(xdotool getwindowgeometry --shell "$WID" 2>/dev/null | grep -E '^(X|Y|WIDTH|HEIGHT)=')
echo "geom: X=$X Y=$Y W=$WIDTH H=$HEIGHT" >> "$LOG"

# col0 hat aktuell ~50px (siehe headless-Test). Trenner-X innerhalb Window:
# 10 (offset) + 50 (col0 width) = 60. Header-Y ist ~20.
# Aber Allokation skaliert: col2=580, also Header ueber gesamte Window-Breite.
# Drag bei 60+X auf X-Achse, Y in Header-Bereich.
GRIP_X=$((X + 10 + 50))
GRIP_Y=$((Y + 30))

echo "drag (${GRIP_X},${GRIP_Y}) -> +100" >> "$LOG"
xdotool mousemove --sync $GRIP_X $GRIP_Y 2>>"$LOG"
sleep 0.2
xdotool mousedown 1 2>>"$LOG"
sleep 0.2
for i in 20 40 60 80 100 120 140; do
    xdotool mousemove --sync $((GRIP_X + i)) $GRIP_Y 2>>"$LOG"
    sleep 0.1
done
xdotool mouseup 1 2>>"$LOG"
echo "drag done" >> "$LOG"

wait $PID 2>/dev/null
echo "== MOO-PROG STDOUT ==" >> "$LOG"
cat "$LOG.out" >> "$LOG"
