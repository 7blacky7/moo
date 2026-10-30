"""Nur synthetische, unabhängige Orakel für den P4-Window-Evaluator."""
import importlib.util
import math
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "skripte/p4_silben/evaluator.py"
spec = importlib.util.spec_from_file_location("p4_eval", SCRIPT)
ev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ev)


def test_window_boundaries_and_no_duplicate_targets():
    assert list(ev.windows(7, 4, 2)) == [(0, 0, 2), (0, 2, 4), (2, 4, 6), (3, 6, 7)]
    assert list(ev.windows(3, 4, 3)) == [(0, 0, 3)]
    assert list(ev.windows(1, 2, 1)) == [(0, 0, 1)]


@pytest.mark.parametrize("count,context,stride", [(0, 3, 1), (3, 0, 1), (3, 2, 0),
                                                   (3, 2, 3), (3, 3, 3), (1, 1, 1), (3, 2.0, 1), (True, 2, 1)])
def test_bad_window_arguments(count, context, stride):
    with pytest.raises(ValueError):
        list(ev.windows(count, context, stride))


@pytest.mark.parametrize("context,stride", [(2, 1), (3, 1), (4, 2), (9, 3)])
def test_uniform_distribution_exact_nll_and_coverage(context, stride):
    raw = "Aä🙂".encode("utf-8")
    ids = [0, 1, 2, 3, 0, 1, 2]  # byte fallback: one target per original byte
    offsets = list(range(len(raw) + 1))
    seen = []
    def score(prefix, pos):
        start = (pos // stride) * stride
        end = min(start + stride, len(ids))
        first = max(0, end - context)
        assert prefix == tuple(ids[first:pos])
        assert len(prefix) < context
        seen.append(pos)
        return [0.0, 0.0, 0.0, 0.0]
    result = ev.evaluate_document(raw, ids, offsets, score, context, stride)
    nll = len(ids) * math.log(4)
    assert seen == list(range(len(ids)))
    assert result["tokens"] == len(ids)
    assert result["bytes"] == len(raw) == 7
    assert result["codepoints"] == 3
    assert result["nll"] == pytest.approx(nll)
    assert result["nll_per_codepoint"] == pytest.approx(nll / 3)
    assert result["bits_per_byte"] == pytest.approx(2.0)


def test_nonuniform_logits_and_stable_large_values():
    def score(prefix, pos):
        rows = [[1000.0, 1001.0], [1002.0, 1000.0], [1000.0, 1000.0]]
        return rows[pos]
    result = ev.evaluate_document(b"abc", [1, 0, 1], [0, 1, 2, 3], score, 3, 2)
    expected = math.log1p(math.exp(-1)) + math.log1p(math.exp(-2)) + math.log(2)
    assert result["nll"] == pytest.approx(expected)
    assert result["bits_per_byte"] == pytest.approx(expected / (3 * math.log(2)))


@pytest.mark.parametrize("raw,ids,offsets", [
    (b"", [0], [0, 0]), (b"a\0", [0], [0, 2]), (b"\xff", [0], [0, 1]),
    (b"abc", [], [0]), (b"abc", [0, 1], [0, 1]), (b"abc", [0], [1, 3]),
    (b"abc", [0], [0, 2]), (b"abc", [0, 1], [0, 0, 3]),
    (b"abc", [-1], [0, 3]), (b"abc", [True], [0, 3]),
    (b"abc", [0, 1], [0, 1.0, 3]),
])
def test_invalid_document_metadata(raw, ids, offsets):
    with pytest.raises((ValueError, UnicodeDecodeError)):
        ev.evaluate_document(raw, ids, offsets, lambda *_: [[0.0]], 2, 1)


@pytest.mark.parametrize("rows", [[], [[0.0, float("nan")]], [[float("inf")]], [[0.0, True]], [[]]])
def test_invalid_scorer_rows(rows):
    with pytest.raises(ValueError):
        ev.evaluate_document(b"a", [0], [0, 1], lambda *_: (rows[0] if rows else []), 2, 1)


def test_target_outside_vocabulary():
    with pytest.raises(ValueError):
        ev.evaluate_document(b"x", [2], [0, 1], lambda *_: [0.0, 0.0], 2, 1)


def test_bound_document_checks_all_identities_and_exact_token_bytes():
    raw, tok, ckpt = "Aä".encode(), b"tokenizer-v1", b"checkpoint-v1"
    ids = [0, 1, 2]
    offsets = [0, 1, 2, 3]
    mapping = {0: b"A", 1: bytes([0xc3]), 2: bytes([0xa4])}
    calls = []
    def scorer(prefix, pos):
        calls.append(pos)
        return [0.0, 0.0, 0.0]
    def token_bytes(blob, token):
        assert blob == tok
        return mapping[token]
    import hashlib
    sha = lambda b: hashlib.sha256(b).hexdigest()
    scorer.expected = ("0" * 64, sha(tok), sha(ckpt))
    args = dict(token_bytes=token_bytes, tokenizer_blob=tok, checkpoint_blob=ckpt,
                expected_document_sha256=sha(raw), expected_tokenizer_sha256=sha(tok),
                expected_checkpoint_sha256=sha(ckpt))
    result = ev.evaluate_bound_document(raw, ids, offsets, scorer, 2, 1, **args)
    assert calls == [0, 1, 2]
    assert result["document_sha256"] == sha(raw)
    assert result["tokenizer_sha256"] == sha(tok)
    assert result["checkpoint_sha256"] == sha(ckpt)
    assert result["nll"] == pytest.approx(3 * math.log(3))
    for field in ("expected_document_sha256", "expected_tokenizer_sha256",
                  "expected_checkpoint_sha256"):
        bad = dict(args, **{field: "0" * 64})
        calls.clear()
        with pytest.raises(ValueError, match="hash mismatch"):
            ev.evaluate_bound_document(raw, ids, offsets, scorer, 2, 1, **bad)
        assert calls == []
    calls.clear()
    scorer.expected = ("0" * 64, sha(b"different valid tokenizer"), sha(ckpt))
    with pytest.raises(ValueError, match="scorer identity"):
        ev.evaluate_bound_document(raw, ids, offsets, scorer, 2, 1, **args)
    assert calls == []
    scorer.expected = ("0" * 64, sha(tok), sha(b"different valid checkpoint"))
    with pytest.raises(ValueError, match="scorer identity"):
        ev.evaluate_bound_document(raw, ids, offsets, scorer, 2, 1, **args)
    assert calls == []
    scorer.expected = ("0" * 64, sha(tok), sha(ckpt))
    bad = dict(args, token_bytes=lambda _blob, _token: b"wrong")
    with pytest.raises(ValueError, match="token bytes differ"):
        ev.evaluate_bound_document(raw, ids, offsets, scorer, 2, 1, **bad)
    assert calls == []
    calls.clear()
    with pytest.raises(ValueError):
        ev.evaluate_bound_document(raw, ids, [0, 2, 1, 3], scorer, 2, 1, **args)
    assert calls == []


def test_target_and_future_ids_are_not_visible_to_scorer():
    seen = []
    def scorer(prefix, pos):
        seen.append((pos, prefix))
        return [0.0, 0.0, 0.0]
    ev.evaluate_document(b"abcd", [0, 1, 2, 0], [0, 1, 2, 3, 4], scorer, 3, 2)
    original = list(seen)
    seen.clear()
    ev.evaluate_document(b"abcd", [0, 2, 1, 1], [0, 1, 2, 3, 4], scorer, 3, 2)
    assert original[1] == seen[1] == (1, (0,))
    assert original[0] == seen[0] == (0, ())


SCORER_PATH = SCRIPT.with_name("moo_scorer.py")
scorer_spec = importlib.util.spec_from_file_location("p4_scorer", SCORER_PATH)
scorer_module = importlib.util.module_from_spec(scorer_spec)
scorer_spec.loader.exec_module(scorer_module)


def test_scorer_hashes_prefix_and_postcall_mutation(monkeypatch, tmp_path):
    import hashlib
    import json
    from types import SimpleNamespace
    exe, tok, checkpoint = [tmp_path / name for name in ("scorer", "tok", "checkpoint")]
    exe.write_bytes(b"binary")
    tok.write_bytes(b"tokenizer")
    checkpoint.write_bytes(b"model")
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    kwargs = dict(executable_sha256=sha(exe), tokenizer_sha256=sha(tok),
                  checkpoint_sha256=sha(checkpoint))
    scorer = scorer_module.MooScorer(exe, "syllable", tok, checkpoint, **kwargs)
    seen = []
    def fake_run(argv, **options):
        env = options["env"]
        seen.append((argv, json.loads(env["P4_PREFIX_JSON"]), env["P4_CHECKPOINT"]))
        return SimpleNamespace(stdout=b"[1.5,-2.0]")
    monkeypatch.setattr(scorer_module.subprocess, "run", fake_run)
    assert scorer((3, 4), 2) == [1.5, -2.0]
    assert seen == [([str(exe)], [3, 4], str(checkpoint))]
    with pytest.raises(ValueError):
        scorer((1, 2), 1)
    with pytest.raises(ValueError):
        scorer((-1,), 1)
    tok.write_bytes(b"mutated")
    with pytest.raises(ValueError, match="hash mismatch"):
        scorer((), 0)
    assert len(seen) == 1
    tok.write_bytes(b"tokenizer")
    def mutate_during_call(argv, **options):
        checkpoint.write_bytes(b"mutated")
        return SimpleNamespace(stdout=b"[0.0]")
    monkeypatch.setattr(scorer_module.subprocess, "run", mutate_during_call)
    with pytest.raises(ValueError, match="hash mismatch"):
        scorer((), 0)


def test_real_untrained_moo_scorer_rejects_wrong_tokenizer(tmp_path):
    import hashlib
    import os
    import subprocess
    root = SCRIPT.parents[2]
    compiler = root / "compiler/target/release/moo-compiler"
    assert compiler.is_file(), "Build compiler first with mise run test-compiler"
    program = tmp_path / "make_checkpoint.moos"
    program.write_text("""
setze d auf umgebung("P4_FIXTURE_DIR")
setze t auf zeichen_fixed_trainiere("ÄÄ ÖÖ", 260)
setze t2 auf zeichen_fixed_trainiere("ÜÜ ßß", 260)
zeichen_fixed_speichern(t, d + "/tok")
zeichen_fixed_speichern(t2, d + "/other")
setze v auf zeichen_fixed_info(t)["vokab"]
setze alle auf [schicht_embedding(v, 8, 1), schicht_position(8, 8, "gelernt", 2),
    schicht_layernorm(8), schicht_attention(8, 2, 3),
    schicht_layernorm(8), schicht_dicht(8, 16, "gelu", 4), schicht_dicht(16, 8, "keine", 5),
    schicht_layernorm(8), schicht_attention(8, 2, 6),
    schicht_layernorm(8), schicht_dicht(8, 16, "gelu", 7), schicht_dicht(16, 8, "keine", 8),
    schicht_layernorm(8), schicht_dicht(8, v, "keine", 9)]
setze netz auf ki_netz(alle)
setze opt auf optimierer_adam(parameter(netz), 0.001)
checkpoint_speichern({"netz": netz, "opt": opt, "schritt": 0, "tokenizer_version": zeichen_fixed_hash(t),
                     "arch_version": "p4-gpt2-v1"}, d + "/ckpt")
zeige "FIXTURE OK"
""", encoding="utf-8")
    executable = tmp_path / "make_checkpoint"
    subprocess.run([str(compiler), "compile", str(program), "-o", str(executable)],
                   cwd=root, check=True, capture_output=True)
    env = dict(os.environ, P4_FIXTURE_DIR=str(tmp_path))
    result = subprocess.run([str(executable)], cwd=root, env=env, check=True, capture_output=True)
    assert b"FIXTURE OK" in result.stdout
    logits = tmp_path / "logits"
    subprocess.run([str(compiler), "compile", str(root / "beispiele/p4_silben/logits.moos"),
                    "-o", str(logits)], cwd=root, check=True, capture_output=True)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    tok, other, ckpt = (tmp_path / x for x in ("tok", "other", "ckpt"))
    kwargs = dict(executable_sha256=sha(logits), tokenizer_sha256=sha(tok),
                  checkpoint_sha256=sha(ckpt))
    scorer = scorer_module.MooScorer(logits, "char", tok, ckpt, **kwargs)
    row = scorer((), 0)
    assert len(row) >= 256 and all(math.isfinite(x) for x in row)
    assert len(scorer((65, 66), 2)) == len(row)
    bad = dict(kwargs, tokenizer_sha256=sha(other))
    wrong = scorer_module.MooScorer(logits, "char", other, ckpt, **bad)
    with pytest.raises(subprocess.CalledProcessError):
        wrong((), 0)
    monkeypatch_path = str(root)
    import sys
    sys.path.insert(0, monkeypatch_path)
    try:
        from skripte.p4_silben.checkpoint_metadata import metadata
        blob = ckpt.read_bytes()
        parsed = metadata(blob)
        assert parsed["tokenizer_version"] == sha(tok)
        assert parsed["arch_version"] == "p4-gpt2-v1" and parsed["opt"] is True
        with pytest.raises(ValueError):
            metadata(blob[:9])
    finally:
        sys.path.remove(monkeypatch_path)


def test_aggregate_uses_global_denominators():
    reports = [
        {"document_sha256": "a" * 64, "nll": 4.0, "bytes": 2, "codepoints": 1, "tokens": 2},
        {"document_sha256": "b" * 64, "nll": 3.0, "bytes": 6, "codepoints": 3, "tokens": 4},
    ]
    result = ev.aggregate_documents(reports)
    assert result["documents"] == 2
    assert (result["bytes"], result["codepoints"], result["tokens"]) == (8, 4, 6)
    assert result["nll"] == pytest.approx(7)
    assert result["nll_per_codepoint"] == pytest.approx(7 / 4)
    assert result["bits_per_byte"] == pytest.approx(7 / (8 * math.log(2)))
    assert result["nll_per_codepoint"] != pytest.approx((4 / 1 + 3 / 3) / 2)


@pytest.mark.parametrize("bad", [
    [], [{"nll": 1, "bytes": 1, "codepoints": 1, "tokens": 1}],
    [{"document_sha256": "a" * 64, "nll": 1, "bytes": 1, "codepoints": 1, "tokens": 1}] * 2,
    [{"document_sha256": "a" * 64, "nll": float("nan"), "bytes": 1, "codepoints": 1, "tokens": 1}],
    [{"document_sha256": "a" * 64, "nll": 1, "bytes": 0, "codepoints": 1, "tokens": 1}],
])
def test_aggregate_rejects_bad_reports(bad):
    with pytest.raises(ValueError):
        ev.aggregate_documents(bad)



def load_budget_module(name):
    import importlib.util
    path = Path(__file__).resolve().parents[1] / "skripte" / "p4_silben" / (name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX process groups")
def test_wall_budget_timeout_and_run_ceiling(tmp_path):
    import os
    import sys
    module = load_budget_module("wall_budget")
    budget = module.WallBudget(max_runs=1, per_run=0.15, total=2)
    result = budget.run([sys.executable, "-c", "import time; time.sleep(2)"],
                        cwd=str(tmp_path), env=os.environ.copy())
    assert result["timed_out"] is True and result["returncode"] is None
    assert result["run"] == 1
    with pytest.raises(RuntimeError, match="exhausted"):
        budget.run([sys.executable, "-c", "pass"], cwd=str(tmp_path), env=os.environ.copy())


@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX ledger")
def test_budget_ledger_concurrent_ceiling_and_crash_damage(tmp_path):
    from concurrent.futures import ThreadPoolExecutor
    module = load_budget_module("budget_ledger")
    path = tmp_path / "ledger"
    module.create(path)

    def reserve(_):
        try:
            return module.reserve(path, kind="train", seconds=600)
        except RuntimeError:
            return None

    with ThreadPoolExecutor(max_workers=12) as pool:
        results = list(pool.map(reserve, range(12)))
    assert sum(item is not None for item in results) == 9
    assert len(set(item for item in results if item is not None)) == 9
    with path.open("ab") as f:
        f.write(b'{"partial"')
    with pytest.raises(ValueError, match="partial"):
        module.reserve(path, kind="eval", seconds=1)



@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX process groups")
def test_wall_budget_late_observed_completion_is_timeout(monkeypatch, tmp_path):
    import os
    import sys
    import time
    module = load_budget_module("wall_budget")
    original = module.subprocess.Popen

    def delayed_return(*args, **kwargs):
        proc = original(*args, **kwargs)
        time.sleep(0.25)
        return proc

    monkeypatch.setattr(module.subprocess, "Popen", delayed_return)
    budget = module.WallBudget(max_runs=1, per_run=0.05, total=2)
    result = budget.run([sys.executable, "-c", "pass"], cwd=str(tmp_path),
                        env=os.environ.copy())
    assert result["timed_out"] is True
    assert result["returncode"] is None



def training_fixture():
    import hashlib
    import json
    raw, tokenizer = b"abcdefg", b"fake-tokenizer"
    digest = lambda b: hashlib.sha256(b).hexdigest()
    docs = [{"id": "d", "split": "train", "sha256": digest(raw),
             "bytes": len(raw), "codepoints": len(raw)}]
    manifest = {"documents": docs}
    manifest_hash = digest(json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                                      separators=(",", ":")).encode())
    manifest["manifest_sha256"] = manifest_hash
    args = dict(manifest=manifest, expected_manifest_sha256=manifest_hash,
                doc_id="d", raw=raw, ids=list(raw), offsets=list(range(8)),
                tokenizer_blob=tokenizer, expected_tokenizer_sha256=digest(tokenizer),
                token_bytes=lambda blob, token: bytes([token]), context=4, stride=2)
    return args


def test_train_prepare_targets_and_masks_cover_once():
    mod = load_budget_module("prepare_training")
    result = mod.prepare_windows(**training_fixture())
    windows = result["windows"]
    selected = [target for w in windows for target, mask in zip(w["target_ids"], w["loss_mask"]) if mask]
    assert selected == list(b"abcdefg")
    assert windows[0]["input_ids"][0] == 0
    for w in windows:
        assert len(w["input_ids"]) == len(w["target_ids"]) == len(w["loss_mask"])
        assert all(m in (0, 1) for m in w["loss_mask"])
        assert w["input_ids"][1:] == w["target_ids"][:-1]
        if w["context_start"]:
            assert w["input_ids"][0] == b"abcdefg"[w["context_start"] - 1]


@pytest.mark.parametrize("case", ["val", "test", "bad_manifest", "bad_tokenizer",
                                  "bad_bytes", "bad_offsets", "bad_token"])
def test_train_prepare_rejects_unbound_data(case):
    import hashlib
    import json
    mod = load_budget_module("prepare_training")
    args = training_fixture()
    if case in ("val", "test"):
        args["manifest"]["documents"][0]["split"] = case
        unsigned = {k: v for k, v in args["manifest"].items() if k != "manifest_sha256"}
        digest = hashlib.sha256(json.dumps(unsigned, ensure_ascii=False, sort_keys=True,
                                separators=(",", ":")).encode()).hexdigest()
        args["manifest"]["manifest_sha256"] = args["expected_manifest_sha256"] = digest
    elif case == "bad_manifest":
        args["expected_manifest_sha256"] = "0" * 64
    elif case == "bad_tokenizer":
        args["expected_tokenizer_sha256"] = "0" * 64
    elif case == "bad_bytes":
        args["raw"] = b"abcdefh"
    elif case == "bad_offsets":
        args["offsets"] = [0, 1, 2, 2, 4, 5, 6, 7]
    else:
        args["ids"][0] = ord("z")
    with pytest.raises(ValueError):
        mod.prepare_windows(**args)



@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX publication")
def test_checkpoint_publication_refuses_bad_hash_and_existing_target(tmp_path):
    import hashlib
    mod = load_budget_module("publish_checkpoint")
    source, target = tmp_path / "staged", tmp_path / "output"
    source.write_bytes(b"synthetic checkpoint")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    with pytest.raises(ValueError, match="hash mismatch"):
        mod.publish_checkpoint(source, target, "0" * 64)
    assert not target.exists()
    target.write_bytes(b"keep existing")
    with pytest.raises(FileExistsError):
        mod.publish_checkpoint(source, target, digest)
    assert target.read_bytes() == b"keep existing"
    target.unlink()
    assert mod.publish_checkpoint(source, target, digest) == digest
    assert target.read_bytes() == source.read_bytes()


@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX publication")
def test_checkpoint_publication_rejects_symlink_and_directory(tmp_path):
    import hashlib
    mod = load_budget_module("publish_checkpoint")
    source, target = tmp_path / "source", tmp_path / "output"
    source.write_bytes(b"checkpoint")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    link = tmp_path / "symlink"
    link.symlink_to(source)
    with pytest.raises(OSError):
        mod.publish_checkpoint(link, target, digest)
    assert not target.exists()
    with pytest.raises(ValueError, match="regular file"):
        mod.publish_checkpoint(tmp_path, target, digest)
    assert not target.exists()



def train_set_fixture():
    import hashlib
    import json
    sha = lambda data: hashlib.sha256(data).hexdigest()
    blobs = {"a": b"alpha", "b": b"beta"}
    docs = [{"id": k, "split": "train", "sha256": sha(v),
             "bytes": len(v), "codepoints": len(v)} for k, v in blobs.items()]
    docs.append({"id": "v", "split": "val", "sha256": sha(b"val"),
                 "bytes": 3, "codepoints": 3})
    docs.append({"id": "t", "split": "test", "sha256": sha(b"test"),
                 "bytes": 4, "codepoints": 4})
    manifest = {"documents": docs, "splits": {"train": {
        "document_ids": ["a", "b"], "bytes": 9, "codepoints": 9,
        "sha256": sha(b"alphabeta")}}}
    digest = sha(json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                            separators=(",", ":")).encode())
    manifest["manifest_sha256"] = digest
    return dict(manifest=manifest, expected_manifest_sha256=digest,
        train_documents=blobs, tokenizer_blob=b"fake", expected_tokenizer_sha256=sha(b"fake"),
        encode=lambda tok, raw: (list(raw), list(range(len(raw) + 1))),
        token_bytes=lambda tok, ident: bytes([ident]), context=4, stride=2)


