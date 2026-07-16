#!/usr/bin/env bash
# kernel-smoke-arm64.sh — P012-D5/E2: ARM64 Kernel-Boot-Smoke (qemu-virt-aarch64).
# ============================================================================
# PHASEN
#   [1/3] Build: beispiele/kernel/arm64_test.moos via Kernel-Pipeline
#         (--board qemu-virt-aarch64: PL011-Defines, ld.lld, Linker-Script
#         linker-arm64-virt.ld @ 0x40080000)
#   [2/3] readelf-Gates: ELF64 + AArch64 + Entry == Board-load_addr
#   [3/3] QEMU-Boot: qemu-system-aarch64 -M virt -kernel (KEIN GRUB/MB2!)
#         Asserts seriell: MOO-ARM64-ENTRY-OK, MOO-ARM64-UART-OK,
#         'DTB: 0x' (Pointer geloggt), MOO-ARM64-MOO-OK (moo-Code lief).
#         Fehlt qemu-system-aarch64: TRANSPARENTER SKIP, Exit 0.
#         Fehlt die Cross-Toolchain (clang/aarch64-gcc bzw. ld.lld/
#         aarch64-ld): ebenfalls TRANSPARENTER SKIP (kein finaler Beweis).
#
# run_all.sh bleibt QEMU-frei — separate Schiene (Projektkonvention).
# EXIT-CODES: 0 = gruen ODER sauber geskippt; 1 = Fehler.
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOO_BIN="${MOO_BIN:-$ROOT/compiler/target/release/moo-compiler}"
KERNEL_SRC="$ROOT/beispiele/kernel/arm64_test.moos"

OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_arm64_smoke.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
ELF="$OUT/arm64_test.elf"

fail() { echo "FAIL: $*" >&2; exit 1; }

echo "############################################################"
echo "# moo ARM64-Kernel-Smoke (P012-E2, qemu-virt-aarch64)"
echo "# Kernel : $KERNEL_SRC"
echo "# Compiler: $MOO_BIN"
echo "############################################################"

# --- Cross-Tooling-Check (P012-E2): ohne aarch64-faehigen cc/ld kann das
# --- Artefakt nicht entstehen -> transparenter Skip statt FAIL.
have() { command -v "$1" >/dev/null 2>&1; }
if ! have clang && ! have aarch64-linux-gnu-gcc && ! have aarch64-elf-gcc; then
  echo "SKIP: kein aarch64-C-Compiler (clang / aarch64-linux-gnu-gcc / aarch64-elf-gcc)."
  echo "SKIP: Dies ist KEIN finaler Beweis — das D/E-Gate verlangt den echten Boot."
  exit 0
fi
if ! have ld.lld && ! have aarch64-linux-gnu-ld && ! have aarch64-elf-ld; then
  echo "SKIP: kein aarch64-Linker (ld.lld / aarch64-linux-gnu-ld / aarch64-elf-ld)."
  echo "SKIP: Dies ist KEIN finaler Beweis — das D/E-Gate verlangt den echten Boot."
  exit 0
fi

# --- [1/3] Build ------------------------------------------------------------
echo "[1/3] Kernel-Pipeline (--board qemu-virt-aarch64) ..."
if [ ! -x "$MOO_BIN" ]; then
  echo "      moo-compiler fehlt — baue via cargo build --release ..."
  (cd "$ROOT/compiler" && cargo build --release) \
    || fail "cargo build --release fehlgeschlagen"
fi
[ -f "$KERNEL_SRC" ] || fail "Kernel-Quelle nicht gefunden: $KERNEL_SRC"

"$MOO_BIN" compile "$KERNEL_SRC" --no-stdlib --kernel --board qemu-virt-aarch64 \
    -o "$ELF" || fail "Kernel-Pipeline fehlgeschlagen"
[ -f "$ELF" ] || fail "kein ELF erzeugt: $ELF"
echo "      OK: $ELF"

# --- [2/3] readelf-Gates ------------------------------------------------------
echo "[2/3] ELF-Gates (ELF64/AArch64/Entry 0x40080000) ..."
readelf -h "$ELF" | grep -q 'Class:.*ELF64'      || fail "kein ELF64"
readelf -h "$ELF" | grep -q 'Machine:.*AArch64'  || fail "keine AArch64-Maschine"
readelf -h "$ELF" | grep -qE 'Entry point address:.*0x40080000' \
  || { readelf -h "$ELF" | grep 'Entry'; fail "Entry != 0x40080000 (Board-load_addr)"; }
