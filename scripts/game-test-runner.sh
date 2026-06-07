#!/usr/bin/env bash
# ============================================================
# scripts/game-test-runner.sh — visuelle Game-Test-Suite (Plan-008 A4)
#
# Faehrt die drei Referenz-Selftests (2D / 3D-Welt / Voxel) headless
# (xvfb) ODER auf einem echten Display durch. Jeder Selftest folgt dem
# gleichen Schema: Fenster starten -> test_sim_*-Eingaben -> Zustands-/
# Pixel-Asserts (test_pixel) -> Screenshot -> sauberer Exit, und gibt eine
# Zeile "SELFTEST_RESULT: PASS|FAIL <name>" aus.
#
# Der Runner sammelt die Artefakte (Screenshots, optionale GIFs) und
# schreibt einen JSON-Report mit Artefaktpfaden + pass/fail pro Test, damit
# andere KIs die Ergebnisse automatisiert finden.
#
# Selftests:
#   2D       beispiele/snake_plus_selftest.moo              (SDL-2D)
#   3D-Welt  beispiele/domain/game/world/siedler3_selftest.moo (Hybrid gl33)
#   Voxel    beispiele/voxel_sandbox_selftest.moo           (Hybrid gl33 + Voxel)
#
# Headless-Support:
#   - DISPLAY gesetzt  -> direkt dort fahren (echte GPU).
#   - sonst            -> jeder Lauf via xvfb-run -a (eigener virt. Screen).
#
# Backend: MOO_3D_BACKEND (Default gl33) fuer die 3D/Voxel-Tests.
#
# Exit 0 wenn alle Selftests gruen, 1 sonst.
#
# Hinweis: Dies ist der GPU/visuelle Pfad. Die kleine, GPU-unabhaengige
# CI-Smoke-Suite ist compiler/tests/run_all.sh (bleibt headless-stabil).
# ============================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
cd "$ROOT" || exit 2

COMPILER="$ROOT/compiler/target/release/moo-compiler"
BACKEND="${MOO_3D_BACKEND:-gl33}"
ART_ROOT="${GAME_TEST_OUT:-$ROOT/beispiele/snapshots/game-test}"
REPORT="$ART_ROOT/game_test_report.json"
SCREEN_SPEC="-screen 0 1280x800x24"

mkdir -p "$ART_ROOT"

# -------------------- Compiler bauen ------------------------
# Default-Features (gl33+vulkan+moo_ui+voxel) sind Pflicht, sonst fehlen die
# test_*-Builtins und moo_voxel.c im Runtime-Archiv.
echo "[runner] Baue Compiler (Default-Features inkl. 3D/Voxel/Test-API)..."
if ! (cd "$ROOT/compiler" && cargo build --release >/dev/null 2>&1); then
    echo "[runner] FEHLER: Compiler-Build fehlgeschlagen." >&2
    exit 2
fi
if [ ! -x "$COMPILER" ]; then
    echo "[runner] FEHLER: Compiler-Binary fehlt: $COMPILER" >&2
    exit 2
fi

# -------------------- Headless-Entscheidung -----------------
HEADLESS=0
if [ -z "${DISPLAY:-}" ]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "[runner] FEHLER: Kein DISPLAY und xvfb-run fehlt." >&2
        exit 2
    fi
    HEADLESS=1
    echo "[runner] Kein DISPLAY -> headless via xvfb-run."
else
    echo "[runner] DISPLAY=$DISPLAY -> echtes Display."
fi
echo "[runner] Backend=$BACKEND  Artefakte=$ART_ROOT"

# -------------------- ffmpeg-Erkennung (MP4-Artefakte) ------
# MP4-Aufnahme ist environment-conditional (Plan-009 V2). Fehlt ffmpeg, wird
# der Video-Selftest als "skipped" markiert — der Runner laeuft normal weiter.
# WICHTIG: run_all.sh bleibt ffmpeg-/GPU-frei; MP4 lebt NUR in diesem Pfad.
HAVE_FFMPEG=0
HAVE_FFPROBE=0
if command -v ffmpeg >/dev/null 2>&1; then
    HAVE_FFMPEG=1
    echo "[runner] ffmpeg gefunden -> MP4-Artefakte aktiv."
