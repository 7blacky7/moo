#!/usr/bin/env bash
# P10-T1: UEFI-Kern mit N QEMU-vCPUs booten und Uebergabe/Start nachweisen.
set -euo pipefail

if [[ $# != 1 || ! $1 =~ ^[1-9][0-9]*$ ]]; then
    echo "Aufruf: bash skripte/p8/smp/lauf.sh <CPU-Anzahl>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
mkdir -p build/p10/smp
out=$(mktemp -d "$root/build/p10/smp/cpu-$1.XXXXXX")
echo "QEMU mit $1 vCPU(s), Ausgabe in $out"
MOO_UEFI_PRUEFUNG=kern MOO_UEFI_SMP="$1" MOO_UEFI_OUT="$out" \
    MOO_UEFI_NETZ=0 MOO_UEFI_PING=0 bash scripts/uefi-smoke.sh

if ! rg -aqF "MOO-LADER SPRUNG " "$out/debug.log"; then
    echo "Loader-Sprung fehlt in $out/debug.log" >&2
    exit 1
fi
if ! rg -aqF "MOO-KERNEL OK" "$out/serial.log"; then
    echo "Kern-Abschlussmarke fehlt in $out/serial.log" >&2
    exit 1
fi
echo "PASS $1 vCPU(s), debugcon: MOO-LADER SPRUNG, COM1: MOO-KERNEL OK"
