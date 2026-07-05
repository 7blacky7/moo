#!/usr/bin/env bash
# run_leak_gates.sh — REG-G1: Frische-Receiver-Methoden-Leak-Gate (Task 6fb1e81f)
# ============================================================================
# ZWECK
#   Langlauf-Heap-Gate: Methodenketten mit FRISCHEN Receivern pro Iteration.
#   G1-LEAK-Lesson: loop-invariante Receiver verstecken Dispatch-Leaks
#   (Refcount-Inflation ohne RSS) — erst frische Receiver machten den
#   +1/Call-Leak sichtbar (430B/Iter). CG1-v4-Lesson: User-Klassen-Receiver
#   double-release (UAF ~350k) + Property-Namen-Leak in free_object.
#
# METHODIK (Messregeln aus G1-LEAK, Thought 43cdfa05):
#   * 1M-vs-4M-Skalierung: Plateau (~gleich) = OK, linear (~4x) = Leak.
#   * ZWEI Laeufe pro Messpunkt, MIN zaehlt — RUSAGE-Einzelmessungen liefern
#     sporadische Ausreisser nach oben (beobachtet: 68MB/599MB Erstlauf-
#     Artefakte bei real ~21MB).
#   * argumente() ist im kompilierten Binary LEER — N wird per sed eingebacken.
#   * FAIL-Kriterium: min(4M) - min(1M) > 10MB (faengt bereits ~4B/Iter)
#     oder Exit-Code != 0 (Crash = haerteste Regression).
#
# PROBES (frische Receiver pro Iteration):
#   user_class : neu Objekt() + Methodencall (CG1-v4-Regression)
#   tensor     : zeilen -> softmax -> zu_liste (Sampling-Kette aus G1-LEAK)
#   string     : frischer String + .enthält (CG1-v3-Transfer-Konvention)
#   listdict   : frische Liste/.länge + frisches Dict/.hat
#
# EXIT-CODES: 0 alle flach | 1 Leak/Crash | 2 transparenter Skip (python3 fehlt)
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
COMPILER="$COMPILER_DIR/target/release/moo-compiler"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/moo_leak_gate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

command -v python3 >/dev/null 2>&1 || { echo "[leak-gate] SKIP: python3 fehlt"; exit 2; }
if [ ! -x "$COMPILER" ]; then
  echo "[leak-gate] Compiler fehlt — baue (cargo build --release)..."
  (cd "$COMPILER_DIR" && cargo build --release) || { echo "[leak-gate] FAIL: Build"; exit 1; }
fi

# RSS-Harness: ru_maxrss des Kindprozesses in KB auf stdout.
cat > "$WORK/rss.py" << 'PYEOF'
import subprocess, resource, sys
p = subprocess.run([sys.argv[1]], capture_output=True, text=True)
kb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
print(f"{kb} {p.returncode}")
PYEOF

# --- Templates (Platzhalter __N__) -------------------------------------------
cat > "$WORK/user_class.tmpl" << 'EOF'
klasse Zaehler:
    funktion erstelle():
        selbst.w = 0
    funktion tick(n):
        gib_zurück n + 1
setze i auf 0
setze s auf 0
solange i < __N__:
    setze z auf neu Zaehler()
    setze s auf z.tick(i)
    setze i auf i + 1
zeige s
EOF
cat > "$WORK/tensor.tmpl" << 'EOF'
setze logits auf tensor_zufall([4, 70], 3)
autograd_aus()
setze i auf 0
solange i < __N__:
    setze tmp auf logits.zeilen(3, 4) * 2.0
    setze sm auf tmp.softmax()
    setze w auf sm.zu_liste()
    setze i auf i + 1
zeige "ok"
EOF
cat > "$WORK/string.tmpl" << 'EOF'
setze i auf 0
setze t auf falsch
solange i < __N__:
    setze s auf "wert-" + i
    setze t auf s.enthält("999")
    setze i auf i + 1
zeige t
EOF
cat > "$WORK/listdict.tmpl" << 'EOF'
setze i auf 0
setze n auf 0
solange i < __N__:
    setze l auf [i, i + 1]
    setze n auf l.länge()
    setze d auf {"k": i}
    setze h auf d.hat("k")
    setze i auf i + 1
zeige n
EOF

# min-RSS aus 2 Laeufen in MB (globale Vars MIN_MB / RUN_RC)
measure_min() {
  local bin="$1" r1 r2 kb1 rc1 kb2 rc2
  r1=$(python3 "$WORK/rss.py" "$bin"); kb1=${r1% *}; rc1=${r1#* }
  r2=$(python3 "$WORK/rss.py" "$bin"); kb2=${r2% *}; rc2=${r2#* }
  RUN_RC=0; [ "$rc1" != "0" ] && RUN_RC=$rc1; [ "$rc2" != "0" ] && RUN_RC=$rc2
  local min_kb=$kb1; [ "$kb2" -lt "$min_kb" ] && min_kb=$kb2
  MIN_MB=$(( min_kb / 1024 ))
}

fail=0
for probe in user_class tensor string listdict; do
  for n in 1000000 4000000; do
    sed "s/__N__/$n/" "$WORK/$probe.tmpl" > "$WORK/${probe}_$n.moo"
    if ! "$COMPILER" compile "$WORK/${probe}_$n.moo" -o "$WORK/${probe}_$n.bin" >/dev/null 2>&1; then
      echo "[leak-gate] FAIL: $probe N=$n kompiliert nicht"; fail=1; continue 2
    fi
  done
  measure_min "$WORK/${probe}_1000000.bin";  m1=$MIN_MB; rcA=$RUN_RC
  measure_min "$WORK/${probe}_4000000.bin";  m4=$MIN_MB; rcB=$RUN_RC
  if [ "$rcA" != "0" ] || [ "$rcB" != "0" ]; then
    echo "[leak-gate] FAIL: $probe crasht (rc 1M=$rcA, 4M=$rcB) — Langlauf-Regression"; fail=1; continue
  fi
  delta=$(( m4 - m1 ))
  if [ "$delta" -gt 10 ]; then
    echo "[leak-gate] FAIL: $probe waechst linear (1M=${m1}MB, 4M=${m4}MB, Δ=${delta}MB > 10MB)"; fail=1
  else
    echo "[leak-gate] PASS: $probe flach (1M=${m1}MB, 4M=${m4}MB)"
  fi
done

[ "$fail" -eq 0 ] && echo "[leak-gate] ALLE PROBES FLACH" || echo "[leak-gate] LEAK-GATE ROT"
exit $fail
