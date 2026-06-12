#!/usr/bin/env bash
# P011-F1: Reproduzierbarer Legacy-BIOS-Boot-Smoke (Stage1 -> Stage2 in moo).
#
# Baut aus FESTEN Projektdateien (kein /tmp-PoC mehr):
#   beispiele/bootloader/stage2.moo          (moo-Anteil, 32-bit PM)
#   beispiele/bootloader/stage2_entry.S     (PM-Entry, SSE-Enable, call haupt)
#   compiler/runtime/boot/stage2_rt.c        (32-bit MooValue-Stubs)
#   beispiele/bootloader/stage1_loader.S     (16-bit, LBA-DAP int 0x13 AH=42)
#   beispiele/bootloader/stage1_loader.ld    (ORG 0x7C00)
#   beispiele/bootloader/stage2.ld           (ORG 0x8000)
#
# Phasen:
#   1) moo -> 32-bit-Objekt (--target i686-unknown-none)
#   2) Entry/RT/Stage1 -> Objekte (clang --target=i686-unknown-none)
#   3) Link + Layout-Gates: stage2.elf->flat, Sektorzahl berechnen,
#      Stage1 mit -DSTAGE2_SECTORS bauen, <=510B + 0x55AA, Disk-Image
#   4) QEMU-Boot OHNE GRUB (qemu -drive format=raw), Marker MOO-S2-OK
#
# EINORDNUNG (P011-D1-Erkenntnis, siehe stage2_entry.S-Header):
#   Der moo-32bit-Stage2 bleibt bewusst im Protected Mode. Der 32->64-Switch
#   gehoert in einen separaten 64-bit-Kernel — call/ret zwischen 32/64 ist
#   wegen unterschiedlicher Stack-Returnbreiten gefaehrlich/falsch.
#
# SKIP-POLITIK (P010): fehlendes Tooling => transparenter SKIP mit Exit 0.
# SKIP ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/compiler/target/release/moo-compiler"
MOO_SRC="$ROOT/beispiele/bootloader/stage2.moo"
ENTRY_S="$ROOT/beispiele/bootloader/stage2_entry.S"
STAGE1_S="$ROOT/beispiele/bootloader/stage1_loader.S"
RT_C="$ROOT/compiler/runtime/boot/stage2_rt.c"
S1_LD="$ROOT/beispiele/bootloader/stage1_loader.ld"
S2_LD="$ROOT/beispiele/bootloader/stage2.ld"

fail() { echo "      FAIL: $*"; echo "# ERGEBNIS: ROT"; exit 1; }
phase() { echo "############################################################"; echo "# PHASE $1: $2"; }
skip() {
  echo "      SKIP: $1"
  echo "      SKIP: Dies ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot (P010-Politik)."
  [ -n "${2:-}" ] && echo "      Hinweis (Arch/CachyOS): $2"
  echo "############################################################"
  echo "# ERGEBNIS: $3"
  echo "############################################################"
  exit 0
}

[ -x "$BIN" ] || fail "moo-compiler fehlt: $BIN (erst cargo build --release)"
for f in "$MOO_SRC" "$ENTRY_S" "$STAGE1_S" "$RT_C" "$S1_LD" "$S2_LD"; do
  [ -f "$f" ] || fail "Projektdatei fehlt: $f"
done
for t in clang ld.lld; do
  command -v "$t" >/dev/null 2>&1 || skip "fehlendes Tooling: $t" \
    "sudo pacman -S clang lld" "Stage2-Smoke uebersprungen (Build-Tooling fehlt)."
done
OBJCOPY="$(command -v objcopy || command -v llvm-objcopy || true)"
[ -n "$OBJCOPY" ] || skip "fehlendes Tooling: objcopy/llvm-objcopy" \
  "sudo pacman -S binutils" "Stage2-Smoke uebersprungen (objcopy fehlt)."

OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_stage2_smoke.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
cd "$OUT"   # Linker-Scripts matchen Objektnamen (stage2_entry.o etc.) per Basename

phase 1 "moo -> 32-bit-Objekt (--target i686-unknown-none)"
"$BIN" compile "$MOO_SRC" --no-stdlib --target i686-unknown-none \
    -o stage2_moo.o 2>moo.err | sed 's/^/      /'
[ -f stage2_moo.o ] || { sed 's/^/      | /' moo.err; fail "moo-Objekt nicht gebaut"; }
file stage2_moo.o | grep -q "Intel i386" || fail "moo-Objekt ist kein i386-ELF: $(file -b stage2_moo.o)"
echo "      OK: $(file -b stage2_moo.o)"

