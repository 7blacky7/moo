#!/usr/bin/env bash
# P011-F1 + P7-T2: Reproduzierbarer UEFI-Boot-Smoke fuer moo als EFI-Application
# MIT Bildschirmnachweis (QEMU/OVMF-Screenshot).
#
# Baut aus FESTEN Projektdateien (kein /tmp-PoC mehr):
#   beispiele/uefi/hallo_uefi.moos           (moo-App: zeichnet MoOS-Bild per surface_*)
#   compiler/runtime/boot/uefi_entry.c      (efi_main, GOP-Adapter, Heap-Arena)
#   compiler/runtime/boot/uefi_rt.c         (Heap-/String-/libm-Bruecke, _fltused)
#   compiler/runtime/moo_bare.c, moo_bare_alloc.c, moo_freestanding_mem.c
#   compiler/runtime/moo_surface_core.c, moo_surface.c   (vorhandener Surface-Kern)
#   compiler/runtime/moo_native_math.c       (P2-T58: Float-%/** nativer Funktionen, kein libm)
#
# Phasen:
#   1) moo -> PE/COFF (--target x86_64-unknown-uefi), file-Check
#   2) C-Runtime -> COFF (clang --target=x86_64-w64-windows-gnu -ffreestanding)
#   3) lld-link -> PE32+ EFI application (BOOTX64.EFI), /nodefaultlib /opt:ref
#   4) GPT-Disk mit ESP + EFI/BOOT/BOOTX64.EFI, OVMF-Boot mit -vga std,
#      Marker MOO-UEFI-ENTRY / GOP-OK / HEAP-OK / OK (debugcon 0xE9) sowie
#      MOO-UEFI-MOO + MOO-UEFI-BILD (COM1, aus dem moo-Teil),
#      Screenshot per QEMU-Monitor 'screendump', OCR mit tesseract (falls da),
#      Eingabe per Monitor: sendkey tab/ret -> Marker MOO-UEFI-FOKUS 1 /
#      MOO-UEFI-AKTION 1, usb-tablet mouse_move/mouse_button -> MOO-UEFI-KLICK /
#      MOO-UEFI-AKTION 0 (Maus fail-closed, wenn MOO-UEFI-MAUS-FEHLT),
#      sendkey esc -> Schleife endet -> MOO-UEFI-OK. Akzentlinie nach Aktion
#      bernstein (Pixelprobe mit magick, falls da).
#      Netzwerk-Konsole (P7-T9): -netdev user + e1000 + filter-dump -> net.pcap;
#      Marker MOO-UEFI-NETZ-OK und Nutzlast 'MOO-NET MOOS STARTET' sowie der
#      Tastenname 'Return' muessen im pcap stehen (grep -a).
#
# ARTEFAKTE: Standard temporaer (geloescht). MOO_UEFI_OUT=<dir> behaelt
# BOOTX64.EFI, uefi.img, screenshot.png, serial.log, debug.log, sha256.txt
# (so nutzt es skripte/p7/uefi/sicht-gate.sh). Es werden NUR Image-Dateien
# beschrieben, nie Blockgeraete.
#
# BEWEISKANAL: 0xE9 debugcon ist zuverlaessiger als COM1 im UEFI-Kontext
# (OVMF nutzt COM1 selbst). Der Entry schreibt auf beide; moo nur auf COM1.
#
# SKIP-POLITIK (P010): fehlendes Tooling => transparenter SKIP mit Exit 0.
# SKIP ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${MOO_COMPILER:-$ROOT/compiler/target/release/moo-compiler}"
# P7-T14: MOO_UEFI_PRUEFUNG=e1000 prueft den Moo-e1000-Treiber (eigene App, eigener QEMU-Lauf).
PRUEFUNG="${MOO_UEFI_PRUEFUNG:-}"
if [ "$PRUEFUNG" = "e1000" ]; then
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/netz_e1000.moos}"
elif [ "$PRUEFUNG" = "lader" ]; then
  # P7-T10: Netz-Lader; Nutzlast ist die e1000-Pruef-App (eigener Lauf, s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/netz_lader.moos}"
elif [ "$PRUEFUNG" = "netz-kern" ]; then
  # P7-T5: N100-Weg; Stick-App ist der Netz-Lader (Ablauf s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/netz_lader.moos}"
elif [ "$PRUEFUNG" = "nativ" ]; then
  # P7-T17: @nativ-Aufruf mit dynamischen Werten (eigene App, Negativfall s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/compiler/tests/p7/nativ_uefi_dyn.moos}"
elif [ "$PRUEFUNG" = "nativ-trap" ]; then
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/compiler/tests/p7/nativ_uefi_dyn_trap.moos}"
elif [ "$PRUEFUNG" = "kern" ]; then
  # P7-T5: Moo-UEFI-Loader + Moo-Kern (eigener Ablauf vor den C-Phasen, s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/moo_lader.moos}"
else
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/hallo_uefi.moos}"
fi
ENTRY_C="$ROOT/compiler/runtime/boot/uefi_entry.c"
RT_C="$ROOT/compiler/runtime/boot/uefi_rt.c"
RT_SOURCES=(moo_bare.c moo_bare_alloc.c moo_freestanding_mem.c moo_surface_core.c moo_surface.c moo_native_math.c)
CFLAGS=(--target=x86_64-w64-windows-gnu -std=c11 -O2 -ffreestanding -fno-stack-protector
        -mno-stack-arg-probe -ffunction-sections -fdata-sections -DMOO_BARE)
WAIT_S="${MOO_UEFI_WAIT:-45}"
QEMU_ACCEL=()
[ "${MOO_UEFI_KVM:-0}" != 1 ] || QEMU_ACCEL+=(-enable-kvm -cpu host)

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

[ -x "$BIN" ] || fail "moo-compiler fehlt: $BIN (erst mise run p4-build-vulkan)"
[ -f "$MOO_SRC" ] || fail "moo-Quelle fehlt: $MOO_SRC"
if [ -n "${MOO_UEFI_OUT:-}" ]; then
  OUT="$MOO_UEFI_OUT"
  mkdir -p "$OUT" || fail "MOO_UEFI_OUT nicht anlegbar: $OUT"
  echo "      Artefakte bleiben in: $OUT"
else
  OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_uefi_smoke.XXXXXX")"
  trap 'rm -rf "$OUT"' EXIT
fi

