#!/usr/bin/env python3
"""P/Q-Seiten, inkrementelles Q und vollständige bekannte Zweier-Ausfälle."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/p11/array-q"
COMPILER = os.environ.get("MOO_COMPILER", str(ROOT / "build/p2-t123/target/release/moo-compiler"))

def mul(a, b):
    v = 0
    for bit in range(8):
        if b & (1 << bit):
            v ^= a << bit
    for bit in range(14, 7, -1):
        if v & (1 << bit):
            v ^= 0x11D << (bit - 8)
    return v

def coefficient(slot):
    c = 1
    for _ in range(slot):
        c = mul(c, 2)
    return c

def parity(count, length, updated):
    p, q = bytearray(length), bytearray(length)
    for source in range(count):
        slot = (source * 5 + 3) % 16
        c = coefficient(slot)
        for i in range(length):
            value = (slot * 37 + i * 13 + (i // 7) * 19 + 17) % 256
            if updated and source == count // 2:
                value ^= 0xA5
            p[i] ^= value
            q[i] ^= mul(value, c)
    return bytes(p + q)

OUT.mkdir(parents=True, exist_ok=True)
subprocess.run([COMPILER, "compile", "compiler/tests/p11/array_q_host.moos", "-o", str(OUT / "matrix")], cwd=ROOT, check=True)
start = time.perf_counter()
actual = subprocess.run([str(OUT / "matrix")], check=True, stdout=subprocess.PIPE, timeout=180).stdout
elapsed = time.perf_counter() - start
expected = bytearray()
cases = 0
pairs = 0
for mode in range(3):
    for count in (4, 14):
        for length in (0, 1, 15, 16, 31, 32, 33, 97, 4096):
            expected += parity(count, length, False)
            expected += parity(count, length, True)
            cases += 1
            pairs += (count + 2) * (count + 3) // 2
assert actual == expected, (len(actual), len(expected))
result = {"faelle": cases, "einzel_und_zweier_ausfaelle": pairs, "bytes": len(actual), "sha256": hashlib.sha256(actual).hexdigest(), "testsekunden": elapsed}
(OUT / "matrix-ergebnis.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"PASS P/Q: {cases} Seitenfälle, {pairs} Einzel-/Zweierausfälle, alle Pfade/Python bitgleich ({elapsed:.3f}s)")
