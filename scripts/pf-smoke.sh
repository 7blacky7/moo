#!/usr/bin/env bash
# pf-smoke.sh — P012-A3: #PF-Handler-Smoke (intentionaler Page Fault).
# ============================================================================
# PHASEN
#   [1/3] Build: beispiele/kernel/pf_test.moos via One-Shot-Kernel-Pipeline
#   [2/3] Tooling-Check (qemu-system-x86_64 / grub-mkrescue / xorriso)
#         -> fehlend: TRANSPARENTER SKIP, Exit 0 (Projekt-Konvention).
#            Der Skip ist KEIN finaler Beweis — das A-Gate verlangt den Boot.
#   [3/3] QEMU-Boot (GRUB-ISO): asserts auf seriell
#         - MOO-PF-START          (Kernel lief an)
#         - '#PF PAGE-FAULT'      (Handler feuerte)
#         - CR2 : 0x0000000040000000  (Fault-Adresse = 1 GB, exakt)
#         - 'P=0(nicht praesent)' (Error-Code korrekt dekodiert)
#         - KEIN MOO-PF-MISS      (Code hinter dem Fault blieb unerreicht)
#
# run_all.sh bleibt QEMU-frei — dieses Script ist eine separate Schiene
# (Konvention wie kernel-smoke.sh / stage2-smoke.sh / uefi-smoke.sh).
# EXIT-CODES: 0 = gruen ODER sauber geskippt; 1 = Fehler.
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOO_BIN="${MOO_BIN:-$ROOT/compiler/target/release/moo-compiler}"
KERNEL_SRC="$ROOT/beispiele/kernel/pf_test.moos"

OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_pf_smoke.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
ELF="$OUT/pf_test.elf"

fail() { echo "FAIL: $*" >&2; exit 1; }

echo "############################################################"
echo "# moo #PF-Smoke (P012-A3)"
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
  echo "      SKIP: Dies ist KEIN finaler Beweis — das A-Gate verlangt den echten Boot."
  echo "      Hinweis (Arch/CachyOS): sudo pacman -S qemu-system-x86 grub xorriso"
  echo "############################################################"
  echo "# ERGEBNIS: Build gruen — QEMU-#PF-Test GESKIPPT (Tooling)."
  echo "############################################################"
  exit 0
fi
echo "      OK: qemu/grub/xorriso vorhanden"

# --- [3/3] QEMU-Boot ----------------------------------------------------------
echo "[3/3] QEMU-Boot + #PF-Asserts ..."
ISO_DIR="$OUT/iso"
mkdir -p "$ISO_DIR/boot/grub"
cp "$ELF" "$ISO_DIR/boot/pf_test.elf"
cat > "$ISO_DIR/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
menuentry "moo-pf-test" {
    multiboot2 /boot/pf_test.elf
    boot
}
EOF

grub-mkrescue -o "$OUT/moo.iso" "$ISO_DIR" >"$OUT/grub.log" 2>&1 \
  || { sed 's/^/      | /' "$OUT/grub.log"; fail "grub-mkrescue fehlgeschlagen"; }

SERIAL_LOG="$OUT/serial.log"
# Der Handler endet in cli/hlt — QEMU laeuft weiter, timeout (rc 124) ist erwartet.
timeout --foreground 20s qemu-system-x86_64 \
    -cdrom "$OUT/moo.iso" -m 128M \
    -display none -no-reboot -serial stdio \
    > "$SERIAL_LOG" 2>&1
QEMU_RC=$?
if [ "$QEMU_RC" -ne 0 ] && [ "$QEMU_RC" -ne 124 ]; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "qemu-system-x86_64 rc=$QEMU_RC (weder 0 noch timeout)"
fi

grep -q 'MOO-PF-START' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Marker MOO-PF-START fehlt (Kernel lief nicht an)"; }
grep -q '#PF PAGE-FAULT' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "#PF-Handler hat nicht gefeuert"; }
grep -q 'CR2 : 0x0000000040000000' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "CR2 zeigt nicht die Fault-Adresse 0x40000000"; }
grep -q 'P=0(nicht praesent)' "$SERIAL_LOG" \
  || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Error-Code-Bit P nicht als 'nicht praesent' dekodiert"; }
if grep -q 'MOO-PF-MISS' "$SERIAL_LOG"; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "MOO-PF-MISS gesehen — Code hinter dem Fault wurde erreicht"
fi

echo "      OK: #PF gefeuert, CR2/Error-Code korrekt, kein MISS."
echo "      Serielle Ausgabe (Auszug):"
grep -E 'MOO-PF|KERN|CR2|RIP|Code|Bits' "$SERIAL_LOG" | sed 's/^/      | /'
echo "############################################################"
echo "# ERGEBNIS: #PF-Smoke gruen."
echo "############################################################"
exit 0
