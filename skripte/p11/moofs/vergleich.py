from pathlib import Path
import hashlib
import re
import sys
import os
import subprocess

out = Path(sys.argv[1])
mode = sys.argv[2]
serial = (out / "serial.log").read_text(errors="replace")
def word(label):
    match = re.search(re.escape(label) + r"([0-9A-F]{16})", serial)
    assert match, label
    return int(match[1], 16)

assert "MOO-MOOFS-PROBE-OK" in serial
assert "MOO-MOOFS-PROBE-FEHLER" not in serial
if mode == "bitkipper":
    assert word("MOO-MOOFS CRC BLOCK ") == 3
    assert "MOO-MOOFS BYTES " not in serial
else:
    assert word("MOO-MOOFS BYTES ") == 1048576
    # Unabhängiges Host-Orakel für den Nutzinhalt, kein Formatgenerator.
    table = []
    for byte in range(256):
        crc = byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
        table.append(crc)
    crc = 0xFFFFFFFF
    for i in range(1048576):
        crc = table[(crc ^ ((i * 37 + 11) & 255)) & 255] ^ (crc >> 8)
    assert word("MOO-MOOFS CRC32C ") == crc ^ 0xFFFFFFFF
expected = (out / "moofs-vorher.sha256").read_text().split()[0]
if mode == "schreiben":
    assert word("MOO-MOOFS COW OK ") > 1
    assert word("MOO-MOOFS SCRUB OK ") > word("MOO-MOOFS COW OK ")
    assert word("MOO-MOOFS SCRUB NEUSTART ") >= 1
    assert word("MOO-MOOFS SCRUB DATEN ") == 530
    assert hashlib.sha256((out / "moofs.img").read_bytes()).hexdigest() != expected
    checker = os.environ["MOO_MOOFS_KERN_PRUEFER"]
    result = subprocess.run([checker], env={**os.environ, "MOO_MOOFS_PRUEF": str(out / "moofs.img")},
                            capture_output=True, timeout=20, check=True)
    assert result.stdout == b"0\n", (result.stdout, result.stderr)
else:
    assert hashlib.sha256((out / "moofs.img").read_bytes()).hexdigest() == expected
print("MooFS-Kern: CoW-Schreibbild hosted bytegleich" if mode == "schreiben" else "MooFS-Kern: " + ("Bitkipper mit Block 3 erkannt" if mode == "bitkipper" else "1 MiB und CRC32C bytegleich") + ", Medium unverändert")
