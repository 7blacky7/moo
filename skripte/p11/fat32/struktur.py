#!/usr/bin/env python3
"""Unabhängige FAT32-Wireprüfung; keine produktive Dateisystemimplementierung."""
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
with path.open("rb") as medium:
    bpb = medium.read(512)
    u16 = lambda off: struct.unpack_from("<H", bpb, off)[0]
    u32 = lambda off: struct.unpack_from("<I", bpb, off)[0]
    breite, spc, reserviert, fats = u16(11), bpb[13], u16(14), bpb[16]
    fat_sektoren, total, root = u32(36), u32(32), u32(44)
    assert breite in (512, 4096) and fats == 2 and root == 2
    assert bpb[510:512] == b"\x55\xaa"
    medium.seek(6 * breite)
    assert medium.read(512) == bpb, "Backup-BPB weicht ab"
    medium.seek(reserviert * breite)
    fat = medium.read(fat_sektoren * breite)
    assert medium.read(len(fat)) == fat, "FAT-Spiegel weichen ab"
    clusters = (total - reserviert - fats * fat_sektoren) // spc
    frei = sum((struct.unpack_from("<I", fat, i * 4)[0] & 0x0FFFFFFF) == 0
               for i in range(2, clusters + 2))
    medium.seek(breite)
    info = medium.read(512)
    assert struct.unpack_from("<I", info, 0)[0] == 0x41615252
    assert struct.unpack_from("<I", info, 484)[0] == 0x61417272
    assert struct.unpack_from("<I", info, 488)[0] == frei, "FSInfo-Freizaehler falsch"
    assert info[508:512] == b"\0\0\x55\xaa"
    medium.seek(7 * breite)
    assert medium.read(512) == info, "Backup-FSInfo weicht ab"
print(f"PASS Wire/Backup/FAT-Spiegel/FSInfo: {breite} Bytes, {clusters} Cluster, {frei} frei")
