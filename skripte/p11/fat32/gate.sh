#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=$root/build/p11/fat32
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
mkdir -p "$out"
mode=${1:-alle}

unicode() {
  ref="$out/UnicodeData-17.0.0.txt"
  if [[ ! -f "$ref" ]]; then
    curl --fail --location --max-time 30 https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt -o "$ref"
  fi
  "$compiler" compile compiler/tests/p11/fat32_unicode.moos -o "$out/unicode"
  "$out/unicode" >"$out/unicode.bin"
  uv run --no-project --offline python -B skripte/p11/fat32/unicode.py "$ref" "$out/unicode.bin"
}


fuzz() {
  "$compiler" compile compiler/tests/p11/fat32_fuzz.moos -o "$out/fuzz"
  timeout 120s "$out/fuzz" >"$out/fuzz.out"
  diff -u compiler/tests/p11/fat32_fuzz.expected "$out/fuzz.out"
  echo "PASS 100000 BPB/100000 UTF-8/10000 LFN/10000 Clusterketten + 80 EIO-Punkte"
}

ram() {
  "$compiler" compile compiler/tests/p11/fat32.moos -o "$out/fat32"
  "$out/fat32" >"$out/fat32.ausgabe"
  diff -u compiler/tests/p11/fat32.expected "$out/fat32.ausgabe"
  "$compiler" compile compiler/tests/p11/fat32_transport.moos -o "$out/transport"
  "$out/transport" >"$out/transport.ausgabe"
  diff -u compiler/tests/p11/fat32_transport.expected "$out/transport.ausgabe"
  "$compiler" compile compiler/tests/p11/fat32_probe.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/fat-bare.ll"
  mise exec -- opt -passes=verify -disable-output "$out/fat-bare.ll"
  "$compiler" compile beispiele/kernel/speicher_fat.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/kern-adapter.ll"
  mise exec -- opt -passes=verify -disable-output "$out/kern-adapter.ll"
  echo "PASS RAM-Dateisystem, Transport und Bare-IR"
}
abbild() {
  "$compiler" compile compiler/tests/p11/fat32_abbild.moos -o "$out/abbild"
  uv run --no-project --offline python -c 'import sys; sys.stdout.buffer.write(bytes((i*37+11)%256 for i in range(256))*4096)' >"$out/payload.soll"
  for breite in 512 4096; do
    image="$out/volume-$breite.img"
    size=64M
    [[ $breite == 4096 ]] && size=512M
    truncate -s 0 "$image"
    truncate -s "$size" "$image"
    MOO_FAT_ABBILD="$image" MOO_FAT_BREITE="$breite" MOO_FAT_MODUS=f "$out/abbild" >"$out/format-$breite.out"
    diff -u compiler/tests/p11/fat32.expected "$out/format-$breite.out"
    fsck.vfat -n "$image" >"$out/fsck-$breite.log"
    uv run --no-project --offline python -B skripte/p11/fat32/struktur.py "$image"
    mcopy -o -i "$image" "::/Moo lange Datei Grüße.bin" "$out/payload-$breite.ist"
    cmp "$out/payload.soll" "$out/payload-$breite.ist"
    mcopy -o -i "$image" "$out/payload.soll" "::/Fremde lange Datei.bin"
    MOO_FAT_ABBILD="$image" MOO_FAT_BREITE="$breite" MOO_FAT_MODUS=r "$out/abbild" >"$out/import-$breite.out"
    diff -u compiler/tests/p11/fat32.expected "$out/import-$breite.out"
    fsck.vfat -n "$image" >"$out/fsck-import-$breite.log"
    uv run --no-project --offline python -B skripte/p11/fat32/struktur.py "$image"
  done
  echo "PASS 512/4096-Byte-Sektoren, beide FATs/FSInfo, fsck.vfat und mcopy in beiden Richtungen: 1 MiB bytegleich"
}
uefi() {
  "$compiler" compile compiler/tests/p11/fat32_uefi.moos --no-stdlib --target x86_64-unknown-uefi --emit-ir -o "$out/uefi.ll"
  mise exec -- opt -passes=verify -disable-output "$out/uefi.ll"
  mise exec -- llvm-extract -S --recursive --func=efi_main "$out/uefi.ll" -o "$out/uefi-entry.ll"
  mise exec -- llc -filetype=obj "$out/uefi-entry.ll" -o "$out/uefi-entry.obj"
  mise exec -- lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib "/out:$out/BOOTX64.EFI" "$out/uefi-entry.obj"
  "$compiler" compile compiler/tests/p11/fat32_abbild.moos -o "$out/abbild"
  truncate -s 0 "$out/esp.img"
  truncate -s 64M "$out/esp.img"
  MOO_FAT_ABBILD="$out/esp.img" MOO_FAT_BREITE=512 MOO_FAT_MODUS=f MOO_FAT_EFI="$out/BOOTX64.EFI" "$out/abbild" >"$out/esp.out"
  diff -u compiler/tests/p11/fat32.expected "$out/esp.out"
  fsck.vfat -n "$out/esp.img" >"$out/fsck-esp.log"
  uv run --no-project --offline python -B skripte/p11/fat32/struktur.py "$out/esp.img"
  mcopy -o -i "$out/esp.img" ::/EFI/BOOT/BOOTX64.EFI "$out/BOOTX64.ist"
  cmp "$out/BOOTX64.EFI" "$out/BOOTX64.ist"
  truncate -s 0 "$out/disk.img"
  truncate -s 128M "$out/disk.img"
  parted -s "$out/disk.img" mklabel gpt mkpart ESP fat32 1MiB 65MiB set 1 esp on
  dd if="$out/esp.img" of="$out/disk.img" bs=1M seek=1 conv=notrunc status=none
  code=/usr/share/edk2/x64/OVMF_CODE.4m.fd
  vars=/usr/share/edk2/x64/OVMF_VARS.4m.fd
  cp "$vars" "$out/vars.fd"
  rc=0
  timeout 45s qemu-system-x86_64 -accel tcg -m 128M -display none -serial none -monitor none -no-reboot \
    -drive "if=pflash,format=raw,unit=0,readonly=on,file=$code" \
    -drive "if=pflash,format=raw,unit=1,file=$out/vars.fd" \
    -drive "format=raw,file=$out/disk.img" -debugcon "file:$out/boot.log" \
    -global isa-debugcon.iobase=0xe9 -device isa-debug-exit,iobase=0xf4,iosize=4 || rc=$?
  [[ $rc == 85 ]] || { echo "FAIL OVMF Exit $rc" >&2; exit 1; }
  grep -Fx "P11-FAT32-BOOT-PASS" "$out/boot.log"
  echo "PASS OVMF bootet von der durch Moo formatierten und beschriebenen GPT-ESP"
}
case "$mode" in
  unicode) unicode ;;
  ram) ram ;;
  fuzz) fuzz ;;
  abbild) abbild ;;
  uefi) uefi ;;
  alle) unicode; ram; fuzz; abbild; uefi ;;
  *) echo "Unbekanntes Gate $mode" >&2; exit 2 ;;
esac
