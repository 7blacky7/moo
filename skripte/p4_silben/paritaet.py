"""SIL-11: same trained in-memory model versus saved and reloaded checkpoint."""
import base64
import hashlib
import json
import os
from pathlib import Path
import time

from skripte.p4_silben.moo_encoder import MooEncoder
from skripte.p4_silben.prepare_run import prepare_run
from skripte.p4_silben.token_bytes import TokenTable
from skripte.p4_silben.wall_budget import WallBudget

ROOT = Path.cwd()
BASE = ROOT / "beispiele/p4_silben/run_sil11_20261030_resume"
OUT = ROOT / "beispiele/p4_silben/run_sil11_paritaet_20261030"
PROMPTS = ("Am Morgen ", "Die Kinder ", "Im Garten ")
def sha(data):
    return hashlib.sha256(data).hexdigest()
def save(path, data):
    with path.open("xb") as f:
        f.write(data)
def main():
    budget = WallBudget(max_runs=3, per_run=300, total=1795)  # includes first failed task within 1800 s
    config = json.loads((BASE / "config.json").read_bytes())
    manifest_raw = Path(config["manifest_path"]).read_bytes()
    manifest = json.loads(manifest_raw)
    if config["manifest_sha256"] != "8e74c14e607844e2172b49534edb131b07bada24320bc035554348d6ff98c80f":
        raise ValueError("unexpected manifest identity")
    # prepare_run -> prepare_train_set -> prepare_windows validates the
    # canonical unsigned manifest against this pinned hash before training.
    documents = {k: Path(v).read_bytes() for k, v in config["train_documents"].items()}
    assert sha(Path(config["corpus_path"]).read_bytes()) == config["corpus_sha256"]
    os.environ["TMPDIR"] = str(OUT)
    import tempfile
    tempfile.tempdir = str(OUT)
    results = []
    for variant in ("char", "bpe", "syllable"):
        if time.monotonic() - budget.started >= 1800:
            raise RuntimeError("Gesamtbudget erreicht")
        token_path = Path(config["tokenizer_paths"][variant])
        blob = token_path.read_bytes()
        token_hash = sha(blob)
        assert token_hash == config["tokenizer_sha256"][variant]
        key = variant + ":11"
        initial = Path(config["initial_checkpoint_paths"][key])
        initial_blob = initial.read_bytes()
        run_config = dict(config)
        run_config["checkpoint_sha256"] = dict(config["checkpoint_sha256"])
        run_config["checkpoint_sha256"][key] = sha(initial_blob)
        enc = MooEncoder(config["encoder_path"], variant, token_path,
                         executable_sha256=config["encoder_sha256"][variant],
                         tokenizer_sha256=token_hash)
        payload, payload_hash = prepare_run(run_config, manifest, documents, blob, initial_blob,
                                            variant=variant, seed=11, encode=enc)
        table = TokenTable(variant, blob, token_hash)
        req = {"prompts": [list(enc(blob, p.encode())[0]) for p in PROMPTS],
               "allowed": [i for i, b in enumerate(table.tokens) if b and b"\x00" not in b]}
        if not req["allowed"]:
            raise ValueError("no generatable tokens")
        windows = OUT / (variant + "-windows.json")
        requests = OUT / (variant + "-prompts.json")
        before = OUT / (variant + "-before.json")
        after = OUT / (variant + "-after.json")
        ckpt = OUT / (variant + "-11.mook")
        save(windows, payload)
        save(requests, json.dumps(req, separators=(",", ":")).encode())
        env = os.environ.copy()
        env.update(MOO_KI_GPU="1", MOO_KI_GPU_ERZWINGEN="1", MOO_KI_GPU_STRIKT="1",
                   P4_TOKENIZER_KIND=variant, P4_TOKENIZER_PATH=str(token_path),
                   P4_CHECKPOINT=str(initial), P4_CHECKPOINT_OUT=str(ckpt),
                   P4_WINDOWS_PATH=str(windows), P4_WINDOWS_SHA256=payload_hash,
                   P4_MANIFEST_SHA256=config["manifest_sha256"],
                   P4_GENERATION_PATH=str(requests), P4_BEFORE_PATH=str(before),
                   P4_AFTER_PATH=str(after))
        run = budget.run([str(OUT / "train_paritaet")], cwd=str(ROOT), env=env)
        if run["timed_out"] or run["returncode"] != 0:
            raise RuntimeError("Abbruch %s: %r" % (variant, run))
        pre = json.loads(before.read_bytes())
        post = json.loads(after.read_bytes())
        if pre["training_ms"] > 300000:
            raise RuntimeError("Trainingsbudget überschritten")
        cases = []
        for prompt, a, b in zip(PROMPTS, pre["ids"], post["ids"], strict=True):
            if len(a) != 12 or len(b) != 12:
                raise ValueError("falsche Tokenzahl")
            raw_a = b"".join(table.tokens[int(i)] for i in a)
            raw_b = b"".join(table.tokens[int(i)] for i in b)
            def valid(raw):
                try:
                    raw.decode("utf-8", "strict")
                    return True
                except UnicodeDecodeError:
                    return False
            first = next((i for i, (x, y) in enumerate(zip(raw_a, raw_b)) if x != y), None)
            if first is None and len(raw_a) != len(raw_b):
                first = min(len(raw_a), len(raw_b))
            cases.append({"prompt": prompt, "before_base64": base64.b64encode(raw_a).decode(),
                          "after_base64": base64.b64encode(raw_b).decode(),
                          "equal": raw_a == raw_b, "first_differing_byte": first,
                          "before_utf8_valid": valid(raw_a), "after_utf8_valid": valid(raw_b),
                          "before_token_ids": a, "after_token_ids": b})
        result = {"variant": variant, "seed": 11, "training_ms": pre["training_ms"],
                  "gpu_statistics": pre["gpu"], "checkpoint_sha256": sha(ckpt.read_bytes()),
                  "tokenizer_sha256": token_hash, "windows_sha256": payload_hash,
                  "wall_seconds": run["wall_seconds"], "cases": cases}
        save(OUT / (variant + "-result.json"), json.dumps(result, ensure_ascii=False, indent=2).encode())
        results.append(result)
        if not all(c["equal"] for c in cases):
            raise RuntimeError("Parität ungleich; weitere Läufe abgebrochen")
    elapsed = time.monotonic() - budget.started
    if elapsed > 1800:
        raise RuntimeError("Gesamtbudget überschritten")
    save(OUT / "summary.json", json.dumps({"total_wall_seconds": elapsed, "runs": results}, ensure_ascii=False, indent=2).encode())
if __name__ == "__main__":
    main()