def test_train_set_checks_multiple_documents():
    mod = load_budget_module("prepare_training")
    result = mod.prepare_train_set(**train_set_fixture())
    assert [d["document_id"] for d in result["documents"]] == ["a", "b"]
    assert result["original_bytes"] == result["original_codepoints"] == 9
    assert all(d["windows"][0]["input_ids"][0] == 0 for d in result["documents"])


@pytest.mark.parametrize("case", ["missing", "val", "test", "split_hash", "split_count"])
def test_train_set_rejects_leaks_and_split_mismatch(case):
    import hashlib
    import json
    mod = load_budget_module("prepare_training")
    args = train_set_fixture()
    if case == "missing":
        del args["train_documents"]["b"]
    elif case in ("val", "test"):
        args["train_documents"][case[0]] = case.encode()
    else:
        split = args["manifest"]["splits"]["train"]
        split["sha256" if case == "split_hash" else "bytes"] = "0" * 64 if case == "split_hash" else 8
        unsigned = {k: v for k, v in args["manifest"].items() if k != "manifest_sha256"}
        digest = hashlib.sha256(json.dumps(unsigned, ensure_ascii=False, sort_keys=True,
                                separators=(",", ":")).encode()).hexdigest()
        args["manifest"]["manifest_sha256"] = args["expected_manifest_sha256"] = digest
    with pytest.raises(ValueError):
        mod.prepare_train_set(**args)



