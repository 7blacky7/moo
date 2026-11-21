#!/usr/bin/env python3
"""Moo-Mul-Xor-Durchsatz, echte CPU-Affinität und ASM-ISA-Belege."""
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/p11/array-q"
COMPILER = os.environ.get("MOO_COMPILER", str(ROOT / "build/p2-t123/target/release/moo-compiler"))
OUT.mkdir(parents=True, exist_ok=True)
subprocess.run([COMPILER, "compile", "compiler/tests/p11/array_q_bench.moos", "-o", str(OUT / "bench")], cwd=ROOT, check=True)
subprocess.run([COMPILER, "compile", "stdlib/array_q.moos", "--no-stdlib", "--target", "x86_64-bare", "--emit-ir", "-o", str(OUT / "math-bare.ll")], cwd=ROOT, check=True)
subprocess.run(["mise", "exec", "--", "opt", "-passes=verify", "-disable-output", str(OUT / "math-bare.ll")], cwd=ROOT, check=True)
subprocess.run(["mise", "exec", "--", "llc", "-O2", "-filetype=asm", str(OUT / "math-bare.ll"), "-o", str(OUT / "math-bare.s")], cwd=ROOT, check=True)
asm = (OUT / "math-bare.s").read_text()
proof = {}
for profile in ("gfni128", "gfni256"):
    name = f"__moo_native.__simd.{profile}.array_q_xor_simd"
    match = re.search(r"^" + re.escape(name) + r":.*?(?=^\.Lfunc_end)", asm, re.M | re.S)
    assert match, profile
    body = match.group()
    proof[profile] = {"mul": body.count("gf2p8mulb"), "affine": body.count("gf2p8affineqb")}
    assert proof[profile]["mul"] > 0 and proof[profile]["affine"] > 0, profile
    if profile == "gfni128":
        assert not re.search(r"%[yz]mm", body), "GFNI128 überschreitet ISA-Deckel"
cpu = json.loads(subprocess.check_output(["lscpu", "-J"], env=dict(os.environ, LC_ALL="C"), text=True))
fields = {entry["field"].rstrip(":"): entry["data"] for entry in cpu["lscpu"]}
flags = set(fields.get("Flags", "").split())
result = {"eingabebytes_je_messung": 65521 * 4096, "modell": fields.get("Model name"), "cpu_flags": sorted(flags & {"ssse3", "avx", "avx2", "gfni", "xsave"}), "asm": proof, "cpus": {}}
for cpu_number in map(int, os.environ.get("MOO_Q_BENCH_CPUS", "28").split()):
    assert cpu_number in os.sched_getaffinity(0), cpu_number
    samples = [[], [], []]
    checksums = set()
    for _ in range(5):
        values = list(map(int, subprocess.check_output(["taskset", "-c", str(cpu_number), str(OUT / "bench")], text=True).split()))
        assert len(values) == 6, values
        for mode in range(3):
            assert values[mode * 2] > 0
            samples[mode].append(values[mode * 2])
            checksums.add(values[mode * 2 + 1])
    assert len(checksums) == 1 and next(iter(checksums)) != 0, checksums
    modes = {}
    for name, times in zip(("skalar", "tabelle", "simd"), samples):
        median = statistics.median(times)
        modes[name] = {"nanosekunden": times, "median_ns": median, "MiB_s": (65521 * 4096 / 2**20) / (median / 1e9)}
    result["cpus"][str(cpu_number)] = {"pruefsumme": next(iter(checksums)), "pfade": modes}
(OUT / "bench-ergebnis.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