# UDP-Nutzlasten gegen jede vollstaendige COM1-Kernzeile pruefen (auch vor NIC-Init).
kern_netz_pruefen() {
  uv run --no-project python - "$1" "$2" <<'PY'
import collections, pathlib, struct, sys
raw = pathlib.Path(sys.argv[1]).read_bytes()
assert raw[:4] in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4")
endian = "<" if raw[0] == 0xd4 else ">"
assert struct.unpack_from(endian + "I", raw, 20)[0] == 1
packets, pos = [], 24
while pos + 16 <= len(raw):
    size = struct.unpack_from(endian + "I", raw, pos + 8)[0]
    frame = raw[pos + 16:pos + 16 + size]
    pos += 16 + size
    if len(frame) < 42 or frame[12:14] != b"\x08\x00" or frame[23] != 17:
        continue
    ip = frame[14:]
    ihl = (ip[0] & 15) * 4
    if len(ip) < ihl + 8:
        continue
    udp = ip[ihl:]
    if struct.unpack_from("!HH", udp) != (7777, 7777):
        continue
    assert frame[:6] == b"\xff" * 6
    assert ip[12:20] == b"\x00" * 4 + b"\xff" * 4
    assert sum(struct.unpack("!" + "H" * (ihl // 2), ip[:ihl])) % 65535 == 0
    length = struct.unpack_from("!H", udp, 4)[0]
    assert 8 <= length <= len(udp)
    packets.append(udp[8:length].decode("ascii"))
serial = pathlib.Path(sys.argv[2]).read_bytes().replace(b"\r", b"").decode("ascii", "replace")
assert "MOO-KERNEL START\n" in serial
lines = [line for line in serial[serial.index("MOO-KERNEL START\n"):].splitlines() if line]
missing = collections.Counter(lines) - collections.Counter(packets)
assert not missing, "Kernzeilen fehlen im UDP: " + repr(missing)
assert "MOO-KERNEL OK" in packets
assert serial.index("MOO-KERNEL NETZ PORTS ") < serial.index("MOO-KERNEL TABELLEN FREI ")
assert "MOO-KERNEL NETZ DMA-STOP FEHLER" not in serial
print("      OK: %d COM1-Kernzeilen im UDP-pcap (%d Datagramme), Port 7777/IPv4-Pruefsumme" % (len(lines), len(packets)))
PY
}

if [ "$PRUEFUNG" = "kern" ]; then
  # P7-T5: UEFI-Loader in Moo (beispiele/uefi/moo_lader.moos) laedt den Moo-Kern
  # (beispiele/kernel/uefi_kern.moos) von der ESP, ruft ExitBootServices und
  # springt hinein. Der Loader wird aus GENAU EINEM Objekt (dem Moo-Objekt)
  # gelinkt, ohne uefi_entry.c/uefi_rt.c. Pruefung nach ExitBootServices:
  # Kern-Marker auf COM1, Bild per screendump, Negativfall ohne Kern-Datei.
  LADER_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/moo_lader.moos}"
  KERN_SRC="${MOO_UEFI_KERN:-$ROOT/beispiele/kernel/uefi_kern.moos}"
  KERN_LD="$ROOT/beispiele/kernel/linker-uefi.ld"
  for t in lld-link ld readelf nm qemu-system-x86_64 parted mformat mmd mcopy; do
    command -v "$t" >/dev/null 2>&1 || skip "fehlendes Tooling: $t" \
      "sudo pacman -S lld binutils qemu-system-x86 parted mtools" "Tooling fehlt — Kern-Pruefung uebersprungen."
  done
  phase K1 "Loader: moo -> COFF -> PE32+ (nur das Moo-Objekt, kein C)"
  "$BIN" compile "$LADER_SRC" --no-stdlib --target x86_64-unknown-uefi -o "$OUT/lader.obj" >"$OUT/lader.err" 2>&1 \
    || { sed 's/^/      | /' "$OUT/lader.err"; fail "Loader nicht kompiliert"; }
  lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /opt:ref /Brepro \
      /out:"$OUT/BOOTX64.EFI" "$OUT/lader.obj" >"$OUT/link.err" 2>&1 \
    || { sed 's/^/      | /' "$OUT/link.err"; fail "Loader nicht gelinkt (fehlende Symbole = versteckte Runtime?)"; }
  file "$OUT/BOOTX64.EFI" | grep -qiE "EFI.*application|PE32\\+" || fail "Loader ist kein PE32+ EFI-Binary"
  echo "      OK: $(file -b "$OUT/BOOTX64.EFI"), $(stat -c %s "$OUT/BOOTX64.EFI") Bytes aus 1 Objekt"
  phase K2 "Kern: moo -> ELF64 (--kernel, linker-uefi.ld, ohne Multiboot)"
  "$BIN" compile "$KERN_SRC" --no-stdlib --target x86_64-bare --kernel --linker-script "$KERN_LD" \
      -o "$OUT/KERN.ELF" >"$OUT/kern.err" 2>&1 || { sed 's/^/      | /' "$OUT/kern.err"; fail "Kern nicht gebaut"; }
  readelf -h "$OUT/KERN.ELF" | grep -q 'Type:.*EXEC' || fail "Kern ist kein ELF EXEC"
  readelf -h "$OUT/KERN.ELF" | grep -q 'Machine:.*X86-64' || fail "Kern ist kein x86-64"
  [ -z "$(nm -u "$OUT/KERN.ELF")" ] || fail "Kern hat undefinierte Symbole"
  ENTRY_ELF="$(readelf -h "$OUT/KERN.ELF" | awk '/Entry point/{print $NF}')"
  ENTRY_SYM="$(nm "$OUT/KERN.ELF" | awk '$3=="moo_kern_start"{print $1}')"
  [ "$((ENTRY_ELF))" -eq "$((16#$ENTRY_SYM))" ] || fail "ELF-Einstieg $ENTRY_ELF ist nicht moo_kern_start ($ENTRY_SYM)"
  od -A n -t x1 "$OUT/KERN.ELF" | tr -d '\n' | grep -q 'd6 50 52 e8' && fail "Multiboot2-Kopf im Kern (GRUB-Pfad nicht verworfen)"
  echo "      OK: ELF64 EXEC x86-64, Einstieg moo_kern_start $ENTRY_ELF, kein Multiboot2-Kopf"
  phase K3 "OVMF-Boot: Moo-Loader -> ExitBootServices -> Moo-Kern"
  KOVMF_CODE=""; KOVMF_VARS=""
  for c in /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd \
           /usr/share/OVMF/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_CODE.fd; do
    [ -f "$c" ] && { KOVMF_CODE="$c"; KOVMF_VARS="${c/CODE/VARS}"; break; }
  done
  [ -n "$KOVMF_CODE" ] && [ -f "$KOVMF_VARS" ] || skip "OVMF fehlt" "sudo pacman -S edk2-ovmf" "Build K1/K2 gruen — OVMF-Boot GESKIPPT."
  kern_image() {  # Image, mit Kern (1/0)
    rm -f "$1"; truncate -s 48M "$1" || fail "Image nicht anlegbar"
    parted -s "$1" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on >/dev/null 2>&1 || fail "parted"
    mformat -i "$1"@@$((2048*512)) -F :: >/dev/null 2>&1 || fail "mformat"
    mmd -i "$1"@@$((2048*512)) ::/EFI ::/EFI/BOOT ::/EFI/MOO >/dev/null 2>&1
    mcopy -i "$1"@@$((2048*512)) "$OUT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1 || fail "mcopy Loader"
    if [ "$2" = 1 ]; then
      mcopy -i "$1"@@$((2048*512)) "$OUT/KERN.ELF" ::/EFI/MOO/KERN.ELF >/dev/null 2>&1 || fail "mcopy Kern"
    fi
  }
  kern_image "$OUT/uefi.img" 1
  kern_image "$OUT/ohne-kern.img" 0
  cp "$KOVMF_VARS" "$OUT/vars.fd"; cp "$KOVMF_VARS" "$OUT/vars-ohne.fd"
  : >"$OUT/serial.log"; : >"$OUT/debug.log"; rm -f "$OUT/monitor.in" "$OUT/screenshot.png"
  mkfifo "$OUT/monitor.in" || fail "FIFO fuer QEMU-Monitor nicht anlegbar"
  exec 3<>"$OUT/monitor.in"
  KERN_QEMU=()
  [ "${MOO_UEFI_DIAG:-0}" != 1 ] || KERN_QEMU+=(-d int,guest_errors,cpu_reset -D "$OUT/cpu.log")
  NETZ="${MOO_UEFI_NETZ:-1}"
  if [ "$NETZ" = 1 ]; then
    KERN_QEMU+=(-netdev user,id=n0 -device e1000,netdev=n0,romfile=
      -object filter-dump,id=klog,netdev=n0,file="$OUT/net.pcap")
  else
    KERN_QEMU+=(-net none)
  fi
  USB="${MOO_UEFI_USB:-1}"
  if [ "$USB" = 1 ]; then
    KERN_QEMU+=(-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0)
  elif [ "$USB" = hub ]; then
    KERN_QEMU+=(-device qemu-xhci,id=xhci -device usb-hub,bus=xhci.0,port=1
      -device usb-kbd,bus=xhci.0,port=1.1)
  fi
  timeout --foreground $((WAIT_S + 60))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" "${KERN_QEMU[@]}" \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$KOVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
      -drive format=raw,file="$OUT/uefi.img" \
      -m 256M -vga std -display none -no-reboot -monitor stdio \
      -serial file:"$OUT/serial.log" -debugcon file:"$OUT/debug.log" -global isa-debugcon.iobase=0xe9 \
      <"$OUT/monitor.in" >"$OUT/monitor.log" 2>&1 &
  QPID=$!
  KEY_SENT=0
  for ((i = 0; i < WAIT_S; i++)); do
    BEREIT="MOO-KERNEL TASTATUR BEREIT 0000000000000001"
    [ "$USB" != 1 ] || BEREIT="MOO-KERNEL USB BEREIT POLLING 0000000000000001"
    if [ "$KEY_SENT" -eq 0 ] && grep -aqF "$BEREIT" "$OUT/serial.log" 2>/dev/null; then
      printf 'sendkey a\n' >&3
      KEY_SENT=1
    fi
    grep -aq "MOO-KERNEL OK" "$OUT/serial.log" 2>/dev/null && break
    grep -aq "MOO-LADER FEHLER" "$OUT/serial.log" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  # USB bleibt nach OK aktiv: Shift und mehr als 256 Press/Release-Reports
  # pruefen Event-Cycle sowie Link-TRB/Cycle des Transfer-Rings.
  if [ "$USB" = 1 ] && grep -aq "MOO-KERNEL OK" "$OUT/serial.log"; then
    printf 'screendump %s -f png\n' "$OUT/boot-screenshot.png" >&3
    for ((j = 0; j < 15; j++)); do [ -s "$OUT/boot-screenshot.png" ] && break; sleep 0.1; done
    printf 'sendkey shift-b 60\n' >&3
    sleep 0.2
    for ((j = 0; j < 140; j++)); do
      printf 'sendkey c 60\n' >&3
      # Genug Abstand fuer Release-Report und sichtbares Present unter VM-Last.
    sleep 0.25
    done
  fi
  sleep 1
  printf 'info registers\ninfo lapic\n' >&3
  printf 'screendump %s -f png\n' "$OUT/screenshot.png" >&3
  for ((i = 0; i < 15; i++)); do [ -s "$OUT/screenshot.png" ] && break; sleep 1; done
  printf 'quit\n' >&3 2>/dev/null || true; exec 3>&-; wait "$QPID" 2>/dev/null
  tr -d '\000\r' <"$OUT/serial.log" | grep -aE "MOO-(LADER|KERNEL)" | sed 's/^/      | /'
  grep -aq "MOO-LADER FEHLER" "$OUT/serial.log" && fail "Loader meldet FEHLER (siehe oben)"
  for m in "MOO-LADER START (MOO, OHNE C)" "MOO-LADER GOP " "MOO-LADER FB-STRIDE " "MOO-LADER RSDP " \
           "MOO-LADER KERN-DATEI " "MOO-LADER SEGMENT " "MOO-LADER KERN-EINSTIEG " \
           "MOO-LADER KERN-PHYSISCH " "MOO-LADER SEITENTABELLE " \
           "MOO-LADER EXITBOOTSERVICES OK " "MOO-LADER KARTE " "MOO-LADER SPRUNG " \
           "MOO-KERNEL START" "MOO-KERNEL RIP FFFFFFFF8" "MOO-KERNEL RSP FFFFFFFF8" \
           "MOO-KERNEL GDT CS 0000000000000008" "MOO-KERNEL PAGING EIGEN CR3 " \
           "MOO-KERNEL PFA FREI-SEITEN " "MOO-KERNEL HEAP OK 0000000000000001" \
           "MOO-KERNEL TABELLEN FREI " "MOO-KERNEL IDT OK 0000000000000001" \
           "MOO-KERNEL RSDP-SIG 0000000000000001" "MOO-KERNEL ACPI FADT-MADT 0000000000000003" \
           "MOO-KERNEL PRESENT-FORMATE 0000000000000005" "MOO-KERNEL SURFACE 0000000000000001" \
           "MOO-KONSOLE: SURFACE-PRESENT AUF GOP" "MOO-KONSOLE: GDT, PAGING, STACK IN MOO" \
           "MOO-KERNEL AUSNAHME VEKTOR 0000000000000003" \
           "MOO-KERNEL AUSNAHME FEHLERCODE 0000000000000000" "MOO-KERNEL AUSNAHME RIP " \
           "MOO-KERNEL AUSNAHME TEST 0000000000000001" "MOO-KERNEL TIMER QUELLE 0000000000000001" \
           "MOO-KERNEL TIMER STARTWERT " "MOO-KERNEL TIMER TICKS " \
           "MOO-KERNEL TASTATUR BEREIT 0000000000000001" \
           "MOO-KERNEL USB REPORT-TEST 0000000000000001" \
           "MOO-KONSOLE: SPEICHER, IDT, ACPI" \
           "MOO-KERNEL MESS PRESENT-TAKTE " "MOO-KERNEL OK"; do
    grep -aqF -- "$m" "$OUT/serial.log" || fail "Marker fehlt: '$m'"
  done
  if [ "$USB" = 1 ]; then
    for m in "MOO-KERNEL USB BEREIT POLLING 0000000000000001" \
             "MOO-KERNEL USB HID 0000000000000004" \
             "MOO-KERNEL USB TASTE 0000000000000061" \
             "MOO-KERNEL USB TASTE 0000000000000042"; do
      grep -aqF "$m" "$OUT/serial.log" || fail "USB-Marker fehlt: $m"
    done
    USB_C="$(grep -acF 'MOO-KERNEL USB TASTE 0000000000000063' "$OUT/serial.log")"
    [ "$USB_C" -ge 135 ] || fail "USB-Ringumlauf: nur $USB_C von 140 Tastendruecken"
  elif [ "$USB" = 0 ]; then
    grep -aqF "MOO-KERNEL TASTATUR SCAN 000000000000001E" "$OUT/serial.log" || fail "PS/2 Scan 0x1E fehlt"
  elif [ "$USB" = hub ]; then
    grep -aqF "MOO-KERNEL USB GERAETEKLASSE 0000000000000009" "$OUT/serial.log" || fail "Hub nicht als Klasse9 diagnostiziert"
    grep -aqF "MOO-KERNEL USB BEREIT POLLING 0000000000000000" "$OUT/serial.log" || fail "Hub faelschlich als Boot-Keyboard bereit"
    grep -aqF "MOO-KERNEL USB TASTE " "$OUT/serial.log" && fail "Hub-Negativfall meldet erfundene Eingabe"
    echo "      OK: USB-Tastatur hinter Hub als Diagnose-/Negativfall (Hubtreiber weiterhin offen)"
  fi
  if [ "$USB" != 0 ]; then
    for m in "HCSPARAMS1" "HCSPARAMS2" "HCCPARAMS1" "PROTOCOL CAP" "PROTOCOL PORTBEREICH" "PORTSC" "CCS" "PED" "PR" "PLS" "PP" "SPEED" "CSC GESEHEN"; do
      grep -aqF "MOO-KERNEL USB $m " "$OUT/serial.log" || fail "USB-Diagnose fehlt: $m"
    done
    for phase in 0 1 2 3 4 5; do
      grep -aqF "MOO-KERNEL USB PHASE 000000000000000$phase" "$OUT/serial.log" || fail "USB-Snapshot Phase$phase fehlt"
    done
  fi
  [ "$NETZ" != 1 ] || kern_netz_pruefen "$OUT/net.pcap" "$OUT/serial.log" || fail "Kern-Netz-Log unvollstaendig"
  # Reihenfolge: Kern laeuft erst NACH ExitBootServices.
  Z_EBS="$(grep -an 'MOO-LADER EXITBOOTSERVICES OK' "$OUT/serial.log" | head -1 | cut -d: -f1)"
  Z_KERN="$(grep -an 'MOO-KERNEL START' "$OUT/serial.log" | head -1 | cut -d: -f1)"
  [ "$Z_EBS" -lt "$Z_KERN" ] || fail "MOO-KERNEL START steht nicht nach ExitBootServices"
  SPRUNG="$(grep -ao 'MOO-LADER SPRUNG [0-9A-F]*' "$OUT/serial.log" | awk '{print $3}')"
  [ "$((16#$SPRUNG))" -eq "$((ENTRY_ELF))" ] || fail "Sprungziel $SPRUNG != ELF-Einstieg $ENTRY_ELF"
  # Boot-Info gegen die Loader-Werte: Framebuffer und Stride kommen im Kern an.
  for f in "GOP:FB" "FB-BREITE:BREITE" "FB-HOEHE:HOEHE" "FB-STRIDE:STRIDE" "FB-FORMAT:FORMAT" "RSDP:RSDP"; do
    L="$(grep -ao "MOO-LADER ${f%%:*} [0-9A-F]*" "$OUT/serial.log" | awk '{print $3}')"
    K="$(grep -ao "MOO-KERNEL ${f##*:} [0-9A-F]*" "$OUT/serial.log" | awk '{print $3}')"
    [ -n "$L" ] && [ "$L" = "$K" ] || fail "Boot-Info ${f%%:*}: Loader '$L' != Kern '$K'"
  done
  KARTE="$(grep -ao 'MOO-KERNEL KARTE-ANZ [0-9A-F]*' "$OUT/serial.log" | awk '{print $3}')"
  [ "$((16#$KARTE))" -gt 0 ] || fail "Speicherkarte im Kern leer"
  PFA="$(grep -ao 'MOO-KERNEL PFA FREI-SEITEN [0-9A-F]*' "$OUT/serial.log" | awk 'NR==1{print $4}')"
  TABELLEN="$(grep -ao 'MOO-KERNEL TABELLEN FREI [0-9A-F]*' "$OUT/serial.log" | awk 'NR==1{print $4}')"
  TICKS="$(grep -ao 'MOO-KERNEL TIMER TICKS [0-9A-F]*' "$OUT/serial.log" | awk '{print $4}')"
  [ "$((16#$PFA))" -gt 0 ] || fail "PFA meldet keine freien Seiten"
  [ "$((16#$TABELLEN))" -gt 0 ] || fail "Firmware-/Loader-Seitentabellen nicht freigegeben"
  [ "$((16#$TICKS))" -ge 3 ] || fail "LAPIC-Timer lieferte weniger als drei Ticks"
  [ "$KEY_SENT" -eq 1 ] || fail "QEMU sendkey a wurde nicht ausgeloest"
  echo "      OK: Loader -> ExitBootServices (Zeile $Z_EBS) -> Kern (Zeile $Z_KERN), Sprung $SPRUNG = ELF-Einstieg, Boot-Info identisch, $((16#$KARTE)) Kartendeskriptoren, $((16#$PFA)) freie Frames, $((16#$TABELLEN)) alte Tabellenseiten, $((16#$TICKS)) Timer-Ticks, Eingabepfad USB=$USB"
  [ -s "$OUT/screenshot.png" ] || fail "kein Screenshot"
  if command -v magick >/dev/null 2>&1; then
    # Hintergrund (32,96,128) direkt vom Kern, Balken (255,196,64) ueber moo_surface_core.
    HG="$(magick "$OUT/screenshot.png" -format '%[pixel:p{10,400}]' info: 2>/dev/null)"
    BALKEN="$(magick "$OUT/screenshot.png" -format '%[pixel:p{45,45}]' info: 2>/dev/null)"
    case "$HG" in *"32,96,128"*) ;; *) fail "Hintergrund ist '$HG' statt srgb(32,96,128) (Kern hat nicht gezeichnet?)" ;; esac
    case "$BALKEN" in *"255,196,64"*) ;; *) fail "Balken ist '$BALKEN' statt srgb(255,196,64)" ;; esac
    # Text ueber moo_surface_core_text_3x5: \"M\" (Skala 12 ab 64,60) hat links eine
    # volle Spalte (dunkel), die Luecke zwischen M und O (Spalte 3) bleibt Balken;
    # Zeile 2 (Skala 6 ab 64,220) beginnt ebenfalls mit voller M-Spalte (hell).
    GLYPHE="$(magick "$OUT/screenshot.png" -format '%[pixel:p{70,100}]' info: 2>/dev/null)"
    LUECKE="$(magick "$OUT/screenshot.png" -format '%[pixel:p{106,100}]' info: 2>/dev/null)"
    ZEILE2="$(magick "$OUT/screenshot.png" -format '%[pixel:p{66,236}]' info: 2>/dev/null)"
    case "$GLYPHE" in *"20,20,20"*) ;; *) fail "Glyphe M (70,100) ist '$GLYPHE' statt srgb(20,20,20)" ;; esac
    case "$LUECKE" in *"255,196,64"*) ;; *) fail "Zeichenluecke (106,100) ist '$LUECKE' statt Balkenfarbe" ;; esac
    case "$ZEILE2" in *"240,240,240"*) ;; *) fail "Zeile 2 (66,236) ist '$ZEILE2' statt srgb(240,240,240)" ;; esac
    echo "      OK: Pixel Hintergrund $HG, Balken $BALKEN, Glyphe $GLYPHE, Luecke $LUECKE, Zeile 2 $ZEILE2"
    # P7-T6: Textkonsole (Surface-Text, Farbe 200,230,255) ab y=300 per Present sichtbar.
    KON="$(magick "$OUT/screenshot.png" -crop 2000x200+0+296 +repage -format %c histogram:info:- 2>/dev/null | awk '/\(200,230,255/{print $1}' | tr -d ':')"
    [ -n "$KON" ] && [ "$KON" -gt 200 ] || fail "Konsolentext (200,230,255) im Bereich y=296..496 fehlt ('$KON' Pixel)"
    echo "      OK: Konsolentext $KON Pixel in der Konsolenfarbe"
    if [ "$USB" = 1 ]; then
      EINGABE_PIXEL="$(magick "$OUT/screenshot.png" -gravity South -crop 2000x80+0+0 +repage -format %c histogram:info:- 2>/dev/null | awk '/[(]255,230,160/{print $1}' | tr -d ':')"
      [ -n "$EINGABE_PIXEL" ] && [ "$EINGABE_PIXEL" -gt 100 ] || fail "Fester Eingabetext nach Scroll-/Ringstress fehlt"
      echo "      OK: Fester Eingabebereich nach 140 Tasten und Konsolen-Scroll: $EINGABE_PIXEL Textpixel"
    fi
  fi
  tr -d '\000\r' <"$OUT/serial.log" | grep -aE "MOO-KERNEL (MESS|TSC)" | sed 's/^/      Messung: /'
  if command -v tesseract >/dev/null 2>&1; then
    # Nur Info: die 3x5-Pixelschrift liest tesseract unzuverlaessig (M -> H/W).
    echo "      Info OCR: $(tesseract "$OUT/screenshot.png" - --psm 6 2>/dev/null | tr '\n' ' ')"
  fi
  # Negativfall: ohne Kern-Datei kein ExitBootServices, Fehlermeldung, Rueckkehr zur Firmware.
  : >"$OUT/serial-ohne.log"
  timeout --foreground 40s qemu-system-x86_64 "${QEMU_ACCEL[@]}" \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$KOVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars-ohne.fd" \
      -drive format=raw,file="$OUT/ohne-kern.img" -net none \
      -m 256M -vga std -display none -no-reboot -monitor none \
      -serial file:"$OUT/serial-ohne.log" >"$OUT/qemu-ohne.log" 2>&1 &
  QPID=$!
  for ((i = 0; i < 35; i++)); do
    grep -aq "KERN NICHT GEFUNDEN" "$OUT/serial-ohne.log" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  grep -aq "MOO-LADER FEHLER: KERN NICHT GEFUNDEN" "$OUT/serial-ohne.log" || fail "Negativfall: Fehlermeldung KERN NICHT GEFUNDEN fehlt"
  grep -aq "EXITBOOTSERVICES OK" "$OUT/serial-ohne.log" && fail "Negativfall: ExitBootServices trotz fehlendem Kern"
  echo "      OK: Negativfall ohne KERN.ELF: Meldung, kein ExitBootServices"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI KERN.ELF uefi.img screenshot.png serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
    sha256sum "$BIN" | sed 's/^/      sha256: /'
  fi
  echo "############################################################"
  echo "# ERGEBNIS: Kern-Pruefung gruen (Moo-Loader ohne C -> ExitBootServices -> Moo-Kern zeichnet)."
  echo "############################################################"
  exit 0