def run_fixture():
    import hashlib
    import json
    args = train_set_fixture()
    manifest = args["manifest"]
    manifest.pop("manifest_sha256")
    manifest["source_sha256"] = "a" * 64
    digest = hashlib.sha256(json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                                      separators=(",", ":")).encode()).hexdigest()
    manifest["manifest_sha256"] = digest
    checkpoint = b"synthetic checkpoint"
    token_hash = args["expected_tokenizer_sha256"]
    checkpoint_hash = hashlib.sha256(checkpoint).hexdigest()
    config = {"device_mode": "vulkan_forced_strict", "protocol_version": "moo-p4-measure-v1", "manifest_sha256": digest,
              "protocol_sha256": "b" * 64, "corpus_sha256": "a" * 64,
              "variants": ["char", "bpe", "syllable"], "seeds": [1, 2, 3],
              "gpu_seconds_per_run": 600, "gpu_seconds_total": 7200,
              "split": "train", "context": 4, "stride": 2,
              "tokenizer_sha256": {"char": token_hash},
              "checkpoint_sha256": {"char:1": checkpoint_hash}}
    return dict(config=config, manifest=manifest, train_documents=args["train_documents"],
        tokenizer_blob=args["tokenizer_blob"], checkpoint_blob=checkpoint,
        variant="char", seed=1, encode=args["encode"])


class FakeTokenTable:
    tokens = tuple(range(256))

    def __init__(self, variant, blob, digest):
        pass

    def __call__(self, blob, ident):
        return bytes([ident])


class FakeRunEncoder:
    kind = "char"
    executable_sha256 = "e" * 64

    def __init__(self, tokenizer_sha256):
        self.tokenizer_sha256 = tokenizer_sha256

    def __call__(self, tokenizer, raw):
        return list(raw), list(range(len(raw) + 1))


def bind_fake_encoder(monkeypatch, module, args):
    encoder = FakeRunEncoder(args["config"]["tokenizer_sha256"]["char"])
    args["encode"] = encoder
    args["config"]["encoder_sha256"] = {"char": encoder.executable_sha256}
    monkeypatch.setattr(module, "MooEncoder", FakeRunEncoder)


