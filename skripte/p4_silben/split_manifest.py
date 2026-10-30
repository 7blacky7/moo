#!/usr/bin/env python3
"""Build a sealed, document-level P4 split manifest without printing corpus text.

Input boundaries JSON: {"documents":[{"id":"doc-01","start":0,"end":123}, ...]}
Offsets are half-open byte offsets into the exact pinned extract. The boundary
list must be independently checked before use: this tool checks coverage, not
whether the offsets really correspond to complete stories or near-duplicates.
It does not write split text or train anything.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

EXPECTED = "3770d821afd03adb39f1a1e042b87f7f4a4fb35e3c5add6ed17811d63c941e6a"
SEED = "moo-p4-split-v1-2026-10-29"
MAX_CORPUS = 16 * 1024 * 1024


def digest(b):
    return hashlib.sha256(b).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--boundaries", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--expected-sha256", default=EXPECTED,
                        help="vorab genehmigter Extrakt-SHA256; Standard: bestehender Grimm-Extrakt")
    args = parser.parse_args()
    expected = args.expected_sha256
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        raise ValueError("erwarteter SHA256 muss 64 Kleinbuchstaben/Hexziffern enthalten")
    if not 0 < args.corpus.stat().st_size <= MAX_CORPUS:
        raise ValueError("Korpusdatei zu groß oder leer")
    with args.corpus.open("rb") as f:
        raw = f.read(MAX_CORPUS + 1)
    if not 0 < len(raw) <= MAX_CORPUS or digest(raw) != expected:
        raise ValueError("Korpusgröße oder SHA256 stimmt nicht mit dem gepinnten Extrakt überein")
    if b"\0" in raw:
        raise ValueError("NUL im Korpus")
    raw.decode("utf-8", errors="strict")
    with args.boundaries.open("rb") as f:
        bounds_bytes = f.read(65537)
    if len(bounds_bytes) > 65536:
        raise ValueError("Grenzdatei zu groß")
    bounds = json.loads(bounds_bytes)
    docs = bounds.get("documents")
    if not isinstance(docs, list) or len(docs) != 9:
        raise ValueError("Exakt neun geprüfte Dokumente erforderlich")
    ordered = []
    seen = set()
    cursor = 0
    for entry in docs:
        ident = entry.get("id")
        start, end = entry.get("start"), entry.get("end")
        if (not isinstance(ident, str) or not ident or ident in seen or
                type(start) is not int or type(end) is not int or
                start != cursor or end <= start or end > len(raw)):
            raise ValueError("Dokument-IDs oder lückenlose Byteintervalle ungültig")
        part = raw[start:end]
        chars = len(part.decode("utf-8", errors="strict"))
        ordered.append({"id": ident, "start": start, "end": end,
                        "bytes": len(part), "codepoints": chars, "sha256": digest(part)})
        seen.add(ident)
        cursor = end
    if cursor != len(raw):
        raise ValueError("Dokumente decken nicht den ganzen Korpus ab")
    # Identical documents cannot straddle split boundaries. Similar/derived
    # stories need an independent content-aware duplicate audit before use.
    if len({d["sha256"] for d in ordered}) != len(ordered):
        raise ValueError("Exakte Duplikate: Gruppierung vor Split erforderlich")
    ranking = sorted(ordered, key=lambda d: (digest((SEED + "\0" + d["id"]).encode("utf-8")), d["id"]))
    mapping = {d["id"]: ("train" if i < 7 else "val" if i == 7 else "test")
               for i, d in enumerate(ranking)}
    sections = {}
    for name in ("train", "val", "test"):
        items = [d for d in ordered if mapping[d["id"]] == name]
        concatenated = b"".join(raw[d["start"]:d["end"]] for d in items)
        sections[name] = {"document_ids": [d["id"] for d in items],
                          "bytes": len(concatenated),
                          "codepoints": sum(d["codepoints"] for d in items),
                          "sha256": digest(concatenated)}
    for d in ordered:
        d["split"] = mapping[d["id"]]
    result = {"format": "moo-p4-split-v1", "source_sha256": expected,
              "boundaries_sha256": digest(bounds_bytes), "seed": SEED,
              "selection": "sha256(UTF8(seed + NUL + id)), lexicographic; 7/1/1",
              "concatenation": "source order, no inserted bytes",
              "documents": ordered, "splits": sections,
              "limitations": ["boundaries and near-duplicates require independent review",
                              "German rights must be cleared before use"]}
    canonical = json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    result["manifest_sha256"] = digest(canonical)
    output = (json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")
    # Exclusive creation avoids overwriting an existing sealed manifest.
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
    print("Manifest erstellt; SHA256:", result["manifest_sha256"])


if __name__ == "__main__":
    main()
