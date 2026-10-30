"""Create SIL-11 evaluation specs and a config template without opening the gate."""
import argparse
import hashlib
import json
import os
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_new(path, value):
    raw = json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
    with Path(path).open("xb") as output:
        output.write(raw)
        output.flush()
        os.fsync(output.fileno())
    return hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir")
    parser.add_argument("snapshot_sha256")
    parser.add_argument("--bin-dir", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    run = Path(args.run_dir)
    if not run.is_absolute() or not run.is_dir() or len(args.snapshot_sha256) != 64:
        raise ValueError("existing absolute run directory and snapshot hash required")
    manifest_path = root / "dokumentation/p4_silben/synthetic_manifest.json"
    protocol_path = root / "dokumentation/p4_silben/synthetic_run_plan.json"
    corpus_path = root / "dokumentation/p4_silben/synthetic_corpus.txt"
    docs_dir = root / "beispiele/p4_silben/run_sil11_20261030_documents"
    bin_dir = Path(args.bin_dir)
    if not bin_dir.is_absolute() or str(bin_dir) != args.bin_dir or not bin_dir.is_dir():
        raise ValueError("existing canonical absolute binary directory required")
    tok_dir = root / "beispiele/p4_silben/run_sil11_20261030_tokenizers"
    initial_dir = root / "beispiele/p4_silben/run_sil11_20261030_initial"
    manifest = json.loads(manifest_path.read_bytes())
    plan = json.loads(protocol_path.read_bytes())
    mapping = json.loads((docs_dir / "documents.json").read_bytes())
    expected = "8e74c14e607844e2172b49534edb131b07bada24320bc035554348d6ff98c80f"
    unsigned = {k: v for k, v in manifest.items() if k != "manifest_sha256"}
    actual = hashlib.sha256(json.dumps(unsigned, ensure_ascii=False, sort_keys=True,
                            separators=(",", ":")).encode()).hexdigest()
    if actual != expected or mapping["manifest_sha256"] != expected or plan["manifest_sha256"] != expected:
        raise ValueError("manifest identity mismatch")
    if digest(corpus_path) != manifest["source_sha256"]:
        raise ValueError("corpus identity mismatch")
    tok_paths = {"char": tok_dir / "char.mcf", "bpe": tok_dir / "bpe.mbt",
                 "syllable": tok_dir / "syllable.msf"}
    encoder, scorer = bin_dir / "encode", bin_dir / "logits_batch"
    tokens = {v: {"path": str(p), "sha256": digest(p)} for v, p in tok_paths.items()}
    eval_paths = {}
    for split in ("val", "test"):
        spec = {"manifest": manifest, "split": split,
                "documents": mapping["documents"][split], "tokenizers": tokens,
                "encoder_path": str(encoder), "encoder_sha256": digest(encoder),
                "scorer_path": str(scorer), "scorer_sha256": digest(scorer),
                "scorer_mode": "batch", "context": plan["context"], "stride": plan["stride"]}
        path = run / (split + "_input.json")
        write_new(path, spec)
        eval_paths[split] = str(path)
    keys = [(v, s) for v in plan["variants"] for s in plan["seeds"]]
    config = dict(plan)
    config.update(split="train", device_mode="vulkan_forced_strict", snapshot_sha256=args.snapshot_sha256,
                  manifest_path=str(manifest_path), corpus_path=str(corpus_path),
                  protocol_path=str(protocol_path), train_documents=mapping["documents"]["train"],
                  tokenizer_paths={v: str(p) for v, p in tok_paths.items()},
                  encoder_path=str(encoder), train_executable_path=str(bin_dir / "train_window"),
                  eval_executable_path=str(Path("/usr/bin/python3").resolve()),
                  eval_script_path=str(root / "skripte/p4_silben/eval_worker.py"),
                  eval_input_path=eval_paths,
                  initial_checkpoint_paths={v + ":" + str(s): str(initial_dir / (v + "-" + str(s) + ".mook")) for v, s in keys},
                  output_checkpoints={v + ":" + str(s): str(run / (v + "-" + str(s) + ".mook")) for v, s in keys},
                  output_reports={v + ":" + str(s) + ":" + split: str(run / (v + "-" + str(s) + "-" + split + ".json")) for v, s in keys for split in ("val", "test")},
                  ledger_path=str(run / "budget.json"))
    path = run / "config_template.json"
    print("template_sha256", write_new(path, config))


if __name__ == "__main__":
    main()
