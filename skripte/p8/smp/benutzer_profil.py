"""Privaten ELF-Kern für die T18-Benutzerlaufzeit-Abnahme konfigurieren."""
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
data = bytearray(path.read_bytes())
tag = struct.pack("<QQ", 0x3154524C454F4F4D, 0)
if data.count(tag) != 1:
    raise SystemExit("T18-Profil muss genau einmal im unkonfigurierten Kern stehen")
position = data.index(tag)
struct.pack_into("<Q", data, position + 8, 1)
path.write_bytes(data)
print("T18: privates Benutzerlaufzeit-Profil gesetzt")
