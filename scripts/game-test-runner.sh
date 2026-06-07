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

# -------------------- Test-Definitionen ---------------------
# name | moo-datei | braucht_3d_backend (1=ja)
TESTS=(
    "snake_plus_2d|beispiele/snake_plus_selftest.moo|0"
    "siedler3_3d|beispiele/domain/game/world/siedler3_selftest.moo|1"
    "voxel_sandbox|beispiele/voxel_sandbox_selftest.moo|1"
)

declare -a RESULT_JSON=()
PASS=0
FAIL=0

run_one() {
    local name="$1" moo_file="$2" needs3d="$3"
    local log="$ART_ROOT/${name}.log"
    local shot="$ART_ROOT/${name}.bmp"
    local gif="$ART_ROOT/${name}.gif"

    echo ""
    echo "==========================================================="
    echo "[runner] Selftest: $name  ($moo_file)"
    echo "==========================================================="

    if [ ! -f "$moo_file" ]; then
        echo "[runner] FEHLER: $moo_file fehlt"
        RESULT_JSON+=("$(test_json "$name" "$moo_file" "fail" "datei-fehlt" "" "" "$log")")
        return 1
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

    RESULT_JSON+=("$(test_json "$name" "$moo_file" "$status" "$reason" "$produced" "$gif_path" "$log")")
    [ "$status" = "pass" ]
}

# Baut ein JSON-Objekt fuer einen Test ueber python3 (sichere Escapes).
test_json() {
    python3 - "$@" <<'PY'
import json, sys, os
name, moo_file, status, reason, shot, gif, log = sys.argv[1:8]
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
    "log": rel(log) if log else None,
    "asserts_ok": ok,
    "asserts_fail": bad,
}
print(json.dumps(obj))
PY
}

# -------------------- Tests laufen lassen -------------------
for entry in "${TESTS[@]}"; do
    IFS='|' read -r t_name t_file t_3d <<<"$entry"
    run_one "$t_name" "$t_file" "$t_3d" || true
done

# -------------------- Report schreiben ----------------------
python3 - "$REPORT" "$BACKEND" "$HEADLESS" "$PASS" "$FAIL" "${RESULT_JSON[@]}" <<'PY'
import json, sys, datetime
report_path = sys.argv[1]
backend     = sys.argv[2]
headless    = sys.argv[3] == "1"
n_pass      = int(sys.argv[4])
n_fail      = int(sys.argv[5])
results     = [json.loads(r) for r in sys.argv[6:] if r.strip()]
report = {
    "suite": "game-test-runner",
    "plan": "Plan-008 A4",
    "timestamp": datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
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
