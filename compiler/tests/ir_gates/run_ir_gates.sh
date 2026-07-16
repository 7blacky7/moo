#!/usr/bin/env bash
# run_ir_gates.sh — REG-G1: Entry-Alloca-Guard (Task f9b5e570, G1-LEAK-Lesson)
# ============================================================================
# ZWECK
#   Kompiliert probe_loop_alloca.moos mit --emit-ir und prueft das LLVM-IR:
#   JEDE alloca muss im Entry-Block ihrer Funktion liegen. Eine alloca im
#   Schleifenkoerper waechst den Stack pro Iteration (G1-LEAK: 16B/Iter =>
#   SEGV nach ~700k bei list/dict/comp-Slots; for_idx-Fund 2026-07-05:
#   für-in-solange SEGV bei ~1M).
#
# WARUM EIN IR-GATE UND KEIN ASAN-GATE (Methodik-Lektion aus G1-LEAK):
#   AddressSanitizer sieht STACK-WACHSTUM NICHT — der FWA-Stack-Bug lief
#   unter ASan 800k Iterationen sauber durch. Die Werkzeuge fuer diese
#   Bug-Klasse sind (a) statisches IR-Audit (dieses Gate) und
#   (b) ulimit-Bisektion (ulimit -s 4096 laesst den Crash frueher kommen).
#
# NUTZUNG
#   ./run_ir_gates.sh                # baut Compiler falls noetig, prueft Probe
#   Wird ausserdem am Ende von compiler/tests/run_all.sh aufgerufen.
#
# EXIT-CODES
#   0  keine Nicht-Entry-alloca im IR der Probe
#   1  mindestens eine Nicht-Entry-alloca (Regressionsfund) oder Build-Fehler
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"   # compiler/
COMPILER="$COMPILER_DIR/target/release/moo-compiler"
PROBE="$SCRIPT_DIR/probe_loop_alloca.moos"
IR_OUT="$(mktemp "${TMPDIR:-/tmp}/moo_ir_gate.XXXXXX")"
trap 'rm -f "$IR_OUT"' EXIT

if [ ! -x "$COMPILER" ]; then
  echo "[ir-gate] Compiler fehlt — baue (cargo build --release)..."
  (cd "$COMPILER_DIR" && cargo build --release) || { echo "[ir-gate] FAIL: Build"; exit 1; }
fi

# --emit-ir schreibt das IR EXAKT an den -o Pfad (kein .ll-Suffix-Anhang).
if ! "$COMPILER" compile "$PROBE" --emit-ir -o "$IR_OUT" >/dev/null 2>&1; then
  echo "[ir-gate] FAIL: Probe kompiliert nicht ($PROBE)"
  exit 1
fi

# Audit: nach jedem 'define' gilt der erste Block als Entry; jedes weitere
# Label beendet den Entry-Bereich. allocas ausserhalb => Fund.
FUNDE=$(awk '/^define/{entry=1} /^[a-zA-Z0-9_.]+:/{if(!/^entry/)entry=0} /= alloca/{if(!entry) print $1}' "$IR_OUT" | sort | uniq -c)

if [ -n "$FUNDE" ]; then
  echo "[ir-gate] FAIL: Nicht-Entry-alloca(s) im Schleifenkoerper gefunden:"
  echo "$FUNDE" | sed 's/^/    /'
  echo "[ir-gate] Fix-Muster: self.entry_alloca(...) statt build_alloca am Insert-Point (codegen.rs)."
  exit 1
fi

echo "[ir-gate] PASS: alle allocas im Entry-Block ($(grep -c '= alloca' "$IR_OUT") allocas geprueft)"
exit 0