def test_prepare_run_deterministic(monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import prepare_run as module
    a = run_fixture()
    bind_fake_encoder(monkeypatch, module, a)
    monkeypatch.setattr(module, "TokenTable", FakeTokenTable)
    monkeypatch.setattr(module, "checkpoint_metadata", lambda blob: {"tokenizer_version": a["config"]["tokenizer_sha256"]["char"], "arch_version": "p4-gpt2-v1", "opt": True, "vocab": 256, "context": 8, "step": 0, "global_step": 0})
    assert module.prepare_run(**a) == module.prepare_run(**a)


@pytest.mark.parametrize("case", ["val", "test", "corpus", "checkpoint", "tokenizer",
                                  "seed", "variant", "checkpoint_metadata", "vocab", "context", "step", "global_step"])
def test_prepare_run_rejects_unbound_configuration(case, monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import prepare_run as module
    args = run_fixture()
    bind_fake_encoder(monkeypatch, module, args)
    monkeypatch.setattr(module, "TokenTable", FakeTokenTable)
    monkeypatch.setattr(module, "checkpoint_metadata", lambda blob: {"tokenizer_version": args["config"]["tokenizer_sha256"]["char"], "arch_version": "p4-gpt2-v1", "opt": True, "vocab": 256, "context": 8, "step": 0, "global_step": 0})
    if case in ("val", "test"):
        args["config"]["split"] = case
    elif case == "corpus":
        args["config"]["corpus_sha256"] = "c" * 64
    elif case == "checkpoint":
        args["checkpoint_blob"] = b"wrong"
    elif case == "tokenizer":
        args["tokenizer_blob"] = b"wrong"
    elif case == "seed":
        args["seed"] = 4
    elif case == "variant":
        args["variant"] = "unknown"
    elif case in ("vocab", "context", "step", "global_step"):
        meta = {"tokenizer_version": args["config"]["tokenizer_sha256"]["char"],
                "arch_version": "p4-gpt2-v1", "opt": True, "vocab": 256, "context": 8, "step": 0, "global_step": 0}
        meta[case] = 257 if case == "vocab" else 2 if case == "context" else 1
        monkeypatch.setattr(module, "checkpoint_metadata", lambda blob: meta)
    else:
        monkeypatch.setattr(module, "checkpoint_metadata", lambda blob: {"arch_version": "wrong"})
    with pytest.raises(ValueError):
        module.prepare_run(**args)



@pytest.mark.parametrize("kind", ["char", "bpe", "syllable"])
def test_native_encoder_with_synthetic_fallback_artifact(kind, monkeypatch, tmp_path):
    import hashlib
    import struct
    import subprocess
    root = Path(__file__).resolve().parents[1]
    monkeypatch.syspath_prepend(str(root))
    from skripte.p4_silben.moo_encoder import MooEncoder
    from skripte.p4_silben.token_bytes import TokenTable
    u32 = lambda *values: struct.pack("<" + "I" * len(values), *values)
    if kind == "char":
        body = b"MOOCF001" + u32(1, 0, 256)
        blob = body + hashlib.sha256(body).digest()
    elif kind == "syllable":
        pattern = (root / "compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat").read_bytes()
        body = b"MOOSIL01" + u32(1, len(pattern), 0, 0) + pattern
        blob = body + hashlib.sha256(body).digest()
    else:
        blob = b"MOOBPE01" + u32(1, 0, 256, 0, 0, 0)
        blob += b"".join(u32(1) + bytes([i]) for i in range(256))
    artifact = tmp_path / (kind + ".tok")
    artifact.write_bytes(blob)
    executable = tmp_path / "encoder"
    compiler = root / "compiler/target/release/moo-compiler"
    subprocess.run([str(compiler), "compile", str(root / "beispiele/p4_silben/encode.moos"),
                    "-o", str(executable)], cwd=root, check=True, capture_output=True)
    sha = lambda data: hashlib.sha256(data).hexdigest()
    table = TokenTable(kind, blob, sha(blob))
    assert table(blob, 65) == b"A"
    encoder = MooEncoder(executable, kind, artifact,
                         executable_sha256=sha(executable.read_bytes()), tokenizer_sha256=sha(blob))
    raw = "Wald Ä".encode()
    ids, offsets = encoder(blob, raw)
    assert b"".join(table(blob, i) for i in ids) == raw
    assert offsets[0] == 0 and offsets[-1] == len(raw)
    with pytest.raises(ValueError):
        MooEncoder(executable, kind, artifact,
                   executable_sha256="0" * 64, tokenizer_sha256=sha(blob))
    with pytest.raises(ValueError):
        TokenTable(kind, blob[:-1], sha(blob[:-1]))



@pytest.mark.parametrize("field,value", [
    ("schritt", None), ("global_schritt", None), ("schritt", -1),
    ("global_schritt", 1.5), ("schritt", True), ("global_schritt", "3"),
])
def test_checkpoint_parser_rejects_bad_counters(field, value):
    import json
    mod = load_budget_module("checkpoint_metadata")
    ckpt = {"format_version": 2, "opt": {"art": "adam"}, "schritt": 0, "global_schritt": 0,
            "tokenizer_version": "a" * 64, "arch_version": "p4-gpt2-v1"}
    norm = {"typ": "layernorm", "dim": 8}
    attn = {"typ": "attention", "dim": 8, "koepfe": 2, "kv_koepfe": 2,
            "maske": "causal", "fenster": 0, "rope": 0}
    up = {"typ": "dicht", "ein": 8, "aus": 16, "aktivierung": "gelu"}
    down = {"typ": "dicht", "ein": 16, "aus": 8, "aktivierung": "keine"}
    architecture = [{"typ": "embedding", "dim": 8, "vokab": 256},
        {"typ": "position", "dim": 8, "max": 8, "art": "gelernt"},
        norm, attn, norm, up, down, norm, attn, norm, up, down, norm,
        {"typ": "dicht", "ein": 8, "aus": 256, "aktivierung": "keine"}]
    header = {"__metadata__": {"moo_arch": json.dumps(architecture),
              "moo_ckpt": json.dumps(ckpt)},
              "weight": {"dtype": "F32", "shape": [1], "data_offsets": [0, 4]}}
    def make_blob():
        header["__metadata__"]["moo_ckpt"] = json.dumps(ckpt)
        raw = json.dumps(header).encode()
        return len(raw).to_bytes(8, "little") + raw + bytes(4)
    parsed = mod.metadata(make_blob())
    assert parsed["step"] == parsed["global_step"] == 0
    architecture[0]["typ"] = "unknown"
    header["__metadata__"]["moo_arch"] = json.dumps(architecture)
    with pytest.raises(ValueError, match="layer sequence"):
        mod.metadata(make_blob())
    architecture[0]["typ"] = "embedding"
    header["__metadata__"]["moo_arch"] = json.dumps(architecture)
    if value is None:
        del ckpt[field]
    else:
        ckpt[field] = value
    with pytest.raises(ValueError, match="step counters"):
        mod.metadata(make_blob())



@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX runner")
@pytest.mark.parametrize("case", ["closed", "missing_trainer_hash", "wrong_trainer_hash", "existing_target"])
def test_runner_rejects_before_reserving_or_launching(case, monkeypatch, tmp_path):
    import hashlib
    import json
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import run_training as runner
    config = run_fixture()["config"]
    token, checkpoint, executable = [tmp_path / name for name in ("tok", "ckpt", "trainer")]
    for path in (token, checkpoint, executable):
        path.write_bytes(b"synthetic")
    destination = tmp_path / "output"
    if case == "existing_target":
        destination.write_bytes(b"do not replace")
    calls = []
    monkeypatch.setattr(runner.experiment_ledger, "reserve", lambda *a, **kw: calls.append("reserve"))
    budget = runner.WallBudget(max_runs=1, per_run=1, total=2)
    monkeypatch.setattr(budget, "run", lambda *a, **kw: calls.append("run"))
    if case != "closed":
        monkeypatch.setattr(runner.harness, "require_ready", lambda c, e: [("char", 1)])
        if case == "existing_target":
            config["train_executable_sha256"] = hashlib.sha256(b"synthetic").hexdigest()
            config["train_codepoint_budget"] = 9
            monkeypatch.setattr(runner, "prepare_run", lambda *a, **kw:
                                (json.dumps({"original_codepoints": 9}).encode(), "p"))
            monkeypatch.setattr(runner, "metadata", lambda blob: {})
        elif case == "wrong_trainer_hash":
            config["train_executable_sha256"] = "0" * 64
    with pytest.raises((RuntimeError, ValueError, FileExistsError)):
        runner.execute_run(config, b"", {}, {}, variant="char", seed=1, encoder=None,
            tokenizer_path=token, checkpoint_path=checkpoint, train_executable=executable,
            destination=destination, ledger_path=tmp_path / "ledger", wall_budget=budget)
    assert calls == []
    if case == "existing_target":
        assert destination.read_bytes() == b"do not replace"



def test_init_untrained_checkpoint_and_seed_guards(monkeypatch, tmp_path):
    import hashlib
    import os
    import struct
    import subprocess
    root = Path(__file__).resolve().parents[1]
    monkeypatch.syspath_prepend(str(root))
    from skripte.p4_silben.checkpoint_metadata import metadata
    body = b"MOOCF001" + struct.pack("<III", 1, 0, 256)
    blob = body + hashlib.sha256(body).digest()
    tok = tmp_path / "tokenizer"
    tok.write_bytes(blob)
    executable = tmp_path / "init"
    subprocess.run([str(root / "compiler/target/release/moo-compiler"), "compile",
                    str(root / "beispiele/p4_silben/init_model.moos"), "-o", str(executable)],
                   cwd=root, check=True, capture_output=True)
    base = dict(os.environ, P4_TOKENIZER_KIND="char", P4_TOKENIZER_PATH=str(tok),
                P4_SEED="4", P4_DIM="8", P4_HEADS="2", P4_CONTEXT="8", P4_HIDDEN="16")
    target = tmp_path / "initial.mook"
    result = subprocess.run([str(executable)], env=dict(base, P4_CHECKPOINT_OUT=str(target)),
                            cwd=root, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    parsed = metadata(target.read_bytes())
    assert parsed["tokenizer_version"] == hashlib.sha256(blob).hexdigest()
    assert parsed["step"] == parsed["global_step"] == 0
    assert parsed["vocab"] == 256 and parsed["context"] == 8 and parsed["opt"]
    before = target.read_bytes()
    collision = subprocess.run([str(executable)], env=dict(base, P4_CHECKPOINT_OUT=str(target)),
                               cwd=root, capture_output=True, timeout=30)
    assert collision.returncode != 0 and target.read_bytes() == before
    for key, value in (("P4_SEED", "-1"), ("P4_SEED", "1.5"),
                       ("P4_HEADS", "3"), ("P4_CONTEXT", "1")):
        out = tmp_path / ("bad_" + key + value)
        result = subprocess.run([str(executable)], env=dict(base, **{key: value}, P4_CHECKPOINT_OUT=str(out)),
                                cwd=root, capture_output=True, timeout=30)
        assert result.returncode != 0 and not out.exists()

    import json
    import sys
    encoder, scorer = tmp_path / "encoder", tmp_path / "scorer"
    compiler = root / "compiler/target/release/moo-compiler"
    for src, dest in (("encode.moos", encoder), ("logits.moos", scorer)):
        subprocess.run([str(compiler), "compile", str(root / "beispiele/p4_silben" / src),
                        "-o", str(dest)], cwd=root, check=True, capture_output=True)
    doc = tmp_path / "doc"
    raw = b"AB" * 20
    doc.write_bytes(raw)
    sha = lambda data: hashlib.sha256(data).hexdigest()
    manifest = {"documents": [{"id": "v", "split": "val", "sha256": sha(raw),
                 "bytes": len(raw), "codepoints": len(raw)}],
                "splits": {"val": {"document_ids": ["v"], "sha256": sha(raw),
                                    "bytes": len(raw), "codepoints": len(raw)}}}
    manifest_hash = sha(json.dumps(manifest, sort_keys=True, ensure_ascii=False,
                                    separators=(",", ":")).encode())
    manifest["manifest_sha256"] = manifest_hash
    spec = {"manifest": manifest, "split": "val", "documents": {"v": str(doc)},
            "tokenizers": {"char": {"path": str(tok), "sha256": sha(blob)}},
            "encoder_path": str(encoder), "encoder_sha256": sha(encoder.read_bytes()),
            "scorer_path": str(scorer), "scorer_sha256": sha(scorer.read_bytes()),
            "context": 8, "stride": 7}
    eval_input, report = tmp_path / "eval.json", tmp_path / "report.json"
    eval_input.write_text(json.dumps(spec), encoding="utf-8")
    env = dict(os.environ, PYTHONPATH=str(root), P4_EVAL_INPUT=str(eval_input),
               P4_VARIANT="char", P4_SEED="4", P4_MANIFEST_SHA256=manifest_hash,
               P4_CHECKPOINT=str(target), P4_CHECKPOINT_SHA256=sha(target.read_bytes()),
               P4_EVAL_OUTPUT=str(report))
    import time
    started = time.monotonic()
    result = subprocess.run([sys.executable, str(root / "skripte/p4_silben/eval_worker.py")],
                            cwd=tmp_path, env=env, capture_output=True, timeout=30)
    elapsed = time.monotonic() - started
    print(f"P4_EVAL_TIMING bytes={len(raw)} elapsed_seconds={elapsed:.6f}", flush=True)
    assert result.returncode == 0, result.stderr
    parsed_report = json.loads(report.read_bytes())
    assert parsed_report["bytes"] == len(raw) and parsed_report["codepoints"] == len(raw)
    assert parsed_report["checkpoint_sha256"] == sha(target.read_bytes())
    assert math.isfinite(parsed_report["nll"])
    from skripte.p4_silben.moo_scorer import MooScorer, MooBatchScorer
    batch_exe = tmp_path / "batch_scorer"
    subprocess.run([str(compiler), "compile", str(root / "beispiele/p4_silben/logits_batch.moos"),
                    "-o", str(batch_exe)], cwd=root, check=True, capture_output=True)
    hashes = dict(tokenizer_sha256=sha(blob), checkpoint_sha256=sha(target.read_bytes()))
    one = MooScorer(scorer, "char", tok, target, executable_sha256=sha(scorer.read_bytes()), **hashes)
    batched = MooBatchScorer(batch_exe, "char", tok, target,
                             executable_sha256=sha(batch_exe.read_bytes()), **hashes)
    requests = [((), 0), ((65,), 1), ((65, 66, 65), 3)]
    batch_rows = batched.score_batch(requests)
    for request, row in zip(requests, batch_rows):
        single_row = one(*request)
        assert len(row) == len(single_row)
        assert all(abs(x - y) <= 1e-5 for x, y in zip(row, single_row))
    spec.update(scorer_mode="batch", scorer_path=str(batch_exe),
                scorer_sha256=sha(batch_exe.read_bytes()))
    eval_input.write_text(json.dumps(spec), encoding="utf-8")
    batch_report = tmp_path / "batch_report.json"
    env["P4_EVAL_OUTPUT"] = str(batch_report)
    started = time.monotonic()
    result = subprocess.run([sys.executable, str(root / "skripte/p4_silben/eval_worker.py")],
                            cwd=tmp_path, env=env, capture_output=True, timeout=30)
    elapsed = time.monotonic() - started
    print(f"P4_EVAL_BATCH_TIMING bytes={len(raw)} elapsed_seconds={elapsed:.6f}", flush=True)
    assert result.returncode == 0, result.stderr
    batch_data = json.loads(batch_report.read_bytes())
    assert abs(batch_data["nll"] - parsed_report["nll"]) <= 1e-5




@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX runner")
@pytest.mark.parametrize("case", ["vocab", "context", "architecture", "step", "global_step", "tokenizer_version"])
def test_runner_rejects_changed_output_identity(case, monkeypatch, tmp_path):
    import hashlib
    import json
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import run_training as runner
    cfg = run_fixture()["config"]
    cfg["train_executable_sha256"] = hashlib.sha256(b"binary").hexdigest()
    cfg["train_codepoint_budget"] = 9
    cfg["ledger_path"] = str(tmp_path / "ledger")
    tok, initial, exe = [tmp_path / name for name in ("tok", "initial", "exe")]
    tok.write_bytes(b"tokenizer")
    initial.write_bytes(b"initial")
    exe.write_bytes(b"binary")
    token_hash = hashlib.sha256(b"tokenizer").hexdigest()
    old = {"tokenizer_version": token_hash, "arch_version": "p4-gpt2-v1",
           "vocab": 256, "context": 8, "architecture": ["original"],
           "step": 0, "global_step": 0}
    new = dict(old, step=1, global_step=1)
    new[case] = {"vocab": 257, "context": 9, "architecture": ["other"],
                 "step": 2, "global_step": 2, "tokenizer_version": "0" * 64}[case]
    monkeypatch.setattr(runner.harness, "require_ready", lambda c, e: [("char", 1)])
    monkeypatch.setattr(runner, "prepare_run", lambda *a, **kw:
                        (json.dumps({"original_codepoints": 9, "windows": [{}]}).encode(), "p"))
    monkeypatch.setattr(runner, "metadata", lambda blob: old if blob == b"initial" else new)
    monkeypatch.setattr(runner.experiment_ledger, "reserve", lambda *a, **kw: "reserved")
    published = []
    monkeypatch.setattr(runner, "publish_checkpoint", lambda *a: published.append(a))
    budget = runner.WallBudget(max_runs=1, per_run=1, total=2)
    monkeypatch.setenv("MOO_KI_GPU", "0")
    monkeypatch.setenv("MOO_KI_GPU_ERZWINGEN", "0")
    monkeypatch.setenv("MOO_KI_GPU_STRIKT", "0")
    def worker(argv, *, cwd, env):
        assert {k: env[k] for k in ("MOO_KI_GPU", "MOO_KI_GPU_ERZWINGEN", "MOO_KI_GPU_STRIKT")} == {
            "MOO_KI_GPU": "1", "MOO_KI_GPU_ERZWINGEN": "1", "MOO_KI_GPU_STRIKT": "1"}
        Path(env["P4_CHECKPOINT_OUT"]).write_bytes(b"output")
        return {"timed_out": False, "returncode": 0, "wall_seconds": 0.01}
    monkeypatch.setattr(budget, "run", worker)
    out = tmp_path / "output"
    with pytest.raises(ValueError, match="checkpoint identity"):
        runner.execute_run(cfg, b"", {}, {}, variant="char", seed=1, encoder=None,
            tokenizer_path=tok, checkpoint_path=initial, train_executable=exe,
            destination=out, ledger_path=tmp_path / "ledger", wall_budget=budget)
    assert published == [] and not out.exists()



def synthetic_gate_fixture(tmp_path, monkeypatch):
    import hashlib
    import json
    from skripte.p4_silben import experiment_manifest, harness

    monkeypatch.setattr(harness, "_ROOT", tmp_path)
    def make_file(relative, data=b"synthetic-only"):
        path = tmp_path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return str(path)
    product_files = [make_file(name) for name in harness._PRODUCT_RELATIVE]
    for name in harness._RUNTIME_MODULES:
        make_file("skripte/p4_silben/" + name)
    cfg = run_fixture()["config"]
    cfg.update(train_codepoint_budget=9)
    for key in ("manifest_path", "corpus_path", "protocol_path", "encoder_path",
                "train_executable_path", "eval_executable_path", "eval_script_path"):
        cfg[key] = make_file("artifacts/" + key)
    for key in ("train_documents", "tokenizer_paths", "initial_checkpoint_paths"):
        cfg[key] = {"synthetic": make_file("artifacts/" + key)}
    scorer = make_file("artifacts/scorer")
    eval_document = make_file("artifacts/eval_document")
    cfg["eval_input_path"] = {}
    for stage in ("val", "test"):
        spec = {"split": stage, "scorer_path": scorer, "documents": {"synthetic": eval_document}}
        cfg["eval_input_path"][stage] = make_file("artifacts/" + stage + ".json", json.dumps(spec).encode())
    cfg["product_files"] = product_files
    cfg["experiment_files"] = sorted(harness._required_experiment_paths(cfg))
    manifests = {}
    for prefix in ("product", "experiment"):
        manifest = experiment_manifest.build(cfg[prefix + "_files"])
        data = experiment_manifest.canonical(manifest)
        cfg[prefix + "_manifest_path"] = make_file("artifacts/" + prefix + "-manifest.json", data)
        cfg[prefix + "_manifest_sha256"] = hashlib.sha256(data).hexdigest()
        manifests[prefix] = manifest
    cfg["snapshot_sha256"] = cfg["product_manifest_sha256"]
    cert = {"status": "approved", "manifest_sha256": cfg["manifest_sha256"],
            "protocol_sha256": cfg["protocol_sha256"], "corpus_sha256": cfg["corpus_sha256"],
            "product_manifest_sha256": cfg["product_manifest_sha256"],
            "experiment_manifest_sha256": cfg["experiment_manifest_sha256"],
            "rights_review_sha256": "e" * 64,
            "gpu_isolation_evidence_sha256": "e" * 64,
            "budget_enforcement_evidence_sha256": "e" * 64, "test_reports": {}}
    for stage, marker in (("local", "a"), ("workspace", "b"), ("windows", "c")):
        doc = {"stage": stage, "snapshot_sha256": marker * 64, "rc": 0, "checks": 1,
               "product_manifest_sha256": cfg["product_manifest_sha256"],
               "product_files": manifests["product"]["files"]}
        data = json.dumps(doc, sort_keys=True).encode()
        path = make_file("artifacts/" + stage + "-report.json", data)
        cert["test_reports"][stage] = {"rc": 0, "checks": 1, "snapshot_sha256": marker * 64,
                                       "evidence_path": path, "evidence_sha256": hashlib.sha256(data).hexdigest()}
    def seal():
        canonical = json.dumps(cfg, sort_keys=True, ensure_ascii=False,
                               separators=(",", ":"), allow_nan=False).encode()
        cert["config_sha256"] = hashlib.sha256(canonical).hexdigest()
        data = json.dumps(cert, sort_keys=True).encode()
        monkeypatch.setattr(harness, "APPROVED_GATE_SHA256", hashlib.sha256(data).hexdigest())
        return data
    return harness, experiment_manifest, cfg, cert, seal


@pytest.mark.parametrize("case", ["ok", "pin", "mode", "product_drift", "experiment_drift",
                                  "foreign_product", "foreign_experiment", "missing_stage",
                                  "wrong_stage", "foreign_report", "missing_report_file",
                                  "product_undercoverage", "experiment_undercoverage"])
def test_gate_component_and_stage_bindings(case, tmp_path, monkeypatch):
    import hashlib
    import json
    harness, manifests, cfg, cert, seal = synthetic_gate_fixture(tmp_path, monkeypatch)
    if case == "ok":
        assert len(harness.require_ready(cfg, seal())) == 9
        return
    if case == "pin":
        evidence = seal()
        with pytest.raises(ValueError, match="hash not approved"):
            harness.require_ready(cfg, evidence + b" ")
        return
    if case == "mode":
        cfg.pop("device_mode")
        expected = "explicit forced strict Vulkan"
    elif case == "product_drift":
        Path(cfg["product_files"][0]).write_bytes(b"changed synthetic product")
        expected = "manifest coverage or file hash mismatch"
    elif case == "experiment_drift":
        Path(cfg["train_executable_path"]).write_bytes(b"changed synthetic experiment")
        expected = "manifest coverage or file hash mismatch"
    elif case == "foreign_product":
        cert["product_manifest_sha256"] = "0" * 64
        expected = "product manifest is not approved"
    elif case == "foreign_experiment":
        cert["experiment_manifest_sha256"] = "0" * 64
        expected = "experiment manifest is not approved"
    elif case == "missing_stage":
        del cert["test_reports"]["windows"]
        expected = "missing or invalid evidence for windows"
    elif case == "missing_report_file":
        Path(cert["test_reports"]["windows"]["evidence_path"]).unlink()
        with pytest.raises(FileNotFoundError):
            harness.require_ready(cfg, seal())
        return
    elif case in ("wrong_stage", "foreign_report"):
        report = cert["test_reports"]["windows"]
        path = Path(report["evidence_path"])
        doc = json.loads(path.read_bytes())
        if case == "wrong_stage":
            doc["stage"] = "local"
        else:
            doc["product_manifest_sha256"] = "0" * 64
        data = json.dumps(doc, sort_keys=True).encode()
        path.write_bytes(data)
        report["evidence_sha256"] = hashlib.sha256(data).hexdigest()
        expected = "evidence coverage mismatch for windows"
    elif case in ("product_undercoverage", "experiment_undercoverage"):
        prefix = case.split("_", 1)[0]
        cfg[prefix + "_files"] = cfg[prefix + "_files"][1:]
        data = manifests.canonical(manifests.build(cfg[prefix + "_files"]))
        Path(cfg[prefix + "_manifest_path"]).write_bytes(data)
        cfg[prefix + "_manifest_sha256"] = hashlib.sha256(data).hexdigest()
        cert[prefix + "_manifest_sha256"] = cfg[prefix + "_manifest_sha256"]
        if prefix == "product":
            cfg["snapshot_sha256"] = cfg["product_manifest_sha256"]
        expected = prefix + " manifest required coverage mismatch"
    with pytest.raises(ValueError, match=expected):
        harness.require_ready(cfg, seal())

@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX ledger")
def test_experiment_ledger_concurrency_binding_and_recovery_gate(monkeypatch, tmp_path):
    from concurrent.futures import ThreadPoolExecutor
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import experiment_ledger as ledger
    cfg = run_fixture()["config"]
    cfg["ledger_path"] = str(tmp_path / "experiment.ledger")
    cfg["output_checkpoints"] = {"char:1": str(tmp_path / "output.mook")}
    cfg["gpu_seconds_per_run"] = 2
    cfg["gpu_seconds_total"] = 3
    ledger.create(cfg)
    with pytest.raises(FileExistsError):
        ledger.create(cfg)

    def reserve(_):
        try:
            return ledger.reserve(cfg, variant="char", seed=1, seconds=2)
        except ValueError:
            return None

    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(reserve, range(8)))
    assert sum(x is not None for x in results) == 1
    with pytest.raises(ValueError, match="already reserved"):
        ledger.reserve(cfg, variant="char", seed=1, seconds=1)
    with pytest.raises(RuntimeError, match="budget exhausted"):
        ledger.reserve(cfg, variant="char", seed=2, seconds=2)
    with pytest.raises(ValueError, match="another experiment"):
        ledger.reserve(dict(cfg, corpus_sha256="f" * 64), variant="char", seed=2, seconds=1)
    with pytest.raises(ValueError, match="schedule"):
        ledger.reserve(cfg, variant="char", seed=999, seconds=1)
    with pytest.raises(FileNotFoundError):
        ledger.reserve(cfg, variant="char", seed=1, seconds=1, kind="eval")
    with open(cfg["ledger_path"], "ab") as f:
        f.write(b"partial")
    with pytest.raises(ValueError, match="partial"):
        ledger.reserve(cfg, variant="char", seed=2, seconds=1)


@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX ledger")
def test_checkpoint_completion_before_eval_and_crash_recovery(monkeypatch, tmp_path):
    import hashlib
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import experiment_ledger as ledger
    cfg = run_fixture()["config"]
    cfg["ledger_path"] = str(tmp_path / "exp.ledger")
    checkpoint = tmp_path / "published.mook"
    cfg["output_checkpoints"] = {"char:1": str(checkpoint)}
    cfg["gpu_seconds_per_run"] = 2
    cfg["gpu_seconds_total"] = 10
    ledger.create(cfg)
    ledger.reserve(cfg, variant="char", seed=1, seconds=2)
    checkpoint.write_bytes(b"synthetic checkpoint")
    digest = hashlib.sha256(checkpoint.read_bytes()).hexdigest()
    with pytest.raises(FileNotFoundError):
        ledger.reserve(cfg, variant="char", seed=1, seconds=1, kind="eval")
    with pytest.raises(ValueError):
        ledger.complete(cfg, variant="char", seed=1, checkpoint_path=checkpoint, checkpoint_sha256="0" * 64)
    ledger.complete(cfg, variant="char", seed=1, checkpoint_path=checkpoint, checkpoint_sha256=digest)
    assert ledger.verify_complete(cfg, variant="char", seed=1)["checkpoint_sha256"] == digest
    with pytest.raises(FileExistsError):
        ledger.complete(cfg, variant="char", seed=1, checkpoint_path=checkpoint, checkpoint_sha256=digest)
    checkpoint.write_bytes(b"changed")
    with pytest.raises(ValueError, match="completion record mismatch"):
        ledger.reserve(cfg, variant="char", seed=1, seconds=1, kind="eval")
    checkpoint.write_bytes(b"synthetic checkpoint")
    ledger.reserve(cfg, variant="char", seed=1, seconds=1, kind="eval")
    with pytest.raises(ValueError, match="already reserved"):
        ledger.reserve(cfg, variant="char", seed=1, seconds=1, kind="eval")




@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX orchestrator")
@pytest.mark.parametrize("mode", ["normal", "bad_report", "timeout"])
def test_experiment_sequential_mock_workers(mode, monkeypatch, tmp_path):
    import hashlib
    import json
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import run_experiment as exp
    cfg = run_fixture()["config"]
    cfg.update(ledger_path=str(tmp_path / "ledger"), eval_seconds_per_run=1, eval_split="val",
               output_reports={"char:1:val": str(tmp_path / "report1v"), "char:2:val": str(tmp_path / "report2v"),
                               "char:1:test": str(tmp_path / "report1t"), "char:2:test": str(tmp_path / "report2t")},
               output_checkpoints={"char:1": str(tmp_path / "one"),
                                   "char:2": str(tmp_path / "two")})
    exe, eval_input, eval_script = tmp_path / "eval", tmp_path / "input", tmp_path / "script"
    exe.write_bytes(b"worker")
    eval_input.write_bytes(b"synthetic")
    eval_script.write_bytes(b"script")
    cfg["eval_executable_sha256"] = hashlib.sha256(b"worker").hexdigest()
    cfg["eval_input_sha256"] = {"val": hashlib.sha256(b"synthetic").hexdigest(), "test": hashlib.sha256(b"synthetic").hexdigest()}
    cfg["eval_script_sha256"] = hashlib.sha256(b"script").hexdigest()
    events = []
    phase_state = ["val"]
    for key in ("MOO_KI_GPU", "MOO_KI_GPU_ERZWINGEN", "MOO_KI_GPU_STRIKT"):
        monkeypatch.setenv(key, "0")
    cfg["validation_decision"] = "Use all preregistered runs"
    monkeypatch.setattr(exp.experiment_ledger, "verify_complete", lambda config, **kw: {"checkpoint_sha256": "c" * 64})
    monkeypatch.setattr(exp.harness, "require_ready", lambda c, e: [("char", 1), ("char", 2)])
    def train(*args, **kwargs):
        events.append(("train", kwargs["seed"]))
        Path(kwargs["destination"]).write_bytes(b"fake checkpoint")
        return {"checkpoint_sha256": "c" * 64}
    monkeypatch.setattr(exp.run_training, "execute_run", train)
    monkeypatch.setattr(exp.experiment_ledger, "reserve",
                        lambda config, **kw: events.append(("reserve_eval", kw["seed"])))
    class FakeBudget:
        def __init__(self, **kwargs):
            pass
        def run(self, argv, *, cwd, env):
            assert {k: env[k] for k in ("MOO_KI_GPU", "MOO_KI_GPU_ERZWINGEN", "MOO_KI_GPU_STRIKT")} == {
                "MOO_KI_GPU": "1", "MOO_KI_GPU_ERZWINGEN": "1", "MOO_KI_GPU_STRIKT": "1"}
            seed = int(env["P4_SEED"])
            events.append(("eval", seed))
            if mode == "timeout":
                return {"timed_out": True, "returncode": None, "wall_seconds": 0.01}
            report = {"variant": env["P4_VARIANT"], "seed": seed,
                      "manifest_sha256": env["P4_MANIFEST_SHA256"],
                      "checkpoint_sha256": "x" * 64 if mode == "bad_report" else env["P4_CHECKPOINT_SHA256"],
                      "tokenizer_sha256": cfg["tokenizer_sha256"]["char"], "split": phase_state[0],
                      "nll": 1.0, "nll_per_codepoint": 1.0, "bits_per_byte": 1.0}
            Path(env["P4_EVAL_OUTPUT"]).write_text(json.dumps(report), encoding="utf-8")
            return {"timed_out": False, "returncode": 0, "wall_seconds": 0.01}
    monkeypatch.setattr(exp, "WallBudget", FakeBudget)
    args = dict(encoders={"char": None}, tokenizer_paths={"char": "unused"},
                initial_checkpoints={"char:1": "unused", "char:2": "unused"},
                train_executable="unused", eval_executable=exe, eval_input=eval_input, eval_script=eval_script)
    if mode == "normal":
        assert len(exp.run_experiment(cfg, b"", {}, {}, **args)) == 2
        assert events == [("train", 1), ("reserve_eval", 1), ("eval", 1),
                          ("train", 2), ("reserve_eval", 2), ("eval", 2)]
        exp.freeze_validation(cfg, [("char", 1), ("char", 2)])
        phase_state[0] = "test"
        assert len(exp.run_experiment(cfg, b"", {}, {}, phase="test", **args)) == 2
        assert events[6:] == [("reserve_eval", 1), ("eval", 1), ("reserve_eval", 2), ("eval", 2)]
    else:
        with pytest.raises((ValueError, RuntimeError)):
            exp.run_experiment(cfg, b"", {}, {}, **args)
        assert events == [("train", 1), ("reserve_eval", 1), ("eval", 1)]



@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX process groups")
def test_wall_budget_stops_synthetic_descendant(tmp_path):
    import os
    import sys
    import time
    mod = load_budget_module("wall_budget")
    marker = tmp_path / "child_survived"
    child = "import time,pathlib; time.sleep(0.8); pathlib.Path(" + repr(str(marker)) + ").write_text('alive')"
    parent = "import subprocess,sys,time; subprocess.Popen([sys.executable,'-c'," + repr(child) + "]); time.sleep(4)"
    budget = mod.WallBudget(max_runs=1, per_run=0.2, total=5)
    try:
        result = budget.run([sys.executable, "-c", parent], cwd=str(tmp_path), env=os.environ.copy())
    except RuntimeError as exc:
        assert "process group did not terminate" in str(exc)
    else:
        assert result["timed_out"] and result["returncode"] is None
    time.sleep(1)
    assert not marker.exists()


@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX orchestrator")
def test_experiment_cli_unapproved_certificate_never_creates_ledger(tmp_path):
    import json
    import os
    import subprocess
    import sys
    root = Path(__file__).resolve().parents[1]
    cfg = run_fixture()["config"]
    ledger = tmp_path / "ledger"
    cfg["ledger_path"] = str(ledger)
    config_path, cert = tmp_path / "config.json", tmp_path / "certificate.json"
    config_path.write_text(json.dumps(cfg), encoding="utf-8")
    cert.write_text("{}", encoding="utf-8")
    result = subprocess.run([sys.executable, "-m", "skripte.p4_silben.run_experiment",
                             str(config_path), str(cert), "--create-ledger"],
                            cwd=tmp_path, env=dict(os.environ, PYTHONPATH=str(root)),
                            capture_output=True, timeout=10)
    assert result.returncode != 0
    assert b"gate closed" in result.stderr or b"certificate hash not approved" in result.stderr
    assert not ledger.exists()



@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX phase lock")
def test_validation_freeze_requires_all_reports_and_detects_mutation(monkeypatch, tmp_path):
    import json
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[1]))
    from skripte.p4_silben import run_experiment as exp
    cfg = run_fixture()["config"]
    cfg.update(ledger_path=str(tmp_path / "ledger"),
               validation_decision="Use all preregistered runs",
               output_reports={"char:1:val": str(tmp_path / "val1"),
                               "char:2:val": str(tmp_path / "val2")})
    schedule = [("char", 1), ("char", 2)]
    monkeypatch.setattr(exp.experiment_ledger, "verify_complete",
                        lambda config, **kw: {"checkpoint_sha256": "c" * 64})
    with pytest.raises(FileNotFoundError):
        exp.verify_validation_freeze(cfg, schedule)
    def report(seed):
        return {"variant": "char", "seed": seed, "split": "val",
                "manifest_sha256": cfg["manifest_sha256"],
                "checkpoint_sha256": "c" * 64,
                "tokenizer_sha256": cfg["tokenizer_sha256"]["char"], "nll": 1.0}
    Path(cfg["output_reports"]["char:1:val"]).write_text(json.dumps(report(1)), encoding="utf-8")
    with pytest.raises(FileNotFoundError):
        exp.freeze_validation(cfg, schedule)
    Path(cfg["output_reports"]["char:2:val"]).write_text(json.dumps(report(2)), encoding="utf-8")
    exp.freeze_validation(cfg, schedule)
    exp.verify_validation_freeze(cfg, schedule)
    with pytest.raises(FileExistsError):
        exp.freeze_validation(cfg, schedule)
    changed = report(2)
    changed["nll"] = 2.0
    Path(cfg["output_reports"]["char:2:val"]).write_text(json.dumps(changed), encoding="utf-8")
    with pytest.raises(ValueError, match="freeze missing or changed"):
        exp.verify_validation_freeze(cfg, schedule)