fi

# --- Tooling-Pruefung (clang/lld-link + MinGW-Header fuer den COFF-Build) ---
for t in clang lld-link; do
  command -v "$t" >/dev/null 2>&1 || skip "fehlendes Tooling: $t" \
    "sudo pacman -S clang lld" "Build-Tooling fehlt — UEFI-Smoke uebersprungen."
done
printf '#include <stdlib.h>\n#include <math.h>\n' \
  | clang "${CFLAGS[@]}" -fsyntax-only -x c - >/dev/null 2>&1 \
  || skip "MinGW-Header fuer x86_64-w64-windows-gnu fehlen (stdlib.h/math.h)" \
       "sudo pacman -S mingw-w64-headers" "Header fehlen — UEFI-Smoke uebersprungen."

phase 1 "moo -> PE/COFF (--target x86_64-unknown-uefi)"
"$BIN" compile "$MOO_SRC" --no-stdlib --target x86_64-unknown-uefi \
    -o "$OUT/uefi_moo.obj" 2>"$OUT/moo.err" | sed 's/^/      /'
[ -f "$OUT/uefi_moo.obj" ] || { sed 's/^/      | /' "$OUT/moo.err"; fail "moo-COFF nicht gebaut"; }
file "$OUT/uefi_moo.obj" | grep -qi "COFF" || fail "moo-Objekt ist kein COFF"
echo "      OK: $(file -b "$OUT/uefi_moo.obj")"

