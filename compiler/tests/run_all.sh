#!/usr/bin/env bash
set -euo pipefail

# Compiler-Testsuite fuer moo
# Fuer jede .moo Datei mit zugehoeriger .expected:
#   1. Kompiliere mit moo-compiler
#   2. Fuehre aus
#   3. Vergleiche Output mit .expected

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_DIR="$(dirname "$SCRIPT_DIR")"
COMPILER="$COMPILER_DIR/target/release/moo-compiler"

# Farben
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

# Compiler bauen.
# WICHTIG: immer mit Default-Features (gl33+vulkan+moo_ui) bauen, denn nur
# dann wird runtime/moo_voxel.c in das Runtime-Archiv aufgenommen (build.rs
# gated moo_voxel.c hinter den 3D-Features). Ein zuvor mit
# --no-default-features gebautes Binary fuehrt sonst beim Linken der
# Voxel-Tests zu "undefined reference moo_voxel_free/moo_voxel_holen".
# cargo ist idempotent: ist das Binary bereits mit Default-Features aktuell,
# passiert nichts; wurde es ohne 3D gebaut, erzwingt der abweichende
# Feature-Satz einen Rebuild.
echo "Baue Compiler (Default-Features inkl. 3D/Voxel)..."
if ! (cd "$COMPILER_DIR" && cargo build --release); then
    echo -e "${RED}FEHLER:${NC} Compiler-Build fehlgeschlagen — Abbruch."
    exit 1
fi

pass=0
fail=0
skip=0
errors=""

# REG-G1: Entry-Alloca-Guard (IR-Gate) — statisches Audit gegen
# Loop-Body-allocas (Stack-Wachstum pro Iteration; ASan-blind, siehe
# ir_gates/run_ir_gates.sh). Laeuft VOR der Suite, failt hart.
if ! "$SCRIPT_DIR/ir_gates/run_ir_gates.sh"; then
    echo -e "${RED}FEHLER:${NC} IR-Gate (Entry-Alloca-Guard) fehlgeschlagen — Abbruch."
    exit 1
fi

# TYP-ZENTRAL Phase 3: MooTag-Drift-Gate — vergleicht die Tag-Zweitpflege-
# Stellen (codegen.rs moo_tag, moo_wasm.c #defines) wertgenau mit dem
# MooTag-enum in moo_runtime.h. Drift = SEGV-Klasse.
if ! "$SCRIPT_DIR/ir_gates/run_tag_sync_gate.sh"; then
    echo -e "${RED}FEHLER:${NC} Tag-Sync-Gate fehlgeschlagen — Abbruch."
    exit 1
fi