@pytest.mark.parametrize("case", ["ok", "missing", "checkpoint", "eval", "collision"])
def test_prepare_config_binds_synthetic_artifacts(tmp_path, monkeypatch, case):
    import hashlib
    import json
    from skripte.p4_silben import prepare_config as module

    def write(name, data):
        path = tmp_path / name
        path.write_bytes(data)
        return str(path)

    def sha(path):
        return hashlib.sha256(__import__("pathlib").Path(path).read_bytes()).hexdigest()

    corpus = write("corpus", b"synthetic corpus")
    encoder = write("encoder", b"synthetic encoder")
    scorer = write("scorer", b"synthetic scorer")
    tokenizer = write("tokenizer", b"synthetic tokenizer")
    checkpoint = write("checkpoint", b"synthetic untrained checkpoint")
    executable = write("executable", b"synthetic executable")
    protocol = write("protocol", b"synthetic protocol")
    manifest = {"source_sha256": sha(corpus), "splits": {
        "train": {"codepoints": 3, "document_ids": ["train"]},
        "val": {"document_ids": ["val"]}, "test": {"document_ids": ["test"]}}}
    manifest["manifest_sha256"] = hashlib.sha256(json.dumps(
        manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode()).hexdigest()
    manifest_path = write("manifest", json.dumps(manifest).encode())
    specs = {}
    for split in ("val", "test"):
        spec = {"split": split, "manifest": manifest, "documents": {split: {}},
                "encoder_path": encoder, "encoder_sha256": sha(encoder),
                "scorer_path": scorer, "scorer_sha256": sha(scorer),
                "context": 8, "stride": 7,
                "tokenizers": {"char": {"path": tokenizer, "sha256": sha(tokenizer)}}}
        if case == "eval" and split == "val":
            spec["stride"] = 6
        specs[split] = write(split, json.dumps(spec).encode())
    output = str(tmp_path / "out")
    config = {"manifest_path": manifest_path, "corpus_path": corpus,
              "protocol_path": protocol, "split": "train", "train_codepoint_budget": 3,
              "train_documents": ["train"], "train_executable_path": executable,
              "eval_executable_path": executable, "eval_script_path": scorer,
              "encoder_path": encoder, "variants": ["char"],
              "tokenizer_paths": {"char": tokenizer},
              "initial_checkpoint_paths": {"char:1": checkpoint},
              "output_checkpoints": {"char:1": output},
              "output_reports": {"char:1:val": str(tmp_path / "rv"),
                                 "char:1:test": str(tmp_path / "rt")},
              "ledger_path": output if case == "collision" else str(tmp_path / "ledger"),
              "eval_input_path": specs, "context": 8, "stride": 7,
              "validation_decision": "synthetic preregistration"}
    monkeypatch.setattr(module, "validate_config", lambda _: [("char", 1)])
    monkeypatch.setattr(module, "TokenTable", lambda *args: type("Table", (), {"tokens": [0]})())
    monkeypatch.setattr(module, "metadata", lambda _: {
        "tokenizer_version": "bad" if case == "checkpoint" else sha(tokenizer),
        "arch_version": "p4-gpt2-v1", "step": 0, "global_step": 0,
        "vocab": 1, "context": 8})
    if case == "missing":
        config["protocol_path"] = str(tmp_path / "missing")
    if case == "ok":
        result = module.prepare(config)
        assert result["manifest_sha256"] == manifest["manifest_sha256"]
        assert result["tokenizer_sha256"]["char"] == sha(tokenizer)
        assert result["checkpoint_sha256"]["char:1"] == sha(checkpoint)
        assert result["eval_input_sha256"]["val"] == sha(specs["val"])
        assert not (tmp_path / "out").exists()
    else:
        with pytest.raises(ValueError):
            module.prepare(config)



def test_prepare_config_with_real_synthetic_tokenizers_and_checkpoints(tmp_path):
    import hashlib
    import json
    import os
    import struct
    import subprocess
    from skripte.p4_silben import prepare_config

    root = Path(__file__).resolve().parents[1]
    sha = lambda data: hashlib.sha256(data).hexdigest()
    u32 = lambda *xs: struct.pack("<" + "I" * len(xs), *xs)
    char_body = b"MOOCF001" + u32(1, 0, 256)
    pattern = (root / "compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat").read_bytes()
    syllable_body = b"MOOSIL01" + u32(1, len(pattern), 0, 0) + pattern
    bpe = b"MOOBPE01" + u32(1, 0, 256, 0, 0, 0)
    bpe += b"".join(u32(1) + bytes([i]) for i in range(256))
    blobs = {"char": char_body + hashlib.sha256(char_body).digest(),
             "bpe": bpe,
             "syllable": syllable_body + hashlib.sha256(syllable_body).digest()}
    tokenizers = {}
    for variant, blob in blobs.items():
        path = tmp_path / (variant + ".tok")
        path.write_bytes(blob)
        tokenizers[variant] = str(path)
    initializer = tmp_path / "init"
    subprocess.run([str(root / "compiler/target/release/moo-compiler"), "compile",
                    str(root / "beispiele/p4_silben/init_model.moos"), "-o", str(initializer)],
                   cwd=root, check=True, capture_output=True)
    seeds = [1, 2, 3]
    checkpoints = {}
    for variant in blobs:
        for seed in seeds:
            key = f"{variant}:{seed}"
            path = tmp_path / (key + ".mook")
            env = dict(os.environ, P4_TOKENIZER_KIND=variant, P4_TOKENIZER_PATH=tokenizers[variant],
                       P4_SEED=str(seed), P4_DIM="8", P4_HEADS="2", P4_CONTEXT="8",
                       P4_HIDDEN="16", P4_CHECKPOINT_OUT=str(path))
            completed = subprocess.run([str(initializer)], cwd=root, env=env,
                                       capture_output=True, timeout=30)
            assert completed.returncode == 0, completed.stderr
            checkpoints[key] = str(path)
    corpus = tmp_path / "corpus"
    corpus.write_bytes(b"synthetic training text")
    manifest = {"source_sha256": sha(corpus.read_bytes()), "splits": {
        "train": {"codepoints": 3, "document_ids": ["train"]},
        "val": {"document_ids": ["val"]}, "test": {"document_ids": ["test"]}}}
    manifest["manifest_sha256"] = sha(json.dumps(manifest, sort_keys=True,
        ensure_ascii=False, separators=(",", ":")).encode())
    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text(json.dumps(manifest))
    executable = tmp_path / "worker"
    executable.write_bytes(b"synthetic unused worker")
    protocol = tmp_path / "protocol"
    protocol.write_bytes(b"synthetic protocol")
    specs = {}
    for split in ("val", "test"):
        spec = {"split": split, "manifest": manifest, "documents": {split: "unused"},
                "encoder_path": str(executable), "encoder_sha256": sha(executable.read_bytes()),
                "scorer_path": str(executable), "scorer_sha256": sha(executable.read_bytes()),
                "context": 8, "stride": 7,
                "tokenizers": {v: {"path": tokenizers[v], "sha256": sha(blobs[v])} for v in blobs}}
        path = tmp_path / (split + ".json")
        path.write_text(json.dumps(spec))
        specs[split] = str(path)
    config = {"protocol_version": "moo-p4-measure-v1", "seeds": seeds,
              "variants": ["char", "bpe", "syllable"], "gpu_seconds_per_run": 600,
              "gpu_seconds_total": 7200, "manifest_path": str(manifest_path),
              "corpus_path": str(corpus), "protocol_path": str(protocol), "split": "train",
              "train_codepoint_budget": 3, "train_documents": ["train"],
              "train_executable_path": str(executable), "eval_executable_path": str(executable),
              "eval_script_path": str(executable), "encoder_path": str(executable),
              "tokenizer_paths": tokenizers, "initial_checkpoint_paths": checkpoints,
              "output_checkpoints": {k: str(tmp_path / ("output_" + k)) for k in checkpoints},
              "output_reports": {k + ":" + s: str(tmp_path / ("report_" + k + s))
                                 for k in checkpoints for s in ("val", "test")},
              "ledger_path": str(tmp_path / "ledger"), "eval_input_path": specs,
              "context": 8, "stride": 7, "validation_decision": "synthetic decision"}
    result = prepare_config.prepare(config)
    assert result["tokenizer_sha256"] == {v: sha(blob) for v, blob in blobs.items()}
    assert result["checkpoint_sha256"] == {k: sha(Path(p).read_bytes()) for k, p in checkpoints.items()}
    assert not Path(config["ledger_path"]).exists()
    config["initial_checkpoint_paths"]["char:1"] = checkpoints["bpe:1"]
    with pytest.raises(ValueError, match="untrained checkpoint mismatch"):
        prepare_config.prepare(config)



@pytest.mark.parametrize("case", ["ok", "manifest_hash", "source_hash", "boundary", "document_hash", "split_hash", "existing"])
def test_materialize_documents_synthetic(tmp_path, case):
    import hashlib
    import json
    import stat
    from skripte.p4_silben.materialize_documents import materialize

    sha = lambda b: hashlib.sha256(b).hexdigest()
    parts = [("one", "train", "Ä".encode()), ("two", "val", b"V"), ("three", "test", b"T")]
    corpus = b"".join(p[2] for p in parts)
    documents, splits, cursor = [], {}, 0
    for ident, split, raw in parts:
        documents.append({"id": ident, "split": split, "start": cursor,
                          "end": cursor + len(raw), "bytes": len(raw),
                          "codepoints": len(raw.decode()), "sha256": sha(raw)})
        splits[split] = {"document_ids": [ident], "bytes": len(raw),
                         "codepoints": len(raw.decode()), "sha256": sha(raw)}
        cursor += len(raw)
    manifest = {"documents": documents, "splits": splits, "source_sha256": sha(corpus)}
    if case == "boundary":
        documents[1]["start"] += 1
    elif case == "document_hash":
        documents[1]["sha256"] = "0" * 64
    elif case == "split_hash":
        splits["val"]["sha256"] = "0" * 64
    digest = sha(json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                            separators=(",", ":")).encode())
    manifest["manifest_sha256"] = digest
    if case == "manifest_hash":
        digest = "0" * 64
    if case == "source_hash":
        corpus += b"x"
    destination = tmp_path / "private"
    if case == "existing":
        destination.mkdir()
        (destination / "sentinel").write_bytes(b"unchanged")
        with pytest.raises(FileExistsError):
            materialize(manifest, corpus, destination, digest)
        assert (destination / "sentinel").read_bytes() == b"unchanged"
    elif case == "ok":
        mapping = json.loads(materialize(manifest, corpus, destination, digest).read_bytes())
        assert mapping["manifest_sha256"] == digest
        for ident, split, raw in parts:
            path = Path(mapping["documents"][split][ident])
            assert path.read_bytes() == raw
            assert stat.S_IMODE(path.stat().st_mode) == 0o600
        assert stat.S_IMODE(destination.stat().st_mode) == 0o700
    else:
        with pytest.raises(ValueError):
            materialize(manifest, corpus, destination, digest)
        assert not destination.exists()



