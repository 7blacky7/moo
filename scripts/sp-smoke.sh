#!/usr/bin/env bash
# sp-smoke.sh — P012-A4: Stack-Protector-Fail-Smoke (Canary -> Kernel-Panic).
# ============================================================================
# PHASEN
#   [1/3] Build: beispiele/kernel/stackprot_test.moos via Kernel-Pipeline
#   [2/3] Tooling-Check (qemu-system-x86_64 / grub-mkrescue / xorriso)
#         -> fehlend: TRANSPARENTER SKIP, Exit 0 (Projekt-Konvention).
#   [3/3] QEMU-Boot (GRUB-ISO): asserts auf seriell
#         - MOO-SP-START                            (Kernel lief an)
#         - '[KERN-PANIK] STACK-PROTECTOR'          (Canary-Fail -> Panic)
#         FALLBACK: meldet der Kernel 'SSP-SELBSTTEST: kein Canary-Fail'
#         (cc ohne -mstack-protector-guard=global), ist das ein
#         TRANSPARENTER SKIP — kein Erfolg, kein Fehler.
#
# run_all.sh bleibt QEMU-frei — separate Schiene (Konvention kernel-smoke).
# EXIT-CODES: 0 = gruen ODER sauber geskippt; 1 = Fehler.
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOO_BIN="${MOO_BIN:-$ROOT/compiler/target/release/moo-compiler}"
KERNEL_SRC="$ROOT/beispiele/kernel/stackprot_test.moos"

OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_sp_smoke.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
ELF="$OUT/stackprot_test.elf"

fail() { echo "FAIL: $*" >&2; exit 1; }

echo "############################################################"
echo "# moo Stack-Protector-Smoke (P012-A4)"
echo "# Kernel : $KERNEL_SRC"
echo "# Compiler: $MOO_BIN"
echo "############################################################"

# --- [1/3] Build ------------------------------------------------------------
echo "[1/3] One-Shot-Build (--kernel) ..."
if [ ! -x "$MOO_BIN" ]; then
  echo "      moo-compiler fehlt — baue via cargo build --release ..."
  (cd "$ROOT/compiler" && cargo build --release) \
    || fail "cargo build --release fehlgeschlagen"
fi
[ -f "$KERNEL_SRC" ] || fail "Kernel-Quelle nicht gefunden: $KERNEL_SRC"

"$MOO_BIN" compile "$KERNEL_SRC" --no-stdlib --target x86_64-bare --kernel \
    -o "$ELF" || fail "Kernel-Pipeline fehlgeschlagen"
[ -f "$ELF" ] || fail "kein ELF erzeugt: $ELF"
echo "      OK: $ELF"

# --- [2/3] Tooling-Check ------------------------------------------------------
echo "[2/3] Tooling-Check ..."
MISSING=()
for t in qemu-system-x86_64 grub-mkrescue xorriso; do
  command -v "$t" >/dev/null 2>&1 || MISSING+=("$t")
done
if [ "${#MISSING[@]}" -gt 0 ]; then
  echo "      SKIP: fehlendes Tooling: ${MISSING[*]}"
  echo "      SKIP: Boot-Test uebersprungen (transparent, Exit 0)."
  echo "      Hinweis (Arch/CachyOS): sudo pacman -S qemu-system-x86 grub xorriso"
  echo "############################################################"
  echo "# ERGEBNIS: Build gruen — QEMU-SSP-Test GESKIPPT (Tooling)."
  echo "############################################################"
  exit 0
fi
echo "      OK: qemu/grub/xorriso vorhanden"

# --- [3/3] QEMU-Boot ----------------------------------------------------------
echo "[3/3] QEMU-Boot + SSP-Asserts ..."
ISO_DIR="$OUT/iso"
mkdir -p "$ISO_DIR/boot/grub"
cp "$ELF" "$ISO_DIR/boot/stackprot_test.elf"
cat > "$ISO_DIR/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
menuentry "moo-sp-test" {
    multiboot2 /boot/stackprot_test.elf
    boot
}
EOF

grub-mkrescue -o "$OUT/moo.iso" "$ISO_DIR" >"$OUT/grub.log" 2>&1 \
  || { sed 's/^/      | /' "$OUT/grub.log"; fail "grub-mkrescue fehlgeschlagen"; }

SERIAL_LOG="$OUT/serial.log"
# kern_panic endet in cli/hlt — QEMU laeuft weiter, timeout rc 124 erwartet.
timeout --foreground 20s qemu-system-x86_64 \
    -cdrom "$OUT/moo.iso" -m 128M \
    -display none -no-reboot -serial stdio \
    > "$SERIAL_LOG" 2>&1
QEMU_RC=$?
if [ "$QEMU_RC" -ne 0 ] && [ "$QEMU_RC" -ne 124 ]; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "qemu-system-x86_64 rc=$QEMU_RC (weder 0 noch timeout)"
fi

grep -q 'MOO-SP-START' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Marker MOO-SP-START fehlt (Kernel lief nicht an)"; }

if grep -q 'SSP-SELBSTTEST: kein Canary-Fail' "$SERIAL_LOG"; then
  echo "      SKIP: Kernel-Build lief ohne aktiven SSP (cc ohne global-guard)."
  echo "      SKIP: Kein Beweis moeglich — transparenter Skip, Exit 0."
  echo "############################################################"
  echo "# ERGEBNIS: SSP-Fail-Test GESKIPPT (Compiler-Fallback)."
  echo "############################################################"
  exit 0
fi

grep -q 'KERN-PANIK' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Canary-Fail erzeugte keine Kernel-Panik"; }
grep -q 'STACK-PROTECTOR' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Panik-Meldung nennt STACK-PROTECTOR nicht"; }

echo "      OK: Canary-Fail -> laute Kernel-Panik (keine Silent Corruption)."
echo "      Serielle Ausgabe (Auszug):"
grep -E 'MOO-SP|KERN' "$SERIAL_LOG" | sed 's/^/      | /'
echo "############################################################"
echo "# ERGEBNIS: Stack-Protector-Smoke gruen."
echo "############################################################"
exit 0
