"""Canonical file manifests for the trusted-local SIL-11 gate."""
import hashlib
import json
import os
from pathlib import Path


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def build(paths):
    paths = list(paths)
    if not paths or len(paths) != len(set(paths)):
        raise ValueError("manifest paths must be nonempty and unique")
    entries = {}
    for name in sorted(paths):
        path = Path(name)
        if not isinstance(name, str) or not path.is_absolute() or str(path) != name:
            raise ValueError("canonical absolute paths required")
        if not path.is_file() or path.is_symlink():
            raise ValueError("regular non-symlink file required: " + name)
        entries[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return {"format": "moo-p4-file-manifest-v1", "files": entries}


def verify(manifest, expected_paths):
    if not isinstance(manifest, dict) or set(manifest) != {"format", "files"}:
        raise ValueError("invalid manifest")
    if manifest["format"] != "moo-p4-file-manifest-v1" or not isinstance(manifest["files"], dict):
        raise ValueError("invalid manifest format")
    current = build(expected_paths)
    if current != manifest:
        raise ValueError("manifest coverage or file hash mismatch")
    return hashlib.sha256(canonical(manifest)).hexdigest()


def write_new(path, manifest):
    raw = canonical(manifest)
    with open(path, "xb") as output:
        output.write(raw)
        output.flush()
        os.fsync(output.fileno())
    return hashlib.sha256(raw).hexdigest()
