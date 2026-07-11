#!/usr/bin/env bash
# ============================================================
# KIP-G4c — Production-Wiring-Gate: moo_nn-Pfade auf residente GPU-Ops (STRIKT)
#
# Prueft die 6 Abnahme-Kriterien aus docs/kip/G4c-production-wiring-plan.md §4:
#   [1] CPU-Default bit-identisch : run_all GPU-frei 60/0 unveraendert.
#   [2] ki_sprachmodell GPU==CPU  : STRIKT-Loss-Kurve vs CPU-Kurve in Toleranz.
#   [3] Vulkan-Smoke              : ki-gpu-smoke.sh + G4c-Wiring-Smoke gruen.
#   [4] Coverage                  : kip_gpu_coverage.sh weiter gruen (G0 §3).
#   [5] Keine versteckten CPU-FB  : STRIKT -> cpu_fallbacks==0 + Negativ-Kontrolle
#                                    (nicht-geroutetes Op unter STRIKT MUSS failen).
#   [6] Stub ohne MOO_HAS_VULKAN  : kompiliert; STRIKT ohne Vulkan = Init-Fehler.
#
# PHASEN-STATUS (Stand Phase 1 — Prework):
#   Kriterien [1][3][4] laufen HEUTE (baseline, unabhaengig vom Wiring).
#   Kriterien [2][5][6-STRIKT] sind mit "PHASE2" markiert und werden aktiv,
#   sobald das Production-Wiring + tests/ki_gpu_g4c_wiring.c existiert.
#   In Phase 1 melden sie PENDING (Exit-neutral), damit das Skript als lauffaehige
#   Baseline dient, ohne falsch-gruen zu sein.
#
# Transparent skippbar: ohne libvulkan -> GPU-Teile SKIP (Exit 0, KEIN Beweis).
# Nur auf der inventarisierten GPU (4070 Ti) aussagekraeftig.
# ============================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/compiler/runtime"
FAIL=0
PENDING=0

pass()    { echo "  ✅ $*"; }
fail()    { echo "  ❌ $*"; FAIL=1; }
skip()    { echo "  ⏭️  SKIP: $*"; }
pending() { echo "  🕓 PHASE2-PENDING: $*"; PENDING=1; }