echo "      OK: ELF64/AArch64, Entry an Board-load_addr"

# --- [3/3] QEMU-Boot ----------------------------------------------------------
echo "[3/3] QEMU-Boot (-M virt) ..."
if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "      SKIP: qemu-system-aarch64 fehlt."
  echo "      SKIP: Boot-Test uebersprungen (transparent, Exit 0)."
  echo "      SKIP: Dies ist KEIN finaler Beweis — das D/E-Gate verlangt den echten Boot."
  echo "      Hinweis (Arch/CachyOS): sudo pacman -S qemu-system-aarch64"
  echo "############################################################"
  echo "# ERGEBNIS: Build/ELF-Gates gruen — QEMU-Boot GESKIPPT (Tooling)."
  echo "############################################################"
  exit 0
fi

SERIAL_LOG="$OUT/serial.log"
# Der Kernel endet in der wfi-Schleife — timeout (rc 124) ist erwartet.
timeout --foreground 20s qemu-system-aarch64 \
    -M virt,gic-version=2 -cpu cortex-a72 -m 256M \
    -display none -no-reboot -serial stdio \
    -kernel "$ELF" \
    > "$SERIAL_LOG" 2>&1
QEMU_RC=$?
if [ "$QEMU_RC" -ne 0 ] && [ "$QEMU_RC" -ne 124 ]; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "qemu-system-aarch64 rc=$QEMU_RC (weder 0 noch timeout)"
fi

grep -q 'MOO-ARM64-ENTRY-OK' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Marker MOO-ARM64-ENTRY-OK fehlt (Entry/Stack/BSS-Pfad)"; }
grep -q 'MOO-ARM64-UART-OK' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Marker MOO-ARM64-UART-OK fehlt (PL011-Pfad, P012-D1)"; }
grep -q 'DTB: 0x' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "DTB-Pointer-Log fehlt (x0-Durchreichung)"; }
grep -q 'MOO-ARM64-MOO-OK' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Marker MOO-ARM64-MOO-OK fehlt (moo-Code lief nicht)"; }
# P012-D2: Generic-Timer Stufe 1 — Frequenz geloggt + Counter steigt.
grep -q 'TMR-FREQ: ' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "TMR-FREQ-Log fehlt (cntfrq_el0)"; }
grep -q 'MOO-ARM64-TIMER-OK' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "MOO-ARM64-TIMER-OK fehlt (Counter steigt nicht?)"; }
if grep -q 'MOO-ARM64-TIMER-FAIL' "$SERIAL_LOG"; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "Kernel meldet TIMER-FAIL (c2 <= c1)"
fi
# P012-D3: Identity-MMU — Marker NACH Enable (Device-Map) + RAM-Roundtrip.
grep -q 'MOO-ARM64-MMU-OK' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "MOO-ARM64-MMU-OK fehlt (MMU-Enable/Device-Map/RAM-Roundtrip)"; }
if grep -q 'MOO-ARM64-MMU-FAIL' "$SERIAL_LOG"; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "Kernel meldet MMU-FAIL (RAM-Roundtrip nach Enable)"
fi
# P012-D4: GICv2-Init-Dump + Timer-IRQ-Ticks (gic-version=2 erzwungen).
grep -q 'GIC: distributor an' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "GIC-Init-Dump fehlt"; }
grep -q 'GIC-TICKS: ' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "GIC-TICKS-Log fehlt"; }
grep -q 'MOO-ARM64-GIC-OK' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "MOO-ARM64-GIC-OK fehlt (Timer-IRQ feuerte nicht)"; }
if grep -q 'MOO-ARM64-GIC-FAIL' "$SERIAL_LOG"; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "Kernel meldet GIC-FAIL (<3 IRQ-Ticks in 1s)"
fi

echo "      OK: Entry, PL011-UART, DTB-Log, Generic-Timer und moo-Code auf qemu-virt."
echo "      Serielle Ausgabe (Auszug):"
grep -E 'MOO-ARM64|DTB|TMR|GIC' "$SERIAL_LOG" | sed 's/^/      | /'
echo "############################################################"
echo "# ERGEBNIS: ARM64-Smoke gruen."
echo "############################################################"
exit 0
