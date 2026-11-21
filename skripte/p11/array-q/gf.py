#!/usr/bin/env python3
"""Unabhängige Polynom-Multiplikation und -Division als GF256-Orakel."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/p11/array-q"
COMPILER = os.environ.get("MOO_COMPILER", str(ROOT / "build/p2-t123/target/release/moo-compiler"))

def mul(a, b):
    product = 0
    for bit in range(8):
        if b & (1 << bit):
            product ^= a << bit
    for bit in range(14, 7, -1):
        if product & (1 << bit):
            product ^= 0x11D << (bit - 8)
    return product

OUT.mkdir(parents=True, exist_ok=True)
subprocess.run([COMPILER, "compile", "compiler/tests/p11/array_q_gf.moos", "-o", str(OUT / "gf")], cwd=ROOT, check=True)
actual = subprocess.run([str(OUT / "gf")], check=True, stdout=subprocess.PIPE).stdout
expected = bytes(mul(a, b) for a in range(256) for b in range(256))
assert len(actual) == 3 * 65536 + 274, len(actual)
for i, name in enumerate(("skalar", "tabelle", "simd")):
    block = actual[i * 65536:(i + 1) * 65536]
    assert block == expected, name
inverses = bytes(0 if a == 0 else next(b for b in range(1, 256) if mul(a, b) == 1) for a in range(256))
weights = [1]
for _ in range(15):
    weights.append(mul(weights[-1], 2))
assert actual[3 * 65536:] == inverses + bytes(weights) + b"\x00\x00", "Inversen/Slotgewichte"
print("PASS GF256: alle 256 Inversen, 16 Slotgewichte und 2 ungueltige Slots")
result = {"polynom": "0x11D", "bytepaare_je_pfad": 65536, "pfade": ["skalar", "tabelle", "simd"], "sha256": hashlib.sha256(expected).hexdigest()}
(OUT / "gf-ergebnis.json").write_text(json.dumps(result, indent=2) + "\n")
print("PASS GF256: 3 x 65536 Bytepaare gegen unabhängige Polynomdivision")