phase 2 "Entry + Runtime + Surface -> COFF (clang --target=x86_64-w64-windows-gnu)"
: >"$OUT/cc.err"
clang "${CFLAGS[@]}" -DMOO_UEFI_SURFACE -c "$ENTRY_C" -o "$OUT/uefi_entry.obj" 2>>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "uefi_entry.c-Build fehlgeschlagen"; }
clang "${CFLAGS[@]}" -DMOO_UEFI_USE_BARE_RUNTIME -c "$RT_C" -o "$OUT/uefi_rt.obj" 2>>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "uefi_rt.c-Build fehlgeschlagen"; }
clang "${CFLAGS[@]}" -c "$ROOT/compiler/runtime/boot/uefi_net.c" -o "$OUT/uefi_net.obj" 2>>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "uefi_net.c-Build fehlgeschlagen"; }
OBJS=("$OUT/uefi_entry.obj" "$OUT/uefi_moo.obj" "$OUT/uefi_rt.obj" "$OUT/uefi_net.obj")
for src in "${RT_SOURCES[@]}"; do
  obj="$OUT/${src%.c}.obj"
  clang "${CFLAGS[@]}" -c "$ROOT/compiler/runtime/$src" -o "$obj" 2>>"$OUT/cc.err" \
    || { sed 's/^/      | /' "$OUT/cc.err"; fail "$src-Build fehlgeschlagen"; }
  OBJS+=("$obj")
done
echo "      OK: ${#OBJS[@]} COFF-Objekte (Entry, moo, rt, ${RT_SOURCES[*]})"

phase 3 "lld-link -> PE32+ EFI application"
lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /opt:ref \
    /out:"$OUT/BOOTX64.EFI" "${OBJS[@]}" 2>"$OUT/link.err" | sed 's/^/      /'
[ -f "$OUT/BOOTX64.EFI" ] || { sed 's/^/      | /' "$OUT/link.err"; fail "BOOTX64.EFI nicht gelinkt"; }
file "$OUT/BOOTX64.EFI" | grep -qiE "EFI.*application|PE32\\+" || fail "kein PE32+ EFI-Binary"
echo "      OK: $(file -b "$OUT/BOOTX64.EFI")"

