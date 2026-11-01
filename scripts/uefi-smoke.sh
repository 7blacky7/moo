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
BIN="$ROOT/compiler/target/release/moo-compiler"
# P7-T14: MOO_UEFI_PRUEFUNG=e1000 prueft den Moo-e1000-Treiber (eigene App, eigener QEMU-Lauf).
PRUEFUNG="${MOO_UEFI_PRUEFUNG:-}"
if [ "$PRUEFUNG" = "e1000" ]; then
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/netz_e1000.moos}"
else
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/hallo_uefi.moos}"
fi
ENTRY_C="$ROOT/compiler/runtime/boot/uefi_entry.c"
RT_C="$ROOT/compiler/runtime/boot/uefi_rt.c"
RT_SOURCES=(moo_bare.c moo_bare_alloc.c moo_freestanding_mem.c moo_surface_core.c moo_surface.c)
CFLAGS=(--target=x86_64-w64-windows-gnu -std=c11 -O2 -ffreestanding -fno-stack-protector
        -mno-stack-arg-probe -ffunction-sections -fdata-sections -DMOO_BARE)
WAIT_S="${MOO_UEFI_WAIT:-45}"

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
  timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 -machine q35 \
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
  ECAM="$(grep -ao 'MOO-E1000-ECAM [0-9a-f]*' "$SERIAL_LOG" | awk '{print $2}')"
  [ -n "$ECAM" ] && [ "$((16#$ECAM))" -ne 0 ] || fail "keine ECAM-Basis aus ACPI-MCFG"
  grep -q "MOO-E1000-PCI " "$SERIAL_LOG" || fail "e1000 im PCI-Scan nicht gefunden"
  grep -q "MOO-E1000-MAC 525400123458" "$SERIAL_LOG" || fail "MAC stimmt nicht (erwartet 52:54:00:12:34:58)"
  grep -q "MOO-E1000-LINK 1" "$SERIAL_LOG" || fail "kein Link"
  grep -q "MOO-E1000-TX 2" "$SERIAL_LOG" || fail "Senden: nicht beide Rahmen fertig (STA.DD)"
  grep -q "MOO-E1000-ARP 1" "$SERIAL_LOG" || fail "Empfang: keine ARP-Antwort von 10.0.2.2"
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
# QEMU-Monitor ueber FIFO: nach dem Rueckkehr-Marker 'screendump', dann 'quit'.
mkfifo "$OUT/monitor.in" || fail "FIFO fuer QEMU-Monitor nicht anlegbar"
exec 3<>"$OUT/monitor.in"
timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 \
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
