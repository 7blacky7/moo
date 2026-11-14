#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/uefi-runtime
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for tool in clang lld-link qemu-system-x86_64 parted mformat mmd mcopy; do
    command -v "$tool" >/dev/null || { echo "Fehlendes Werkzeug: $tool" >&2; exit 1; }
done
code=/usr/share/edk2/x64/OVMF_CODE.4m.fd
vars=/usr/share/edk2/x64/OVMF_VARS.4m.fd
[[ -r "$code" && -r "$vars" ]] || { echo 'OVMF fehlt' >&2; exit 1; }
"$compiler" compile beispiele/uefi/hallo_uefi.moos --no-stdlib --target x86_64-unknown-uefi -o "$out/moo.obj"
clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector -c compiler/runtime/boot/uefi_entry.c -o "$out/entry.obj"
clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector -c compiler/runtime/boot/uefi_rt.c -o "$out/rt.obj"
lld-link /subsystem:efi_application /entry:efi_main /out:"$out/BOOTX64.EFI" "$out/entry.obj" "$out/moo.obj" "$out/rt.obj"
truncate -s 0 "$out/disk.img"
truncate -s 48M "$out/disk.img"
parted -s "$out/disk.img" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on
image="$out/disk.img@@1048576"
mformat -i "$image" -F ::
mmd -i "$image" ::/EFI ::/EFI/BOOT
mcopy -i "$image" "$out/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
cp "$vars" "$out/vars.fd"
: > "$out/serial.log"
: > "$out/debug.log"
rc=0
timeout 20s qemu-system-x86_64 -drive "if=pflash,format=raw,unit=0,readonly=on,file=$code" -drive "if=pflash,format=raw,unit=1,file=$out/vars.fd" -drive "format=raw,file=$out/disk.img" -m 128M -display none -no-reboot -serial "file:$out/serial.log" -debugcon "file:$out/debug.log" -global isa-debugcon.iobase=0xe9 || rc=$?
[[ "$rc" == 0 || "$rc" == 124 ]] || { echo "QEMU RC=$rc" >&2; exit 1; }
grep -Fq 'MOO-UEFI-ENTRY' "$out/debug.log"
grep -Fq 'MOO-UEFI-OK' "$out/debug.log"
grep -Fq 'MOO-UEFI-MOO' "$out/serial.log"
printf 'UEFI QEMU/OVMF: C-Entry, Moo-Serienmarker und Rückkehr PASS; kein Bildschirmnachweis\n'
