#!/usr/bin/env bash
# P016-S1: kontrollierter, ausschliesslich headless ausgefuehrter Demo-Smoke.
# Kompiliert genau einmal und startet die Binary seriell in isolierten
# Prozessgruppen. Standard: 20 Wiederholungen.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

export MOO_UI_BACKEND="${MOO_UI_BACKEND:-gtk}"
ITERATIONS="${MOO_UI_DEMO_ITERATIONS:-20}"
case "$ITERATIONS" in
    ''|*[!0-9]*)
        echo "FEHLER: MOO_UI_DEMO_ITERATIONS muss eine positive Ganzzahl sein." >&2
        exit 2
        ;;
esac
if [ "$ITERATIONS" -lt 1 ]; then
    echo "FEHLER: MOO_UI_DEMO_ITERATIONS muss mindestens 1 sein." >&2
    exit 2
fi

source "$ROOT/scripts/ui-harness-lib.sh"
harness_rc=0
ui_harness_init "demo-smoke" || harness_rc=$?
if [ "$harness_rc" -ne 0 ]; then
    exit "$harness_rc"
fi
ui_harness_start_display || harness_rc=$?
if [ "$harness_rc" -ne 0 ]; then
    exit "$harness_rc"
fi

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

BINARY="$UI_HARNESS_RUN_DIR/ui_moo_demo"
if [ "${OS:-}" = "Windows_NT" ]; then
    BINARY="${BINARY}.exe"
fi
COMPILE_LOG="$UI_HARNESS_RUN_DIR/compile.log"

rc=0
ui_harness_run 120 "$COMPILE_LOG"     "${COMPILER_CMD[@]}" compile beispiele/ui_moo_demo.moo --output "$BINARY" || rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FEHLER: Demo-Kompilierung rc=$rc" >&2
    tail -n 40 "$COMPILE_LOG" >&2 || true
    if [ "$rc" -eq 125 ] || [ "$rc" -eq 126 ]; then
        UI_HARNESS_KEEP_RUN_DIR=1
    fi
    exit "$rc"
fi

i=1
while [ "$i" -le "$ITERATIONS" ]; do
    log="$UI_HARNESS_RUN_DIR/run_${i}.log"
    rc=0
    ui_harness_run 10 "$log"         env UIMOO_DEMO_AUTO=1 MOO_UI_BACKEND="$MOO_UI_BACKEND" "$BINARY" || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FEHLER: Demo-Lauf $i/$ITERATIONS rc=$rc" >&2
        tail -n 40 "$log" >&2 || true
        if [ "$rc" -eq 125 ] || [ "$rc" -eq 126 ]; then
            UI_HARNESS_KEEP_RUN_DIR=1
        fi
        exit "$rc"
    fi
    if ! grep -Fxq "UIMOO-DEMO-OK" "$log"; then
        echo "FEHLER: Demo-Lauf $i/$ITERATIONS ohne UIMOO-DEMO-OK" >&2
        tail -n 40 "$log" >&2 || true
        exit 1
    fi
    echo "[demo-smoke] Lauf $i/$ITERATIONS: OK"
    i=$((i + 1))
done

echo "[demo-smoke] $ITERATIONS/$ITERATIONS kontrollierte Laeufe ohne Timeout oder Rest-Prozessgruppe."
