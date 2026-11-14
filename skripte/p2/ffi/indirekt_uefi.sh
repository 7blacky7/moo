#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/indirekt-uefi
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for tool in clang lld-link qemu-system-x86_64 parted mformat mmd mcopy; do
    command -v "$tool" >/dev/null || { echo "Fehlendes Werkzeug: $tool" >&2; exit 1; }
done
code=/usr/share/edk2/x64/OVMF_CODE.4m.fd
vars=/usr/share/edk2/x64/OVMF_VARS.4m.fd
[[ -r "$code" && -r "$vars" ]] || { echo 'OVMF fehlt' >&2; exit 1; }
"$compiler" compile compiler/tests/p2/ffi_indirekt_teil.moos --no-stdlib --target x86_64-unknown-uefi --emit-ir -o "$out/moo.ll"
mise exec -- opt -passes=verify -disable-output "$out/moo.ll"
mise exec -- llvm-extract -S --recursive --func=__moo_native.p2_indirekt_zehn --func=__moo_native.p2_indirekt_i8 --func=__moo_native.p2_indirekt_u8 --func=__moo_native.p2_indirekt_i16 --func=__moo_native.p2_indirekt_u16 "$out/moo.ll" -o "$out/moo.extract.ll"
mise exec -- llc -filetype=obj "$out/moo.extract.ll" -o "$out/moo.obj"
"$compiler" compile compiler/tests/p2/ffi_indirekt_void_teil.moos --no-stdlib --target x86_64-unknown-uefi --emit-ir -o "$out/void.ll"
mise exec -- opt -passes=verify -disable-output "$out/void.ll"
mise exec -- llvm-extract -S --recursive --func=__moo_native.p2_indirekt_void "$out/void.ll" -o "$out/void.extract.ll"
mise exec -- llc -filetype=obj "$out/void.extract.ll" -o "$out/void.obj"
clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector -c compiler/tests/p2/ffi_indirekt_uefi.c -o "$out/entry.obj"
lld-link /subsystem:efi_application /entry:efi_main /out:"$out/BOOTX64.EFI" "$out/entry.obj" "$out/moo.obj" "$out/void.obj"
truncate -s 0 "$out/disk.img"
truncate -s 48M "$out/disk.img"
parted -s "$out/disk.img" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on
image="$out/disk.img@@1048576"
mformat -i "$image" -F ::
mmd -i "$image" ::/EFI ::/EFI/BOOT
mcopy -i "$image" "$out/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
cp "$vars" "$out/vars.fd"
: > "$out/debug.log"
rc=0
timeout 20s qemu-system-x86_64 -drive "if=pflash,format=raw,unit=0,readonly=on,file=$code" -drive "if=pflash,format=raw,unit=1,file=$out/vars.fd" -drive "format=raw,file=$out/disk.img" -m 128M -display none -no-reboot -serial none -debugcon "file:$out/debug.log" -global isa-debugcon.iobase=0xe9 || rc=$?
[[ "$rc" == 0 || "$rc" == 124 ]] || { echo "QEMU RC=$rc" >&2; exit 1; }
if grep -Fq 'P2-UEFI-INDIRECT-FAIL' "$out/debug.log"; then echo 'UEFI indirekter Aufruf falsch' >&2; exit 1; fi
grep -Fq 'P2-UEFI-INDIRECT-PASS' "$out/debug.log"
printf 'UEFI QEMU/OVMF indirekter 10-Argumente-Aufruf PASS; kein GOP-Bild\n'
