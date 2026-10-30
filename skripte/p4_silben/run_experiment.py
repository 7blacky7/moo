"""Sequential P4 experiment coordinator. Gate must be approved before any worker.

Trusted worker executables, private paths and no manually detached processes
are required. Wall time upper-bounds GPU use only for this isolated single GPU
and only while the worker/process group owns all work.
"""
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile
import time

from skripte.p4_silben import experiment_ledger, harness, run_training
from skripte.p4_silben.wall_budget import WallBudget
from skripte.p4_silben.publish_checkpoint import publish_checkpoint


def _sha_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for part in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(part)
    return digest.hexdigest()


def _validation_record(config, schedule):
    decision = config.get("validation_decision")
    if not isinstance(decision, str) or not decision.strip():
        raise ValueError("preregistered validation decision required")
    hashes = {}
    for variant, seed in schedule:
        key = variant + ":" + str(seed)
        completed = experiment_ledger.verify_complete(config, variant=variant, seed=seed)
        path = config["output_reports"][key + ":val"]
        raw = Path(path).read_bytes()
        if len(raw) > 16 * 1024 * 1024:
            raise ValueError("validation report too large")
        report = json.loads(raw)
        if (report.get("variant") != variant or type(report.get("seed")) is not int
                or report["seed"] != seed or report.get("split") != "val"
                or report.get("manifest_sha256") != config["manifest_sha256"]
                or report.get("checkpoint_sha256") != completed["checkpoint_sha256"]
                or report.get("tokenizer_sha256") != config["tokenizer_sha256"][variant]):
            raise ValueError("validation report identity mismatch")
        hashes[key] = hashlib.sha256(raw).hexdigest()
    return {"format": "moo-p4-validation-freeze-v1",
            "config_sha256": experiment_ledger.config_hash(config),
            "decision": decision, "validation_report_sha256": hashes}


