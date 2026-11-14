#!/usr/bin/env python3
"""Capture and compare normalized LLVM IR without rebuilding the compiler."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
VERSION = 1


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalize_ir(data):
    # Do not rewrite SSA names or string constants: they may encode semantics.
    text = data.decode("utf-8").replace("\r\n", "\n")
    text = re.sub(r"(?m)^; ModuleID = .*?$", "; ModuleID = '<module>'", text)
    text = re.sub(r'(?m)^source_filename = ".*?"$', 'source_filename = "<module>"', text)
    text = text.replace(str(ROOT) + "/", "<root>/")
    return text.encode("utf-8")


def capture_one(source, compiler, work, timeout, profiles):
    relative = source.relative_to(ROOT).as_posix()
    ir = work / (digest(relative.encode()) + ".ll")
    try:
        proc = subprocess.run(
            [str(compiler), "compile", relative, "--emit-ir", "-o", str(ir), *profiles.get(relative, [])],
            cwd=ROOT, capture_output=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return relative, {"status": "timeout"}
    if proc.returncode != 0:
        return relative, {"status": "compile_error", "exit_code": proc.returncode}
    if not ir.is_file():
        return relative, {"status": "missing_ir"}
    return relative, {"status": "ok", "sha256": digest(normalize_ir(ir.read_bytes()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "compiler/target/release/moo-compiler")
    parser.add_argument("--output", type=Path, required=True, help="JSON output under build/p2-ir/")
    parser.add_argument("--baseline", type=Path, help="compare with previously captured JSON")
    parser.add_argument("--profiles", type=Path, help="use verified IR-only profile arguments from test manifest")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.timeout <= 0 or args.jobs < 1:
        parser.error("--timeout and --jobs must be positive")
    compiler = args.compiler.resolve()
    if not compiler.is_file() or not os.access(compiler, os.X_OK):
        parser.error("compiler binary is missing or not executable")
    sources = sorted(set((ROOT / "compiler/tests").glob("*.moos")) |
                     set((ROOT / "beispiele").rglob("*.moos")))
    if not sources:
        parser.error("no sources found")
    profiles = {}
    if args.profiles:
        try:
            data = json.loads(args.profiles.read_text(encoding="utf-8"))
            if data["schema_version"] != 1 or not isinstance(data["cases"], list):
                raise ValueError("unsupported profile manifest")
            seen = set()
            available = {p.relative_to(ROOT).as_posix() for p in sources}
            for item in data["cases"]:
                path = item["path"]
                if not isinstance(path, str) or path in seen:
                    raise ValueError("duplicate or invalid case path")
                seen.add(path)
                if item["category"] != "profile":
                    continue
                flags = item["args"]
                if (path not in available or item["status"] != "ir_only_observed_success"
                        or not isinstance(flags, list) or len(flags) != 3
                        or flags[:2] != ["--no-stdlib", "--target"]
                        or not isinstance(flags[2], str) or not flags[2]
                        or flags[2].startswith("-")):
                    raise ValueError(f"invalid or missing profile: {path}")
                profiles[path] = flags
        except (OSError, ValueError, KeyError, TypeError) as exc:
            parser.error(f"invalid profile manifest: {exc}")
    binary_hash = digest(compiler.read_bytes())
    source_hashes = {p.relative_to(ROOT).as_posix(): digest(p.read_bytes()) for p in sources}
    input_sha256 = digest(json.dumps(source_hashes, sort_keys=True, ensure_ascii=False).encode("utf-8"))
    output = args.output.resolve()
    if args.baseline and output == args.baseline.resolve():
        parser.error("--output must differ from --baseline")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ir-", dir=output.parent) as tmp:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            pairs = list(pool.map(lambda p: capture_one(p, compiler, Path(tmp), args.timeout, profiles), sources))
    results = dict(pairs)
    current_sources = sorted(set((ROOT / "compiler/tests").glob("*.moos")) |
                             set((ROOT / "beispiele").rglob("*.moos")))
    drift = (binary_hash != digest(compiler.read_bytes()) or current_sources != sources
             or any(digest(p.read_bytes()) != source_hashes[p.relative_to(ROOT).as_posix()]
                    for p in sources))
    manifest = {"format": VERSION, "compiler_sha256": binary_hash,
                "normalization": "module-header-root-v1", "profiles": profiles,
                "snapshot_drift": drift, "input_sha256": input_sha256, "files": results}
    output.write_text(json.dumps(manifest, indent=2, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")
    failures = [p for p, item in pairs if item["status"] != "ok"]
    print(f"Sources: {len(pairs)}, IR: {len(pairs)-len(failures)}, failures: {len(failures)}")
    if drift:
        print("FAIL: compiler or source inventory changed during capture")
    for path in failures:
        print(f"FAIL {path}: {results[path]['status']}")
    different = False
    if args.baseline:
        try:
            old = json.loads(args.baseline.read_text(encoding="utf-8"))
            if old["format"] != VERSION or old["normalization"] != manifest["normalization"]:
                raise ValueError("incompatible manifest")
            if old.get("profiles", {}) != profiles or old.get("snapshot_drift", False):
                raise ValueError("baseline profiles differ or baseline has snapshot drift")
            old_files = old["files"]
            if not isinstance(old_files, dict):
                raise ValueError("invalid files")
        except (OSError, ValueError, KeyError, TypeError) as exc:
            print(f"Invalid baseline: {exc}", file=sys.stderr)
            return 2
        for path in sorted(set(old_files) | set(results)):
            if old_files.get(path) != results.get(path):
                different = True
                print(f"DIFF {path}: {old_files.get(path)} -> {results.get(path)}")
        if old.get("compiler_sha256") != manifest["compiler_sha256"]:
            print("Compiler binary hash changed (informational)")
        if old.get("input_sha256") != input_sha256:
            print("Direct source snapshot changed or is missing (informational)")
        print("Comparison:", "DIFFERENT" if different else "IDENTICAL")
    return 1 if failures or different or drift else 0


if __name__ == "__main__":
    sys.exit(main())