def test_materialize_existing_sealed_synthetic_corpus(tmp_path):
    import hashlib
    import json
    from skripte.p4_silben.materialize_documents import materialize

    root = Path(__file__).resolve().parents[1]
    base = root / "dokumentation/p4_silben"
    manifest = json.loads((base / "synthetic_manifest.json").read_bytes())
    source = (base / "synthetic_corpus.txt").read_bytes()
    expected = "8e74c14e607844e2172b49534edb131b07bada24320bc035554348d6ff98c80f"
    mapping_path = materialize(manifest, source, tmp_path / "docs", expected)
    mapping = json.loads(mapping_path.read_bytes())
    assert mapping["manifest_sha256"] == expected
    for doc in manifest["documents"]:
        path = Path(mapping["documents"][doc["split"]][doc["id"]])
        assert hashlib.sha256(path.read_bytes()).hexdigest() == doc["sha256"]
    assert sum(len(m) for m in mapping["documents"].values()) == len(manifest["documents"])



@pytest.mark.skipif(not hasattr(__import__("os"), "wait4") or not Path("/proc/self/status").is_file(),
                    reason="Linux procfs and wait4 are required for RSS measurements")
def test_p4_synthetic_tokenizer_resource_measurement(tmp_path):
    import hashlib
    import json
    import os
    import statistics
    import struct
    import subprocess
    import time
    from skripte.p4_silben.token_bytes import TokenTable

    root = Path(__file__).resolve().parents[1]
    sha = lambda data: hashlib.sha256(data).hexdigest()
    u32 = lambda *values: struct.pack("<" + "I" * len(values), *values)
    chars = b"MOOCF001" + u32(1, 0, 256)
    pattern = (root / "compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat").read_bytes()
    parts = [b"Zu", b"cker", b"Bag", b"ger"]
    syllables = b"MOOSIL01" + u32(1, len(pattern), len(parts), 0) + pattern
    syllables += b"".join(u32(len(part)) + part for part in parts)
    bpe = b"MOOBPE01" + u32(1, 0, 256, 0, 0, 0)
    bpe += b"".join(u32(1) + bytes([i]) for i in range(256))
    blobs = {"char": chars + hashlib.sha256(chars).digest(), "bpe": bpe,
             "syllable": syllables + hashlib.sha256(syllables).digest()}
    text = "Zucker und Bagger stehen am grünen Fluss. Äpfel, Häuser, Straße!\n" * 300
    raw = text.encode("utf-8")
    document = tmp_path / "synthetic.txt"
    document.write_bytes(raw)
    encoder = tmp_path / "encoder"
    compiler = root / "compiler/target/release/moo-compiler"
    subprocess.run([str(compiler), "compile", str(root / "beispiele/p4_silben/encode.moos"),
                    "-o", str(encoder)], cwd=root, check=True, capture_output=True)
    for kind, blob in blobs.items():
        artifact = tmp_path / (kind + ".tok")
        artifact.write_bytes(blob)
        table = TokenTable(kind, blob, sha(blob))
        samples = []
        count = None
        for iteration in range(3):
            output = tmp_path / (kind + str(iteration) + ".json")
            errors = tmp_path / (kind + str(iteration) + ".stderr")
            env = dict(os.environ, P4_TOKENIZER_KIND=kind, P4_TOKENIZER_PATH=str(artifact),
                       P4_TEXT_PATH=str(document))
            started = time.monotonic()
            with output.open("wb") as out, errors.open("wb") as err:
                child = subprocess.Popen([str(encoder)], cwd=root, env=env, stdout=out, stderr=err)
                deadline = started + 90
                sampled_peak = 0
                while True:
                    try:
                        if os.readlink(f"/proc/{child.pid}/exe") == str(encoder):
                            status_text = Path(f"/proc/{child.pid}/status").read_text()
                            for line in status_text.splitlines():
                                if line.startswith("VmHWM:"):
                                    sampled_peak = max(sampled_peak, int(line.split()[1]))
                    except (FileNotFoundError, ProcessLookupError):
                        pass
                    pid, status, usage = os.wait4(child.pid, os.WNOHANG)
                    if pid:
                        child.returncode = os.waitstatus_to_exitcode(status)
                        break
                    if time.monotonic() >= deadline:
                        child.kill()
                        os.wait4(child.pid, 0)
                        child.returncode = -1
                        pytest.fail("encoder exceeded 90 seconds")
                    time.sleep(0.001)
            elapsed = time.monotonic() - started
            assert child.returncode == 0, errors.read_bytes()
            ids = json.loads(output.read_bytes())
            assert b"".join(table(blob, int(token)) for token in ids) == raw
            assert count is None or count == len(ids)
            count = len(ids)
            assert sampled_peak > 0, "encoder VmHWM was not observed"
            samples.append((elapsed, usage.ru_maxrss, sampled_peak))
        seconds = statistics.median(item[0] for item in samples)
        print("P4_SYNTHETIC_RESOURCE " + json.dumps({
            "kind": kind, "text_sha256": sha(raw), "artifact_sha256": sha(blob),
            "bytes": len(raw), "codepoints": len(text), "ids": count, "runs": len(samples),
            "seconds_median": round(seconds, 6), "bytes_per_second": round(len(raw) / seconds),
            "ids_per_second": round(count / seconds),
            "wait4_peak_rss_kib_max": max(item[1] for item in samples),
            "proc_sampled_peak_rss_kib_max": max(item[2] for item in samples)}, sort_keys=True), flush=True)



