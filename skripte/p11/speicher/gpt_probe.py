"""Bytevergleich der GPT- und Partitionsbereiche in privaten QEMU-Images."""

import hashlib
import struct
import subprocess
import sys
import uuid
import zlib
from argparse import ArgumentParser
from pathlib import Path

from host import gpt_pruefen, image


DISK_BYTES = 64 * 1024 * 1024
LINUX_GUID = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4").bytes_le


def kopf(sektor, lba, alternativ, erste, letzte, disk, tab_lba, tabelle):
    roh = bytearray(sektor)
    struct.pack_into("<8sIIIIQQQQ16sQIII", roh, 0, b"EFI PART", 0x10000, 92,
                     0, 0, lba, alternativ, erste, letzte, disk, tab_lba,
                     128, 128, zlib.crc32(tabelle))
    struct.pack_into("<I", roh, 16, zlib.crc32(roh[:92]))
    return roh


def erwartete_bereiche(nr, sektor):
    ende = DISK_BYTES // sektor - 1
    abstand = 1024 * 1024 // sektor
    mbr = bytearray(sektor)
    mbr[446:454] = bytes.fromhex("00 00 02 00 ee ff ff ff")
    struct.pack_into("<II", mbr, 454, 1, ende)
    mbr[510:512] = b"\x55\xaa"
    tabelle = bytearray(128 * 128)
    for n, (anfang, name) in enumerate(((abstand, "Mooü"), (3 * abstand, "P2"))):
        kennung = struct.pack("<QQ", 0x4433221100112233 + nr * 16 + n, 0x9988776655443322)
        struct.pack_into("<16s16sQQQ72s", tabelle, 128 * n, LINUX_GUID, kennung, anfang,
                         anfang + abstand - 1, 0, name.encode("utf-16-le"))
    disk = struct.pack("<QQ", 0x4422334411223344 + nr, 0xAA7755FF2288CC44)
    tab_bloecke = len(tabelle) // sektor
    backup = ende - tab_bloecke
    erste, letzte = 2 + tab_bloecke, backup - 1
    nutzdaten = b"".join(struct.pack("<Q", ((0x62 + nr) << 56) | i) for i in range(sektor // 8))
    return (
        (0, mbr),
        (sektor, kopf(sektor, 1, ende, erste, letzte, disk, 2, tabelle)),
        (2 * sektor, tabelle),
        ((abstand + 2) * sektor, nutzdaten),
        (backup * sektor, tabelle),
        (ende * sektor, kopf(sektor, ende, 1, erste, letzte, disk, backup, tabelle)),
    )


def pruefen(pfad, nr, sektor):
    pfad = image(pfad)
    if pfad.stat().st_size != DISK_BYTES:
        raise ValueError(f"Unerwartete Größe {pfad}")
    erwartet = hashlib.sha256()
    nullen = bytes(1024 * 1024)
    pos = 0
    with pfad.open("rb") as datei:
        for offset, soll in erwartete_bereiche(nr, sektor):
            datei.seek(offset)
            if datei.read(len(soll)) != soll:
                raise ValueError(f"{pfad.name}: Bereich bei Byte {offset} weicht ab")
            while pos < offset:
                n = min(offset - pos, len(nullen))
                erwartet.update(nullen[:n])
                pos += n
            erwartet.update(soll)
            pos += len(soll)
        datei.seek(0)
        wirklich = hashlib.file_digest(datei, "sha256").hexdigest()
    if pos != DISK_BYTES or wirklich != erwartet.hexdigest():
        raise ValueError(f"{pfad.name}: Bytes außerhalb der Sollbereiche weichen ab")
    print(f"{sektor}-Byte-Blöcke: GPT-Kopien, Schutz-MBR und Partitionsdaten bytegleich; SHA-256 {wirklich}")


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("lauf", type=Path)
    parser.add_argument("serial", type=Path)
    args = parser.parse_args()
    try:
        serial = args.serial.read_bytes().replace(b"\r", b"")
        for marker in (b"MOO-GPT REPARATUR BLOCKBYTES 0000000000000200",
                       b"MOO-GPT REPARATUR BLOCKBYTES 0000000000001000", b"MOO-GPT-PROBE-OK"):
            if serial.count(marker) != 1:
                auszug = b"\n".join(zeile for zeile in serial.splitlines() if b"MOO-GPT" in zeile)
                print(auszug.decode(errors="replace"), file=sys.stderr)
                raise ValueError(f"Probe-Marker fehlt oder ist mehrdeutig: {marker.decode()}")
        for nr, sektor in enumerate((512, 4096)):
            pruefen(args.lauf / f"nvme{nr}.img", nr, sektor)
        # sgdisk behandelt reguläre Images als Datenträger mit 512-Byte-Sektoren.
        gpt_pruefen(args.lauf / "nvme0.img")
    except (OSError, ValueError, subprocess.SubprocessError) as fehler:
        sys.exit(f"GPT-Hostprüfung: {fehler}")


if __name__ == "__main__":
    main()
