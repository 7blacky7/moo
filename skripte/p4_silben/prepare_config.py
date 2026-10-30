"""Prepare a reviewable P4 runner configuration; never trains or grants access."""
import argparse
import hashlib
import json
import os
from pathlib import Path

from skripte.p4_silben.checkpoint_metadata import metadata
from skripte.p4_silben.harness import validate_config
from skripte.p4_silben.token_bytes import TokenTable


def digest(path, maximum=512 * 1024 * 1024):
    path = Path(path)
    if not path.is_file() or path.stat().st_size > maximum:
        raise ValueError("missing or oversized artifact: " + str(path))
    h = hashlib.sha256()
    with path.open("rb") as f:
        for part in iter(lambda: f.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def prepare(template):
    config = dict(template)
    manifest = json.loads(Path(config["manifest_path"]).read_bytes())
    unsigned = {k: v for k, v in manifest.items() if k != "manifest_sha256"}
    config["manifest_sha256"] = hashlib.sha256(json.dumps(
        unsigned, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")).hexdigest()
    if manifest.get("manifest_sha256") != config["manifest_sha256"]:
        raise ValueError("invalid manifest hash")
    config["corpus_sha256"] = digest(config["corpus_path"])
    if config["corpus_sha256"] != manifest.get("source_sha256"):
        raise ValueError("corpus and manifest differ")
    config["protocol_sha256"] = digest(config["protocol_path"])
    schedule = validate_config(config)
    if config["split"] != "train":
        raise ValueError("training split required")
    if config.get("train_codepoint_budget") != manifest["splits"]["train"]["codepoints"]:
        raise ValueError("train codepoint budget mismatch")
    if set(config["train_documents"]) != set(manifest["splits"]["train"]["document_ids"]):
        raise ValueError("training document IDs mismatch")
    config["train_executable_sha256"] = digest(config["train_executable_path"])
    config["eval_executable_sha256"] = digest(config["eval_executable_path"])
    config["eval_script_sha256"] = digest(config["eval_script_path"])
    encoder_hash = digest(config["encoder_path"])
    config["encoder_sha256"] = {v: encoder_hash for v in config["variants"]}
    config["tokenizer_sha256"] = {}
    tables = {}
    for variant in config["variants"]:
        blob = Path(config["tokenizer_paths"][variant]).read_bytes()
        if len(blob) > 16 * 1024 * 1024:
            raise ValueError("tokenizer too large")
        h = hashlib.sha256(blob).hexdigest()
        config["tokenizer_sha256"][variant] = h
        tables[variant] = TokenTable(variant, blob, h)
    config["checkpoint_sha256"] = {}
    for variant, seed in schedule:
        key = variant + ":" + str(seed)
        path = config["initial_checkpoint_paths"][key]
        h = digest(path)
        m = metadata(Path(path).read_bytes())
        if (m["tokenizer_version"] != config["tokenizer_sha256"][variant]
                or m["arch_version"] != "p4-gpt2-v1"
                or m["step"] != 0 or m["global_step"] != 0
                or m["vocab"] != len(tables[variant].tokens)
                or config["context"] > m["context"]):
            raise ValueError("untrained checkpoint mismatch: " + key)
        config["checkpoint_sha256"][key] = h
    config["eval_input_sha256"] = {}
    for split in ("val", "test"):
        path = config["eval_input_path"][split]
        spec = json.loads(Path(path).read_bytes())
        if spec.get("split") != split or spec.get("manifest") != manifest:
            raise ValueError("evaluation spec and manifest mismatch: " + split)
        if (set(spec.get("documents", {})) != set(manifest["splits"][split]["document_ids"])
                or digest(spec["encoder_path"]) != spec["encoder_sha256"]
                or spec["encoder_sha256"] != encoder_hash
                or digest(spec["scorer_path"]) != spec["scorer_sha256"]
                or spec["context"] != config["context"] or spec["stride"] != config["stride"]):
            raise ValueError("evaluation worker configuration mismatch: " + split)
        for variant in config["variants"]:
            tok = spec["tokenizers"][variant]
            if (tok["sha256"] != config["tokenizer_sha256"][variant]
                    or digest(tok["path"], 16 * 1024 * 1024) != tok["sha256"]):
                raise ValueError("evaluation tokenizer mismatch: " + variant)
        config["eval_input_sha256"][split] = digest(path, 16 * 1024 * 1024)
    keys = {v + ":" + str(s) for v, s in schedule}
    if set(config["initial_checkpoint_paths"]) != keys or set(config["output_checkpoints"]) != keys:
        raise ValueError("checkpoint path mapping mismatch")
    expected_reports = {key + ":" + split for key in keys for split in ("val", "test")}
    if set(config["output_reports"]) != expected_reports:
        raise ValueError("report path mapping mismatch")
    outputs = list(config["output_checkpoints"].values()) + list(config["output_reports"].values()) + [config["ledger_path"]]
    if (len(set(outputs)) != len(outputs)
            or any(not isinstance(p, str) or not Path(p).is_absolute() or str(Path(p)) != p
                   or Path(p).exists() or Path(p).is_symlink() for p in outputs)):
        raise ValueError("output paths must be unique, absolute and absent")
    if not isinstance(config.get("validation_decision"), str) or not config["validation_decision"].strip():
        raise ValueError("preregistered validation decision required")
    return config


def main():
    parser = argparse.ArgumentParser(description="Prepare P4 config; no training or approval")
    parser.add_argument("template")
    parser.add_argument("output")
    args = parser.parse_args()
    template = json.loads(Path(args.template).read_bytes())
    config = prepare(template)
    data = json.dumps(config, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")
    with open(args.output, "xb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    print("config_sha256=" + hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    main()