def freeze_validation(config, schedule):
    """Explicitly fix the preregistered decision and all validation reports."""
    fd = os.open(config["ledger_path"] + ".experiment.lock",
                 os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        record = _validation_record(config, schedule)
        path = config["ledger_path"] + ".validation.complete"
        out = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                      getattr(os, "O_NOFOLLOW", 0), 0o600)
        try:
            data = experiment_ledger.canonical(record) + b"\n"
            while data:
                n = os.write(out, data)
                if n <= 0:
                    raise OSError("short freeze record write")
                data = data[n:]
            os.fsync(out)
        finally:
            os.close(out)
        directory = os.open(str(Path(path).parent), os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        os.close(fd)


def verify_validation_freeze(config, schedule):
    path = Path(config["ledger_path"] + ".validation.complete")
    raw = path.read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError("validation freeze too large")
    actual = json.loads(raw)
    if raw != experiment_ledger.canonical(actual) + b"\n" or actual != _validation_record(config, schedule):
        raise ValueError("validation freeze missing or changed")




def run_experiment(config, evidence, manifest, train_documents, *, encoders,
                   tokenizer_paths, initial_checkpoints, train_executable,
                   eval_executable, eval_input, eval_script, phase="val"):
    """Run the certified schedule; each result is published before evaluation.

    Evaluation executable contract: reads P4_EVAL_INPUT, P4_CHECKPOINT and
    P4_EVAL_OUTPUT, exits zero only after writing JSON with the matching
    checkpoint_sha256, manifest_sha256, variant and seed. This contract and its
    executable are reviewed and pinned as part of the certified snapshot.
    """
    schedule = harness.require_ready(config, evidence)
    if not isinstance(config.get("ledger_path"), str):
        raise ValueError("certified ledger path required")
    if phase not in ("val", "test"):
        raise ValueError("invalid evaluation phase")
    evaluation_seconds = config.get("eval_seconds_per_run")
    if (type(evaluation_seconds) not in (int, float) or not math.isfinite(evaluation_seconds)
            or not 0 < evaluation_seconds <= min(600, config["gpu_seconds_total"])):
        raise ValueError("invalid evaluation budget")
    if len(schedule) * (math.ceil(config["gpu_seconds_per_run"]) + 2 * math.ceil(evaluation_seconds)) > math.floor(config["gpu_seconds_total"]):
        raise ValueError("reserved training and evaluation budgets exceed total")
    reports = config.get("output_reports")
    if (not isinstance(reports, dict) or set(reports) != {v + ":" + str(s) + ":" + split for v, s in schedule for split in ("val", "test")}
            or any(not isinstance(p, str) or not Path(p).is_absolute() or str(Path(p)) != p for p in reports.values())
            or len(set(reports.values())) != len(reports)):
        raise ValueError("unique absolute report destinations required")
    if (_sha_file(eval_executable) != config.get("eval_executable_sha256")
            or _sha_file(eval_input) != config.get("eval_input_sha256", {}).get(phase)
            or _sha_file(eval_script) != config.get("eval_script_sha256")):
        raise ValueError("evaluation executable or input hash mismatch")
    lock_path = config["ledger_path"] + ".experiment.lock"
    fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("another experiment instance is active")
        if phase == "test":
            verify_validation_freeze(config, schedule)
        started = time.monotonic()
        trainer_budget = WallBudget(max_runs=9, per_run=config["gpu_seconds_per_run"],
                                    total=config["gpu_seconds_total"])
        results = []
        for variant, seed in schedule:
            remaining = config["gpu_seconds_total"] - (time.monotonic() - started)
            if remaining <= 0:
                raise RuntimeError("experiment wall budget exhausted")
            key = variant + ":" + str(seed)
            destination = config["output_checkpoints"][key]
            if phase == "val":
                result = run_training.execute_run(
                    config, evidence, manifest, train_documents, variant=variant, seed=seed,
                    encoder=encoders[variant], tokenizer_path=tokenizer_paths[variant],
                    checkpoint_path=initial_checkpoints[key], train_executable=train_executable,
                    destination=destination, ledger_path=config["ledger_path"],
                    wall_budget=trainer_budget)
            else:
                completed = experiment_ledger.verify_complete(config, variant=variant, seed=seed)
                result = {"variant": variant, "seed": seed,
                          "checkpoint_sha256": completed["checkpoint_sha256"]}
            remaining = config["gpu_seconds_total"] - (time.monotonic() - started)
            if remaining <= 0:
                raise RuntimeError("experiment wall budget exhausted")
            reservation = min(evaluation_seconds, remaining)
            experiment_ledger.reserve(config, variant=variant, seed=seed,
                                      seconds=reservation, kind="eval_" + phase)
            eval_budget = WallBudget(max_runs=1, per_run=reservation, total=remaining)
            with tempfile.TemporaryDirectory(prefix=".p4-eval-", dir=Path(destination).parent) as tmp:
                report_path = Path(tmp) / "report.json"
                env = os.environ.copy()
                env.update(harness.device_environment(config))
                env.update(P4_EVAL_INPUT=str(Path(eval_input).resolve()),
                           P4_CHECKPOINT=str(Path(destination).resolve()),
                           P4_EVAL_OUTPUT=str(report_path), P4_VARIANT=variant,
                           P4_SEED=str(seed), P4_MANIFEST_SHA256=config["manifest_sha256"],
                           P4_CHECKPOINT_SHA256=result["checkpoint_sha256"])
                env["PYTHONPATH"] = str(Path(__file__).resolve().parents[2])
                outcome = eval_budget.run([str(Path(eval_executable).resolve()),
                                           str(Path(eval_script).resolve())], cwd=tmp, env=env)
                if outcome["timed_out"] or outcome["returncode"] != 0:
                    raise RuntimeError("evaluation failed or timed out")
                if (_sha_file(eval_executable) != config["eval_executable_sha256"]
                        or _sha_file(eval_input) != config["eval_input_sha256"][phase]
                        or _sha_file(eval_script) != config["eval_script_sha256"]):
                    raise ValueError("evaluation input changed")
                if report_path.stat().st_size > 16 * 1024 * 1024:
                    raise ValueError("evaluation report too large")
                report = json.loads(report_path.read_bytes())
                if (not isinstance(report, dict) or report.get("variant") != variant
                        or type(report.get("seed")) is not int or report["seed"] != seed
                        or report.get("manifest_sha256") != config["manifest_sha256"]
                        or report.get("checkpoint_sha256") != result["checkpoint_sha256"]
                        or report.get("tokenizer_sha256") != config["tokenizer_sha256"][variant]
                        or report.get("split") != phase
                        or any(type(report.get(k)) not in (int, float) or not math.isfinite(report[k]) or report[k] < 0
                               for k in ("nll", "nll_per_codepoint", "bits_per_byte"))):
                    raise ValueError("evaluation report identity mismatch")
                report_destination = reports[key + ":" + phase]
                report_hash = _sha_file(report_path)
                publish_checkpoint(report_path, report_destination, report_hash)
                result["evaluation_report_sha256"] = report_hash
                result["evaluation_report_path"] = report_destination
                result["evaluation"] = report
                result["evaluation_wall_seconds"] = outcome["wall_seconds"]
                results.append(result)
        return results
    finally:
        os.close(fd)



def main():
    import argparse
    from skripte.p4_silben.moo_encoder import MooEncoder

    parser = argparse.ArgumentParser(description="Run a certified P4 experiment")
    parser.add_argument("config", help="certified experiment configuration JSON")
    parser.add_argument("certificate", help="pinned review certificate")
    parser.add_argument("--phase", choices=("val", "test"), default="val")
    parser.add_argument("--freeze-validation", action="store_true")
    parser.add_argument("--create-ledger", action="store_true",
                        help="create the new experiment ledger exclusively")
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_bytes())
    evidence = Path(args.certificate).read_bytes()
    harness.require_ready(config, evidence)
    if args.create_ledger:
        experiment_ledger.create(config)
    if args.freeze_validation:
        freeze_validation(config, harness.validate_config(config))
        return
    manifest = json.loads(Path(config["manifest_path"]).read_bytes())
    documents = {name: Path(path).read_bytes()
                 for name, path in config["train_documents"].items()}
    tokenizers = config["tokenizer_paths"]
    encoder = {kind: MooEncoder(config["encoder_path"], kind, tokenizers[kind],
                                executable_sha256=config["encoder_sha256"][kind],
                                tokenizer_sha256=config["tokenizer_sha256"][kind])
               for kind in config["variants"]}
    result = run_experiment(
        config, evidence, manifest, documents, encoders=encoder,
        tokenizer_paths=tokenizers, initial_checkpoints=config["initial_checkpoint_paths"],
        train_executable=config["train_executable_path"],
        eval_executable=config["eval_executable_path"], eval_input=config["eval_input_path"][args.phase],
        eval_script=config["eval_script_path"], phase=args.phase)
    print(json.dumps(result, sort_keys=True, ensure_ascii=False, allow_nan=False))


if __name__ == "__main__":
    main()