else
    echo "[runner] ffmpeg fehlt -> MP4-Selftests werden uebersprungen (skipped)."
fi
if command -v ffprobe >/dev/null 2>&1; then
    HAVE_FFPROBE=1
fi

# -------------------- Test-Definitionen ---------------------
# name | moo-datei | braucht_3d_backend (1=ja) | will_mp4 (1=ja, Plan-009 V2)
# will_mp4=1 markiert Selftests, die per test_video_* ein MP4 erzeugen. Fehlt
# ffmpeg, wird der Test als video_status=skipped uebersprungen (kein Fehler).
TESTS=(
    "snake_plus_2d|beispiele/snake_plus_selftest.moo|0|0"
    "siedler3_3d|beispiele/domain/game/world/siedler3_selftest.moo|1|0"
    "voxel_sandbox|beispiele/voxel_sandbox_selftest.moo|1|0"
    "voxel_kamera_video|beispiele/voxel_kamera_video_selftest.moo|1|1"
)

declare -a RESULT_JSON=()
PASS=0
FAIL=0

run_one() {
    local name="$1" moo_file="$2" needs3d="$3" wants_video="${4:-0}"
    local log="$ART_ROOT/${name}.log"
    local shot="$ART_ROOT/${name}.bmp"
    local gif="$ART_ROOT/${name}.gif"
    local video="$ART_ROOT/${name}.mp4"

    # MP4-Default: kein Video angefordert -> video_status "none".
    local video_status="none"
    local video_reason=""
    local video_path=""
    local video_frames=""

    echo ""
    echo "==========================================================="
    echo "[runner] Selftest: $name  ($moo_file)"
    echo "==========================================================="

    if [ ! -f "$moo_file" ]; then
        echo "[runner] FEHLER: $moo_file fehlt"
        RESULT_JSON+=("$(test_json "$name" "$moo_file" "fail" "datei-fehlt" "" "" "$log" "none" "" "" "")")
        return 1
    fi

    # Environment-conditional MP4: Wenn der Selftest ein Video aufnimmt
    # (test_video_*), aber ffmpeg fehlt, wuerde test_video_start moo_throw
    # ausloesen und den Prozess rot machen. Deshalb ueberspringen wir den
    # Video-Selftest sauber, BEVOR wir ihn starten. Der Runner bleibt gruen.
    if [ "$wants_video" -eq 1 ] && [ "$HAVE_FFMPEG" -ne 1 ]; then
        echo "[runner] $name: SKIP (ffmpeg fehlt -> video_status=skipped)"
        RESULT_JSON+=("$(test_json "$name" "$moo_file" "skip" "ffmpeg-missing" "" "" "$log" "skipped" "ffmpeg-missing" "" "")")
        return 0
    fi

    rm -f "$shot" "$log" 2>/dev/null || true

    # Env fuer den Selftest: Artefakt-Ausgabe + Backend.
    # Der Selftest schreibt seinen Screenshot nach $SELFTEST_OUT/<name-prefix>.bmp;
    # wir geben ART_ROOT vor und benennen danach auf den kanonischen Namen um.
    local env_prefix=(env "SELFTEST_OUT=$ART_ROOT" "MOO_3D_BACKEND=$BACKEND")

    local rc=0
    if [ "$HEADLESS" -eq 1 ]; then
        timeout 120 xvfb-run -a -s "$SCREEN_SPEC" \
            "${env_prefix[@]}" "$COMPILER" run "$moo_file" >"$log" 2>&1
        rc=$?
    else
        timeout 120 "${env_prefix[@]}" "$COMPILER" run "$moo_file" >"$log" 2>&1
        rc=$?
    fi

    # Selbst-deklariertes Ergebnis aus der Ausgabe lesen.
    local declared
    declared="$(grep -E '^SELFTEST_RESULT:' "$log" | tail -1)"

    # Vom Selftest geschriebene Screenshots auf kanonischen Namen mappen.
    local produced=""
    for cand in \
        "$ART_ROOT/snake_plus_selftest.bmp" \
        "$ART_ROOT/siedler3_selftest.bmp" \
        "$ART_ROOT/voxel_sandbox_selftest.bmp"; do
        case "$name:$cand" in
            snake_plus_2d:*snake_plus_selftest.bmp) [ -f "$cand" ] && mv -f "$cand" "$shot" && produced="$shot" ;;
            siedler3_3d:*siedler3_selftest.bmp)     [ -f "$cand" ] && mv -f "$cand" "$shot" && produced="$shot" ;;
            voxel_sandbox:*voxel_sandbox_selftest.bmp) [ -f "$cand" ] && mv -f "$cand" "$shot" && produced="$shot" ;;
        esac
    done

    # Optionale GIF-Artefakte (Plan-008 A3B). Wenn der Selftest ein GIF
    # ablegt, wird es eingesammelt; fehlt es, ist das KEIN Fehler (GIF ist
    # optional bis A3B verdrahtet ist).
    local gif_path=""
    [ -f "$gif" ] && gif_path="$gif"

    # MP4-Artefakt (Plan-009 V2). Der Video-Selftest schreibt sein MP4 nach
    # $SELFTEST_OUT/<name>.mp4 (= $ART_ROOT/<name>.mp4). Wir sammeln es ein,
    # setzen video_status und verifizieren optional via ffprobe, dass es
    # H.264/yuv420p ist. ffprobe ist NICHT zwingend; fehlt es, gilt das
    # vorhandene MP4 als "ok" ohne Codec-Pruefung.
    if [ "$wants_video" -eq 1 ]; then
        if [ -f "$video" ]; then
            video_path="$video"
            if [ "$HAVE_FFPROBE" -eq 1 ]; then
                local probe
                probe="$(ffprobe -v error -select_streams v:0 \
                    -show_entries stream=codec_name,pix_fmt \
                    -of default=noprint_wrappers=1:nokey=1 "$video" 2>/dev/null | tr '\n' ' ')"
                echo "[runner] $name: ffprobe -> $probe"
                # Optionaler Zusatzcheck (Plan-009 REST): exakte Framezahl via
                # ffprobe -count_frames. Dekodiert das ganze File; nur Best-effort,
                # Fehler/leeres Ergebnis sind KEIN harter Fehler (video_frames bleibt leer).
                local frames
                frames="$(ffprobe -v error -count_frames -select_streams v:0 \
                    -show_entries stream=nb_read_frames \
                    -of default=noprint_wrappers=1:nokey=1 "$video" 2>/dev/null | tr -dc '0-9')"
                if [ -n "$frames" ]; then
                    video_frames="$frames"
                    echo "[runner] $name: ffprobe -count_frames -> $frames Frames"
                fi
                if printf '%s' "$probe" | grep -q 'h264' && printf '%s' "$probe" | grep -q 'yuv420p'; then
                    video_status="ok"
                else
                    video_status="failed"
                    video_reason="codec-unerwartet: $probe"
                fi
            else
                video_status="ok"
                video_reason="ffprobe-fehlt-keine-codec-pruefung"
            fi
        else
            # ffmpeg war da, aber es kam kein MP4 raus -> echter Fehler.
            video_status="failed"
            video_reason="kein-mp4-erzeugt"
        fi
    fi

    local status="fail"
    local reason=""
    if [ "$rc" -ne 0 ]; then
        reason="exit-code-$rc"
        echo "[runner] $name: Prozess rc=$rc"
        tail -n 15 "$log" || true
    elif printf '%s' "$declared" | grep -q '^SELFTEST_RESULT: PASS'; then
        status="pass"
    else
        reason="kein-PASS-marker"
        echo "[runner] $name: kein PASS-Marker (declared='$declared')"
        tail -n 15 "$log" || true
    fi

    if [ "$status" = "pass" ]; then
        echo "[runner] $name: PASS  (screenshot: ${produced:-<keiner>})"
        PASS=$((PASS + 1))
    else
        echo "[runner] $name: FAIL  ($reason)"
        FAIL=$((FAIL + 1))
    fi

    if [ "$wants_video" -eq 1 ]; then
        echo "[runner] $name: video_status=$video_status  (${video_path:-<kein-mp4>})"
    fi

    RESULT_JSON+=("$(test_json "$name" "$moo_file" "$status" "$reason" "$produced" "$gif_path" "$log" "$video_status" "$video_reason" "$video_path" "$video_frames")")
    [ "$status" = "pass" ]
}

