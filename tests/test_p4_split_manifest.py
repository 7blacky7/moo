"""Synthetische, unabhängige Prüfungen für das dokumentweise Splitmanifest."""
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "skripte/p4_silben/split_manifest.py"
spec = importlib.util.spec_from_file_location("p4_split_manifest", SCRIPT)
splitter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(splitter)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare(tmp_path):
    chunks = [f"Dokument {i}: Übung {i*i}.\n".encode("utf-8") for i in range(9)]
    raw = b"".join(chunks)
    entries = []
    offset = 0
    for i, chunk in enumerate(chunks):
        entries.append({"id": f"doc-{i:02}", "start": offset, "end": offset + len(chunk)})
        offset += len(chunk)
    corpus = tmp_path / "corpus.txt"
    bounds = tmp_path / "bounds.json"
    corpus.write_bytes(raw)
    bounds.write_text(json.dumps({"documents": entries}), encoding="utf-8")
    return corpus, bounds, raw, chunks, entries


def invoke(monkeypatch, corpus, bounds, output, expected):
    monkeypatch.setattr(splitter, "EXPECTED", expected)
    monkeypatch.setattr(sys, "argv", ["split_manifest.py", "--corpus", str(corpus),
                                     "--boundaries", str(bounds), "--output", str(output)])
    splitter.main()


def test_manifest_deterministic_and_exact_bytes(monkeypatch, tmp_path):
    corpus, bounds, raw, chunks, entries = prepare(tmp_path)
    first, second = tmp_path / "one.json", tmp_path / "two.json"
    invoke(monkeypatch, corpus, bounds, first, sha(raw))
    invoke(monkeypatch, corpus, bounds, second, sha(raw))
    assert first.read_bytes() == second.read_bytes()
    m = json.loads(first.read_bytes())
    assert m["source_sha256"] == sha(raw)
    assert m["boundaries_sha256"] == sha(bounds.read_bytes())
    assert [d["id"] for d in m["documents"]] == [e["id"] for e in entries]
    ranking = sorted(entries, key=lambda d: (sha((splitter.SEED + "\0" + d["id"]).encode()), d["id"]))
    expected = {d["id"]: ("train" if i < 7 else "val" if i == 7 else "test")
                for i, d in enumerate(ranking)}
    for i, item in enumerate(m["documents"]):
        assert item["sha256"] == sha(chunks[i])
        assert item["bytes"] == len(chunks[i])
        assert item["codepoints"] == len(chunks[i].decode("utf-8"))
        assert item["split"] == expected[item["id"]]
    for name, count in [("train", 7), ("val", 1), ("test", 1)]:
        section = m["splits"][name]
        selected = [chunk for chunk, entry in zip(chunks, entries) if expected[entry["id"]] == name]
        assert len(section["document_ids"]) == count
        assert section["sha256"] == sha(b"".join(selected))
        assert section["bytes"] == sum(map(len, selected))
        assert section["codepoints"] == sum(len(x.decode("utf-8")) for x in selected)
    seal = m.pop("manifest_sha256")
    canonical = json.dumps(m, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    assert seal == sha(canonical)


@pytest.mark.parametrize("kind", ["wrong_hash", "duplicate_id", "gap", "overlap", "bad_end",
                                 "partial_utf8", "exact_duplicate", "nul", "bad_utf8", "wrong_count"])
def test_rejects_invalid_input_without_output(monkeypatch, tmp_path, kind):
    corpus, bounds, raw, chunks, entries = prepare(tmp_path)
    expected = sha(raw)
    if kind == "wrong_hash":
        expected = "0" * 64
    elif kind == "duplicate_id":
        entries[1]["id"] = entries[0]["id"]
    elif kind == "gap":
        entries[1]["start"] += 1
    elif kind == "overlap":
        entries[1]["start"] -= 1
    elif kind == "bad_end":
        entries[-1]["end"] -= 1
    elif kind == "partial_utf8":
        pos = chunks[0].index("Ü".encode()) + 1
        entries[0]["end"] = pos
        entries[1]["start"] = pos
    elif kind == "exact_duplicate":
        chunks[1] = chunks[0]
        raw = b"".join(chunks)
        entries[1]["end"] = len(chunks[0]) + len(chunks[1])
        for i in range(2, len(entries)):
            entries[i]["start"] = entries[i-1]["end"]
            entries[i]["end"] = entries[i]["start"] + len(chunks[i])
        corpus.write_bytes(raw)
        expected = sha(raw)
    elif kind in ("nul", "bad_utf8"):
        raw = (b"\0" if kind == "nul" else b"\xff") + raw[1:]
        corpus.write_bytes(raw)
        expected = sha(raw)
    elif kind == "wrong_count":
        entries.pop()
    bounds.write_text(json.dumps({"documents": entries}), encoding="utf-8")
    output = tmp_path / "must-not-exist.json"
    with pytest.raises((ValueError, UnicodeDecodeError)):
        invoke(monkeypatch, corpus, bounds, output, expected)
    assert not output.exists()


def test_existing_manifest_never_overwritten(monkeypatch, tmp_path):
    corpus, bounds, raw, _, _ = prepare(tmp_path)
    output = tmp_path / "sealed.json"
    output.write_bytes(b"original")
    with pytest.raises(FileExistsError):
        invoke(monkeypatch, corpus, bounds, output, sha(raw))
    assert output.read_bytes() == b"original"



def multigenre_fixture(tmp_path):
    chunks = [f"Synthetisches Dokument {i}.\n".encode() for i in range(12)]
    docs, offset = [], 0
    for i, chunk in enumerate(chunks):
        docs.append({"id": str(i), "genre": "A" if i < 6 else "B",
                     "group": f"g{i}", "source": "synthetisch",
                     "revision": "1", "licence": "test", "start": offset,
                     "end": offset + len(chunk), "test_eligible": i not in (0, 6)})
        offset += len(chunk)
    corpus, bounds = tmp_path / "multi.txt", tmp_path / "multi.json"
    corpus.write_bytes(b"".join(chunks))
    bounds.write_text(json.dumps({"documents": docs}), encoding="utf-8")
    return corpus, bounds, docs


def invoke_multigenre(monkeypatch, corpus, bounds, out):
    path = SCRIPT.with_name("split_multigenre.py")
    spec = importlib.util.spec_from_file_location("p4_multi_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(sys, "argv", [str(path), "--corpus", str(corpus),
        "--boundaries", str(bounds), "--expected-sha256", sha(corpus.read_bytes()),
        "--output", str(out)])
    module.main()