have_vulkan() { [ -n "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; }

echo "== KIP-G4c Gate =="

# ----------------------------------------------------------------
# [1] CPU-Default bit-identisch — run_all GPU-frei
# ----------------------------------------------------------------
echo "[1] CPU-Default (run_all GPU-frei)"
if [ -f "$ROOT/compiler/tests/run_all.sh" ]; then
    if ( cd "$ROOT" && MOO_KI_GPU=0 mise run test-compiler ) > /tmp/g4c_runall.log 2>&1; then
        pass "run_all GPU-frei gruen (Default-Pfad unveraendert)"
    else
        fail "run_all GPU-frei ROT — siehe /tmp/g4c_runall.log"
    fi
else
    skip "compiler/tests/run_all.sh nicht gefunden"
fi

# ----------------------------------------------------------------
# [4] Coverage-Gate (G0 §3) weiter gruen
# ----------------------------------------------------------------
echo "[4] kip_gpu_coverage.sh"
if [ -f "$ROOT/skripte/kip_gpu_coverage.sh" ]; then
    if bash "$ROOT/skripte/kip_gpu_coverage.sh" > /tmp/g4c_cov.log 2>&1; then
        pass "Coverage gruen (Residenz-Beweis unveraendert)"
    else
        # SKIP ohne Vulkan gibt Exit 0; ein echter Fehler ist != 0
        fail "Coverage ROT — siehe /tmp/g4c_cov.log"
    fi
else
    skip "kip_gpu_coverage.sh nicht gefunden"
fi

# ----------------------------------------------------------------
# [3] Vulkan-Smoke (bestehend) + G4c-Wiring-Smoke (Phase 2)
# ----------------------------------------------------------------
echo "[3] Vulkan-Smoke"
if have_vulkan; then
    if [ -f "$RT/ki-gpu-smoke.sh" ]; then
        if bash "$RT/ki-gpu-smoke.sh" > /tmp/g4c_smoke.log 2>&1; then
            pass "ki-gpu-smoke.sh gruen"
        else
            fail "ki-gpu-smoke.sh ROT — siehe /tmp/g4c_smoke.log"
        fi
    else
        skip "ki-gpu-smoke.sh nicht gefunden"
    fi
    # G4c-spezifischer Wiring-Smoke (Phase 2)
    if [ -f "$RT/tests/ki_gpu_g4c_wiring.c" ]; then
        gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I"$RT" \
            -o /tmp/ki_gpu_g4c_wiring "$RT/tests/ki_gpu_g4c_wiring.c" "$RT/moo_ki_gpu.c" -lvulkan -lm \
            && /tmp/ki_gpu_g4c_wiring && pass "ki_gpu_g4c_wiring gruen" || fail "ki_gpu_g4c_wiring ROT"
    else
        pending "tests/ki_gpu_g4c_wiring.c fehlt (Phase-2-Deliverable)"
    fi
else
    skip "libvulkan nicht installiert — GPU-Smoke nicht bewiesen"
fi

# ----------------------------------------------------------------
# [2] ki_sprachmodell GPU(STRIKT) == CPU in Toleranz
# ----------------------------------------------------------------
echo "[2] ki_sprachmodell GPU==CPU"
# PHASE2: benoetigt gpu_statistik()-Builtin + STRIKT-Routing im moo_nn-Pfad.
# Ablauf (Phase 2): identischer Param-Snapshot -> N Schritte CPU-Loss-Kurve
#   vs. MOO_KI_GPU_STRIKT=1-Kurve -> rel. Abweichung < Toleranz.
pending "STRIKT-Kurvenvergleich braucht Production-Wiring (Phase 2)"

# ----------------------------------------------------------------
# [5] Keine versteckten CPU-Fallbacks (STRIKT) — Stufe 1(Fwd)+2(Bwd,symm.)+3(SGD)
# ----------------------------------------------------------------
echo "[5] cpu_fallbacks==0 unter STRIKT + Negativ-Kontrolle"
if have_vulkan; then
    if [ -f "$RT/tests/test_g4c_strikt.c" ]; then
        gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I"$RT" \
            -o /tmp/test_g4c_strikt "$RT/tests/test_g4c_strikt.c" \
            "$RT/moo_nn.c" "$RT/moo_nn_easy.c" "$RT/moo_json.c" "$RT/moo_tensor.c" \
            "$RT/moo_tensor_ops.c" "$RT/moo_ki_gpu.c" "$RT/moo_autograd.c" \
            "$RT/moo_memory.c" "$RT/moo_value.c" "$RT/moo_print.c" "$RT/moo_string.c" \
            "$RT/moo_dict.c" "$RT/moo_list.c" "$RT/moo_ops.c" -lvulkan -lm \
            > /tmp/g4c_strikt_build.log 2>&1 \
            && /tmp/test_g4c_strikt > /tmp/g4c_strikt_run.log 2>&1 \
            && pass "test_g4c_strikt gruen (Stufe1/2/3 resident, cpu_fallbacks==0, Negativ-Kontrolle ok)" \
            || fail "test_g4c_strikt ROT — siehe /tmp/g4c_strikt_build.log / /tmp/g4c_strikt_run.log"
    else
        pending "tests/test_g4c_strikt.c fehlt"
    fi
else
    skip "libvulkan nicht installiert — STRIKT-Enforcement nicht bewiesen"
fi

# ----------------------------------------------------------------
# [6] Stub ohne MOO_HAS_VULKAN kompiliert
# ----------------------------------------------------------------
echo "[6] Stub-Compile ohne MOO_HAS_VULKAN"
if gcc -std=gnu11 -O2 -I"$RT" -c "$RT/moo_ki_gpu.c" -o /tmp/g4c_stub.o > /tmp/g4c_stub.log 2>&1; then
    pass "moo_ki_gpu.c Stub kompiliert ohne Vulkan"
else
    fail "Stub-Compile ROT — siehe /tmp/g4c_stub.log"
fi
# STRIKT ohne Vulkan: moo_ki_gpu_strikt_aktiv() liest den Env trotzdem (reiner
# Host-Zustand) -> Aufrufer werfen beim ersten scheiternden GPU-Op fail-loud,
# kein stiller CPU-Lauf. Volle Verifikation (echter moo-Programm-Lauf ohne
# Vulkan + STRIKT) ist ein Follow-up (braucht gpu_statistik()-Builtin).
pending "STRIKT-ohne-Vulkan End-to-End-Fail-Loud-Beweis via moo-Programm (Follow-up)"

# ----------------------------------------------------------------
echo "== Ergebnis =="
if [ "$FAIL" -ne 0 ]; then
    echo "KIP-G4c Gate: ROT ($FAIL Kriterien fehlgeschlagen)"
    exit 1
fi
if [ "$PENDING" -ne 0 ]; then
    echo "KIP-G4c Gate: BASELINE GRUEN, aber PHASE2-PENDING offen (Wiring noch nicht verdrahtet)"
    echo "  -> In Phase 1 erwartet. Vollstaendig gruen erst nach Production-Wiring."
    exit 0
fi
echo "KIP-G4c Gate: VOLLSTAENDIG GRUEN — alle 6 Kriterien erfuellt"
exit 0