def test_compile_experiment_rejects_existing_output_without_partial_writes(tmp_path):
    import os
    import subprocess

    root = Path(__file__).resolve().parents[1]
    target = tmp_path / "train_window"
    target.write_bytes(b"existing private artifact")
    before = target.read_bytes()
    env = dict(os.environ, P4_RUN_DIR=str(tmp_path))
    result = subprocess.run(["mise", "run", "p4-compile-experiment"], cwd=root,
                            env=env, capture_output=True, timeout=30)
    assert result.returncode != 0
    assert b"P4 output already exists: train_window" in result.stderr
    assert target.read_bytes() == before
    assert not (tmp_path / "train_tokenizers").exists()



@pytest.mark.parametrize("mode", [None, "cpu", "vulkan", "auto", "VULKAN_FORCED_STRICT"])
def test_device_environment_rejects_missing_or_non_strict_mode(mode):
    from skripte.p4_silben.harness import device_environment

    with pytest.raises(ValueError, match="explicit forced strict Vulkan"):
        device_environment({"device_mode": mode})


def test_device_environment_declares_all_required_values():
    from skripte.p4_silben.harness import device_environment

    assert device_environment({"device_mode": "vulkan_forced_strict"}) == {
        "MOO_KI_GPU": "1", "MOO_KI_GPU_ERZWINGEN": "1", "MOO_KI_GPU_STRIKT": "1"}



@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX process groups")
@pytest.mark.parametrize("outcome", ["success", "failure", "timeout"])
def test_wall_budget_drains_bounded_stderr_and_preserves_result(tmp_path, outcome):
    import os
    import sys
    module = load_budget_module("wall_budget")
    code = "import os,time; os.write(1,b'private stdout'); os.write(2,b'A'*1048576); "
    code += "time.sleep(5)" if outcome == "timeout" else "os._exit(" + ("7" if outcome == "failure" else "0") + ")"
    budget = module.WallBudget(max_runs=1, per_run=0.5 if outcome == "timeout" else 5, total=10)
    result = budget.run([sys.executable, "-c", code], cwd=str(tmp_path), env=os.environ.copy())
    assert result["stderr"] == "A" * 8192
    assert "private stdout" not in str(result)
    assert result["timed_out"] is (outcome == "timeout")
    assert result["returncode"] == (None if outcome == "timeout" else 7 if outcome == "failure" else 0)
    assert result["run"] == 1 and result["wall_total_seconds"] >= result["wall_seconds"]
    with pytest.raises(RuntimeError, match="exhausted"):
        budget.run([sys.executable, "-c", "pass"], cwd=str(tmp_path), env=os.environ.copy())


