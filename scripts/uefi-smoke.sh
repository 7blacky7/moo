#!/usr/bin/env bash
# P011-F1: Reproduzierbarer UEFI-Boot-Smoke fuer moo als EFI-Application.
#
# Baut aus FESTEN Projektdateien (kein /tmp-PoC mehr):
#   beispiele/uefi/hallo_uefi.moo           (moo-App)
#   compiler/runtime/boot/uefi_entry.c      (efi_main-Entry)
#   compiler/runtime/boot/uefi_rt.c         (COFF-MooValue-Stubs)
#
# Phasen:
#   1) moo -> PE/COFF (--target x86_64-unknown-uefi), file-Check
#   2) Entry+RT -> COFF (clang --target=x86_64-unknown-windows)
#   3) lld-link -> PE32+ EFI application (BOOTX64.EFI)
#   4) GPT-Disk mit ESP + EFI/BOOT/BOOTX64.EFI, OVMF-Boot,
#      Marker MOO-UEFI-ENTRY + MOO-UEFI-OK (+ MOO-UEFI-MOO aus dem moo-Teil)
#
# BEWEISKANAL: 0xE9 debugcon (QEMU-stdio) ist zuverlaessiger als COM1 im
# UEFI-Kontext (OVMF nutzt COM1 selbst). Der Entry schreibt auf beide.
#
# SKIP-POLITIK (P010): fehlendes Tooling => transparenter SKIP mit Exit 0.
# SKIP ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/compiler/target/release/moo-compiler"
MOO_SRC="$ROOT/beispiele/uefi/hallo_uefi.moo"
ENTRY_C="$ROOT/compiler/runtime/boot/uefi_entry.c"
RT_C="$ROOT/compiler/runtime/boot/uefi_rt.c"

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
[ -f "$MOO_SRC" ] || fail "moo-Quelle fehlt: $MOO_SRC"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_uefi_smoke.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

# --- Tooling-Pruefung (clang/lld-link/parted/mtools/qemu/OVMF) ---------------
for t in clang lld-link; do
  command -v "$t" >/dev/null 2>&1 || skip "fehlendes Tooling: $t" \
    "sudo pacman -S clang lld" "Build-Tooling fehlt — UEFI-Smoke uebersprungen."
done

phase 1 "moo -> PE/COFF (--target x86_64-unknown-uefi)"
"$BIN" compile "$MOO_SRC" --no-stdlib --target x86_64-unknown-uefi \
    -o "$OUT/uefi_moo.obj" 2>"$OUT/moo.err" | sed 's/^/      /'
[ -f "$OUT/uefi_moo.obj" ] || { sed 's/^/      | /' "$OUT/moo.err"; fail "moo-COFF nicht gebaut"; }
file "$OUT/uefi_moo.obj" | grep -qi "COFF" || fail "moo-Objekt ist kein COFF"
echo "      OK: $(file -b "$OUT/uefi_moo.obj")"

phase 2 "Entry + Runtime -> COFF (clang --target=x86_64-unknown-windows)"
clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector \
    -c "$ENTRY_C" -o "$OUT/uefi_entry.obj" 2>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "efi_entry.c-Build fehlgeschlagen"; }
clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector \
    -c "$RT_C" -o "$OUT/uefi_rt.obj" 2>>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "uefi_rt.c-Build fehlgeschlagen"; }
echo "      OK: uefi_entry.obj + uefi_rt.obj"

phase 3 "lld-link -> PE32+ EFI application"
lld-link /subsystem:efi_application /entry:efi_main /out:"$OUT/BOOTX64.EFI" \
    "$OUT/uefi_entry.obj" "$OUT/uefi_moo.obj" "$OUT/uefi_rt.obj" \
    2>"$OUT/link.err" | sed 's/^/      /'
[ -f "$OUT/BOOTX64.EFI" ] || { sed 's/^/      | /' "$OUT/link.err"; fail "BOOTX64.EFI nicht gelinkt"; }
file "$OUT/BOOTX64.EFI" | grep -qiE "EFI.*application|PE32\+" || fail "kein PE32+ EFI-Binary"
echo "      OK: $(file -b "$OUT/BOOTX64.EFI")"

phase 4 "OVMF-Boot (GPT-Disk mit ESP)"
# Tooling fuer den echten Boot
MISS=()
for t in qemu-system-x86_64 parted mformat mcopy; do
  command -v "$t" >/dev/null 2>&1 || MISS+=("$t")
done
OVMF_CODE=""
for c in /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd \
         /usr/share/OVMF/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_CODE.fd; do
  [ -f "$c" ] && { OVMF_CODE="$c"; break; }
done
OVMF_VARS=""
for v in /usr/share/edk2/x64/OVMF_VARS.4m.fd /usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd \
         /usr/share/OVMF/OVMF_VARS.4m.fd /usr/share/edk2/x64/OVMF_VARS.fd; do
  [ -f "$v" ] && { OVMF_VARS="$v"; break; }
done
[ -z "$OVMF_CODE" ] && MISS+=("OVMF_CODE.fd")
[ -z "$OVMF_VARS" ] && MISS+=("OVMF_VARS.fd")
if [ "${#MISS[@]}" -gt 0 ]; then
  skip "fehlendes Tooling: ${MISS[*]}" \
    "sudo pacman -S qemu-system-x86 parted mtools edk2-ovmf" \
    "Build-Phasen 1-3 gruen — OVMF-Boot GESKIPPT (Tooling)."
fi

DISK="$OUT/uefi.img"
dd if=/dev/zero of="$DISK" bs=1M count=48 status=none
parted -s "$DISK" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on >/dev/null 2>&1 \
  || fail "parted GPT/ESP fehlgeschlagen"
OFF=$((2048*512))
mformat -i "$DISK"@@${OFF} -F :: >/dev/null 2>&1 || fail "mformat ESP fehlgeschlagen"
mmd -i "$DISK"@@${OFF} ::/EFI ::/EFI/BOOT >/dev/null 2>&1
mcopy -i "$DISK"@@${OFF} "$OUT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1 \
  || fail "mcopy BOOTX64.EFI fehlgeschlagen"

cp "$OVMF_VARS" "$OUT/vars.fd"
SERIAL_LOG="$OUT/serial.log"
# EFI-App endet in hlt-Schleife — timeout (rc 124) ist erwartet.
timeout --foreground 30s qemu-system-x86_64 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
    -drive format=raw,file="$DISK" \
    -m 128M -display none -no-reboot \
    -debugcon file:"$SERIAL_LOG" -global isa-debugcon.iobase=0xe9 \
    >/dev/null 2>&1
RC=$?
if [ "$RC" -ne 0 ] && [ "$RC" -ne 124 ]; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -20
  fail "qemu rc=$RC (weder 0 noch timeout)"
fi
grep -q "MOO-UEFI-ENTRY" "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -20; fail "Marker MOO-UEFI-ENTRY fehlt"; }
grep -q "MOO-UEFI-OK"    "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -20; fail "Marker MOO-UEFI-OK fehlt (Rueckkehr aus moo?)"; }
grep -E "MOO-UEFI" "$SERIAL_LOG" | sed 's/^/      | /'
echo "############################################################"
echo "# ERGEBNIS: ALLE 4 Phasen gruen (moo bootet als UEFI-App via OVMF)."
echo "############################################################"
