#!/usr/bin/env bash
# ============================================================
# voxel_vulkan_visual_test.sh — wiederholbarer Vulkan-Visual-Test
# fuer die Voxel-Sandbox (Plan-006 R5).
#
# Ablauf:
#   1. Praeflight: Display (Wayland/X11) + echte Vulkan-GPU vorhanden?
#      -> wenn NICHT: sauberer SKIP (Exit 0) mit Diagnose. (CI-Fallback;
#         auf einem echten Arbeitsplatz mit GPU laeuft der Test ECHT.)
#   2. gl33-Referenzlauf (voxel_sandbox_selftest.moos) -> _before/_after.
#   3. echter Vulkan-Lauf (voxel_sandbox_selftest_vulkan.moos,
#      MOO_3D_BACKEND=vulkan) -> /tmp/voxel_vulkan_before/after.bmp.
#      stderr wird gesichert und auf Vulkan-Validation-Errors geprueft.
#   4. Pixel-Validierung beider Backends (non-blank, Farbanzahl-Schwelle)
#      + grobe Aehnlichkeit gl33 vs vulkan (Farbanzahl-Groessenordnung).
#
# Exit: 0 = OK oder sauberer Skip, 1 = Test fehlgeschlagen.
#
# Voraussetzung: moo-compiler mit DEFAULT-Features (gl33+vulkan+moo_ui).
#   MOO_COMPILER=/pfad/zu/moo-compiler  (Default: compiler/target/release/moo-compiler)
# ============================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${MOO_COMPILER:-$ROOT/compiler/target/release/moo-compiler}"
CHECK="$ROOT/compiler/tests/regression/voxel_screenshot_check.py"
MIN_COLORS="${MIN_COLORS:-64}"

GL33_BEFORE=/tmp/voxel_sandbox_before.bmp
GL33_AFTER=/tmp/voxel_sandbox_after.bmp
GL33_REF_BEFORE=/tmp/voxel_gl33_before.bmp
VK_BEFORE=/tmp/voxel_vulkan_before.bmp
VK_AFTER=/tmp/voxel_vulkan_after.bmp
VK_STDERR=/tmp/voxel_vulkan_stderr.log

skip() { echo "[vk-visual] SKIP: $*"; exit 0; }
fail() { echo "[vk-visual] FAIL: $*"; exit 1; }

# --- 1. Praeflight ---------------------------------------------------
[ -x "$COMPILER" ] || skip "moo-compiler nicht gefunden/ausfuehrbar: $COMPILER (mit DEFAULT-Features bauen)"

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    skip "kein Display (DISPLAY/WAYLAND_DISPLAY leer) — Headless-CI ohne GPU-Session."
fi

# Echte Vulkan-GPU? vulkaninfo + DRI-Renderknoten. llvmpipe/Software ist KEIN
# echter GPU-Test -> dann lieber skippen als blendwerken.
if ! command -v vulkaninfo >/dev/null 2>&1; then
    skip "vulkaninfo nicht installiert — Vulkan-Verfuegbarkeit nicht verifizierbar."
fi
if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    skip "kein DRI-Renderknoten (/dev/dri/renderD*) — keine echte GPU."
fi
VK_DEV="$(vulkaninfo --summary 2>/dev/null | grep -m1 deviceName | sed 's/.*= //')"
[ -n "$VK_DEV" ] || skip "vulkaninfo listet kein Vulkan-Device."
echo "[vk-visual] Vulkan-Device: $VK_DEV"
echo "[vk-visual] Display: DISPLAY='${DISPLAY:-}' WAYLAND_DISPLAY='${WAYLAND_DISPLAY:-}'"

# --- 2. gl33-Referenzlauf -------------------------------------------
echo "[vk-visual] === gl33-Referenzlauf ==="
rm -f "$GL33_BEFORE" "$GL33_AFTER" "$GL33_REF_BEFORE"
if ! env MOO_3D_BACKEND=gl33 "$COMPILER" run "$ROOT/beispiele/voxel_sandbox_selftest.moos" 2>&1 | grep -v '^warning:'; then
    fail "gl33-Selftest-Lauf fehlgeschlagen"
fi
[ -s "$GL33_BEFORE" ] || fail "gl33: kein before-Screenshot"
cp "$GL33_BEFORE" "$GL33_REF_BEFORE"

# --- 3. echter Vulkan-Lauf ------------------------------------------
echo "[vk-visual] === Vulkan-Lauf (MOO_3D_BACKEND=vulkan) ==="
rm -f "$VK_BEFORE" "$VK_AFTER" "$VK_STDERR"
env MOO_3D_BACKEND=vulkan "$COMPILER" run "$ROOT/beispiele/voxel_sandbox_selftest_vulkan.moos" 2>"$VK_STDERR" | grep -v '^warning:'
[ -s "$VK_BEFORE" ] || fail "Vulkan: kein before-Screenshot (blank/Crash?) — siehe $VK_STDERR"
[ -s "$VK_AFTER" ]  || fail "Vulkan: kein after-Screenshot — siehe $VK_STDERR"

# Vulkan-Validation-Errors sind ein echter Mangel (Plan-005-TASK-I-Historie).
if grep -iE 'validation|VK_ERROR|VUID-' "$VK_STDERR" >/dev/null 2>&1; then
    echo "[vk-visual] Vulkan-Diagnose (stderr):"
    grep -iE 'validation|VK_ERROR|VUID-' "$VK_STDERR" | head -20
    fail "Vulkan-Validation-Errors aufgetreten — nicht stillschweigend ignorieren."
fi
echo "[vk-visual] Vulkan-stderr sauber (keine Validation-Errors)."

# --- 4. Pixel-Validierung + Vergleich -------------------------------
echo "[vk-visual] === Validierung ==="
python3 "$CHECK" check "$VK_BEFORE" "$MIN_COLORS" || fail "Vulkan before-Screenshot ungueltig (blank/zu wenige Farben)"
python3 "$CHECK" check "$VK_AFTER"  "$MIN_COLORS" || fail "Vulkan after-Screenshot ungueltig"
python3 "$CHECK" compare "$GL33_REF_BEFORE" "$VK_BEFORE" "$MIN_COLORS" || fail "gl33 vs vulkan: Vergleich fehlgeschlagen"

echo "[vk-visual] OK: echter Vulkan-Lauf, Terrain sichtbar, gl33-Vergleich bestanden."
exit 0
