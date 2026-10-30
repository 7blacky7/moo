"""Assemble only the externally pinned manifest's training documents."""
import argparse
import hashlib
import json
import os
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest")
    parser.add_argument("mapping")
    parser.add_argument("output")
    parser.add_argument("expected_manifest_sha256")
    args = parser.parse_args()
    manifest = json.loads(Path(args.manifest).read_bytes())
    unsigned = {k: v for k, v in manifest.items() if k != "manifest_sha256"}
    canonical = json.dumps(unsigned, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":"), allow_nan=False).encode("utf-8")
    if hashlib.sha256(canonical).hexdigest() != args.expected_manifest_sha256 or manifest["manifest_sha256"] != args.expected_manifest_sha256:
        raise ValueError("manifest hash mismatch")
    mapping = json.loads(Path(args.mapping).read_bytes())
    if mapping["manifest_sha256"] != args.expected_manifest_sha256:
        raise ValueError("document mapping mismatch")
    train = mapping["documents"]["train"]
    expected = manifest["splits"]["train"]["document_ids"]
    if set(train) != set(expected) or len(expected) != len(set(expected)):
        raise ValueError("train document IDs mismatch")
    docs = {d["id"]: d for d in manifest["documents"]}
    parts = []
    for ident in expected:
        meta = docs[ident]
        raw = Path(train[ident]).read_bytes()
        if meta["split"] != "train" or hashlib.sha256(raw).hexdigest() != meta["sha256"]:
            raise ValueError("training document mismatch")
        parts.append(raw)
    data = b"".join(parts)
    if len(data.decode("utf-8")) != manifest["splits"]["train"]["codepoints"]:
        raise ValueError("training codepoint count mismatch")
    with open(args.output, "xb") as out:
        out.write(data)
        out.flush()
        os.fsync(out.fileno())
    print("train corpus sha256:", hashlib.sha256(data).hexdigest(), "codepoints:", len(data.decode("utf-8")))


if __name__ == "__main__":
    main()
