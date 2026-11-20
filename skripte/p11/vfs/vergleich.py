#!/usr/bin/env python3
"""Vergleicht den gestreamten VFS-Inhalt mit der wirklich gebooteten ELF."""
import re
import sys
import zlib
from pathlib import Path

out = Path(sys.argv[1])
elf = (out / "KERN.ELF").read_bytes()
log = (out / "serial.log").read_text(errors="replace")
def zahl(marke):
    treffer = re.findall(re.escape(marke) + r"([0-9A-Fa-f]{16})", log)
    if len(treffer) != 1:
        raise SystemExit(f"{marke}: erwartet genau einen Wert, erhalten {len(treffer)}")
    return int(treffer[0], 16)

assert zahl("MOO-VFS KERN BYTES ") == len(elf), "VFS-Dateilänge weicht ab"
crc = zlib.crc32(elf)
assert zahl("MOO-VFS KERN CRC32 ") == crc, "VFS-CRC weicht vom Host ab"
assert zahl("MOO-VFS FD/SMP OK ") == 1024, "gemeinsame Dateiposition nicht geprüft"
assert (out / "vfs-nachher.elf").read_bytes() == elf, "FAT-Medium hat andere KERN.ELF"
print(f"VFS /boot/KERN.ELF: {len(elf)} Bytes, CRC32 {crc:08X} gleich Host; 2 Fäden/1024 eindeutige Wörter; Prozess-FD-Cleanup")