phase 4 "OVMF-Boot (GPT-Disk mit ESP) + Screenshot"
# Tooling fuer den echten Boot
MISS=()
for t in qemu-system-x86_64 parted mformat mmd mcopy; do
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
rm -f "$DISK"
truncate -s 48M "$DISK" || fail "Image-Datei nicht anlegbar"
[ -f "$DISK" ] && [ ! -L "$DISK" ] || fail "Image ist keine regulaere Datei"
parted -s "$DISK" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on >/dev/null 2>&1 \
  || fail "parted GPT/ESP fehlgeschlagen"
OFF=$((2048*512))
mformat -i "$DISK"@@${OFF} -F :: >/dev/null 2>&1 || fail "mformat ESP fehlgeschlagen"
mmd -i "$DISK"@@${OFF} ::/EFI ::/EFI/BOOT >/dev/null 2>&1
mcopy -i "$DISK"@@${OFF} "$OUT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1 \
  || fail "mcopy BOOTX64.EFI fehlgeschlagen"
# P7-T9: Fremder UEFI-Treiber (z. B. selbst gebautes iPXE intel.efidrv fuer die
# I226 des N100). Liegt er vor, kommt er nach \EFI\MOO\treiber\ und die NICs
# laufen OHNE Option-ROM (romfile=): SNP kann dann nur vom geladenen Treiber
# stammen. MOO_UEFI_TREIBER= (leer) erzwingt den Lauf ohne Treiber.
TREIBER="${MOO_UEFI_TREIBER-$ROOT/build/p7-ipxe/intel.efidrv}"
# Im e1000-Pruefmodus darf kein Firmware-Treiber die NIC binden.
[ "$PRUEFUNG" = "e1000" ] && TREIBER=""
[ "$PRUEFUNG" = "lader" ] && TREIBER=""
[ "$PRUEFUNG" = "netz-kern" ] && TREIBER=""
[ "${PRUEFUNG#nativ}" != "$PRUEFUNG" ] && TREIBER=""
NIC_ROM=""
TREIBER_ERWARTET="FEHLT"
if [ -n "$TREIBER" ] && [ -f "$TREIBER" ]; then
  mmd -i "$DISK"@@${OFF} ::/EFI/MOO ::/EFI/MOO/treiber >/dev/null 2>&1
  mcopy -i "$DISK"@@${OFF} "$TREIBER" ::/EFI/MOO/treiber/intel.efi >/dev/null 2>&1 \
    || fail "mcopy Treiber fehlgeschlagen"
  cp "$TREIBER" "$OUT/treiber.efi"
  NIC_ROM=",romfile="
  TREIBER_ERWARTET="OK"
  echo "      Treiber: $TREIBER -> \\EFI\\MOO\\treiber\\intel.efi (NICs ohne Option-ROM)"
else
  echo "      Treiber: keiner (erwartet MOO-UEFI-TREIBER-FEHLT, NICs mit QEMU-Option-ROM)"
fi

cp "$OVMF_VARS" "$OUT/vars.fd"
SERIAL_LOG="$OUT/serial.log"
DEBUG_LOG="$OUT/debug.log"
SHOT="$OUT/screenshot.png"
: >"$SERIAL_LOG"; : >"$DEBUG_LOG"; rm -f "$SHOT" "$OUT/monitor.in"

if [ "$PRUEFUNG" = "e1000" ]; then
  # P7-T14: Moo-e1000-Treiber (beispiele/uefi/netz_treiber.moos). q35 hat ECAM
  # (ACPI-MCFG); die NIC laeuft ohne Option-ROM, OVMF bindet sie also nicht.
  # slirp beantwortet die ARP-Anfrage nach 10.0.2.2 -> Empfangsbeweis.
  # P7-T18: MOO_UEFI_KVM=1 startet mit KVM (Messlaeufe); Standard bleibt TCG.
  timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
      -drive format=raw,file="$DISK" \
      -m 256M -vga std -display none -no-reboot -monitor none \
      -netdev user,id=n0 -device e1000,netdev=n0,mac=52:54:00:12:34:58,romfile= \
      -object filter-dump,id=f0,netdev=n0,file="$OUT/net.pcap" \
      -serial file:"$SERIAL_LOG" \
      -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
      >"$OUT/qemu.log" 2>&1 &
  QPID=$!
  for ((i = 0; i < WAIT_S + 20; i++)); do
    grep -q "MOO-E1000-ENDE" "$SERIAL_LOG" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  grep -a "MOO-E1000" "$SERIAL_LOG" | sed 's/^/      | /'
  grep -q "MOO-E1000-ENDE" "$SERIAL_LOG" || { sed 's/^/      | /' "$DEBUG_LOG" | tail -10; fail "Marker MOO-E1000-ENDE fehlt"; }
  grep -q "MOO-E1000-FEHLER" "$SERIAL_LOG" && fail "Moo-Treiber meldet FEHLER (siehe oben)"
  grep -q "MOO-E1000-SELBST 00" "$SERIAL_LOG" || fail "Selbsttests der Treiberlogik melden Fehler (MOO-E1000-SELBST != 00)"
  grep -q "MOO-E1000-I226 0" "$SERIAL_LOG" || fail "I226-Zaehlung ohne Geraet nicht 0"
  # P7-T10: 1000 Rahmen > 16 Ringplaetze; TX-Rueckgewinnung ohne Verwerfen und alle im pcap.
  grep -aq "MOO-E1000-SERIE 03ea 0000" "$SERIAL_LOG" || { grep -a "MOO-E1000-SERIE" "$SERIAL_LOG"; fail "Serie: erwartet 1002 gesendet (2 + 1000), 0 verworfen"; }
  SERIE=$(grep -a -o "MOO-E1000-SERIE" "$OUT/net.pcap" | wc -l)
  [ "$SERIE" -eq 1000 ] || fail "Serie: $SERIE statt 1000 Rahmen im pcap"
  echo "      OK: TX-Serie: 1000 Rahmen ueber 16er-Ring, 0 verworfen, 1000 im pcap"
  # P7-T10: 200 Logzeilen ueber 3 Ports (echt, festgefahrener Ring, ohne Link):
  # festgefahrener Port 15 gesendet/185 verworfen, Port ohne Link 0, je Zeile < 5 ms.
  grep -aq "MOO-E1000-LOG 000f 00b9 0000" "$SERIAL_LOG" || { grep -a "MOO-E1000-LOG" "$SERIAL_LOG"; fail "Log: erwartet LOG 000f 00b9 0000"; }
  LOGMS=$(tcpdump -r "$OUT/net.pcap" -n -tt -A 'udp port 7777' 2>/dev/null | awk '/^[0-9]{10}/{t=$1; next} /MOO-E1000-LOG/{if(!a)a=t; b=t; n++} END{if(n>1) printf "%d %.3f", n, (b-a)*1000/(n-1); else print n " 999"}')
  set -- $LOGMS
  [ "$1" -eq 200 ] || fail "Log: $1 statt 200 Logzeilen im pcap"
  awk -v ms="$2" 'BEGIN{exit !(ms < 5)}' || fail "Log: $2 ms je Zeile (Grenze 5 ms)"
  echo "      OK: 200 Logzeilen, $2 ms je Zeile, festgefahrener Port verwirft ohne Warten, Port ohne Link unberuehrt"
  ECAM="$(grep -ao 'MOO-E1000-ECAM [0-9a-f]*' "$SERIAL_LOG" | awk '{print $2}')"
  [ -n "$ECAM" ] && [ "$((16#$ECAM))" -ne 0 ] || fail "keine ECAM-Basis aus ACPI-MCFG"
  grep -q "MOO-E1000-PCI " "$SERIAL_LOG" || fail "e1000 im PCI-Scan nicht gefunden"
  grep -q "MOO-E1000-MAC 525400123458" "$SERIAL_LOG" || fail "MAC stimmt nicht (erwartet 52:54:00:12:34:58)"
  grep -q "MOO-E1000-LINK 1" "$SERIAL_LOG" || fail "kein Link"
  grep -q "MOO-E1000-TX 2" "$SERIAL_LOG" || fail "Senden: nicht beide Rahmen fertig (STA.DD)"
  grep -q "MOO-E1000-ARP 1" "$SERIAL_LOG" || fail "Empfang: keine ARP-Antwort von 10.0.2.2"
  grep -aq "MOO-E1000-MESS .* 01adb0" "$SERIAL_LOG" || fail "Messzeile MESS fehlt oder Link-Zaehler != 110000"
  [ -s "$OUT/net.pcap" ] || fail "kein net.pcap"
  HALLO=$(grep -a -c "MOO-E1000-HALLO" "$OUT/net.pcap")
  [ "$HALLO" -ge 1 ] || fail "UDP-Rahmen 'MOO-E1000-HALLO' fehlt im pcap"
  echo "      OK: Moo-e1000: ECAM 0x$ECAM, MAC/Link ok, 2 Rahmen gesendet, ARP-Antwort empfangen, pcap $(stat -c %s "$OUT/net.pcap") Bytes"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI uefi.img net.pcap serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  fi
  echo "############################################################"
  echo "# ERGEBNIS: e1000-Pruefung gruen (Moo-Treiber sendet und empfaengt in QEMU)."
  echo "############################################################"
  exit 0
