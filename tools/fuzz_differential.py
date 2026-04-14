#!/usr/bin/env python3
"""
fuzz_differential.py — Lexer-Differential zwischen Rust-Compiler und Python-Toolchain.

Phase 1 (dieser Commit): Compilierbarkeit. Für eine Korpus-Menge von .moo-Dateien
prüfen: kompiliert der Rust-Compiler? Parst der Python-Transpiler?

Wenn Rust OK und Python NOK oder umgekehrt → Drift gefunden, ausgeben.

Phase 2 (nicht in diesem Commit): Tokenvergleich per Stream, Parser-AST-Vergleich.

Aufruf:
  python3 tools/fuzz_differential.py               # default corpus
  python3 tools/fuzz_differential.py compiler/tests/*.moo beispiele/*.moo
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOO_RUST = ROOT / "compiler" / "target" / "release" / "moo-compiler"
PY_CLI = [sys.executable, "-m", "moo"]


def try_rust(path: pathlib.Path) -> tuple[bool, str]:
    out = subprocess.run(
        [str(MOO_RUST), "compile", str(path), "--emit-ir", "-o", "/tmp/_fuzz.ll"],
        capture_output=True, text=True, cwd=str(ROOT), timeout=15,
    )
    return out.returncode == 0, (out.stderr or out.stdout).strip()[:400]


def try_python(path: pathlib.Path) -> tuple[bool, str]:
    # --target=python ist die leichteste Variante: lext + parst + generiert py-Code,
    # ohne Ausführung. Timeout 10s — Drift-Signal wenn Rust akzeptiert aber Python
    # haengt.
    try:
        out = subprocess.run(
            PY_CLI + ["build", str(path), "--target=python", "-o", "/tmp/_fuzz.py"],
            capture_output=True, text=True, cwd=str(ROOT / "src"), timeout=10,
        )
    except subprocess.TimeoutExpired:
        return False, "TIMEOUT (>10s)"
    return out.returncode == 0, (out.stderr or out.stdout).strip()[:400]


def main() -> int:
    if len(sys.argv) > 1:
        files = [pathlib.Path(a) for a in sys.argv[1:]]
    else:
        files = list((ROOT / "compiler" / "tests").glob("*.moo"))[:20]
        files += list((ROOT / "beispiele").glob("*.moo"))[:20]
    files = [f.resolve() for f in files if f.exists()]

    rust_ok = rust_fail = py_ok = py_fail = 0
    drifts: list[tuple[str, str, str, str, str]] = []

    for f in files:
        r_ok, r_err = try_rust(f)
        p_ok, p_err = try_python(f)
        if r_ok: rust_ok += 1
        else:    rust_fail += 1
        if p_ok: py_ok += 1
        else:    py_fail += 1
        if r_ok != p_ok:
            drifts.append((f.name, "OK" if r_ok else "FAIL", r_err, "OK" if p_ok else "FAIL", p_err))

    print(f"Corpus: {len(files)} Dateien")
    print(f"  Rust:   {rust_ok} OK, {rust_fail} FAIL")
    print(f"  Python: {py_ok} OK, {py_fail} FAIL")
    print(f"  Drift:  {len(drifts)} Dateien (Rust-OK, Python-NOK oder umgekehrt)")
    if drifts:
        print()
        print("Details (Rust | Python | Kurz-Fehler):")
        for name, rust_s, rust_e, py_s, py_e in drifts[:50]:
            short_e = (rust_e if rust_s == "FAIL" else py_e).replace("\n", " | ")[:120]
            print(f"  {name:<45s}  rust={rust_s:<4s}  py={py_s:<4s}  {short_e}")
    return 1 if drifts else 0


if __name__ == "__main__":
    sys.exit(main())
