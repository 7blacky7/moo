"""Prüfer-CLI auf privaten Moo-mkfs-Bildern; nur Testmedien werden beschädigt."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

binary = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
base = out / "abbild.img"

def run(image, expected, block=None, records=4096):
    before = hashlib.sha256(image.read_bytes()).hexdigest()
    result = subprocess.run([binary], env={**os.environ,
        "MOO_MOOFS_PRUEF_BILD": str(image), "MOO_MOOFS_PRUEF_RECORDS": str(records)},
        capture_output=True, timeout=20)
    text = result.stdout.decode()
    assert result.returncode == (0 if expected == 0 else 1), (result.returncode, text, result.stderr)
    if expected == 0:
        assert text.startswith("MooFS OK\n") and "SUPERBLOECKE 2\n" in text
        assert "DATEN 522\n" in text
    else:
        assert text.startswith(f"MooFS FEHLER {expected}\n"), text
        if block is not None:
            assert f"BLOCK {block}\n" in text, text
    assert hashlib.sha256(image.read_bytes()).hexdigest() == before
    return {"image": image.name, "status": result.returncode, "output": text, "sha256": before}

results = [run(base, 0)]
for name, position, block in (("daten", 3 * 4096 + 64, 3),
                             ("knoten", 2 * 4096 + 100, 2),
                             ("super", 200, 0)):
    data = bytearray(base.read_bytes())
    data[position] ^= 1
    image = out / f"pruef-{name}.img"
    image.write_bytes(data)
    results.append(run(image, 0x40001, block))
results.append(run(base, 0x40008, 2, records=1))
(out / "pruef-cli-ergebnis.json").write_text(json.dumps(results, indent=2) + "\n")
print("MooFS Prüfer-CLI: readonly, beide Superkopien, Daten/Knoten/Super lokalisiert, Workspacefehler korrekt")
