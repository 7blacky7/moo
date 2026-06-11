#!/usr/bin/env bash
# P011-C2: GRUB-freier Boot-Smoke fuer den Stage1-Pfad.
#
# Phasen:
#   1) Sector-Image via moo-Pipeline (--linker-script stage1.ld --emit sector)
#   2) Layout-Gates: exakt 512 Bytes, Signatur 0x55AA an 510/511,
#      Negativ-Test (uebergrosses Image wird vom Sector-Gate abgelehnt)
#   3) QEMU-Boot OHNE GRUB (qemu -drive format=raw), Marker MOO-BOOT-S1-OK
#
# SERIAL-KLAERUNG (empirisch): BIOS int 0x10 erscheint NICHT auf -serial
# stdio — das Stage1-Template schreibt den Marker direkt an COM1 (0x3F8).
#
# SKIP-POLITIK (P010): fehlendes Tooling => transparenter SKIP mit Exit 0.
# SKIP ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/compiler/target/release/moo-compiler"
fail() { echo "      FAIL: $*"; echo "# ERGEBNIS: ROT"; exit 1; }
phase() { echo "############################################################"; echo "# PHASE $1: $2"; }

[ -x "$BIN" ] || fail "moo-compiler fehlt: $BIN (erst cargo build --release)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_loader_smoke.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
printf 'setze egal auf 1\n' > "$OUT/dummy.moo"

phase 1 "Sector-Image via --emit sector (Stage1-asm-Template)"
"$BIN" compile "$OUT/dummy.moo" --no-stdlib --target x86_64-bare \
    --linker-script "$ROOT/beispiele/bootloader/stage1.ld" \
    --emit sector -o "$OUT/stage1.img" 2>"$OUT/build.err" | sed 's/^/      /'
[ -f "$OUT/stage1.img" ] || { sed 's/^/      | /' "$OUT/build.err"; fail "Sector-Image nicht gebaut"; }

phase 2 "Layout-Gates (512B / 0x55AA / Negativ-Test)"
SZ=$(stat -c %s "$OUT/stage1.img")
[ "$SZ" -eq 512 ] || fail "Image ist $SZ Bytes statt exakt 512"
SIG=$(od -A n -t x1 -j 510 -N 2 "$OUT/stage1.img" | tr -d ' ')
[ "$SIG" = "55aa" ] || fail "Signatur ist '$SIG' statt 55aa"
echo "      OK: exakt 512 Bytes, Signatur 0x55AA an 510/511"
if "$BIN" compile "$ROOT/beispiele/kernel/hallo_kern.moo" --no-stdlib --target x86_64-bare \
    --kernel --emit sector -o "$OUT/neg.img" >/dev/null 2>"$OUT/neg.err"; then
  fail "Negativ-Gate: uebergrosses Image wurde NICHT abgelehnt"
fi
grep -q "Sector-Gate VERLETZT" "$OUT/neg.err" || { sed 's/^/      | /' "$OUT/neg.err" | tail -5; fail "Negativ-Gate: unerwartete Fehlermeldung"; }
echo "      OK: Negativ-Gate greift (uebergrosses Image abgelehnt, klare Meldung)"

phase 3 "QEMU-Boot OHNE GRUB (qemu -drive format=raw)"
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "      SKIP: qemu-system-x86_64 fehlt."
  echo "      SKIP: Dies ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot (P010-Politik)."
  echo "      Hinweis (Arch/CachyOS): sudo pacman -S qemu-system-x86"
  echo "############################################################"
  echo "# ERGEBNIS: Gates gruen — QEMU-Boot GESKIPPT (Tooling)."
  echo "############################################################"
  exit 0
fi
SERIAL_LOG="$OUT/serial.log"
# Stage1 endet in hlt-Schleife — timeout (rc 124) ist erwartet.
timeout --foreground 15s qemu-system-x86_64 \
    -drive format=raw,file="$OUT/stage1.img" -m 64M \
    -display none -no-reboot -serial stdio > "$SERIAL_LOG" 2>&1
RC=$?
if [ "$RC" -ne 0 ] && [ "$RC" -ne 124 ]; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -20
  fail "qemu rc=$RC (weder 0 noch timeout)"
fi
grep -q "MOO-BOOT-S1-OK" "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -20; fail "Marker MOO-BOOT-S1-OK fehlt auf seriell"; }
grep "MOO-BOOT-S1-OK" "$SERIAL_LOG" | sed 's/^/      | /'
echo "############################################################"
echo "# ERGEBNIS: ALLE 3 Phasen gruen (GRUB-frei gebootet)."
echo "############################################################"
