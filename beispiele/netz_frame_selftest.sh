#!/usr/bin/env bash
# ============================================================
# netz_frame_selftest.sh — Loopback-Selftest fuer netz_frame.moos
# ============================================================
# Startet Server und Client als zwei localhost-Prozesse und prueft
# auf "SELFTEST PASS". Port ueber NF_PORT (Default 5399).
# Kompiliert aus dem Repo-Root, damit `importiere netz_frame` die
# Datei stdlib/netz_frame.moos ueber cwd/stdlib aufloest.
# ============================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPILER="$ROOT/compiler/target/release/moo-compiler"
PORT="${NF_PORT:-5399}"
BIN="$(mktemp /tmp/nf_selftest.XXXXXX)"
SRVLOG="$(mktemp)"
CLILOG="$(mktemp)"
SRVPID=""

aufraeumen() {
    [ -n "$SRVPID" ] && kill "$SRVPID" 2>/dev/null
    rm -f "$BIN" "$SRVLOG" "$CLILOG"
}
trap aufraeumen EXIT

cd "$ROOT" || { echo "cd $ROOT fehlgeschlagen"; exit 2; }

if [ ! -x "$COMPILER" ]; then
    echo "Compiler nicht gefunden: $COMPILER"
    exit 2
fi

"$COMPILER" compile beispiele/netz_frame_selftest.moos -o "$BIN" >/dev/null 2>&1 || {
    echo "COMPILE FAIL"
    "$COMPILER" compile beispiele/netz_frame_selftest.moos -o "$BIN"
    exit 2
}

NF_ROLLE=server NF_PORT="$PORT" "$BIN" > "$SRVLOG" 2>&1 &
SRVPID=$!
sleep 0.6
NF_ROLLE=client NF_PORT="$PORT" "$BIN" > "$CLILOG" 2>&1
CLIRC=$?
wait "$SRVPID" 2>/dev/null
SRVPID=""

echo "=== SERVER ==="
cat "$SRVLOG"
echo "=== CLIENT (rc=$CLIRC) ==="
cat "$CLILOG"

if grep -q "SELFTEST PASS" "$CLILOG"; then
    echo "RESULT: PASS"
    exit 0
fi
echo "RESULT: FAIL"
exit 1