fi
if [ "$PRUEFUNG" = "nativ" ] || [ "$PRUEFUNG" = "nativ-trap" ]; then
  # P7-T17: @nativ-Systemkoerper mit dynamischen Werten im UEFI-Profil.
  # Die App schreibt Zeilen "N <wert>" auf COM1. nativ: alle Werte exakt
  # (u64 2^60+8, i64 -6e18-1), danach ruft sie sich als nativ-trap mit dem
  # Negativfall auf (256 an u8 -> Trap, keine Folgezeile).
  timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
      -drive format=raw,file="$DISK" \
      -m 256M -vga std -display none -no-reboot -monitor none -net none \
      -serial file:"$SERIAL_LOG" \
      -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
      >"$OUT/qemu.log" 2>&1 &
  QPID=$!
  ENDE="N 999"; [ "$PRUEFUNG" = "nativ-trap" ] && ENDE="N 111"
  for ((i = 0; i < WAIT_S + 20; i++)); do
    tr -d '\r' <"$SERIAL_LOG" 2>/dev/null | grep -qax "$ENDE" && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  # Negativfall: Zeit fuer (verbotene) Folgezeilen nach dem Trap.
  [ "$PRUEFUNG" = "nativ-trap" ] && sleep 5
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  tr -d '\r' <"$SERIAL_LOG" | grep -a '^N ' | sed 's/^/      | /'
  if [ "$PRUEFUNG" = "nativ" ]; then
    # P7-T19: "N 3"/"N 90" = u64 aus @nativ als Laenge/Adresse an kern_text_in_puffer.
    # P7-T22: "N 700481670"/"N 165" = reine Hardwarekoerper (Port-I/O) dynamisch gerufen.
    for m in "N -12345" "N 213" "N 1152921504606846984" "N -6000000000000000001" "N 1" "N 3" "N 90" "N 700481670" "N 165" "N 2023" "N 999"; do
      tr -d '\r' <"$SERIAL_LOG" | grep -qax -- "$m" || fail "Zeile '$m' fehlt auf COM1"
    done
    echo "      OK: dynamische Argumente, exakte i64/u64-Argumente und -Rueckgaben im UEFI-Profil"
    MOO_UEFI_PRUEFUNG=nativ-trap MOO_UEFI_APP="$ROOT/compiler/tests/p7/nativ_uefi_dyn_trap.moos" \
      MOO_UEFI_OUT="$OUT/trap" bash "$0" >"$OUT/trap.log" 2>&1 \
      || { sed 's/^/      | /' "$OUT/trap.log" | tail -20; fail "Negativfall (Trap) nicht gruen"; }
    grep -q "ERGEBNIS: nativ-trap gruen" "$OUT/trap.log" \
      || { sed 's/^/      | /' "$OUT/trap.log" | tail -20; fail "Negativfall ohne Ergebniszeile (SKIP?)"; }
    echo "      OK: Negativfall: 256 an u8 -> Trap, keine Folgezeile"
    # P7-T19: Typfehler an einem C-Builtin (Text statt Zahl) -> Trap.
    MOO_UEFI_PRUEFUNG=nativ-trap MOO_UEFI_APP="$ROOT/compiler/tests/p7/nativ_uefi_typ_trap.moos" \
      MOO_UEFI_OUT="$OUT/typ" bash "$0" >"$OUT/typ.log" 2>&1 \
      || { sed 's/^/      | /' "$OUT/typ.log" | tail -20; fail "Negativfall Typfehler nicht gruen"; }
    grep -q "ERGEBNIS: nativ-trap gruen" "$OUT/typ.log" \
      || { sed 's/^/      | /' "$OUT/typ.log" | tail -20; fail "Negativfall Typfehler ohne Ergebniszeile"; }
    echo "      OK: Negativfall: Text an kern_warte_us -> Trap, keine Folgezeile"
    echo "############################################################"
    echo "# ERGEBNIS: nativ-Pruefung gruen (Positiv- und Negativfall in OVMF)."
    echo "############################################################"
    exit 0
  fi
  tr -d '\r' <"$SERIAL_LOG" | grep -qax "N 111" || fail "Zeile 'N 111' vor dem Trap fehlt"
  for m in "N 222" "N 999"; do
    tr -d '\r' <"$SERIAL_LOG" | grep -qax "$m" && fail "Zeile '$m' nach dem Trap erschienen"
  done
  echo "# ERGEBNIS: nativ-trap gruen (Trap haelt die App an)."
  exit 0
fi
if [ "$PRUEFUNG" = "lader" ]; then
  # P7-T10: Nutzlast bauen und pruefen (e1000-Modus dieses Skripts), dann per
  # QEMU-slirp-TFTP (-netdev user,tftp=) an den Lader ausliefern.
  MOO_UEFI_PRUEFUNG=e1000 MOO_UEFI_OUT="$OUT/nutzlast" bash "$0" >"$OUT/nutzlast.log" 2>&1 \
    || { tail -5 "$OUT/nutzlast.log" | sed 's/^/      | /'; fail "Nutzlast (e1000-Modus) nicht gruen"; }
  echo "      OK: Nutzlast gebaut und einzeln gruen ($OUT/nutzlast/BOOTX64.EFI)"
  pruefdatei() {  # Datei Ziel: "<laenge> <crc32hex>" (CRC32 aus dem gzip-Trailer, RFC 1952)
    printf '%s %s\n' "$(stat -c %s "$1")" "$(gzip -c -n "$1" | tail -c8 | od -An -tx4 -N4 | tr -d ' \n')" >"$2"
  }
  mkdir -p "$OUT/tftp" "$OUT/tftp-falsch"
  cp "$OUT/nutzlast/BOOTX64.EFI" "$OUT/tftp/moo.efi"
  pruefdatei "$OUT/tftp/moo.efi" "$OUT/tftp/moo.efi.pruef"
  cp "$OUT/tftp/moo.efi" "$OUT/tftp-falsch/moo.efi"
  awk '{ printf "%s 00000000\n", $1 }' "$OUT/tftp/moo.efi.pruef" >"$OUT/tftp-falsch/moo.efi.pruef"
  echo "      Pruefdatei: $(cat "$OUT/tftp/moo.efi.pruef")"
  lader_qemu() {  # tftp-Verzeichnis, Serial-Log, Pcap, Endemuster
    cp "$OVMF_VARS" "$OUT/vars-lader.fd"
    : >"$2"
    timeout --foreground $((WAIT_S + 60))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,format=raw,unit=1,file="$OUT/vars-lader.fd" \
        -drive format=raw,file="$DISK" \
        -m 512M -vga std -display none -no-reboot -monitor none \
        -netdev user,id=n0,tftp="$1" -device e1000,netdev=n0,mac=52:54:00:12:34:58,romfile= \
        -object filter-dump,id=f0,netdev=n0,file="$3" \
        -serial file:"$2" -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
        >"$OUT/qemu-lader.log" 2>&1 &
    local pid=$! i
    for ((i = 0; i < WAIT_S + 50; i++)); do
      grep -aq -- "$4" "$2" 2>/dev/null && break
      kill -0 "$pid" 2>/dev/null || break
      sleep 1
    done
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    grep -a -E "MOO-LADER|MOO-UEFI-BILD|MOO-E1000-(SELBST|ARP|ENDE)" "$2" | tr -d '\000' | sed 's/^/      | /'
  }
  lader_qemu "$OUT/tftp" "$SERIAL_LOG" "$OUT/net.pcap" "MOO-E1000-ENDE"
  for m in "MOO-LADER NETZKARTEN: 1 (E1000)" "MOO-LADER LINK: PORT 0" "MOO-LADER SERVER 10.0.2.2" \
           "MOO-LADER PRUEFSUMME OK" "MOO-LADER STARTE IMAGE" "MOO-UEFI-BILD-START" \
           "MOO-E1000-SELBST 00" "MOO-E1000-ARP 1" "MOO-E1000-ENDE"; do
    grep -aq -- "$m" "$SERIAL_LOG" || fail "Lader: '$m' fehlt"
  done
  grep -aq "MOO-LADER FEHLER" "$SERIAL_LOG" && fail "Lader meldet FEHLER"
  echo "      OK: Lader holt moo.efi per TFTP, CRC32/PE ok, startet die Nutzlast, Nutzlast laeuft (ARP 1, ENDE)"
  # Negativfall: falsche Pruefsumme -> nichts starten, Statusanzeige bleibt.
  lader_qemu "$OUT/tftp-falsch" "$OUT/serial-falsch.log" "$OUT/net-falsch.pcap" "MOO-LADER R = NEU"
  grep -aq "MOO-LADER FEHLER: PRUEFSUMME FALSCH" "$OUT/serial-falsch.log" || fail "Negativfall: PRUEFSUMME FALSCH fehlt"
  grep -aq "MOO-UEFI-BILD-START" "$OUT/serial-falsch.log" && fail "Negativfall: Image trotz falscher Pruefsumme gestartet"
  grep -aq "MOO-LADER R = NEU" "$OUT/serial-falsch.log" || fail "Negativfall: Statusanzeige (R/ESC) fehlt"
  echo "      OK: Negativfall: falsche Pruefsumme erkannt, nichts gestartet, Statusanzeige bleibt"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI uefi.img tftp/moo.efi tftp/moo.efi.pruef net.pcap serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  fi
  echo "############################################################"
  echo "# ERGEBNIS: Lader-Pruefung gruen (TFTP, Pruefung, Start aus dem Speicher, Negativfall)."
  echo "############################################################"
  exit 0
