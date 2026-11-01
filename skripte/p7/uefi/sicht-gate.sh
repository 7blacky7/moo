#!/usr/bin/env bash
# P7-T2: Sichtbeweis-Gate (QEMU/OVMF-Screenshot der Moo-UEFI-App).
# Duenner Wrapper um das bestehende P011-Gate scripts/uefi-smoke.sh, das seit
# P7-T2 Surface-Runtime-Link, GOP/HEAP/BILD-Marker, Screenshot und OCR selbst
# prueft (regel-bestand-vor-neubau: kein zweites ESP-/OVMF-Harness).
# Artefakte bleiben unter build/p7-uefi/run.XXXXXXXX (BOOTX64.EFI, uefi.img,
# screenshot.png, serial.log, debug.log, sha256.txt). Es wird ausschliesslich
# in Image-Dateien geschrieben, nie auf Blockgeraete.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd -P)"
mkdir -p "$root/build/p7-uefi"
out="$(mktemp -d "$root/build/p7-uefi/run.XXXXXXXX")"
printf 'Artefakte: %s\n' "$out"
MOO_UEFI_OUT="$out" exec bash "$root/scripts/uefi-smoke.sh"
