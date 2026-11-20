"""USB-BOT-QEMU-Gate mit 1 GiB, USB-Tasten und RAW-Bytebelegen."""

import hashlib
import json
import os
import struct
import subprocess
import sys
import time
from argparse import ArgumentParser

from geschirr import MIB, Qmp, befehlszeile, laufpfad
from host import image


GIB = 1024 * MIB
TEIL = 65536
LBA64 = (1 << 32) + 8
START = b"MOO-MSC SCHREIBEN START "
ENDE = b"MOO-MSC SCHREIBEN ENDE "


def aufbauen(lauf):
    lauf.mkdir(parents=True)
    sockel = lauf / "qmp.sock"
    if len(os.fsencode(sockel)) > 107 or "," in str(sockel):
        raise ValueError("QMP-Socketpfad ist nicht verwendbar")
    befehl = ["-qmp", f"unix:{sockel},server=on,wait=off"]
    for i, (groesse, block_bytes) in enumerate(((1280 * MIB, 4096), (3 * 1024 * GIB, 512))):
        pfad = lauf / f"msc{i}.img"
        with pfad.open("xb") as datei:
            datei.truncate(groesse)
        knoten = f"p11_usb_msc{i}"
        backend = befehlszeile({"knoten": knoten, "image": str(pfad)}, LBA64 if i == 1 else None)
        befehl.extend([
            "-blockdev", json.dumps(backend), "-device",
            f"usb-storage,bus=xhci.0,port={i + 2},id=msc{i},drive={knoten},"
            f"logical_block_size={block_bytes},physical_block_size={block_bytes},serial=MOOP11MSC000000{i + 1}",
        ])
    (lauf / "qemu-argumente.nul").write_bytes(b"\0".join(map(os.fsencode, befehl)) + b"\0")


def tippen(lauf, serial, frist):
    ende = time.monotonic() + frist
    with Qmp(lauf / "qmp.sock") as qmp:
        i = 0
        while time.monotonic() < ende:
            daten = serial.read_bytes()
            if b"MOO-MSC-PROBE-FEHLER" in daten:
                raise ValueError("Gastprobe meldet Fehler")
            if ENDE in daten:
                print(f"Tippbefehle während des Schreibens: {i}")
                return
            if START in daten:
                taste = "bcde"[i % 4]
                antwort = qmp.ausfuehren("human-monitor-command", **{"command-line": f"sendkey {taste} 50"})
                if antwort.strip():
                    raise ValueError(f"QEMU sendkey {taste}: {antwort.strip()}")
                print(f"Host-Tastendruck {taste} bei {time.monotonic():.3f}", flush=True)
                i += 1
                time.sleep(0.12)
            else:
                time.sleep(0.05)
    raise ValueError("Schreib-Endemarker blieb aus")


def muster(laenge, saat):
    return bytearray().join(struct.pack("<Q", (saat << 56) | i) for i in range(laenge // 8))


def pruefen(lauf, serial):
    daten = serial.read_bytes().replace(b"\r", b"")
    merkmale = (
        START + b"0000000040000000", ENDE + b"0000000040000000",
        b"MOO-MSC SCHREIBEN LBA64 0000000100000008",
        b"MOO-MSC SCSI FEHLERSTATUS ", b"MOO-KERNEL USB MSC RESET RECOVERY 0000000000000001",
        b"MOO-MSC-PROBE-OK",
    )
    for marker in merkmale:
        if daten.count(marker) != 1:
            raise ValueError(f"Fehlender oder mehrdeutiger Marker {marker.decode()}")
    zwischen = daten[daten.index(START):daten.index(ENDE)]
    tasten = {taste for taste in "bcde" if
              f"MOO-KERNEL USB TASTE {ord(taste):016X}".encode() in zwischen}
    if len(tasten) < 2:
        raise ValueError(f"Weniger als zwei verschiedene USB-Tasten während I/O: {sorted(tasten)}")
    print(f"USB-Tasten während 1 GiB BOT-Schreiben: {', '.join(sorted(tasten))}")

    soll = muster(TEIL, 0x63)
    digest = hashlib.sha256()
    with image(lauf / "msc0.img").open("rb") as datei:
        for i in range(GIB // TEIL):
            struct.pack_into("<Q", soll, 0, i)
            gelesen = datei.read(TEIL)
            if gelesen != soll:
                raise ValueError(f"1-GiB-Image weicht ab Segment {i} ab")
            digest.update(gelesen)
        while tail := datei.read(MIB):
            if tail != bytes(len(tail)):
                raise ValueError("Bytes hinter 1 GiB geändert")
    print(f"1 GiB WRITE(10) bytegleich, SHA-256 {digest.hexdigest()}")

    offset = LBA64 * 512
    with image(lauf / "msc1.img").open("rb") as datei:
        datei.seek(offset)
        block = datei.read(512)
        if block != muster(512, 0x64):
            raise ValueError("FUA-/WRITE(16)-Sektor weicht ab")
        print(f"WRITE(16) LBA {LBA64:#x} bytegleich, SHA-256 {hashlib.sha256(block).hexdigest()}")


def main():
    parser = ArgumentParser(description=__doc__)
    aktionen = parser.add_subparsers(dest="aktion", required=True)
    aktionen.add_parser("aufbauen").add_argument("lauf")
    for name in ("tippen", "pruefen"):
        sub = aktionen.add_parser(name)
        sub.add_argument("lauf")
        sub.add_argument("serial")
        if name == "tippen":
            sub.add_argument("--frist", type=float, required=True)
    args = parser.parse_args()
    try:
        lauf = laufpfad(args.lauf)
        if args.aktion == "aufbauen":
            aufbauen(lauf)
        elif args.aktion == "tippen":
            tippen(lauf, laufpfad(args.serial), args.frist)
        else:
            pruefen(lauf, laufpfad(args.serial))
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as fehler:
        sys.exit(f"USB-MSC-QEMU: {fehler}")


if __name__ == "__main__":
    main()
