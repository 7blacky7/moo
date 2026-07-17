#!/usr/bin/env bash
# ============================================================
# ki_verteilt_run.sh — Gate-Runner fuer ki_verteilt.moos (KIP-X1b A)
# ============================================================
# GATE 1a: Leader-Loss(Block1)  == lokale Referenz Block 1  (f32-BITS)
# GATE 1b: Follower-Loss(Block2) == lokale Referenz Block 2  (f32-BITS)
# GATE 2 : Parameter-Fingerprint Leader==Follower alle 100 Steps
# GATE 3 : falscher Seed -> HELLO-Abbruch (Negativtest)
# GATE 4 : Wallclock lokal vs. verteilt (ehrlich protokolliert)
# ============================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPILER="$ROOT/compiler/target/release/moo-compiler"
PORT="${MOO_DIST_PORT:-5299}"
BIN="$(mktemp /tmp/ki_verteilt.XXXXXX)"
OUT="$(mktemp -d /tmp/kivx.XXXXXX)"
SRVPID=""

aufraeumen() {
    [ -n "$SRVPID" ] && kill "$SRVPID" 2>/dev/null
    rm -rf "$BIN" "$OUT"
}
trap aufraeumen EXIT
cd "$ROOT" || exit 2

echo "== kompiliere ki_verteilt.moos =="
"$COMPILER" compile beispiele/ki_verteilt.moos -o "$BIN" || { echo "COMPILE FAIL"; exit 2; }

# ---------------- Lauf 1: lokale Referenz (GATE 4 lokal) ----------------
echo "== lokale Referenz =="
T0=$(date +%s.%N)
MOO_DIST_ROLLE=lokal "$BIN" > "$OUT/ref.log" 2>&1
T1=$(date +%s.%N)
WALL_LOKAL=$(awk "BEGIN{printf \"%.2f\", $T1-$T0}")

# ---------------- Lauf 2: verteilt (Leader + Follower) ------------------
echo "== verteilt (Leader+Follower) =="
T0=$(date +%s.%N)
MOO_DIST_ROLLE=leader MOO_DIST_PORT="$PORT" "$BIN" > "$OUT/leader.log" 2>&1 &
SRVPID=$!
sleep 0.5
MOO_DIST_ROLLE=follower MOO_DIST_HOST=127.0.0.1 MOO_DIST_PORT="$PORT" "$BIN" > "$OUT/follower.log" 2>&1
wait "$SRVPID" 2>/dev/null
SRVPID=""
T1=$(date +%s.%N)
WALL_VERT=$(awk "BEGIN{printf \"%.2f\", $T1-$T0}")

# ---------------- Gate-Auswertung ----------------
grep '^R1BITS' "$OUT/ref.log"    | awk '{print $2, $3}' | sort -n > "$OUT/r1.txt"
grep '^R2BITS' "$OUT/ref.log"    | awk '{print $2, $3}' | sort -n > "$OUT/r2.txt"
grep '^L1BITS' "$OUT/leader.log" | awk '{print $2, $3}' | sort -n > "$OUT/l1.txt"
grep '^F2BITS' "$OUT/follower.log" | awk '{print $2, $3}' | sort -n > "$OUT/f2.txt"

N_R1=$(wc -l < "$OUT/r1.txt"); N_L1=$(wc -l < "$OUT/l1.txt")
N_R2=$(wc -l < "$OUT/r2.txt"); N_F2=$(wc -l < "$OUT/f2.txt")

fails=0

echo ""
echo "==================== GATES ===================="
# GATE 1a
if [ "$N_R1" -ge 200 ] && [ "$N_L1" -ge 200 ] && diff -q "$OUT/r1.txt" "$OUT/l1.txt" >/dev/null; then
    echo "GATE 1a PASS  (Leader Block1 == Referenz, $N_L1 Steps bit-identisch)"
else
    echo "GATE 1a FAIL  (R1=$N_R1 L1=$N_L1)"; diff "$OUT/r1.txt" "$OUT/l1.txt" | head -6; fails=$((fails+1))
fi
# GATE 1b
if [ "$N_R2" -ge 200 ] && [ "$N_F2" -ge 200 ] && diff -q "$OUT/r2.txt" "$OUT/f2.txt" >/dev/null; then
    echo "GATE 1b PASS  (Follower Block2 == Referenz, $N_F2 Steps bit-identisch)"
else
    echo "GATE 1b FAIL  (R2=$N_R2 F2=$N_F2)"; diff "$OUT/r2.txt" "$OUT/f2.txt" | head -6; fails=$((fails+1))
fi
# GATE 2
N_SYNC=$(grep -c 'GATE2 .* MATCH' "$OUT/follower.log")
N_SYNC_BAD=$(grep -c 'GATE2 .* MISMATCH' "$OUT/follower.log")
if [ "$N_SYNC" -ge 1 ] && [ "$N_SYNC_BAD" -eq 0 ]; then
    echo "GATE 2  PASS  ($N_SYNC SYNC-Fingerprints Leader==Follower)"
else
    echo "GATE 2  FAIL  (match=$N_SYNC mismatch=$N_SYNC_BAD)"; fails=$((fails+1))
fi

# ---------------- GATE 3: Negativtest (falscher Seed) ----------------
echo "== GATE 3: Negativtest (Follower Seed-Offset 1) =="
MOO_DIST_ROLLE=leader MOO_DIST_PORT="$PORT" "$BIN" > "$OUT/nleader.log" 2>&1 &
SRVPID=$!
sleep 0.5
MOO_DIST_ROLLE=follower MOO_DIST_HOST=127.0.0.1 MOO_DIST_PORT="$PORT" MOO_DIST_SEED=1 "$BIN" > "$OUT/nfollower.log" 2>&1
wait "$SRVPID" 2>/dev/null
SRVPID=""
if grep -q 'HELLO-MISMATCH' "$OUT/nleader.log" "$OUT/nfollower.log" && ! grep -q 'LEADER FERTIG' "$OUT/nleader.log"; then
    echo "GATE 3  PASS  (falscher Seed -> HELLO-Abbruch, kein Training)"
else
    echo "GATE 3  FAIL  (kein HELLO-Abbruch bei falschem Seed)"; fails=$((fails+1))
fi

# ---------------- GATE 4: Wallclock ----------------
echo ""
echo "GATE 4  Wallclock (ehrlich): lokal=${WALL_LOKAL}s  verteilt=${WALL_VERT}s"
echo "        (localhost, CPU; verteilt hat Sync-Overhead — PoC beweist Korrektheit, nicht Speedup)"
echo "==============================================="

if [ "$fails" -eq 0 ]; then
    echo "RESULT: PASS (alle Korrektheits-Gates gruen)"
    exit 0
fi
echo "RESULT: FAIL ($fails Gate(s))"
exit 1
