"""Materialize sealed P4 document files without printing their contents."""
import argparse
import hashlib
import json
import os
from pathlib import Path


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def materialize(manifest, source, destination, expected_manifest_sha256):
    if not isinstance(manifest, dict) or not isinstance(source, bytes):
        raise ValueError("manifest and corpus bytes required")
    unsigned = {k: v for k, v in manifest.items() if k != "manifest_sha256"}
    canonical = json.dumps(unsigned, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":"), allow_nan=False).encode("utf-8")
    if (not isinstance(expected_manifest_sha256, str) or len(expected_manifest_sha256) != 64
            or sha(canonical) != expected_manifest_sha256
            or sha(canonical) != manifest.get("manifest_sha256") or sha(source) != manifest.get("source_sha256")):
        raise ValueError("manifest or source hash mismatch")
    docs = manifest.get("documents")
    if not isinstance(docs, list) or not docs:
        raise ValueError("missing documents")
    seen, cursor, pieces, split_ids = set(), 0, [], {"train": [], "val": [], "test": []}
    for doc in docs:
        ident, split = doc.get("id"), doc.get("split")
        start, end = doc.get("start"), doc.get("end")
        if (not isinstance(ident, str) or not ident or ident in seen or split not in split_ids
                or type(start) is not int or type(end) is not int
                or start != cursor or not start < end <= len(source)):
            raise ValueError("invalid document boundaries or ID")
        raw = source[start:end]
        if (b"\0" in raw or len(raw) != doc.get("bytes") or sha(raw) != doc.get("sha256")
                or len(raw.decode("utf-8", errors="strict")) != doc.get("codepoints")):
            raise ValueError("document hash or size mismatch")
        seen.add(ident)
        split_ids[split].append(ident)
        pieces.append((ident, split, raw))
        cursor = end
    if cursor != len(source):
        raise ValueError("document coverage mismatch")
    for split, ids in split_ids.items():
        meta = manifest.get("splits", {}).get(split)
        if not isinstance(meta, dict) or meta.get("document_ids") != ids:
            raise ValueError("split document ordering mismatch")
        joined = b"".join(raw for _, part, raw in pieces if part == split)
        if (sha(joined) != meta.get("sha256") or len(joined) != meta.get("bytes")
                or len(joined.decode("utf-8")) != meta.get("codepoints")):
            raise ValueError("split hash or size mismatch")
    destination = Path(destination)
    if not destination.is_absolute() or str(destination) != str(destination.resolve(strict=False)):
        raise ValueError("absolute canonical destination required")
    destination.mkdir(mode=0o700)
    mapping = {"train": {}, "val": {}, "test": {}}
    for index, (ident, split, raw) in enumerate(pieces):
        path = destination / ("doc-" + str(index).zfill(5) + ".txt")
        with path.open("xb") as f:
            os.fchmod(f.fileno(), 0o600)
            f.write(raw)
            f.flush()
            os.fsync(f.fileno())
        mapping[split][ident] = str(path)
    manifest_path = destination / "documents.json"
    with manifest_path.open("x", encoding="utf-8") as f:
        json.dump({"manifest_sha256": manifest["manifest_sha256"], "documents": mapping},
                  f, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        f.flush()
        os.fsync(f.fileno())
    fd = os.open(destination, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)
    return manifest_path


def main():
    parser = argparse.ArgumentParser(description="Materialize sealed P4 documents")
    parser.add_argument("manifest")
    parser.add_argument("corpus")
    parser.add_argument("destination")
    parser.add_argument("expected_manifest_sha256")
    args = parser.parse_args()
    manifest = json.loads(Path(args.manifest).read_bytes())
    corpus = Path(args.corpus).read_bytes()
    result = materialize(manifest, corpus, args.destination, args.expected_manifest_sha256)
    print("document mapping:", result)


if __name__ == "__main__":
    main()