# Baut ein JSON-Objekt fuer einen Test ueber python3 (sichere Escapes).
test_json() {
    python3 - "$@" <<'PY'
import json, sys, os
name, moo_file, status, reason, shot, gif, log = sys.argv[1:8]
video_status = sys.argv[8] if len(sys.argv) > 8 else "none"
video_reason = sys.argv[9] if len(sys.argv) > 9 else ""
video       = sys.argv[10] if len(sys.argv) > 10 else ""
video_frames = sys.argv[11] if len(sys.argv) > 11 else ""
def rel(p):
    return os.path.relpath(p) if p else None
def asserts(logpath):
    ok = fail = 0
    if logpath and os.path.isfile(logpath):
        with open(logpath, "r", errors="replace") as f:
            for line in f:
                if line.startswith("ASSERT OK"):   ok += 1
                elif line.startswith("ASSERT FAIL"): fail += 1
    return ok, fail
ok, bad = asserts(log)
obj = {
    "name": name,
    "moo_file": moo_file,
    "status": status,
    "reason": reason or None,
    "screenshot": rel(shot) if shot else None,
    "gif": rel(gif) if gif else None,
    "video": rel(video) if video else None,
    "video_status": video_status,
    "video_reason": video_reason or None,
    "video_frames": int(video_frames) if video_frames.strip().isdigit() else None,
    "log": rel(log) if log else None,
    "asserts_ok": ok,
    "asserts_fail": bad,
}
print(json.dumps(obj))
PY
}

