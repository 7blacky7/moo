from pathlib import Path
import struct
import sys

elf = Path(sys.argv[1])
mode = 2 if sys.argv[2] == "bitkipper" else 1
data = bytearray(elf.read_bytes())
profile = b"MOMFS001" + bytes(8)
assert data.count(profile) == 1, "MooFS-Profil nicht eindeutig"
pos = data.index(profile)
struct.pack_into("<Q", data, pos + 8, mode)
target = struct.pack("<4Q", 0x4E564D45524F3031, 0x68, 1, 20) + b"MOOP11-MOOFS-READ001" + bytes(4)
assert data.count(target) == 1, "MooFS-NVMe-Ziel nicht eindeutig"
pos = data.index(target)
struct.pack_into("<Q", data, pos + 8, 0x70)
if sys.argv[2] == "schreiben":
    write_profile = b"MOMFW001" + bytes(8)
    assert data.count(write_profile) == 1, "MooFS-Schreibprofil nicht eindeutig"
    struct.pack_into("<Q", data, data.index(write_profile) + 8, 1)
elf.write_bytes(data)
