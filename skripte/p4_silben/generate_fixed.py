"""Post-hoc fixed-prompt CPU generation and checkpoint reload repeatability."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time

from skripte.p4_silben.experiment_ledger import verify_complete
from skripte.p4_silben.experiment_manifest import write_new
from skripte.p4_silben.harness import validate_config
from skripte.p4_silben.moo_encoder import MooEncoder
from skripte.p4_silben.moo_scorer import MooBatchScorer
from skripte.p4_silben.run_experiment import verify_validation_freeze
from skripte.p4_silben.token_bytes import TokenTable

PROMPTS = ("Am Morgen ", "Die Kinder ", "Im Garten ")
GENERATED_TOKENS = 12


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def generate(config, variant, seed, scorer_path):
    key = variant + ":" + str(seed)
    completion = verify_complete(config, variant=variant, seed=seed)
    token_path = config["tokenizer_paths"][variant]
    token_blob = Path(token_path).read_bytes()
    token_hash = config["tokenizer_sha256"][variant]
    table = TokenTable(variant, token_blob, token_hash)
    encoder = MooEncoder(config["encoder_path"], variant, token_path,
                         executable_sha256=config["encoder_sha256"][variant],
                         tokenizer_sha256=token_hash)
    initial = [list(encoder(token_blob, p.encode("utf-8"))[0]) for p in PROMPTS]
    checkpoint = config["output_checkpoints"][key]
    scorer = MooBatchScorer(scorer_path, variant, token_path, checkpoint,
                            executable_sha256=sha(scorer_path), tokenizer_sha256=token_hash,
                            checkpoint_sha256=completion["checkpoint_sha256"])
    prefixes = [list(p) for p in initial]
    generated = [[] for _ in PROMPTS]
    allowed = [i for i, b in enumerate(table.tokens) if b and b"\0" not in b]
    for _ in range(GENERATED_TOKENS):
        # At most context-1 tokens; scorer prepends BOS only at document start.
        requests = [(p[-(config["context"] - 1):], len(p)) for p in prefixes]
        rows = scorer.score_batch(requests)
        for i, row in enumerate(rows):
            if len(row) != len(table.tokens):
                raise ValueError("logit vocabulary mismatch")
            token = max(allowed, key=lambda j: (row[j], -j))
            generated[i].append(token)
            prefixes[i].append(token)
    results = []
    for prompt, ids in zip(PROMPTS, generated):
        raw = b"".join(table.tokens[i] for i in ids)
        results.append({"prompt": prompt, "token_ids": ids,
                        "continuation_base64": base64.b64encode(raw).decode("ascii"),
                        "continuation_utf8_replacement": raw.decode("utf-8", errors="replace")})
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config")
    parser.add_argument("output")
    parser.add_argument("temp_dir")
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_bytes())
    schedule = validate_config(config)
    verify_validation_freeze(config, schedule)
    spec = json.loads(Path(config["eval_input_path"]["val"]).read_bytes())
    scorer_path = spec["scorer_path"]
    if sha(scorer_path) != spec["scorer_sha256"]:
        raise ValueError("scorer hash mismatch")
    tmp = Path(args.temp_dir)
    if not tmp.is_absolute() or not tmp.is_dir() or tmp.is_symlink():
        raise ValueError("existing private absolute temp directory required")
    os.environ.update(MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0", MOO_KI_GPU_STRIKT="0")
    tempfile.tempdir = str(tmp)
    started = time.monotonic()
    cases = {}
    for variant, seed in schedule:
        first = generate(config, variant, seed, scorer_path)
        second = generate(config, variant, seed, scorer_path)
        if first != second:
            raise ValueError("checkpoint reload generation mismatch: " + variant + ":" + str(seed))
        cases[variant + ":" + str(seed)] = first
    report = {
        "method": "post-hoc fixed prompts; CPU greedy; 12 tokens; NUL-containing tokens excluded",
        "prompts_selected_after_test": True,
        "reload_vs_reload_equal": True,
        "in_memory_before_save_comparison": "not captured and cannot be reconstructed",
        "scorer_sha256": spec["scorer_sha256"],
        "config_sha256": sha(args.config),
        "elapsed_seconds": time.monotonic() - started,
        "cases": cases,
    }
    print("generation_report_sha256", write_new(args.output, report))


if __name__ == "__main__":
    main()
