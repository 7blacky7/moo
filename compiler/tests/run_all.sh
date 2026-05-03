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

# Compiler bauen falls noetig
if [[ ! -x "$COMPILER" ]]; then
    echo "Baue Compiler..."
    (cd "$COMPILER_DIR" && cargo build --release 2>/dev/null)
fi

pass=0
fail=0
skip=0
errors=""

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
