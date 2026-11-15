#!/usr/bin/env bash
# P2-T28 (Plan 607b20ba D3/D4/D5; Bootloader-Bezug P1-T118).
#   laufzeitfrei  in jeder __moo_native.*-Funktion (nativ_kern.moos und P7-Fixture
#                 system_koerper.moos) kein Aufruf eines moo_*-Symbols ausser der
#                 freistehenden Mathematik (moo_native_fmod/fmodf/pow/powf)
#   kernel        Kernel-ELF aus nativ_kern.moos: .multiboot2 genau 24 Byte und
#                 bytegleich zur C-Referenz, g_mb2_header fehlt, MB2_KOPF da;
#                 GRUB-ISO + QEMU ohne Fenster mit Marker MOO-NATIV-KERN-OK
#   referenz      hallo_kern.moos (C-Kopf) unveraendert: .multiboot2 bytegleich
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-bare-nativ
mkdir -p "$out" build/p2-bare-tmp
modus="${1:-alle}"
erlaubt='^(moo_native_fmod|moo_native_fmodf|moo_native_pow|moo_native_powf)$'

pruefe_ir() { # ll-datei
  awk -v erlaubt="$erlaubt" '
    /^define .*@"?__moo_native[.]/ { innen=1; name=$0; next }
    innen && /^}/ { innen=0; next }
    innen {
      while (match($0, /@"?moo_[A-Za-z0-9_]+/)) {
        sym = substr($0, RSTART + 1, RLENGTH - 1); gsub(/"/, "", sym)
        if ($0 ~ /call / && sym !~ erlaubt) { print "VERBOTEN " sym " in " name; fehler=1 }
        $0 = substr($0, RSTART + RLENGTH)
      }
    }
    END { exit fehler }
  ' "$1"
}

laufzeitfrei() {
  timeout 60s "$compiler" compile beispiele/kernel/nativ_kern.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/nativ_kern.ll"
  pruefe_ir "$out/nativ_kern.ll"
  timeout 60s "$compiler" compile skripte/p7/lader/system_koerper.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/system_koerper.ll"
  pruefe_ir "$out/system_koerper.ll"
  # Kontrollprobe: eine eingeschleuste moo_*-Aufrufzeile muss erkannt werden.
  awk '/^define .*@"?__moo_native[.]/ && !d { print; print "  call void @moo_print(ptr null)"; d=1; next } { print }' \
    "$out/nativ_kern.ll" > "$out/kontrolle.ll"
  if pruefe_ir "$out/kontrolle.ll" > /dev/null; then echo "FAIL laufzeitfrei: Kontrollprobe nicht erkannt"; exit 1; fi
  echo "PASS laufzeitfrei: keine moo_*-Aufrufe in __moo_native.* (nativ_kern, system_koerper; Kontrollprobe erkannt)"
}

mb2_bytes() { # elf ziel
  # Im gelinkten ELF legt das Linkerscript .multiboot2 in eine Ausgabesektion;
  # daher die 24 Byte ab dem (einzigen) Magic in den ersten 32 KiB lesen.
  head -c 32768 "$1" > "$out/kopf32k.bin"
  local treffer
  treffer=$(LC_ALL=C grep -obUaP '\xd6\x50\x52\xe8' "$out/kopf32k.bin" | cut -d: -f1)
  [ "$(printf '%s\n' "$treffer" | sed '/^$/d' | wc -l)" -eq 1 ] \
    || { echo "FAIL: nicht genau ein Multiboot2-Magic in den ersten 32 KiB von $1"; exit 1; }
  dd if="$1" of="$2" bs=1 skip="$treffer" count=24 status=none
}

kernel() {
  timeout 120s "$compiler" compile beispiele/kernel/nativ_kern.moos --no-stdlib --target x86_64-bare --kernel -o "$out/nativ_kern.elf"
  mb2_bytes "$out/nativ_kern.elf" "$out/nativ_kern_mb2.bin"
  [ "$(stat -c %s "$out/nativ_kern_mb2.bin")" -eq 24 ] || { echo "FAIL kernel: .multiboot2 nicht 24 Byte"; exit 1; }
  bash skripte/p2/bare/header_reference.sh > "$out/referenz.log"
  cmp build/p2-bare-header/header.bin "$out/nativ_kern_mb2.bin"
  nm "$out/nativ_kern.elf" > "$out/nativ_kern.nm"
  if rg -q 'g_mb2_header' "$out/nativ_kern.nm"; then echo "FAIL kernel: C-Kopf g_mb2_header noch vorhanden"; exit 1; fi
  rg -q ' MB2_KOPF$' "$out/nativ_kern.nm" || { echo "FAIL kernel: MB2_KOPF fehlt"; exit 1; }
  echo "PASS kernel-ELF: .multiboot2 = 24 Byte aus Moo, bytegleich zur C-Referenz, g_mb2_header entfaellt"
  TMPDIR="$PWD/build/p2-bare-tmp" MOO_BIN="$PWD/$compiler" KERNEL_MARKER="MOO-NATIV-KERN-OK" \
    bash scripts/kernel-smoke.sh "$PWD/beispiele/kernel/nativ_kern.moos" | tee "$out/kernel_smoke.log"
  rg -q 'OK: Marker MOO-NATIV-KERN-OK' "$out/kernel_smoke.log" || { echo "FAIL kernel: QEMU-Marker nicht belegt (SKIP?)"; exit 1; }
  echo "PASS kernel-nativ: GRUB/QEMU bootet den Moo-Kopf, nativer Treiber sendet MOO-NATIV-KERN-OK"
}

referenz() {
  timeout 120s "$compiler" compile beispiele/kernel/hallo_kern.moos --no-stdlib --target x86_64-bare --kernel -o "$out/hallo_kern.elf"
  mb2_bytes "$out/hallo_kern.elf" "$out/hallo_kern_mb2.bin"
  bash skripte/p2/bare/header_reference.sh > "$out/referenz.log"
  cmp build/p2-bare-header/header.bin "$out/hallo_kern_mb2.bin"
  nm "$out/hallo_kern.elf" > "$out/hallo_kern.nm"
  rg -q 'g_mb2_header' "$out/hallo_kern.nm" || { echo "FAIL referenz: C-Kopf fehlt"; exit 1; }
  echo "PASS referenz: hallo_kern mit C-Kopf, .multiboot2 bytegleich"
}

case "$modus" in
  laufzeitfrei) laufzeitfrei ;;
  kernel) kernel ;;
  referenz) referenz ;;
  alle) laufzeitfrei; referenz; kernel ;;
  *) echo "Unbekannter Modus" >&2; exit 2 ;;
esac
