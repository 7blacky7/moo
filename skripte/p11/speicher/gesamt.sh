#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."

if [ "$#" -eq 0 ]; then
    bytes=2147483648
    marker="P11-GESAMT OK"
elif [ "$#" -eq 1 ] && [ "$1" = --kurz ]; then
    bytes=4194304
    marker="P11-GESAMT KURZ OK"
else
    echo "Aufruf: bash skripte/p11/speicher/gesamt.sh [--kurz]" >&2
    exit 2
fi

for werkzeug in qemu-system-x86_64 sgdisk fsck.vfat parted mformat mmd mcopy mdir; do
    command -v "$werkzeug" >/dev/null || { echo "P11-Gesamtprobe benötigt $werkzeug" >&2; exit 1; }
done
mkdir -p build/p11/speicher
lauf="$(mktemp -d "$PWD/build/p11/speicher/gesamt.XXXXXX")"
echo "P11-Gesamtprobe: $lauf"
MOO_UEFI_PRUEFUNG=kern MOO_UEFI_ESP_KERN=1 MOO_UEFI_INSTALL_PROBE=gesamt \
  MOO_UEFI_SMP=4 MOO_UEFI_CPU=max MOO_GESAMT_BYTES="$bytes" \
  MOO_UEFI_KVM="${MOO_UEFI_KVM:-1}" MOO_UEFI_WAIT="${MOO_UEFI_WAIT:-7200}" \
  MOO_UEFI_OUT="$lauf" bash scripts/uefi-smoke.sh | tee "$lauf/gate.log"
grep -Fq "$marker:" "$lauf/gate.log"
test -s "$lauf/gesamt-protokoll.json"