# -------------------- Tests laufen lassen -------------------
for entry in "${TESTS[@]}"; do
    IFS='|' read -r t_name t_file t_3d t_video <<<"$entry"
    run_one "$t_name" "$t_file" "$t_3d" "${t_video:-0}" || true
done

# -------------------- Report schreiben ----------------------
python3 - "$REPORT" "$BACKEND" "$HEADLESS" "$PASS" "$FAIL" "${RESULT_JSON[@]}" <<'PY'
import json, sys, datetime
from datetime import timezone
report_path = sys.argv[1]
backend     = sys.argv[2]
headless    = sys.argv[3] == "1"
n_pass      = int(sys.argv[4])
n_fail      = int(sys.argv[5])
results     = [json.loads(r) for r in sys.argv[6:] if r.strip()]
report = {
    "suite": "game-test-runner",
    "plan": "Plan-008 A4",
    "timestamp": datetime.datetime.now(timezone.utc).replace(microsecond=0, tzinfo=None).isoformat() + "Z",
    "backend": backend,
    "headless": headless,
    "passed_count": n_pass,
    "failed_count": n_fail,
    "all_passed": n_fail == 0,
    "results": results,
}
with open(report_path, "w") as f:
    json.dump(report, f, indent=2, ensure_ascii=False)
print("[runner] JSON-Report: " + report_path)
PY

echo ""
echo "==========================================================="
echo "[runner] Zusammenfassung: $PASS gruen / $FAIL rot"
echo "[runner] Report: $REPORT"
echo "==========================================================="

[ "$FAIL" -eq 0 ]