def test_multigenre_seals_groups_and_excludes_exposed(monkeypatch, tmp_path):
    corpus, bounds, docs = multigenre_fixture(tmp_path)
    docs[1]["group"] = docs[0]["group"]
    bounds.write_text(json.dumps({"documents": docs}), encoding="utf-8")
    out, out2 = tmp_path / "m1.json", tmp_path / "m2.json"
    invoke_multigenre(monkeypatch, corpus, bounds, out)
    invoke_multigenre(monkeypatch, corpus, bounds, out2)
    assert out.read_bytes() == out2.read_bytes()
    result = json.loads(out.read_bytes())
    for split in ("train", "val", "test"):
        assert set(result["splits"][split]["genres"]) == {"A", "B"}
    mapped = {d["id"]: d for d in result["documents"]}
    assert mapped["0"]["split"] == mapped["1"]["split"] != "test"
    assert mapped["6"]["split"] != "test"
    assert all(d["source"] == "synthetisch" and d["revision"] == "1"
               and d["licence"] == "test" for d in result["documents"])


@pytest.mark.parametrize("field", ["source", "revision", "licence"])
def test_multigenre_rejects_missing_provenance(monkeypatch, tmp_path, field):
    corpus, bounds, docs = multigenre_fixture(tmp_path)
    del docs[0][field]
    bounds.write_text(json.dumps({"documents": docs}), encoding="utf-8")
    out = tmp_path / "manifest.json"
    with pytest.raises(ValueError):
        invoke_multigenre(monkeypatch, corpus, bounds, out)
    assert not out.exists()


def test_multigenre_rejects_group_crossing_genres(monkeypatch, tmp_path):
    corpus, bounds, docs = multigenre_fixture(tmp_path)
    docs[6]["group"] = docs[0]["group"]
    bounds.write_text(json.dumps({"documents": docs}), encoding="utf-8")
    out = tmp_path / "manifest.json"
    with pytest.raises(ValueError):
        invoke_multigenre(monkeypatch, corpus, bounds, out)
    assert not out.exists()
