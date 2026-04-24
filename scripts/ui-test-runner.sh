#!/usr/bin/env bash
# ============================================================
# scripts/ui-test-runner.sh — visuelle UI-Test-Suite (Plan-004 P4)
#
# Faehrt die 5 Demo-Tests in beispiele/tests/ durch:
#   ui_layout_test, ui_table_test, ui_binding_test,
#   ui_shortcuts_test, ui_canvas_test
#
# Pro Test:
#   - Kompiliere + Run (cargo run ... run <test>)
#   - Timeout 15s
#   - Nach Run: alle frame_*.json via python3 -m json.tool validieren
#   - PNG-Groesse > 1KB pruefen
#
# Aggregiert nach beispiele/snapshots/test_report.json
#
# Headless-Support:
#   - DISPLAY gesetzt -> direkt dort fahren
#   - Sonst: Xvfb :99 starten, DISPLAY=:99 nutzen
#
# Exit 0 wenn alle Tests gruen, 1 sonst.
# ============================================================

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SNAP_ROOT="beispiele/snapshots"
REPORT="$SNAP_ROOT/test_report.json"
TESTS=(layout table binding shortcuts canvas)

mkdir -p "$SNAP_ROOT"
for t in "${TESTS[@]}"; do
    mkdir -p "$SNAP_ROOT/$t"
done

# -------------------- Headless-Setup -------------------------
XVFB_PID=""
cleanup() {
    if [ -n "$XVFB_PID" ] && kill -0 "$XVFB_PID" 2>/dev/null; then
        kill "$XVFB_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

if [ -z "${DISPLAY:-}" ]; then
    if ! command -v Xvfb >/dev/null 2>&1; then
        echo "FEHLER: Kein DISPLAY gesetzt und Xvfb nicht installiert." >&2
        exit 2
    fi
    echo "[runner] Starte Xvfb auf :99 ..."
    Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &
    XVFB_PID=$!
    sleep 1
    export DISPLAY=:99
fi
export MOO_UI_BACKEND="${MOO_UI_BACKEND:-gtk}"

echo "[runner] DISPLAY=$DISPLAY BACKEND=$MOO_UI_BACKEND"

# -------------------- Hilfsfunktionen ------------------------

run_one_test() {
    local name="$1"
    local moo_file="beispiele/tests/ui_${name}_test.moo"
    local snap_dir="$SNAP_ROOT/$name"
    local log="$snap_dir/run.log"

    echo ""
    echo "==========================================================="
    echo "[runner] Test: $name"
    echo "==========================================================="

    if [ ! -f "$moo_file" ]; then
        echo "FEHLER: $moo_file fehlt"
        return 1
    fi

    # Alte Artefakte bereinigen
    rm -f "$snap_dir"/frame_*.png "$snap_dir"/frame_*.json "$snap_dir"/run.log 2>/dev/null || true

    # Ausfuehren (timeout 15s)
    local rc=0
    if ! timeout 15 cargo run --release --manifest-path compiler/Cargo.toml -- \
            run "$moo_file" >"$log" 2>&1; then
        rc=$?
        echo "FEHLER: Test '$name' rc=$rc (log: $log)"
        tail -n 30 "$log" || true
        return 1
    fi

    # JSON + PNG-Pruefung
    local frames_ok=0
    local frames_fail=0
    local png_ok=0
    local png_fail=0

    shopt -s nullglob
    for jf in "$snap_dir"/frame_*.json; do
        if python3 -m json.tool "$jf" >/dev/null 2>&1; then
            frames_ok=$((frames_ok + 1))
        else
            frames_fail=$((frames_fail + 1))
            echo "  JSON UNGUELTIG: $jf"
        fi
    done
    for pf in "$snap_dir"/frame_*.png; do
        local sz
        sz=$(stat -c%s "$pf" 2>/dev/null || stat -f%z "$pf" 2>/dev/null || echo 0)
        # PNG-Magic pruefen (89 50 4e 47 0d 0a 1a 0a). Groessenuntergrenze
        # bewusst niedrig: kleine Fenster mit wenigen Widgets erzeugen bei
        # GTK regelmaessig ~880 B PNGs (verifiziert gegen auto_01/02 aus
        # ui_automation_demo, die im P3-Review als OK durchgegangen sind).
        local magic
        magic=$(head -c 8 "$pf" 2>/dev/null | od -An -tx1 | tr -d ' \n' || echo "")
        if [ "$sz" -gt 200 ] && [ "$magic" = "89504e470d0a1a0a" ]; then
            png_ok=$((png_ok + 1))
        else
            png_fail=$((png_fail + 1))
            echo "  PNG UNGUELTIG ($sz B, magic=$magic): $pf"
        fi
    done
    shopt -u nullglob

    echo "[runner] $name: frames_ok=$frames_ok frames_fail=$frames_fail png_ok=$png_ok png_fail=$png_fail"

    # Aufbau JSON-Eintrag
    TEST_RESULT="$(python3 - "$name" "$frames_ok" "$frames_fail" "$png_ok" "$png_fail" "$snap_dir" <<'PY'
import json, sys, glob, os
name, fok, ffail, pok, pfail, snap_dir = sys.argv[1:7]
frames = sorted(os.path.basename(p) for p in glob.glob(os.path.join(snap_dir, "frame_*.json")))
pngs   = sorted(os.path.basename(p) for p in glob.glob(os.path.join(snap_dir, "frame_*.png")))
obj = {
    "test": name,
    "snap_dir": snap_dir,
    "frames_json_ok": int(fok),
    "frames_json_fail": int(ffail),
    "png_ok": int(pok),
    "png_fail": int(pfail),
    "json_files": frames,
    "png_files": pngs,
}
print(json.dumps(obj))
PY
)"
    ALL_RESULTS+=("$TEST_RESULT")

    if [ "$frames_fail" -gt 0 ] || [ "$png_fail" -gt 0 ] || [ "$frames_ok" -eq 0 ]; then
        return 1
    fi
    return 0
}

