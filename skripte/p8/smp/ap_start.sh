#!/usr/bin/env bash
# P10-T12: 20 frische OVMF-Boots desselben Images, nur AP-/Kernstart prüfen.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "Aufruf: bash skripte/p8/smp/ap_start.sh <Smoke-Artefaktordner> [Laeufe=20] [CPUs=4]" >&2
    exit 2
fi
basis=$(realpath "$1")
laeufe=${2:-20}
cpus=${3:-4}
stress=${MOO_SMP_STRESS:-0}
[[ $laeufe =~ ^[1-9][0-9]*$ && $cpus =~ ^[1-9][0-9]*$ && $cpus -le 64 ]] || exit 2
[[ $stress == 0 || ( $stress == 1 && $cpus -le 8 ) ]] || exit 2
accel=(-accel tcg,thread=multi)
if [[ ${MOO_SMP_KVM:-0} == 1 ]]; then
    accel=(-enable-kvm -cpu host)
fi
frist=300
ablage=p10-t12
if [[ $stress == 1 ]]; then
    frist=800
    ablage=p10-t13
    maske=$(printf '%016X' "$(((1 << cpus) - 1))")
fi
frist=${MOO_SMP_FRIST:-$frist}
[[ $frist =~ ^[1-9][0-9]*$ ]] || exit 2
[[ -f "$basis/uefi.img" && -f "$basis/KERN.ELF" ]] || exit 2
ovmf=""
for c in /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd \
         /usr/share/OVMF/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_CODE.fd; do
    if [[ -f $c && -f ${c/CODE/VARS} ]]; then ovmf=$c; break; fi
done
[[ -n $ovmf ]] || { echo "OVMF fehlt" >&2; exit 1; }
mkdir -p "$root/build/$ablage"
out=${MOO_SMP_OUT:-}
if [[ -z "$out" ]]; then
    out=$(mktemp -d "$root/build/$ablage/serie.XXXXXX")
else
    mkdir -p "$out"
    out=$(realpath "$out")
fi
echo "AP-Serie: $laeufe Laeufe, $cpus CPUs, $out"
sha256sum "$basis/KERN.ELF" "$basis/uefi.img" | tee "$out/sha256.txt"
pid=""
stop_qemu() {
    if [[ -n $pid ]]; then
        kill -TERM "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        pid=""
    fi
}
trap stop_qemu EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
for ((lauf=1; lauf<=laeufe; lauf++)); do
    ort="$out/lauf-$lauf"
    mkdir -p "$ort"
    cp "${ovmf/CODE/VARS}" "$ort/vars.fd"
    qemu-system-x86_64 "${accel[@]}" -smp "$cpus" -m 256M -net none \
        -drive "if=pflash,format=raw,unit=0,readonly=on,file=$ovmf" \
        -drive "if=pflash,format=raw,unit=1,file=$ort/vars.fd" \
        -drive "format=raw,snapshot=on,file=$basis/uefi.img" \
        -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0,usb_version=2 \
        -vga std -display none -no-reboot -monitor none \
        -serial "file:$ort/serial.log" -debugcon "file:$ort/debug.log" -global isa-debugcon.iobase=0xe9 \
        >"$ort/qemu.log" 2>&1 &
    pid=$!
    ok=0
    for ((tick=0; tick<frist; tick++)); do
        if [[ -f "$ort/serial.log" ]]; then
            if grep -aEq '^MOO-AP-FEHLER ' "$ort/serial.log"; then break; fi
            if grep -aEx "MOO-AP-ONLINE $((cpus-1))[[:space:]]*" "$ort/serial.log" >/dev/null &&
               grep -aEx "MOO-KERNE $cpus[[:space:]]*" "$ort/serial.log" >/dev/null &&
               grep -aEx 'MOO-KERNEL OK[[:space:]]*' "$ort/serial.log" >/dev/null; then
                if [[ $stress == 0 ]] ||
                   {
                       grep -aFxq 'MOO-SMP-OK' "$ort/serial.log" &&
                       grep -aFxq 'MOO-SMP-TLB-OK' "$ort/serial.log" &&
                       grep -aFxq 'MOO-SMP-HALT-OK' "$ort/serial.log" &&
                       grep -aFxq 'MOO-SMP FERTIG 0000000000000010' "$ort/serial.log" &&
                       grep -aFxq 'MOO-SMP ATOM 0000000000F42400' "$ort/serial.log" &&
                       grep -aFxq 'MOO-SMP MUTEX 0000000000F42400' "$ort/serial.log" &&
                       grep -aFxq "MOO-SMP CPU-MASKE $maske" "$ort/serial.log"
                   }; then
                    ok=1
                    break
                fi
            fi
        fi
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    stop_qemu
    if [[ $ok != 1 ]]; then
        echo "FAIL AP-Start Lauf $lauf, Belege $ort" >&2
        tail -60 "$ort/serial.log" "$ort/qemu.log" >&2
        exit 1
    fi
    echo "PASS AP-Start $lauf/$laeufe: MOO-AP-ONLINE $((cpus-1)), MOO-KERNEL OK"
done
echo "PASS AP-Serie $laeufe/$laeufe ohne Haenger"
