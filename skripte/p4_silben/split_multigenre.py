#!/usr/bin/env python3
"""Deterministic multi-genre P4 manifest; never prints document text.

Boundaries JSON has documents in source byte order with id, genre, group,
start, end and explicit test_eligible boolean. The caller independently
verifies boundaries, source rights, genre labels, duplicate groups and prior
exposure. Equal/related works must share a group; groups cannot span genres.
No model or tokenizer training occurs here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

MAX_CORPUS = 16 * 1024 * 1024
MAX_BOUNDARIES = 1024 * 1024
SEED = "moo-p4-multigenre-v1-2026-10-29"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--boundaries", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    expected = args.expected_sha256
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        raise ValueError("expected SHA256 must be lower-case hex")
    with args.corpus.open("rb") as f:
        raw = f.read(MAX_CORPUS + 1)
    if not raw or len(raw) > MAX_CORPUS or sha(raw) != expected or b"\0" in raw:
        raise ValueError("corpus does not match pinned SHA256/limits")
    raw.decode("utf-8", errors="strict")
    with args.boundaries.open("rb") as f:
        boundaries = f.read(MAX_BOUNDARIES + 1)
    if len(boundaries) > MAX_BOUNDARIES:
        raise ValueError("boundaries too large")
    source = json.loads(boundaries)
    docs = source.get("documents")
    if not isinstance(docs, list) or not 9 <= len(docs) <= 10000:
        raise ValueError("need 9..10000 documents")
    ordered = []
    groups = {}
    ids = set()
    cursor = 0
    for d in docs:
        if not isinstance(d, dict):
            raise ValueError("invalid document")
        ident, genre, group = d.get("id"), d.get("genre"), d.get("group")
        source_ref, revision, licence = d.get("source"), d.get("revision"), d.get("licence")
        start, end, eligible = d.get("start"), d.get("end"), d.get("test_eligible")
        if (any(not isinstance(x, str) or not x or len(x) > 256 for x in (ident, genre, group))
                or any(not isinstance(x, str) or not x or len(x) > 2048 for x in (source_ref, revision, licence))
                or ident in ids or type(start) is not int or type(end) is not int
                or type(eligible) is not bool or start != cursor or not start < end <= len(raw)):
            raise ValueError("invalid id, genre, group or byte boundary")
        old = groups.get(group)
        if old is not None and old["genre"] != genre:
            raise ValueError("a duplicate group may not cross genres")
        group_info = groups.setdefault(group, {"genre": genre, "eligible": True})
        group_info["eligible"] = group_info["eligible"] and eligible
        piece = raw[start:end]
        chars = len(piece.decode("utf-8", errors="strict"))
        ordered.append({"id": ident, "genre": genre, "group": group,
                        "source": source_ref, "revision": revision, "licence": licence,
                        "test_eligible": eligible, "start": start, "end": end,
                        "bytes": len(piece), "codepoints": chars, "sha256": sha(piece)})
        ids.add(ident)
        cursor = end
    if cursor != len(raw):
        raise ValueError("corpus has uncovered bytes")
    # Exact duplicate content must be grouped even if the caller missed it.
    hashes = {}
    for d in ordered:
        if d["sha256"] in hashes and hashes[d["sha256"]] != d["group"]:
            raise ValueError("identical texts assigned to different groups")
        hashes[d["sha256"]] = d["group"]
    mapping = {}
    genres = sorted({item["genre"] for item in groups.values()})
    if len(genres) < 2:
        raise ValueError("at least two distinct genres required")
    for genre in genres:
        names = [name for name, g in groups.items() if g["genre"] == genre]
        names.sort(key=lambda x: (sha((SEED + "\0" + genre + "\0" + x).encode()), x))
        if len(names) < 3:
            raise ValueError("each genre requires at least three independent groups")
        count = max(1, len(names) // 10)
        eligible = [n for n in names if groups[n]["eligible"]]
        if len(eligible) < count:
            raise ValueError("not enough test-eligible groups in genre")
        test = set(eligible[-count:])
        remaining = [n for n in names if n not in test]
        if len(remaining) <= count:
            raise ValueError("not enough train/validation groups")
        val = set(remaining[-count:])
        for name in names:
            mapping[name] = "test" if name in test else "val" if name in val else "train"
    sections = {}
    for d in ordered:
        d["split"] = mapping[d["group"]]
    for split in ("train", "val", "test"):
        selected = [d for d in ordered if d["split"] == split]
        blob = b"".join(raw[d["start"]:d["end"]] for d in selected)
        sections[split] = {"document_ids": [d["id"] for d in selected],
                           "genres": sorted({d["genre"] for d in selected}),
                           "bytes": len(blob), "codepoints": sum(d["codepoints"] for d in selected),
                           "sha256": sha(blob)}
    result = {"format": "moo-p4-multigenre-v1", "source_sha256": expected,
              "boundaries_sha256": sha(boundaries), "seed": SEED,
              "selection": "per genre SHA256(seed NUL genre NUL group); >=1 val/test group, ~10% each",
              "concatenation": "source order; no inserted bytes",
              "documents": ordered, "splits": sections,
              "limitations": ["rights and source revisions require review",
                              "document boundaries and near-duplicate groups require independent review"]}
    canonical = json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    result["manifest_sha256"] = sha(canonical)
    output = (json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode()
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(output)
            f.flush()
            os.fsync(f.fileno())
    except BaseException:
        try:
            os.unlink(args.output)
        except OSError:
            pass
        raise
    print("Manifest created:", result["manifest_sha256"])


if __name__ == "__main__":
    main()