phase 2 "Entry/RT/Stage1 -> Objekte (clang --target=i686-unknown-none)"
clang --target=i686-unknown-none -ffreestanding -fno-stack-protector -fno-pic \
    -c "$ENTRY_S" -o stage2_entry.o 2>cc.err \
  || { sed 's/^/      | /' cc.err; fail "stage2_entry.S-Build fehlgeschlagen"; }
clang --target=i686-unknown-none -ffreestanding -fno-stack-protector -fno-pic \
    -c "$RT_C" -o stage2_rt.o 2>>cc.err \
  || { sed 's/^/      | /' cc.err; fail "stage2_rt.c-Build fehlgeschlagen"; }
echo "      OK: stage2_entry.o + stage2_rt.o (Stage1 folgt nach Sektorberechnung)"

phase 3 "Link + Layout-Gates (Sektorzahl, 512B/0x55AA, Disk-Image)"
ld.lld -m elf_i386 -T "$S2_LD" -o stage2.elf \
    stage2_entry.o stage2_moo.o stage2_rt.o 2>ld.err \
  || { sed 's/^/      | /' ld.err; fail "Stage2-Link fehlgeschlagen"; }
"$OBJCOPY" -O binary stage2.elf stage2.bin || fail "objcopy Stage2 fehlgeschlagen"
S2_SZ=$(stat -c %s stage2.bin)
SECTORS=$(( (S2_SZ + 511) / 512 ))
[ "$SECTORS" -ge 1 ] || fail "Stage2 ist leer"
[ "$SECTORS" -le 127 ] || fail "Stage2 zu gross fuer einen DAP-Read ($SECTORS Sektoren > 127)"
truncate -s $(( SECTORS * 512 )) stage2.bin
echo "      OK: stage2.bin = $S2_SZ Bytes -> $SECTORS Sektor(en)"

clang --target=i686-unknown-none -ffreestanding -fno-pic -DSTAGE2_SECTORS=$SECTORS \
    -c "$STAGE1_S" -o stage1_loader.o 2>>cc.err \
  || { sed 's/^/      | /' cc.err; fail "stage1_loader.S-Build fehlgeschlagen"; }
ld.lld -m elf_i386 -T "$S1_LD" -o stage1.elf stage1_loader.o 2>ld1.err \
  || { sed 's/^/      | /' ld1.err; fail "Stage1-Link fehlgeschlagen"; }
"$OBJCOPY" -O binary stage1.elf stage1.bin || fail "objcopy Stage1 fehlgeschlagen"
S1_SZ=$(stat -c %s stage1.bin)
[ "$S1_SZ" -le 510 ] || fail "Stage1-Code ist $S1_SZ Bytes (> 510) — Sector-Gate VERLETZT"
truncate -s 510 stage1.bin
printf '\x55\xaa' >> stage1.bin
SIG=$(od -A n -t x1 -j 510 -N 2 stage1.bin | tr -d ' ')
[ "$SIG" = "55aa" ] || fail "Signatur ist '$SIG' statt 55aa"
echo "      OK: stage1.bin = $S1_SZ Bytes Code, 512 total, Signatur 0x55AA"

cat stage1.bin stage2.bin > disk.img
truncate -s 65536 disk.img   # auf 64K auffuellen (BIOS-freundlich)
echo "      OK: disk.img = Stage1@LBA0 + Stage2@LBA1..$SECTORS"

phase 4 "QEMU-Boot OHNE GRUB (qemu -drive format=raw)"
command -v qemu-system-x86_64 >/dev/null 2>&1 || skip "qemu-system-x86_64 fehlt." \
  "sudo pacman -S qemu-system-x86" "Build/Layout-Gates gruen — QEMU-Boot GESKIPPT (Tooling)."
SERIAL_LOG="serial.log"
# Stage2 endet in hlt-Schleife — timeout (rc 124) ist erwartet.
timeout --foreground 15s qemu-system-x86_64 \
    -drive format=raw,file=disk.img -m 64M \
    -display none -no-reboot -serial stdio > "$SERIAL_LOG" 2>&1
RC=$?
if [ "$RC" -ne 0 ] && [ "$RC" -ne 124 ]; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -20
  fail "qemu rc=$RC (weder 0 noch timeout)"
fi
grep -q "MOO-S2-OK" "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -20; fail "Marker MOO-S2-OK fehlt auf seriell"; }
grep "MOO-S2-OK" "$SERIAL_LOG" | sed 's/^/      | /'
echo "############################################################"
echo "# ERGEBNIS: ALLE 4 Phasen gruen (Stage1->Stage2(moo) GRUB-frei gebootet)."
echo "############################################################"
