#!/usr/bin/env bash
set -euo pipefail

# Compiler-Testsuite fuer moo
# Plattformvertrag: Windows-Binaerausgaben tragen explizit .exe; Temp-Artefakte
# liegen in einem pro Lauf isolierten Arbeitsverzeichnis.
# Fuer jede .moos Datei mit zugehoeriger .expected:
#   1. Kompiliere mit moo-compiler
#   2. Fuehre aus
#   3. Vergleiche Output mit .expected

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_DIR="$(dirname "$SCRIPT_DIR")"
HOST_UNAME="$(uname -s)"
case "$HOST_UNAME" in
    MINGW*|MSYS*|CYGWIN*) EXE_SUFFIX=".exe" ;;
    *)                    EXE_SUFFIX="" ;;
esac
# CARGO_TARGET_DIR wird respektiert (eigene Build-Verzeichnisse paralleler Agenten).
COMPILER="${CARGO_TARGET_DIR:-$COMPILER_DIR/target}/release/moo-compiler${EXE_SUFFIX}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/moo_core_tests.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
# Modulsuche (importiere mathe/mini/ui_moo_surface) laeuft relativ zum
# Arbeitsverzeichnis (./stdlib). Unabhaengig vom Aufrufort immer vom
# Repo-Root aus kompilieren, wie es mise run test-compiler ohnehin tut.
cd "$COMPILER_DIR/.."

# ============================================================
# P2-T116: Deklarierte Sonderfaelle. Eine .moos OHNE .expected, die in
# keiner dieser Listen steht, ist FAIL (kein stilles Ueberspringen mehr).
# ============================================================
# Modulfragmente: werden nur von anderen Tests importiert, kein eigener Test.
declare -A MODULFRAGMENT=(
    [test_import_bug_modul]="importiert von test_import_bug_main"
    [test_import_bug2_modul]="importiert von test_import_bug2_main/test_import_von_main"
    [test_import_diamond_a]="importiert von test_import_diamond"
    [test_import_diamond_b]="importiert von test_import_diamond"
    [test_import_diamond_shared]="importiert von test_import_diamond*/test_import_combined"
    [test_import_doppelt_a]="importiert von test_import_doppelt_main"
    [test_import_doppelt_lib]="importiert von test_import_doppelt_main"
    [test_import_vars_modul]="importiert von test_import_vars/test_import_combined"
    [test_modul_mathe]="importiert von test_modul_main/test_modul_from"
)
# Nur kompilieren (Lauf hier nicht aussagekraeftig/moeglich).
declare -A NUR_KOMPILIEREN=(
    [test_http_with_headers_client]="braucht Live-Netz"
    [test_uim_surface_clip_diff_diag]="Diagnosewerkzeug, braucht MOO_SURFACE_DIFF_GOLDEN/_STATE"
)
# Nur laufen: Ausgabe nicht deterministisch (Zufall/Umgebung). Bestanden =
# Exit 0 und kein unbehandelter Laufzeitfehler in der Ausgabe.
declare -A NUR_LAUFEN=(
    [test_crypto]="sichere_zufall() ist zufaellig"
    [test_kern]="gibt HOME aus"
)
# P2-T117: Erwarteter Exit-Code ungleich 0 (Ausgabe wird trotzdem verglichen).
# Alle anderen Tests muessen mit Exit 0 enden.
declare -A ERWARTETER_EXIT=(
    [test_framework_fail]=1
)

# Native Windows-Programme interpretieren den Literalpfad /tmp als Wurzel des
# aktuellen Laufwerks (z. B. D:\tmp), nicht als Git-Bashs internes /tmp.
# Einige File-/Checkpoint-Regressionen testen bewusst diesen historischen Pfad.
if [ "$EXE_SUFFIX" = ".exe" ]; then
    workspace_windows="$(pwd -W)"
    drive_letter="$(printf '%s' "$workspace_windows" | cut -c1 | tr '[:upper:]' '[:lower:]')"
    mkdir -p "/${drive_letter}/tmp"
