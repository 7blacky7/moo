"""QMP-Slotentnahme und Rohbild-Orakel der AHCI-Kernprobe."""

import hashlib
import re
import struct
import sys
import time
from argparse import ArgumentParser
from collections import Counter
from pathlib import Path

from geschirr import Qmp, laufpfad


def entfernen(lauf, serial, frist):
    ende = time.monotonic() + frist
    while time.monotonic() < ende:
        daten = serial.read_bytes() if serial.exists() else b""
        if b"MOO-AHCI-PROBE-FEHLER" in daten:
            raise RuntimeError("Gastprobe vor der PCIe-Slotentnahme gescheitert")
        if b"MOO-AHCI-PROBE HOTPLUG START " in daten:
            break
        time.sleep(0.02)
    else:
        raise RuntimeError("Hotplug-Startmarker fehlt")
    with Qmp(lauf / "qmp.sock") as qmp:
        qmp.entfernen("p11_sata1")
    print("DEVICE_DELETED p11_sata1 über PCIe-Slot")


def pruefen(lauf, serial):
    daten = serial.read_bytes()
    for nr in range(1, 5):
        if daten.count(f"MOO-AHCI-PROBE PLATTE {nr:016X}".encode()) != 1:
            raise RuntimeError(f"Prüfmarker für SATA-Platte {nr} fehlt")
    vektoren = re.findall(rb"MOO-KERNEL AHCI MSI ([0-9A-F]{16})", daten)
    if len(set(vektoren)) != 2 or b"0000000000000000" in vektoren:
        raise RuntimeError("Zwei MSI-Routen fehlen")
    if daten.count(b"MOO-KERNEL AHCI RESET 0000000000000001") != 2:
        raise RuntimeError("AHCI-Resetmarker fehlen")
    if b"MOO-AHCI-PROBE HOTPLUG OFFLINE 000000000000000B" not in daten:
        raise RuntimeError("Getypter Offline-Marker fehlt")
    opcodes = Counter(int(w, 16) for w in re.findall(rb"MOO-KERNEL AHCI OPCODE ([0-9A-F]{16})", daten))
    erwartet = {0xEC: 4, 0x61: 6, 0x60: 12, 0x35: 2, 0x25: 2, 0xEA: 4}
    if opcodes != erwartet:
        raise RuntimeError(f"AHCI-Befehlsfolge {dict(opcodes)}, erwartet {erwartet}")
    for nr in range(4):
        datei = lauf / f"ahci{nr}.img"
        erwartetes_bild = bytearray(64 * 1024 * 1024)
        start = (16 + nr * 64) * 512
        muster = b"".join(struct.pack("<Q", ((0x70 + nr) << 56) | i) for i in range(81920 // 8))
        erwartetes_bild[start:start + len(muster)] = muster
        with datei.open("rb") as bild:
            ist = hashlib.file_digest(bild, "sha256").hexdigest()
        soll = hashlib.sha256(erwartetes_bild).hexdigest()
        if ist != soll:
            raise RuntimeError(f"{datei.name} SHA-256 {ist}, erwartet {soll}")
        print(f"{datei.name} SHA-256 {ist}")


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("aktion", choices=("entfernen", "pruefen"))
    parser.add_argument("lauf")
    parser.add_argument("serial", type=Path)
    parser.add_argument("--frist", type=float, default=60)
    args = parser.parse_args()
    try:
        lauf = laufpfad(args.lauf)
        if args.aktion == "entfernen":
            entfernen(lauf, args.serial, args.frist)
        else:
            pruefen(lauf, args.serial)
    except (OSError, RuntimeError, ValueError) as fehler:
        sys.exit(f"AHCI-Probe: {fehler}")


if __name__ == "__main__":
    main()