fi
if [ "$PRUEFUNG" = "netz-kern" ]; then
  # P7-T5: N100-Weg in QEMU. Auf der ESP liegt nur der Netz-Lader (wie auf dem
  # Stick); per TFTP kommen moo.efi = Moo-UEFI-Loader und moo.kern = Moo-Kern.
  # Der Netz-Lader reicht den Kern als LoadOptions (MooKernUebergabe) weiter,
  # der Loader springt nach ExitBootServices hinein.
  MOO_UEFI_PRUEFUNG=kern MOO_UEFI_OUT="$OUT/kern" bash "$0" >"$OUT/kern.log" 2>&1 \
    || { tail -5 "$OUT/kern.log" | sed 's/^/      | /'; fail "Kern-Modus (Loader + Kern) nicht gruen"; }
  echo "      OK: Loader und Kern gebaut und einzeln gruen ($OUT/kern)"
  mkdir -p "$OUT/tftp"
  cp "$OUT/kern/BOOTX64.EFI" "$OUT/tftp/moo.efi"
  cp "$OUT/kern/KERN.ELF" "$OUT/tftp/moo.kern"
  for f in moo.efi moo.kern; do  # "<laenge> <crc32hex>" (CRC32 aus dem gzip-Trailer)
    printf '%s %s\n' "$(stat -c %s "$OUT/tftp/$f")" \
      "$(gzip -c -n "$OUT/tftp/$f" | tail -c8 | od -An -tx4 -N4 | tr -d ' \n')" >"$OUT/tftp/$f.pruef"
  done
  cp "$OVMF_VARS" "$OUT/vars-netz.fd"
  : >"$SERIAL_LOG"; : >"$DEBUG_LOG"
  NETZ_KERN_QEMU=()
  timeout --foreground $((WAIT_S + 60))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 "${NETZ_KERN_QEMU[@]}" \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars-netz.fd" \
      -drive format=raw,file="$DISK" \
      -m 512M -vga std -display none -no-reboot -monitor none \
      -netdev user,id=n0,tftp="$OUT/tftp" -device e1000,netdev=n0,mac=52:54:00:12:34:58,romfile= \
      -object filter-dump,id=nklog,netdev=n0,file="$OUT/net.pcap" \
      -serial file:"$SERIAL_LOG" -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
      >"$OUT/qemu-netz.log" 2>&1 &
  QPID=$!
  for ((i = 0; i < WAIT_S + 50; i++)); do
    grep -aq "MOO-KERNEL OK" "$SERIAL_LOG" 2>/dev/null && break
    grep -aqE "MOO-LADER (FEHLER|R = NEU)" "$SERIAL_LOG" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  tr -d '\000\r' <"$SERIAL_LOG" | grep -aE "MOO-(LADER|KERNEL|UEFI-BILD)" | sed 's/^/      | /'
  for m in "UEBERGABE AN MOO.EFI" "MOO-UEFI-BILD-KERN" "MOO-LADER START (MOO, OHNE C)" \
           "MOO-LADER KERN AUS UEBERGABE" "MOO-LADER EXITBOOTSERVICES OK" \
           "MOO-KERNEL RIP FFFFFFFF8" "MOO-KERNEL OK"; do
    grep -aqF -- "$m" "$SERIAL_LOG" || fail "Netz-Kern: '$m' fehlt"
  done
  grep -aq "MOO-LADER FEHLER" "$SERIAL_LOG" && fail "Netz-Kern: Fehler gemeldet"
  kern_netz_pruefen "$OUT/net.pcap" "$SERIAL_LOG" || fail "Netz-Boot: Kern-Netz-Log unvollstaendig"
  echo "      OK: Netz-Lader -> TFTP moo.efi + moo.kern -> Uebergabe -> Moo-Loader -> ExitBootServices -> Moo-Kern"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI tftp/moo.efi tftp/moo.efi.pruef tftp/moo.kern tftp/moo.kern.pruef serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  fi
  echo "############################################################"
  echo "# ERGEBNIS: Netz-Kern-Pruefung gruen (N100-Weg: Netz-Lader -> Moo-Loader -> Moo-Kern)."
  echo "############################################################"
  exit 0
fi
# QEMU-Monitor ueber FIFO: nach dem Rueckkehr-Marker 'screendump', dann 'quit'.
mkfifo "$OUT/monitor.in" || fail "FIFO fuer QEMU-Monitor nicht anlegbar"
exec 3<>"$OUT/monitor.in"
timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
    -drive format=raw,file="$DISK" \
    -m 128M -vga std -display none -no-reboot \
    -device qemu-xhci -device usb-tablet \
    -netdev user,id=n0 -device e1000,netdev=n0,mac=52:54:00:12:34:56${NIC_ROM} \
    -netdev user,id=n1 -device e1000,netdev=n1,mac=52:54:00:12:34:57${NIC_ROM} \
    -object filter-dump,id=f0,netdev=n0,file="$OUT/net.pcap" \
    -object filter-dump,id=f1,netdev=n1,file="$OUT/net1.pcap" \
    -monitor stdio -serial file:"$SERIAL_LOG" \
    -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
    <"$OUT/monitor.in" >"$OUT/monitor.log" 2>&1 &
QPID=$!
qemu_ende() { printf 'quit\n' >&3 2>/dev/null || true; exec 3>&-; wait "$QPID" 2>/dev/null || true; }

warte_marker() {  # Datei Muster Sekunden
  local f="$1" m="$2" n="${3:-15}" i
  for ((i = 0; i < n; i++)); do
    grep -q -- "$m" "$f" 2>/dev/null && return 0
    kill -0 "$QPID" 2>/dev/null || return 1
    sleep 1
  done
  return 1
}
schnappschuss() {  # Pfad
  rm -f "$1"; printf 'screendump %s -f png\n' "$1" >&3
  for ((i = 0; i < 15; i++)); do [ -s "$1" ] && break; sleep 1; done
  [ -s "$1" ]
}
monitor() { printf '%s\n' "$1" >&3; }

# Start: Moo hat gezeichnet und praesentiert (MOO-UEFI-BILD) und wartet in
# der Ereignisschleife (kern_eingabe_warten) auf Tastatur/Maus.
if ! warte_marker "$SERIAL_LOG" "MOO-UEFI-BILD" "$WAIT_S"; then
  qemu_ende
  sed 's/^/      | /' "$DEBUG_LOG" "$SERIAL_LOG" | tail -20
  fail "Marker MOO-UEFI-BILD fehlt (GOP/HEAP-FAIL oder kern_bild_zeige lieferte falsch)"
fi
schnappschuss "$OUT/screenshot_start.png" || { qemu_ende; fail "kein Start-Screenshot"; }
# Kabel an NIC 2 ziehen (nach der Startzeile): ab jetzt darf nur NIC 1 senden.
monitor 'set_link n1 off'
sleep 1
# Tastatur: Tab wechselt den Fokus auf INFO, Enter loest INFO aus.
monitor 'sendkey tab'
warte_marker "$SERIAL_LOG" "MOO-UEFI-FOKUS 1" 10 \
  || { qemu_ende; fail "Tastatur: Tab erreicht Moo nicht (MOO-UEFI-FOKUS 1 fehlt)"; }
monitor 'sendkey ret'
warte_marker "$SERIAL_LOG" "MOO-UEFI-AKTION 1" 10 \
  || { qemu_ende; fail "Tastatur: Enter loest INFO nicht aus (MOO-UEFI-AKTION 1 fehlt)"; }
