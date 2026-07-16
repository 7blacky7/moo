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
#   - Nach Run: alle frame_*.json via "$PYTHON_BIN" -m json.tool validieren
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
TESTS=(layout table binding shortcuts canvas tray_lifecycle)

mkdir -p "$SNAP_ROOT"
for t in "${TESTS[@]}"; do
    mkdir -p "$SNAP_ROOT/$t"
done

# -------------------- Headless-Setup -------------------------
export MOO_UI_BACKEND="${MOO_UI_BACKEND:-gtk}"
# Standardmaessig NIEMALS das Host-DISPLAY verwenden. Der gemeinsame
# Harness waehlt einen freien privaten Xvfb und verwaltet alle Kindprozesse.
# Nur MOO_UI_ALLOW_HOST_DISPLAY=1 erlaubt ein bereits gesetztes DISPLAY.
source "$ROOT/scripts/ui-harness-lib.sh"
harness_rc=0
ui_harness_init "suite" || harness_rc=$?
if [ "$harness_rc" -ne 0 ]; then
    exit "$harness_rc"
fi
ui_harness_start_display || harness_rc=$?
if [ "$harness_rc" -ne 0 ]; then
    exit "$harness_rc"
fi

# Vorhandenen Release-Compiler bevorzugen. So bleibt der Visualtest von der
# Shell-PATH-Konfiguration unabhängig und baut nicht vor jedem Test neu.
if [ -n "${MOO_COMPILER:-}" ]; then
    COMPILER_CMD=("$MOO_COMPILER")
elif [ -x "compiler/target/release/moo-compiler" ]; then
    COMPILER_CMD=("compiler/target/release/moo-compiler")
elif command -v cargo >/dev/null 2>&1; then
    COMPILER_CMD=(cargo run --release --manifest-path compiler/Cargo.toml --)
else
    echo "FEHLER: Weder MOO_COMPILER noch Release-Compiler noch cargo verfügbar." >&2
    exit 2
fi

PYTHON_BIN="$(command -v python3 || command -v python || true)"
if [ -z "$PYTHON_BIN" ]; then
    echo "FEHLER: python3/python für JSON- und Visual-Gates fehlt." >&2
    exit 2
fi

echo "[runner] DISPLAY=${DISPLAY:-native} BACKEND=$MOO_UI_BACKEND COMPILER=${COMPILER_CMD[*]} PYTHON=$PYTHON_BIN"

# -------------------- Hilfsfunktionen ------------------------

