"""QMP-Latenz und unabhängiger Rohbildabgleich für das NVMe-Kerngate."""

import hashlib
import re
import struct
import sys
import time
from argparse import ArgumentParser
from pathlib import Path

from geschirr import Qmp, laufpfad


def hmp(qmp, befehl):
    try:
        antwort = qmp.ausfuehren("human-monitor-command", **{"command-line": befehl})
    except OSError as fehler:
        raise RuntimeError(f"{befehl}: {fehler}") from fehler
    if antwort.strip():
        raise RuntimeError(f"{befehl}: {antwort.strip()}")


def latenz(lauf, serial, frist):
    geraet = "p11_nvme1_ns"
    marke = b"MOO-NVME-PROBE LATENZ START "
    with Qmp(lauf / "qmp.sock") as qmp:
        # Der vorhandene Backend-Anschluss vermeidet einen temporären
        # BlockBackend, solange ein I/O-Auftrag am Haltepunkt aussteht.
        hmp(qmp, f'qemu-io -d {geraet} "break write_aio p11_nvme_warten"')
        qmp.ausfuehren("cont")
        ende = time.monotonic() + frist
        while time.monotonic() < ende:
            if serial.exists() and marke in serial.read_bytes():
                break
            time.sleep(0.02)
        else:
            raise RuntimeError("NVMe-Latenzmarker fehlt")
        # Der Write wird unmittelbar nach der seriellen Marke übermittelt.
        # Der Gast misst die Verzögerung einer tatsächlich gehaltenen Anfrage.
        time.sleep(0.15)
        hmp(qmp, f'qemu-io -d {geraet} "resume p11_nvme_warten"')
        print("blkdebug write_aio 150 ms gehalten und freigegeben")


def muster(laenge, saat):
    return b"".join(struct.pack("<Q", (saat << 56) | index)
                    for index in range(laenge // 8))


def pruefen(lauf, serial, schreiben):
    log = serial.read_bytes()
    for blockbytes in (512, 4096):
        marker = f"MOO-NVME-PROBE BLOCKBYTES {blockbytes:016X}".encode()
        if log.count(marker) != 1:
            raise RuntimeError(f"Blockgrößenmarker fehlt für {blockbytes}")
    if log.count(b"MOO-KERNEL NVME QUEUE TIEFE 0000000000000040") != 2:
        raise RuntimeError("64er-Queues fehlen")
    vektoren = re.findall(rb"MOO-KERNEL NVME MSI-X VEKTOR ([0-9A-F]{16})", log)
    if len(vektoren) != 2 or len(set(vektoren)) != 2:
        raise RuntimeError("Zwei getrennte MSI-X-Routen fehlen")
    if schreiben:
        for marker in (b"MOO-NVME-PROBE EIO STATUS ", b"MOO-NVME-PROBE LATENZ US ",
                       b"MOO-NVME-PROBE TIMEOUT US ", b"MOO-KERNEL NVME ABORT ",
                       b"MOO-KERNEL NVME RESET 0000000000000001"):
            if log.count(marker) != 1:
                raise RuntimeError(f"Fehlender Marker: {marker.decode()}")
        if log.count(b"MOO-NVME-PROBE UMLAUF 0000000000000001") != 2:
            raise RuntimeError("CQ-Umläufe fehlen")

    for nr, blockbytes in enumerate((512, 4096)):
        datei = lauf / f"nvme{nr}.img"
        erwartet = bytearray(64 * 1024 * 1024)
        if schreiben:
            if nr == 0:
                erwartet[4096:4608] = muster(512, 0x51)
            start = 64 * blockbytes
            laenge = 307200 - 4096
            erwartet[start:start + laenge] = muster(laenge, 0x60 + nr)
        with datei.open("rb") as bild:
            ist = hashlib.file_digest(bild, "sha256").hexdigest()
        soll = hashlib.sha256(erwartet).hexdigest()
        if ist != soll:
            raise RuntimeError(f"{datei.name} SHA-256 {ist}, erwartet {soll}")
        print(f"{datei.name} {blockbytes} Byte je Block, SHA-256 {ist}")


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("aktion", choices=("latenz", "pruefen"))
    parser.add_argument("lauf")
    parser.add_argument("serial", type=Path)
    parser.add_argument("modus", choices=("1", "ro"))
    parser.add_argument("--frist", type=float, default=60)
    args = parser.parse_args()
    try:
        lauf = laufpfad(args.lauf)
        if args.aktion == "latenz":
            latenz(lauf, args.serial, args.frist)
        else:
            pruefen(lauf, args.serial, args.modus == "1")
    except (RuntimeError, ValueError, OSError) as fehler:
        sys.exit(f"NVMe-Test: {fehler}")


if __name__ == "__main__":
    main()