schnappschuss "$OUT/screenshot_tastatur.png" || { qemu_ende; fail "kein Tastatur-Screenshot"; }
# Maus (usb-tablet, absolut 0..32767): Klick auf die Mitte von WEITER.
# Layout-Herleitung (Font 19 px, 1280x800): Knopf x=52..138, y=253..296 -> Mitte (95,274).
MAUS="Zeigerprotokoll fehlt (fail-closed: MOO-UEFI-MAUS-FEHLT), nur Tastatur geprueft"
if grep -q "MOO-UEFI-MAUS-OK" "$DEBUG_LOG"; then
  # QEMU schickt Mausbefehle an das aktive Geraet (Standard: PS/2-Maus #2, die
  # OVMF nicht als Pointer-Protokoll anbietet). Das usb-tablet per 'info mice'
  # finden und mit 'mouse_set' aktivieren.
  monitor 'info mice'
  sleep 1
  TABLET="$(tr -d '\r' < "$OUT/monitor.log" | grep -oE 'Mouse #[0-9]+: QEMU HID Tablet' | tail -1 | grep -oE '[0-9]+')"
  [ -n "$TABLET" ] || { qemu_ende; fail "Maus: usb-tablet nicht in 'info mice' gefunden"; }
  monitor "mouse_set $TABLET"
  sleep 1
  monitor "mouse_move $((95 * 32767 / 1280)) $((274 * 32767 / 800))"
  sleep 1
  monitor 'mouse_button 1'; sleep 1; monitor 'mouse_button 0'
  warte_marker "$SERIAL_LOG" "MOO-UEFI-KLICK" 10 \
    || { qemu_ende; fail "Maus: Klick auf WEITER nicht erkannt (MOO-UEFI-KLICK fehlt)"; }
  warte_marker "$SERIAL_LOG" "MOO-UEFI-AKTION 0" 10 \
    || { qemu_ende; fail "Maus: Klick loest WEITER nicht aus (MOO-UEFI-AKTION 0 fehlt)"; }
  MAUS="Klick auf WEITER erkannt (MOO-UEFI-KLICK, MOO-UEFI-AKTION 0)"
fi
schnappschuss "$SHOT" || { qemu_ende; fail "kein End-Screenshot ($SHOT)"; }
# Escape beendet die Moo-Schleife -> Rueckkehr aus moo_efi_haupt -> MOO-UEFI-OK.
monitor 'sendkey esc'
warte_marker "$DEBUG_LOG" "MOO-UEFI-OK" 10 \
  || { qemu_ende; fail "Escape beendet die Ereignisschleife nicht (MOO-UEFI-OK fehlt)"; }
qemu_ende

grep -q "MOO-UEFI-ENTRY"   "$DEBUG_LOG" || fail "Marker MOO-UEFI-ENTRY fehlt"
grep -q "MOO-UEFI-GOP-OK"  "$DEBUG_LOG" || fail "Marker MOO-UEFI-GOP-OK fehlt (GOP nicht gefunden/Format)"
grep -q "MOO-UEFI-HEAP-OK" "$DEBUG_LOG" || fail "Marker MOO-UEFI-HEAP-OK fehlt (AllocatePages)"
grep -q "MOO-UEFI-WATCHDOG-AUS" "$DEBUG_LOG" || fail "Marker MOO-UEFI-WATCHDOG-AUS fehlt (SetWatchdogTimer(0) - sonst Neustart nach 5 min)"
grep -q "MOO-UEFI-MOO"     "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -10; fail "Marker MOO-UEFI-MOO fehlt (moo-Teil lief nicht)"; }
grep -q "MOO-UEFI-TASTE"   "$SERIAL_LOG" || fail "Marker MOO-UEFI-TASTE fehlt"
# P7-T15: QEMU hat keine I226 -> der Moo-Treiber findet 0 Ports (Statuszeile "I226 FEHLT").
grep -q "MOO-UEFI-I226 0" "$SERIAL_LOG" || fail "Marker MOO-UEFI-I226 0 fehlt (I226-Erkennung ohne Geraet)"
echo "      OK: I226-Erkennung ohne Geraet: MOO-UEFI-I226 0"
grep -q "MOO-UEFI-TREIBER-$TREIBER_ERWARTET" "$DEBUG_LOG" \
  || { grep -a "MOO-UEFI-TREIBER" "$DEBUG_LOG" | sed 's/^/      | /'; fail "Marker MOO-UEFI-TREIBER-$TREIBER_ERWARTET fehlt"; }
echo "      OK: Treiber-Lader meldet MOO-UEFI-TREIBER-$TREIBER_ERWARTET"
# Netzwerk-Konsole: SNP in OVMF (e1000) muss da sein; Nutzlast im pcap pruefen.
grep -q "MOO-UEFI-NETZ-OK" "$DEBUG_LOG" || fail "Marker MOO-UEFI-NETZ-OK fehlt (SNP/e1000 nicht initialisiert)"
# Zwei NICs (n0, n1), beide bei Init mit Link. Danach zieht das Gate das Kabel an n1
# (Monitor set_link n1 off, siehe unten) und prueft, dass nur n0 noch sendet.
grep -q "MOO-NET-NICS 2 LINKS 2" "$DEBUG_LOG" || fail "NIC-Zaehlung falsch: erwartet 2 NICs mit Link (MOO-NET-NICS 2 LINKS 2 fehlt, siehe $DEBUG_LOG)"
[ -s "$OUT/net.pcap" ] || fail "kein net.pcap (filter-dump)"
NETZ_START=$(grep -a -c "MOO-NET MOOS STARTET" "$OUT/net.pcap")
NETZ_TASTE=$(grep -a -c "Return" "$OUT/net.pcap")
[ "$NETZ_START" -ge 1 ] || fail "UDP-Log 'MOO-NET MOOS STARTET' fehlt im pcap"
[ "$NETZ_TASTE" -ge 1 ] || fail "UDP-Log des Tastennamens 'Return' fehlt im pcap"
[ -s "$OUT/net1.pcap" ] || fail "kein net1.pcap (filter-dump NIC 2)"
NIC1_START=$(grep -a -c "MOO-NET MOOS STARTET" "$OUT/net1.pcap")
NIC1_TASTE=$(grep -a -c "Return" "$OUT/net1.pcap")
[ "$NIC1_START" -ge 1 ] || fail "NIC 2 (Link an) sendete die Startzeile nicht"
[ "$NIC1_TASTE" -eq 0 ] || fail "NIC 2 (Link aus) sendet trotzdem $NIC1_TASTE Tastenzeile(n)"
echo "      OK: Zwei NICs: NIC0 Return=$NETZ_TASTE (Link an); NIC1 Start=$NIC1_START vor Kabelzug, Return=$NIC1_TASTE nach Kabelzug"
echo "      OK: Netzwerk-Konsole: $NETZ_START Startzeile(n), $NETZ_TASTE Tastenzeile(n) im pcap $(stat -c %s "$OUT/net.pcap") Bytes"
echo "      OK: Tastatur Tab -> FOKUS 1, Enter -> AKTION 1; Maus: $MAUS"
[ -s "$SHOT" ] || fail "kein Screenshot ($SHOT) — QEMU-Monitor screendump"
grep -E "MOO-UEFI" "$DEBUG_LOG" "$SERIAL_LOG" | sed 's/^/      | /'
echo "      OK: Screenshot $(stat -c %s "$SHOT") Bytes -> $SHOT"
if command -v magick >/dev/null 2>&1; then
  # Akzentlinie (y = kopf_h .. kopf_h+3, kopf_h = 19*3 + 32 = 89) ist nach einer
  # Aktion bernstein (255,196,64) statt tuerkis (73,192,169).
  AKZENT="$(magick "$SHOT" -format '%[pixel:p{5,90}]' info: 2>/dev/null)"
  case "$AKZENT" in
    *"255,196,64"*) echo "      OK: Akzentlinie nach Aktion bernstein ($AKZENT)" ;;
    *) fail "Akzentlinie zeigt keine Aktion (erwartet srgb(255,196,64), ist '$AKZENT')" ;;
  esac
fi
OCR="uebersprungen (tesseract fehlt)"
if command -v tesseract >/dev/null 2>&1; then
  tesseract "$SHOT" "$OUT/ocr" --psm 6 >/dev/null 2>&1
  if grep -Fq "MOO UEFI SCREEN" "$OUT/ocr.txt" 2>/dev/null; then
    OCR="'MOO UEFI SCREEN' erkannt"
  else
    sed 's/^/      | ocr: /' "$OUT/ocr.txt" 2>/dev/null | head -10
    fail "OCR findet 'MOO UEFI SCREEN' nicht im Screenshot (Firmware-Bild statt Moo-Bild?)"
  fi
fi
echo "      OK: OCR $OCR"
if command -v tesseract >/dev/null 2>&1; then
  echo "      Statuszeile (OCR, nur Info): $(tesseract "$SHOT" - --psm 11 2>/dev/null | grep -aE 'I226|1226|TREIBER|NETZ' | tr '\n' ' ')"
fi
if [ -n "${MOO_UEFI_OUT:-}" ]; then
  (cd "$OUT" && sha256sum BOOTX64.EFI uefi.img screenshot_start.png screenshot_tastatur.png screenshot.png net.pcap net1.pcap $( [ -f treiber.efi ] && echo treiber.efi ) >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  sha256sum "$BIN" | sed 's/^/      sha256: /'
fi
echo "############################################################"
echo "# ERGEBNIS: ALLE 4 Phasen gruen (moo zeichnet als UEFI-App auf den GOP-Bildschirm)."
echo "############################################################"