run_one_test() {
    local name="$1"
    local moo_file="beispiele/tests/ui_${name}_test.moos"
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
    local compile_log="$snap_dir/compile.log"
    rm -f "$snap_dir"/frame_*.png "$snap_dir"/frame_*.json         "$snap_dir"/run.log "$compile_log" 2>/dev/null || true

    # Compile und Run sind absichtlich getrennt. So liegt kein wartender
    # moo-compiler-Parent zwischen Watchdog und UI-Prozess.
    local binary="$UI_HARNESS_RUN_DIR/ui_${name}"
    if [ "${OS:-}" = "Windows_NT" ]; then
        binary="${binary}.exe"
    fi

    local rc=0
    ui_harness_run 120 "$compile_log"         "${COMPILER_CMD[@]}" compile "$moo_file" --output "$binary" || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FEHLER: Kompilieren von '$name' rc=$rc (log: $compile_log)"
        tail -n 30 "$compile_log" || true
        if [ "$rc" -eq 125 ] || [ "$rc" -eq 126 ]; then
            HARNESS_FATAL="$rc"
            UI_HARNESS_KEEP_RUN_DIR=1
        fi
        return 1
    fi

    rc=0
    ui_harness_run 15 "$log" "$binary" || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FEHLER: Test '$name' rc=$rc (log: $log)"
        tail -n 30 "$log" || true
        if [ "$rc" -eq 125 ] || [ "$rc" -eq 126 ]; then
            HARNESS_FATAL="$rc"
            UI_HARNESS_KEEP_RUN_DIR=1
        fi
        return 1
    fi

    # JSON + PNG-Pruefung
    local frames_ok=0
    local frames_fail=0
    local png_ok=0
    local png_fail=0

    shopt -s nullglob
    for jf in "$snap_dir"/frame_*.json; do
        if "$PYTHON_BIN" -m json.tool "$jf" >/dev/null 2>&1; then
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

    # Wenn sich der logische Widget-Baum aendert, muss sich auch das PNG
    # aendern. Andernfalls wurde ein veralteter nativer Puffer fotografiert.
    local visual_metrics
    visual_metrics=$("$PYTHON_BIN" - "$snap_dir" <<'PY'
import glob, hashlib, json, os, sys
frames = []
for jf in sorted(glob.glob(os.path.join(sys.argv[1], "frame_*.json"))):
    with open(jf, encoding="utf-8") as f:
        data = json.load(f)
    pf = os.path.splitext(jf)[0] + ".png"
    digest = ""
    if os.path.exists(pf):
        with open(pf, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
    frames.append((data, digest))

changes = 0
stale = 0
for (prev, prev_hash), (cur, cur_hash) in zip(frames, frames[1:]):
    if prev.get("widget_tree") != cur.get("widget_tree"):
        changes += 1
        if prev_hash == cur_hash:
            stale += 1

sizes = []
for data, _ in frames:
    ws = data.get("window_size") or {}
    sizes.append((int(ws.get("b", 0)), int(ws.get("h", 0))))
runaway = 0
if len(sizes) >= 3 and all(w > 0 and h > 0 for w, h in sizes):
    areas = [w * h for w, h in sizes]
    if all(b > a for a, b in zip(areas, areas[1:])):
        runaway = 1
    # Der historische Tray-Bug war situationsabhängiges Expandieren. Für
    # diesen gezielten Lifecycle-Test ist daher jede Größenabweichung rot.
    if os.path.basename(sys.argv[1]) == "tray_lifecycle" and len(set(sizes)) != 1:
        runaway = 1
print(changes, stale, runaway)
PY
)
    local semantic_changes stale_visual runaway_geometry
    read -r semantic_changes stale_visual runaway_geometry <<<"$visual_metrics"

    echo "[runner] $name: frames_ok=$frames_ok frames_fail=$frames_fail png_ok=$png_ok png_fail=$png_fail semantic_changes=$semantic_changes stale_visual=$stale_visual runaway_geometry=$runaway_geometry"

    # Aufbau JSON-Eintrag
    TEST_RESULT="$("$PYTHON_BIN" - "$name" "$frames_ok" "$frames_fail" "$png_ok" "$png_fail" "$snap_dir" <<'PY'
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

    if [ "$frames_fail" -gt 0 ] || [ "$png_fail" -gt 0 ] || [ "$frames_ok" -eq 0 ] || [ "$stale_visual" -gt 0 ] || [ "$runaway_geometry" -gt 0 ]; then
        return 1
    fi
    return 0
}

# -------------------- Tests laufen lassen --------------------

declare -a ALL_RESULTS=()
FAILED=()
PASSED=()
HARNESS_FATAL=0

for t in "${TESTS[@]}"; do
    if run_one_test "$t"; then
        PASSED+=("$t")
    else
        FAILED+=("$t")
    fi
    if [ "$HARNESS_FATAL" -ne 0 ]; then
        echo "[runner] FATALER Harness-Fehler rc=$HARNESS_FATAL — keine weiteren UI-Tests."
        echo "[runner] Diagnose: $UI_HARNESS_RUN_DIR"
        exit "$HARNESS_FATAL"
    fi
done

# -------------------- Report schreiben -----------------------

"$PYTHON_BIN" - "$REPORT" "${#PASSED[@]}" "${#FAILED[@]}" "${PASSED[*]}" "${FAILED[*]}" "${ALL_RESULTS[@]}" <<'PY'
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