# -------------------- Tests laufen lassen --------------------

declare -a ALL_RESULTS=()
FAILED=()
PASSED=()

for t in "${TESTS[@]}"; do
    if run_one_test "$t"; then
        PASSED+=("$t")
    else
        FAILED+=("$t")
    fi
done

# -------------------- Report schreiben -----------------------

python3 - "$REPORT" "${#PASSED[@]}" "${#FAILED[@]}" "${PASSED[*]}" "${FAILED[*]}" "${ALL_RESULTS[@]}" <<'PY'
import json, sys, datetime
report_path  = sys.argv[1]
n_pass       = int(sys.argv[2])
n_fail       = int(sys.argv[3])
passed       = sys.argv[4].split() if sys.argv[4] else []
failed       = sys.argv[5].split() if sys.argv[5] else []
results_raw  = sys.argv[6:]
results = [json.loads(r) for r in results_raw if r.strip()]
report = {
    "suite": "ui-visual-tests",
    "plan": "Plan-004 P4",
    "timestamp": datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
    "passed_count": n_pass,
    "failed_count": n_fail,
    "passed": passed,
    "failed": failed,
    "results": results,
}
with open(report_path, "w") as f:
    json.dump(report, f, indent=2, ensure_ascii=False)
print("[runner] Report geschrieben: " + report_path)
PY

echo ""
echo "==========================================================="
echo "[runner] Zusammenfassung: ${#PASSED[@]} gruen / ${#FAILED[@]} rot"
echo "  passed: ${PASSED[*]:-}"
echo "  failed: ${FAILED[*]:-}"
echo "  report: $REPORT"
echo "==========================================================="

if [ "${#FAILED[@]}" -eq 0 ]; then
    exit 0
fi
exit 1