# Alle .moo Dateien mit .expected durchgehen (top-level + regression/)
for moo_file in "$SCRIPT_DIR"/*.moo "$SCRIPT_DIR"/regression/*.moo; do
    [[ -f "$moo_file" ]] || continue
    test_name="$(basename "$moo_file" .moo)"
    test_dir="$(dirname "$moo_file")"
    expected_file="${test_dir}/${test_name}.expected"

    # Ohne .expected ueberspringen
    if [[ ! -f "$expected_file" ]]; then
        skip=$((skip + 1))
        echo -e "${YELLOW}SKIP${NC} $test_name (keine .expected Datei)"
        continue
    fi

    # Kompilieren
    tmp_binary="/tmp/moo_test_${test_name}"
    compile_output=$("$COMPILER" compile "$moo_file" -o "$tmp_binary" 2>&1) || {
        fail=$((fail + 1))
        errors="${errors}\n  ${test_name}: Kompilierung fehlgeschlagen: ${compile_output}"
        echo -e "${RED}FAIL${NC} $test_name (Kompilierung fehlgeschlagen)"
        continue
    }

    # Ausfuehren
    actual_output=$("$tmp_binary" 2>&1) || {
        fail=$((fail + 1))
        errors="${errors}\n  ${test_name}: Laufzeitfehler (Exit Code $?)"
        echo -e "${RED}FAIL${NC} $test_name (Laufzeitfehler)"
        rm -f "$tmp_binary"
        continue
    }

    # Vergleichen
    expected_output=$(cat "$expected_file")
    if [[ "$actual_output" == "$expected_output" ]]; then
        pass=$((pass + 1))
        echo -e "${GREEN}PASS${NC} $test_name"
    else
        fail=$((fail + 1))
        echo -e "${RED}FAIL${NC} $test_name"
        errors="${errors}\n  ${test_name}:"
        errors="${errors}\n    Erwartet: $(echo "$expected_output" | head -3)"
        errors="${errors}\n    Bekommen: $(echo "$actual_output" | head -3)"
    fi

    rm -f "$tmp_binary"
done

# ============================================================
# Game-Test-Smoke (Plan-008 A4) — GPU-UNABHAENGIG.
# Die VISUELLEN Selftests (Fenster/Pixel/Screenshot) laufen ueber
# scripts/game-test-runner.sh (braucht xvfb/GPU) und sind hier BEWUSST
# NICHT enthalten, damit run_all.sh in CI ohne GPU/Vulkan/GIF gruen bleibt.
# Hier nur: (1) Runner-Skript bash-syntax-pruefen, (2) die drei Selftest-
# .moo NUR KOMPILIEREN (kein run -> kein Fenster -> kein Display noetig).
# So verrottet die Test-API-Verdrahtung nicht unbemerkt.
# ============================================================
echo ""
echo "--------------------------------"
echo "Game-Test-Smoke (GPU-unabhaengig: nur Syntax + Compile)"
RUNNER="$COMPILER_DIR/../scripts/game-test-runner.sh"
SELFTESTS=(
    "$COMPILER_DIR/../beispiele/snake_plus_selftest.moo"
    "$COMPILER_DIR/../beispiele/domain/game/world/siedler3_selftest.moo"
    "$COMPILER_DIR/../beispiele/voxel_sandbox_selftest.moo"
)

if [[ -f "$RUNNER" ]]; then
    if bash -n "$RUNNER" 2>/dev/null; then
        pass=$((pass + 1)); echo -e "${GREEN}PASS${NC} game-test-runner.sh (bash -n)"
    else
        fail=$((fail + 1)); echo -e "${RED}FAIL${NC} game-test-runner.sh (Syntax)"
        errors="${errors}\n  game-test-runner.sh: bash-Syntaxfehler"
    fi
else
    skip=$((skip + 1)); echo -e "${YELLOW}SKIP${NC} game-test-runner.sh (fehlt)"
fi

for st in "${SELFTESTS[@]}"; do
    st_name="$(basename "$st" .moo)"
    if [[ ! -f "$st" ]]; then
        skip=$((skip + 1)); echo -e "${YELLOW}SKIP${NC} $st_name (fehlt)"
        continue
    fi
    st_bin="/tmp/moo_smoke_${st_name}"
    if "$COMPILER" compile "$st" -o "$st_bin" >/dev/null 2>&1; then
        pass=$((pass + 1)); echo -e "${GREEN}PASS${NC} $st_name (compile)"
    else
        fail=$((fail + 1)); echo -e "${RED}FAIL${NC} $st_name (Kompilierung)"
        errors="${errors}\n  ${st_name}: Selftest-Kompilierung fehlgeschlagen"
    fi
    rm -f "$st_bin"
done

# REG-G1: Frische-Receiver-Leak-Gate — 1M-vs-4M-Heap-Skalierung fuer
# Methodenketten mit frischen Receivern (loop-invariante Receiver verstecken
# Dispatch-Leaks). Transparenter Skip (Exit 2) wenn python3 fehlt.
"$SCRIPT_DIR/leak_gates/run_leak_gates.sh"
leak_rc=$?
if [ "$leak_rc" -eq 1 ]; then
    echo -e "${RED}FEHLER:${NC} Leak-Gate (frische Receiver) fehlgeschlagen."
    exit 1
fi

# Zusammenfassung
echo ""
echo "================================"
total=$((pass + fail))
echo -e "Ergebnis: ${GREEN}${pass} bestanden${NC}, ${RED}${fail} fehlgeschlagen${NC}, ${YELLOW}${skip} uebersprungen${NC} (von $((total + skip)) Tests)"

if [[ -n "$errors" ]]; then
    echo ""
    echo "Fehlerdetails:"
    echo -e "$errors"
fi

echo "================================"

# Exit-Code: 0 nur wenn alle bestanden
[[ $fail -eq 0 ]]
