"""Fresh P4 3x3 experiment: one train/validation phase then frozen test."""
import base64
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
from skripte.p4_silben.moo_encoder import MooEncoder
from skripte.p4_silben.prepare_run import prepare_run
from skripte.p4_silben.token_bytes import TokenTable
from skripte.p4_silben.checkpoint_metadata import metadata
from skripte.p4_silben.wall_budget import WallBudget

ROOT = Path.cwd()
BASE = ROOT / "beispiele/p4_silben/run_sil11_20261030_resume"
OUT = ROOT / "beispiele/p4_silben/run_sil11_fix_20261030"
BIN = OUT / "bin"
PROMPTS = ("Am Morgen ", "Die Kinder ", "Im Garten ")
IDENTITY = "8e74c14e607844e2172b49534edb131b07bada24320bc035554348d6ff98c80f"
SCHEDULE = [(v, s) for v in ("char", "bpe", "syllable") for s in (11, 23, 47)]
def digest(data):
    return hashlib.sha256(data).hexdigest()
def sha(path):
    return digest(Path(path).read_bytes())
def save(path, obj):
    data = json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    with Path(path).open("xb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    return digest(data)
def remaining(start):
    left = 1800 - (time.monotonic() - start)
    if left <= 0:
        raise RuntimeError("Gesamtbudget überschritten")
    return left
def execute(argv, env, start, limit):
    budget = WallBudget(max_runs=1, per_run=min(limit, remaining(start)), total=remaining(start))
    result = budget.run(argv, cwd=str(ROOT), env=env)
    if result["timed_out"] or result["returncode"] != 0:
        raise RuntimeError("Worker fehlgeschlagen: %r" % result)
    return result
def valid_utf8(raw):
    try:
        raw.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False
def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("val", "test"):
        raise ValueError("phase val or test required")
    phase = sys.argv[1]
    lock = (OUT / "experiment.lock").open("a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    os.environ["TMPDIR"] = str(OUT)
    tempfile.tempdir = str(OUT)
    config_raw = (BASE / "config.json").read_bytes()
    config = json.loads(config_raw)
    if config["manifest_sha256"] != IDENTITY:
        raise ValueError("wrong manifest identity")
    manifest = json.loads(Path(config["manifest_path"]).read_bytes())
    unsigned = {k: v for k, v in manifest.items() if k != "manifest_sha256"}
    canonical = json.dumps(unsigned, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    if digest(canonical) != IDENTITY or manifest["manifest_sha256"] != IDENTITY:
        raise ValueError("manifest mismatch")
    if sha(config["corpus_path"]) != config["corpus_sha256"]:
        raise ValueError("corpus mismatch")
    artifacts = {name: sha(BIN / name) for name in ("encode", "logits_batch", "train_paritaet")}
    artifacts["runtime_source"] = sha(ROOT / "compiler/runtime/moo_nn_easy.c")
    artifacts["runner_source"] = sha(ROOT / "skripte/p4_silben/run_fix_3x3.py")
    artifacts["adapter_source"] = sha(ROOT / "beispiele/p4_silben/train_paritaet.moos")
    artifacts["eval_source"] = sha(config["eval_script_path"])
    encoder_hash = artifacts["encode"]
    scorer_hash = artifacts["logits_batch"]
    if phase == "val":
        stamp = json.loads((OUT / "start.json").read_bytes())
        start = stamp["monotonic"]
        if stamp["config_sha256"] != digest(config_raw) or stamp["artifacts"] != artifacts:
            raise ValueError("experiment snapshot changed")
        remaining(start)
        save(OUT / "resume.json", {"original_start_monotonic": start,
             "resume_runner_sha256": sha(Path(__file__)), "validation_limit_seconds": 120,
             "reason": "authorised repeat of char-47 validation after 60 s timeout",
             "earlier_timeout_seconds": 60.0548750879243})
    else:
        stamp = json.loads((OUT / "start.json").read_bytes())
        start = stamp["monotonic"]
        if stamp["config_sha256"] != digest(config_raw) or stamp["artifacts"] != artifacts:
            raise ValueError("experiment snapshot changed")
        remaining(start)
        frozen = json.loads((OUT / "freeze.json").read_bytes())
        if frozen["schedule"] != [list(x) for x in SCHEDULE]:
            raise ValueError("freeze schedule mismatch")
        for key, value in frozen["runs"].items():
            if sha(OUT / (key + "-result.json")) != value["result_sha256"] or sha(OUT / (key + "-val.json")) != value["val_sha256"] or sha(OUT / (key + ".mook")) != value["checkpoint_sha256"]:
                raise ValueError("frozen artifact changed")
    docs = {k: Path(v).read_bytes() for k,v in config["train_documents"].items()} if phase == "val" else None
    spec_path = Path(config["eval_input_path"][phase])
    if sha(spec_path) != config["eval_input_sha256"][phase]:
        raise ValueError("evaluation input changed")
    spec = json.loads(spec_path.read_bytes())
    spec["encoder_path"], spec["encoder_sha256"] = str(BIN / "encode"), encoder_hash
    spec["scorer_path"], spec["scorer_sha256"] = str(BIN / "logits_batch"), scorer_hash
    eval_spec = OUT / (phase + "-input.json")
    if not eval_spec.exists():
        save(eval_spec, spec)
    elif json.loads(eval_spec.read_bytes()) != spec:
        raise ValueError("evaluation input changed")
    eval_script = Path(config["eval_script_path"])
    if sha(eval_script) != config["eval_script_sha256"] or sha(config["eval_executable_path"]) != config["eval_executable_sha256"]:
        raise ValueError("evaluation executable changed")
    for variant, seed in SCHEDULE:
        remaining(start)
        key = variant + "-" + str(seed)
        token_path = Path(config["tokenizer_paths"][variant])
        blob = token_path.read_bytes()
        tokhash = digest(blob)
        if tokhash != config["tokenizer_sha256"][variant]:
            raise ValueError("tokenizer mismatch")
        ckpt = OUT / (key + ".mook")
        if phase == "val" and not (OUT / (key + "-result.json")).exists():
            initial = Path(config["initial_checkpoint_paths"][variant + ":" + str(seed)])
            initial_blob = initial.read_bytes()
            run_config = dict(config)
            run_config["encoder_sha256"] = {v: encoder_hash for v in config["variants"]}
            enc = MooEncoder(BIN / "encode", variant, token_path, executable_sha256=encoder_hash, tokenizer_sha256=tokhash)
            payload, phash = prepare_run(run_config, manifest, docs, blob, initial_blob, variant=variant, seed=seed, encode=enc)
            table = TokenTable(variant, blob, tokhash)
            req = {"prompts": [list(enc(blob, p.encode())[0]) for p in PROMPTS],
                   "allowed": [i for i, b in enumerate(table.tokens) if b and b"\x00" not in b]}
            windows = OUT / (key + "-windows.json")
            with windows.open("xb") as f: f.write(payload)
            requests = OUT / (key + "-prompts.json")
            save(requests, req)
            before, after = OUT / (key + "-before.json"), OUT / (key + "-after.json")
            env = os.environ.copy()
            env.update(MOO_KI_GPU="1", MOO_KI_GPU_ERZWINGEN="1", MOO_KI_GPU_STRIKT="1", MOO_KI_CKPT_BF16="0",
                       P4_TOKENIZER_KIND=variant, P4_TOKENIZER_PATH=str(token_path), P4_CHECKPOINT=str(initial),
                       P4_CHECKPOINT_OUT=str(ckpt), P4_WINDOWS_PATH=str(windows), P4_WINDOWS_SHA256=phash,
                       P4_MANIFEST_SHA256=IDENTITY, P4_GENERATION_PATH=str(requests),
                       P4_BEFORE_PATH=str(before), P4_AFTER_PATH=str(after))
            save(OUT / (key + "-train-attempt.json"), {"started_monotonic": time.monotonic()})
            outcome = execute([str(BIN / "train_paritaet")], env, start, 300)
            pre, post = json.loads(before.read_bytes()), json.loads(after.read_bytes())
            old, new = metadata(initial_blob), metadata(ckpt.read_bytes())
            package = json.loads(payload)
            if new["step"] != old["step"] + len(package["windows"]) or new["global_step"] != old["global_step"] + len(package["windows"]):
                raise ValueError("checkpoint step mismatch")
            if pre["gpu"]["submits"] <= 0 or pre["gpu"]["cpu_fallbacks"] != 0:
                raise RuntimeError("GPU use not established")
            cases = []
            for prompt, a, b in zip(PROMPTS, pre["ids"], post["ids"], strict=True):
                if len(a) != 12 or len(b) != 12:
                    raise ValueError("invalid generated token count")
                ra, rb = (b"".join(table.tokens[int(i)] for i in seq) for seq in (a,b))
                first = next((i for i in range(min(len(ra),len(rb))) if ra[i] != rb[i]),
                             min(len(ra),len(rb)) if len(ra) != len(rb) else None)
                cases.append({"prompt":prompt, "before_base64":base64.b64encode(ra).decode(),
                              "after_base64":base64.b64encode(rb).decode(), "equal":ra==rb,
                              "first_differing_byte":first, "before_utf8_valid":valid_utf8(ra),
                              "after_utf8_valid":valid_utf8(rb), "before_ids":a, "after_ids":b})
            result = {"variant":variant, "seed":seed, "training_ms":pre["training_ms"], "gpu":pre["gpu"],
                      "tokenizer_sha256":tokhash, "initial_sha256":digest(initial_blob),
                      "checkpoint_sha256":sha(ckpt), "windows_sha256":phash, "windows":len(package["windows"]),
                      "wall_seconds":outcome["wall_seconds"], "cases":cases}
            save(OUT / (key + "-result.json"), result)
            if not all(c["equal"] for c in cases):
                raise RuntimeError("Paritätsabweichung bei " + key)
        result = json.loads((OUT / (key + "-result.json")).read_bytes())
        if sha(ckpt) != result["checkpoint_sha256"]:
            raise ValueError("checkpoint changed")
        report = OUT / (key + "-" + phase + ".json")
        completed = OUT / (key + "-" + phase + "-complete.json")
        if completed.exists():
            record = json.loads(completed.read_bytes())
            if sha(report) != record["report_sha256"]:
                raise ValueError("completed report changed")
            continue
        attempt = OUT / (key + "-" + phase + "-attempt.json")
        if attempt.exists():
            if key != "char-47" or phase != "val":
                raise RuntimeError("unapproved evaluation retry")
            attempt = OUT / (key + "-val-retry-attempt.json")
        if report.exists():
            raise FileExistsError("partial report exists")
        save(attempt, {"started_monotonic": time.monotonic()})
        env = os.environ.copy()
        env.update(MOO_KI_GPU="1", MOO_KI_GPU_ERZWINGEN="1", MOO_KI_GPU_STRIKT="1", MOO_KI_CKPT_BF16="0",
                   P4_EVAL_INPUT=str(eval_spec), P4_CHECKPOINT=str(ckpt), P4_EVAL_OUTPUT=str(report),
                   P4_VARIANT=variant, P4_SEED=str(seed), P4_MANIFEST_SHA256=IDENTITY,
                   P4_CHECKPOINT_SHA256=result["checkpoint_sha256"], PYTHONPATH=str(ROOT))
        outcome = execute([str(Path(config["eval_executable_path"])), str(eval_script)], env, start, 120 if phase == "val" else 60)
        metrics = json.loads(report.read_bytes())
        if metrics["split"] != phase or metrics["variant"] != variant or metrics["seed"] != seed or metrics["checkpoint_sha256"] != result["checkpoint_sha256"] or metrics["manifest_sha256"] != IDENTITY or metrics["tokenizer_sha256"] != tokhash:
            raise ValueError("invalid evaluation report identity")
        if any(not math.isfinite(metrics[x]) or metrics[x] < 0 for x in ("nll", "nll_per_codepoint", "bits_per_byte")):
            raise ValueError("invalid evaluation metric")
        save(OUT / (key + "-" + phase + "-complete.json"), {"report_sha256":sha(report), "wall_seconds":outcome["wall_seconds"]})
    remaining(start)
    if phase == "val":
        runs = {}
        for variant, seed in SCHEDULE:
            key = variant + "-" + str(seed)
            runs[key] = {"result_sha256":sha(OUT / (key + "-result.json")),
                         "val_sha256":sha(OUT / (key + "-val.json")), "checkpoint_sha256":sha(OUT / (key + ".mook"))}
        save(OUT / "freeze.json", {"schedule":SCHEDULE, "runs":runs,
             "decision":"Alle Varianten und Seeds unverändert übernehmen; keine Testauswahl.",
             "elapsed_seconds":time.monotonic()-start})
    else:
        aggregates = {}
        for variant in ("char","bpe","syllable"):
            reports = [json.loads((OUT / (variant + "-" + str(seed) + "-test.json")).read_bytes()) for seed in (11,23,47)]
            aggregates[variant] = {"mean_bits_per_byte":sum(x["bits_per_byte"] for x in reports)/3,
                                   "mean_nll_per_codepoint":sum(x["nll_per_codepoint"] for x in reports)/3}
        save(OUT / "summary.json", {"test_aggregates":aggregates, "elapsed_seconds":time.monotonic()-start,
                                     "freeze_sha256":sha(OUT / "freeze.json")})
if __name__ == "__main__":
    main()