fi

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

# P2-T15: C-Pufferkern auf Bounds, Views und Pin-Lebensdauer pruefen.
# Assertions muessen aktiv bleiben; der Lauf ist Teil des Compiler-Gates.
if ! "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    "$COMPILER_DIR/runtime/moo_buffer.c" "$COMPILER_DIR/runtime/moo_struct_box.c" "$COMPILER_DIR/runtime/moo_memory.c" \
    "$COMPILER_DIR/runtime/tests/test_p2_buffer.c" "$SCRIPT_DIR/p2/test_p2_buffer_value.c" \
    -o "$WORK_DIR/p2_buffer${EXE_SUFFIX}" || ! "$WORK_DIR/p2_buffer${EXE_SUFFIX}"; then
    echo -e "${RED}FEHLER:${NC} P2-Puffer-Harness fehlgeschlagen — Abbruch."
    exit 1
fi
echo "P2-Puffer-Harness: PASS"

# P2-T14: kopierende Strukturbox mit validiertem Deskriptor.
if ! "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    "$COMPILER_DIR/runtime/moo_struct_box.c" "$SCRIPT_DIR/p2/test_p2_struct_box.c" \
    -o "$WORK_DIR/p2_struct_box${EXE_SUFFIX}" || ! "$WORK_DIR/p2_struct_box${EXE_SUFFIX}"; then
    echo -e "${RED}FEHLER:${NC} P2-Strukturbox-Harness fehlgeschlagen — Abbruch."
    exit 1
fi
echo "P2-Strukturbox-Harness: PASS"

