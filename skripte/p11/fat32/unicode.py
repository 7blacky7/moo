"""Alle Unicode-Codepunkte gegen die unveränderte offizielle UCD prüfen."""
import hashlib
import struct
import sys
from pathlib import Path

referenz = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(referenz).hexdigest() == "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c"
abbildungen = {}
for zeile in referenz.decode().splitlines():
    felder = zeile.split(";")
    if felder[12]:
        abbildungen[int(felder[0], 16)] = int(felder[12], 16)
ausgabe = Path(sys.argv[2]).read_bytes()
assert len(ausgabe) == 0x110000 * 4, len(ausgabe)
for punkt, (ist,) in enumerate(struct.iter_unpack("<I", ausgabe)):
    soll = abbildungen.get(punkt, punkt)
    assert ist == soll, (hex(punkt), hex(ist), hex(soll))
print(f"PASS Unicode17 Simple-Upper: 1114112 Codepunkte / {len(abbildungen)} Abbildungen")