@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX worker budget")
@pytest.mark.parametrize("timed_out,rc", [(False, 7), (True, None)])
def test_runner_retains_bounded_worker_diagnostic_without_publishing(tmp_path, monkeypatch, timed_out, rc):
    import hashlib
    import json
    from skripte.p4_silben import run_training as runner

    cfg = run_fixture()["config"]
    cfg.update(train_executable_sha256=hashlib.sha256(b"binary").hexdigest(),
               train_codepoint_budget=9, ledger_path=str(tmp_path / "ledger"))
    tok, initial, exe = [tmp_path / name for name in ("tok", "initial", "exe")]
    for path, data in ((tok, b"tokenizer"), (initial, b"initial"), (exe, b"binary")):
        path.write_bytes(data)
    monkeypatch.setattr(runner.harness, "require_ready", lambda c, e: [("char", 1)])
    monkeypatch.setattr(runner, "prepare_run", lambda *a, **kw:
                        (json.dumps({"original_codepoints": 9, "windows": [{}]}).encode(), "synthetic"))
    monkeypatch.setattr(runner, "metadata", lambda _: {"tokenizer_version": hashlib.sha256(b"tokenizer").hexdigest()})
    reservations = []
    monkeypatch.setattr(runner.experiment_ledger, "reserve", lambda *a, **kw: reservations.append(kw))
    budget = runner.WallBudget(max_runs=1, per_run=1, total=2)
    monkeypatch.setattr(budget, "run", lambda *a, **kw: {
        "returncode": rc, "timed_out": timed_out, "stderr": "Z" * 8192})
    out = tmp_path / "output"
    with pytest.raises(RuntimeError) as failure:
        runner.execute_run(cfg, b"", {}, {}, variant="char", seed=1, encoder=None,
            tokenizer_path=tok, checkpoint_path=initial, train_executable=exe,
            destination=out, ledger_path=tmp_path / "ledger", wall_budget=budget)
    message = str(failure.value)
    assert f"rc={rc}" in message and f"timed_out={timed_out}" in message
    assert message.endswith("Z" * 2048) and len(message.split("stderr=", 1)[1]) == 2048
    assert len(reservations) == 1 and reservations[0]["seconds"] == 1
    assert not out.exists()



def test_training_padding_causal_and_window_validation_cpu(tmp_path):
    import hashlib
    import json
    import os
    import struct
    import subprocess

    root = Path(__file__).resolve().parents[1]
    compiler = root / "compiler/target/release/moo-compiler"
    sha = lambda data: hashlib.sha256(data).hexdigest()
    body = b"MOOCF001" + struct.pack("<III", 1, 0, 256)
    blob = body + hashlib.sha256(body).digest()
    tokenizer = tmp_path / "char.tok"
    tokenizer.write_bytes(blob)
    env = dict(os.environ, MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0", MOO_KI_GPU_STRIKT="0",
               P4_TOKENIZER_KIND="char", P4_TOKENIZER_PATH=str(tokenizer), P4_SEED="4",
               P4_DIM="8", P4_HEADS="2", P4_CONTEXT="8", P4_HIDDEN="16")
    def compile_moo(source, name):
        out = tmp_path / name
        subprocess.run([str(compiler), "compile", str(root / source), "-o", str(out)],
                       cwd=root, check=True, capture_output=True, timeout=30)
        return out
    gradient_probe = compile_moo("beispiele/tests/p4_mask_gradient.moos", "gradient")
    result = subprocess.run([str(gradient_probe)], cwd=root, env=env,
                            capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    gradients = json.loads(result.stdout)
    import math
    expected = []
    for row, target, active in [([2, 1, 0, -1], 0, True),
                                ([0.5, 0.5, 0.5, 0.5], 2, False),
                                ([-1, 3, 0, 1], 1, True)]:
        weights = [math.exp(x - max(row)) for x in row]
        expected.extend(((w / sum(weights) - (i == target)) / 2 if active else 0.0)
                        for i, w in enumerate(weights))
    assert len(gradients) == len(expected)
    assert gradients[4:8] == [0.0] * 4
    assert all(abs(actual - reference) < 1e-6
               for actual, reference in zip(gradients, expected))

    initializer = compile_moo("beispiele/p4_silben/init_model.moos", "init")
    checkpoint = tmp_path / "initial.mook"
    result = subprocess.run([str(initializer)], cwd=root, env=dict(env, P4_CHECKPOINT_OUT=str(checkpoint)),
                            capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    before = sha(checkpoint.read_bytes())
    probe = compile_moo("beispiele/tests/p4_padding_causal.moos", "padding")
    result = subprocess.run([str(probe)], cwd=root, env=dict(env, P4_CHECKPOINT=str(checkpoint)),
                            capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    values = json.loads(result.stdout)
    def flatten(value):
        if isinstance(value, list):
            return [x for item in value for x in flatten(item)]
        return [float(value)]
    short, zero, other = [flatten(values[key]) for key in ("short", "zero", "other")]
    assert len(short) == len(zero) == len(other) == 3 * 256
    assert max(abs(a - b) for a, b in zip(short, zero)) < 1e-4
    assert max(abs(a - b) for a, b in zip(zero, other)) < 1e-4
    losses = [flatten(values[key])[0] for key in ("short_loss", "zero_loss", "other_loss")]
    assert max(losses) - min(losses) < 1e-4

    diagnostic = compile_moo("beispiele/p4_silben/train_diagnose.moos", "diagnostic")
    valid = {"input_ids": [1, 2, 3], "target_ids": [2, 3, 4], "loss_mask": [1, 1, 1]}
    cases = {
        "valid": valid,
        "nonbinary": dict(valid, loss_mask=[1, 2, 1]),
        "empty_mask": dict(valid, loss_mask=[0, 0, 0]),
        "shift": dict(valid, target_ids=[9, 3, 4]),
        "shape": dict(valid, target_ids=[2, 3]),
        "bad_id": dict(valid, target_ids=[2, 3, 256]),
        "too_long": {"input_ids": [1] * 9, "target_ids": [1] * 9, "loss_mask": [1] * 9},
    }
    for name, window in cases.items():
        payload = json.dumps({"manifest_sha256": "a" * 64, "tokenizer_sha256": sha(blob),
                              "windows": [window]}, sort_keys=True).encode()
        windows = tmp_path / (name + ".json")
        windows.write_bytes(payload)
        output = tmp_path / (name + ".mook")
        test_env = dict(env, P4_CHECKPOINT=str(checkpoint), P4_CHECKPOINT_OUT=str(output),
                        P4_WINDOWS_PATH=str(windows), P4_WINDOWS_SHA256=sha(payload),
                        P4_MANIFEST_SHA256="a" * 64)
        result = subprocess.run([str(diagnostic)], cwd=root, env=test_env, capture_output=True, timeout=30)
        if name == "valid":
            assert result.returncode == 0, result.stderr
            assert b"P4 DIAG FORWARD LOSS OK" in result.stdout
        else:
            assert result.returncode != 0, name
        assert not output.exists()
    assert sha(checkpoint.read_bytes()) == before
    print("P4_PADDING_CPU logits_max_diff=%.8g loss_max_diff=%.8g negatives=%d" %
          (max(abs(a - b) for a, b in zip(zero, other)), max(losses) - min(losses), len(cases) - 1), flush=True)



@pytest.mark.skipif(__import__("os").name != "posix", reason="POSIX ledger")
@pytest.mark.parametrize("case", ["ok", "missing", "drift", "wrong_link", "extra_entry",
                                  "wrong_run", "wrong_chain", "new_total", "new_per_run",
                                  "reservation", "preparation", "elapsed"])
def test_continuation_validates_original_ledger_and_combined_budget(tmp_path, case):
    import hashlib
    import json
    from skripte.p4_silben import experiment_ledger, harness

    sha = lambda data: hashlib.sha256(data).hexdigest()
    old = run_fixture()["config"]
    ledger = tmp_path / "old-ledger"
    old.update(seeds=[11, 23, 47], ledger_path=str(ledger),
               gpu_seconds_per_run=600, gpu_seconds_total=7200)
    experiment_ledger.create(old)
    experiment_ledger.reserve(old, variant="char", seed=11, seconds=600)
    old_path = tmp_path / "old-config.json"
    old_path.write_text(json.dumps(old), encoding="utf-8")
    prior = {"config_path": str(old_path), "config_sha256": sha(old_path.read_bytes()),
             "ledger_path": str(ledger), "ledger_sha256": sha(ledger.read_bytes()),
             "reserved_seconds": 600, "preparation_allowance_seconds": 300,
             "failed_attempt_wall_upper_seconds": 5}
    cfg = {"gpu_seconds_per_run": 500, "gpu_seconds_total": 6300, "predecessor": prior}
    if case == "missing":
        prior.pop("ledger_sha256")
    elif case == "drift":
        ledger.write_bytes(ledger.read_bytes() + b"x")
    elif case == "wrong_link":
        old["ledger_path"] = str(tmp_path / "different-ledger")
        old_path.write_text(json.dumps(old), encoding="utf-8")
        prior["config_sha256"] = sha(old_path.read_bytes())
    elif case == "extra_entry":
        experiment_ledger.reserve(old, variant="char", seed=23, seconds=1)
        prior["ledger_sha256"] = sha(ledger.read_bytes())
    elif case in ("wrong_run", "wrong_chain"):
        lines = ledger.read_bytes().splitlines()
        item = json.loads(lines[1])
        if case == "wrong_run":
            item["run"][2] = 23
        else:
            item["previous"] = "0" * 64
        ledger.write_bytes(lines[0] + b"\\n" + experiment_ledger.canonical(item) + b"\\n")
        prior["ledger_sha256"] = sha(ledger.read_bytes())
    elif case == "new_total":
        cfg["gpu_seconds_total"] = 6301
    elif case == "new_per_run":
        cfg["gpu_seconds_per_run"] = 596
    elif case == "reservation":
        prior["reserved_seconds"] = 599
    elif case == "preparation":
        prior["preparation_allowance_seconds"] = 299
    elif case == "elapsed":
        prior["failed_attempt_wall_upper_seconds"] = 6
    before = ledger.read_bytes()
    if case == "ok":
        assert harness._validate_predecessor(cfg) is None
    else:
        with pytest.raises(ValueError):
            harness._validate_predecessor(cfg)
    assert ledger.read_bytes() == before

def test_sil08_same_size_identity_and_artifact_version_cpu(tmp_path):
    import hashlib
    import os
    import struct
    import subprocess

    root = Path(__file__).resolve().parents[1]
    patterns = (root / "compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat").read_bytes()
    pack = lambda n: struct.pack("<I", n)
    def artifact(parts, rules=patterns, version=1):
        body = (b"MOOSIL01" + pack(version) + pack(len(rules)) + pack(len(parts)) +
                pack(0) + rules + b"".join(pack(len(part)) + part for part in parts))
        return body + hashlib.sha256(body).digest()
    parts = [b"Zu", b"cker", b"Bag", b"ger"]
    fixtures = {
        "original.msf": artifact(parts),
        "reordered.msf": artifact([parts[1], parts[0], *parts[2:]]),
        "rules.msf": artifact(parts, patterns + b"\n"),
        "version.msf": artifact(parts, version=2),
        "truncated.msf": artifact(parts)[:-10],
    }
    for name, data in fixtures.items():
        (tmp_path / name).write_bytes(data)
    compiler = root / "compiler/target/release/moo-compiler"
    binary = tmp_path / "identity"
    subprocess.run([str(compiler), "compile", str(root / "beispiele/tests/p4_identity_negative.moos"),
                    "-o", str(binary)], cwd=root, check=True, capture_output=True, timeout=30)
    env = dict(os.environ, MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0",
               MOO_KI_GPU_STRIKT="0", P4_IDENTITY_DIR=str(tmp_path))
    result = subprocess.run([str(binary)], cwd=root, env=env, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert b"P4 IDENTITY OK" in result.stdout