# Alle .moos Dateien mit .expected durchgehen (top-level + regression/)
for moo_file in "$SCRIPT_DIR"/*.moos "$SCRIPT_DIR"/regression/*.moos "$SCRIPT_DIR"/p2/test_p2_dispatch.moos "$SCRIPT_DIR"/p2/methode_vorrang.moos "$SCRIPT_DIR"/p2/klasse_eltern_konstruktor.moos "$SCRIPT_DIR"/p2/nichts_hinzufuegen.moos "$SCRIPT_DIR"/p2/test_p2_frontend_legacy.moos "$SCRIPT_DIR"/p2/ffi_deklaration.moos "$SCRIPT_DIR"/p2/struktur_layout.moos; do
    [[ -f "$moo_file" ]] || continue
    test_name="$(basename "$moo_file" .moos)"
    # gfx_* laufen im eigenen GPU-Abschnitt weiter unten.
    [[ "$test_name" == gfx_* ]] && continue
    test_dir="$(dirname "$moo_file")"
    expected_file="${test_dir}/${test_name}.expected"

    # extern "C" ist fuer Apple-Ziele bewusst noch nicht freigegeben
    # (ffi_types.rs FfiSignature::validate, fail-closed).
    if [[ "$test_name" == "ffi_deklaration" && "$(uname -s)" == "Darwin" ]]; then
        skip=$((skip + 1))
        echo -e "${YELLOW}SKIP${NC} $test_name (extern C fuer macOS-ABI noch nicht freigegeben)"
        continue
    fi

    if [[ -n "${MODULFRAGMENT[$test_name]:-}" ]]; then
        skip=$((skip + 1))
        echo -e "${YELLOW}SKIP${NC} $test_name (Modulfragment: ${MODULFRAGMENT[$test_name]})"
        continue
    fi

    if [[ -n "${NUR_KOMPILIEREN[$test_name]:-}" || -n "${NUR_LAUFEN[$test_name]:-}" ]]; then
        tmp_binary="$WORK_DIR/moo_test_${test_name}${EXE_SUFFIX}"
        if ! compile_output=$("$COMPILER" compile "$moo_file" -o "$tmp_binary" 2>&1); then
            fail=$((fail + 1))
            errors="${errors}\n  ${test_name}: Kompilierung fehlgeschlagen: ${compile_output}"
            echo -e "${RED}FAIL${NC} $test_name (Kompilierung fehlgeschlagen)"
            continue
        fi
        if [[ -n "${NUR_KOMPILIEREN[$test_name]:-}" ]]; then
            pass=$((pass + 1))
            echo -e "${GREEN}PASS${NC} $test_name (nur kompiliert: ${NUR_KOMPILIEREN[$test_name]})"
        elif run_output=$("$tmp_binary" 2>&1) && [[ "$run_output" != *"unbehandelte"* && "$run_output" != *"Unbehandelter Fehler"* ]]; then
            pass=$((pass + 1))
            echo -e "${GREEN}PASS${NC} $test_name (nur Lauf geprueft: ${NUR_LAUFEN[$test_name]})"
        else
            fail=$((fail + 1))
            errors="${errors}\n  ${test_name}: Lauf fehlgeschlagen oder unbehandelter Fehler"
            echo -e "${RED}FAIL${NC} $test_name (Lauf)"
        fi
        rm -f "$tmp_binary"
        continue
    fi

    # Ohne .expected und ohne Deklaration: Gate-Luecke -> FAIL
    if [[ ! -f "$expected_file" ]]; then
        fail=$((fail + 1))
        errors="${errors}\n  ${test_name}: keine .expected und nicht in run_all.sh deklariert"
        echo -e "${RED}FAIL${NC} $test_name (keine .expected, nicht deklariert)"
        continue
    fi

    # Kompilieren
    tmp_binary="$WORK_DIR/moo_test_${test_name}${EXE_SUFFIX}"
    compile_output=$("$COMPILER" compile "$moo_file" -o "$tmp_binary" 2>&1) || {
        fail=$((fail + 1))
        errors="${errors}\n  ${test_name}: Kompilierung fehlgeschlagen: ${compile_output}"
        echo -e "${RED}FAIL${NC} $test_name (Kompilierung fehlgeschlagen)"
        continue
    }

    # Ausfuehren (P2-T117: erwarteter Exit-Code je Test, Standard 0)
    run_rc=0
    actual_output=$("$tmp_binary" 2>&1) || run_rc=$?
    want_rc="${ERWARTETER_EXIT[$test_name]:-0}"
    if [[ "$run_rc" -ne "$want_rc" ]]; then
        fail=$((fail + 1))
        errors="${errors}\n  ${test_name}: Exit Code $run_rc statt $want_rc"
        echo -e "${RED}FAIL${NC} $test_name (Exit $run_rc statt $want_rc)"
        rm -f "$tmp_binary"
        continue
    fi

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
# .moos NUR KOMPILIEREN (kein run -> kein Fenster -> kein Display noetig).
# So verrottet die Test-API-Verdrahtung nicht unbemerkt.
# ============================================================
echo ""
echo "--------------------------------"
echo "Game-Test-Smoke (GPU-unabhaengig: nur Syntax + Compile)"
RUNNER="$COMPILER_DIR/../scripts/game-test-runner.sh"
SELFTESTS=(
    "$COMPILER_DIR/../beispiele/snake_plus_selftest.moos"
    "$COMPILER_DIR/../beispiele/domain/game/world/siedler3_selftest.moos"
    "$COMPILER_DIR/../beispiele/voxel_sandbox_selftest.moos"
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
    st_name="$(basename "$st" .moos)"
    if [[ ! -f "$st" ]]; then
        skip=$((skip + 1)); echo -e "${YELLOW}SKIP${NC} $st_name (fehlt)"
        continue
    fi
    st_bin="$WORK_DIR/moo_smoke_${st_name}${EXE_SUFFIX}"
    if "$COMPILER" compile "$st" -o "$st_bin" >/dev/null 2>&1; then
        pass=$((pass + 1)); echo -e "${GREEN}PASS${NC} $st_name (compile)"
    else
        fail=$((fail + 1)); echo -e "${RED}FAIL${NC} $st_name (Kompilierung)"
        errors="${errors}\n  ${st_name}: Selftest-Kompilierung fehlgeschlagen"
    fi
    rm -f "$st_bin"
done

# ============================================================
# P2-T116: GPU-Tests (gfx_*). Immer Compile-Gate. Lauf nur mit xvfb-run
# und Software-Renderer (llvmpipe); bestanden = Exit 0, letzte Zeile
# enthaelt PASS, kein unbehandelter Laufzeitfehler. Ohne xvfb-run oder bei
# MOO_GFX_NUR_KOMPILIEREN=1: nur kompiliert. gfx_frame_statistics braucht
# die BMP-Fixture aus runtime/test-gfx-roi.sh und wird hier nur kompiliert.
# ============================================================
echo ""
echo "--------------------------------"
echo "GPU-Tests (gfx_*: Compile-Gate, Lauf ueber xvfb/llvmpipe)"
gfx_lauf=1
if ! command -v xvfb-run >/dev/null 2>&1 || [[ "${MOO_GFX_NUR_KOMPILIEREN:-0}" == "1" ]]; then
    gfx_lauf=0
fi
for gfx_file in "$SCRIPT_DIR"/gfx_*.moos; do
    [[ -f "$gfx_file" ]] || continue
    gfx_name="$(basename "$gfx_file" .moos)"
    gfx_bin="$WORK_DIR/moo_gfx_${gfx_name}${EXE_SUFFIX}"
    if ! compile_output=$("$COMPILER" compile "$gfx_file" -o "$gfx_bin" 2>&1); then
        fail=$((fail + 1))
        errors="${errors}\n  ${gfx_name}: Kompilierung fehlgeschlagen: ${compile_output}"
        echo -e "${RED}FAIL${NC} $gfx_name (Kompilierung fehlgeschlagen)"
        continue
    fi
    if [[ "$gfx_lauf" == "0" || "$gfx_name" == "gfx_frame_statistics" ]]; then
        pass=$((pass + 1))
        echo -e "${GREEN}PASS${NC} $gfx_name (nur kompiliert)"
        rm -f "$gfx_bin"
        continue
    fi
    if gfx_output=$(env -u WAYLAND_DISPLAY LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
            timeout 120 xvfb-run -a -s "-screen 0 1280x720x24" "$gfx_bin" 2>&1) \
        && [[ "$(printf '%s\n' "$gfx_output" | tail -n 1)" == *PASS* ]] \
        && [[ "$gfx_output" != *"unbehandelte"* ]]; then
        pass=$((pass + 1))
        echo -e "${GREEN}PASS${NC} $gfx_name (llvmpipe)"
    else
        fail=$((fail + 1))
        errors="${errors}\n  ${gfx_name}: GPU-Lauf (llvmpipe): $(printf '%s\n' "$gfx_output" | tail -n 3)"
        echo -e "${RED}FAIL${NC} $gfx_name (llvmpipe-Lauf)"
    fi
    rm -f "$gfx_bin"
done

# REG-G1: Frische-Receiver-Leak-Gate — 1M-vs-4M-Heap-Skalierung fuer
# Methodenketten mit frischen Receivern (loop-invariante Receiver verstecken
# Dispatch-Leaks). Transparenter Skip (Exit 2) wenn python3 fehlt.
set +e
"$SCRIPT_DIR/leak_gates/run_leak_gates.sh"
leak_rc=$?
set -e
if [ "$leak_rc" -eq 1 ]; then
    echo -e "${RED}FEHLER:${NC} Leak-Gate (frische Receiver) fehlgeschlagen."
    exit 1
elif [ "$leak_rc" -eq 2 ]; then
    skip=$((skip + 1))
    echo -e "${YELLOW}SKIP${NC} Leak-Gate (plattformbedingt nicht aussagekraeftig)"
elif [ "$leak_rc" -ne 0 ]; then
    echo -e "${RED}FEHLER:${NC} Leak-Gate lieferte unerwarteten Exit-Code $leak_rc."
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
