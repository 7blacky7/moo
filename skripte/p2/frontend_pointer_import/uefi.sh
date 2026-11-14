#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-frontend-pointer-import/uefi
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for tool in clang++ lld-link qemu-system-x86_64 parted mformat mmd mcopy; do
    command -v "$tool" >/dev/null || { echo "Fehlendes Werkzeug: $tool" >&2; exit 1; }
done
code=/usr/share/edk2/x64/OVMF_CODE.4m.fd
vars=/usr/share/edk2/x64/OVMF_VARS.4m.fd
[[ -r "$code" && -r "$vars" ]] || { echo 'OVMF fehlt' >&2; exit 1; }
clang++ --target=x86_64-unknown-windows -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c compiler/tests/p2/import_zeiger_uefi.cpp -o "$out/entry.obj"
for form in namen pfad; do
    src="compiler/tests/p2/import_zeiger_$form.moos"
    "$compiler" compile "$src" --no-stdlib --target x86_64-unknown-uefi --emit-ir -o "$out/$form.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$form.ll"
    mise exec -- llvm-extract -S --func=__moo_native.p2_modul_zeiger_u16 "$out/$form.ll" -o "$out/$form.extract.ll"
    mise exec -- llc -filetype=obj "$out/$form.extract.ll" -o "$out/$form.obj"
    lld-link /subsystem:efi_application /entry:efi_main /out:"$out/BOOTX64.EFI" "$out/entry.obj" "$out/$form.obj"
    truncate -s 0 "$out/disk.img"
    truncate -s 48M "$out/disk.img"
    parted -s "$out/disk.img" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on
    image="$out/disk.img@@1048576"
    mformat -i "$image" -F ::
    mmd -i "$image" ::/EFI ::/EFI/BOOT
    mcopy -i "$image" "$out/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
    cp "$vars" "$out/vars.fd"
    : > "$out/$form.log"
    rc=0
    timeout 20s qemu-system-x86_64 -drive "if=pflash,format=raw,unit=0,readonly=on,file=$code" -drive "if=pflash,format=raw,unit=1,file=$out/vars.fd" -drive "format=raw,file=$out/disk.img" -m 128M -display none -no-reboot -serial none -debugcon "file:$out/$form.log" -global isa-debugcon.iobase=0xe9 || rc=$?
    [[ "$rc" == 0 || "$rc" == 124 ]] || { echo "QEMU RC=$rc" >&2; exit 1; }
    if rg -q -F 'P2-IMPORT-UEFI-FAIL' "$out/$form.log"; then echo 'UEFI-Pointerimport falsch' >&2; exit 1; fi
    rg -q -F 'P2-IMPORT-UEFI-PASS' "$out/$form.log"
    printf 'PASS %s: UEFI-C++-Adapter ruft importierten ptr[u16]-Callee\n' "$form"
done
