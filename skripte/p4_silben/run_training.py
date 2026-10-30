"""P4 one-variant/seed orchestration, disabled until reviewed gate is pinned.

The trusted-local gate, isolated single-GPU ownership, non-detaching worker,
immutable binary snapshot and non-rollbackable ledger are mandatory external
preconditions. Wall-time reservations are not GPU telemetry. This module
must not be used to override SIL-10, rights or platform test failures.
"""
import hashlib
import json
import os
from pathlib import Path
import tempfile

from skripte.p4_silben import experiment_ledger, harness
from skripte.p4_silben.checkpoint_metadata import metadata
from skripte.p4_silben.prepare_run import prepare_run
from skripte.p4_silben.publish_checkpoint import publish_checkpoint
from skripte.p4_silben.wall_budget import WallBudget


def _read_limited(path, maximum):
    with open(path, "rb") as f:
        data = f.read(maximum + 1)
    if not data or len(data) > maximum:
        raise ValueError("artifact size invalid")
    return data


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def execute_run(config, evidence, manifest, train_documents, *, variant, seed,
                encoder, tokenizer_path, checkpoint_path, train_executable,
                destination, ledger_path, wall_budget):
    """Validate and execute exactly one variant/seed run.

    This is currently gated: APPROVED_GATE_SHA256 is not set and the Moo
    trainer has its own hard stop. The trusted caller must prove the provided
    encoder and executables are from the certified read-only snapshot.
    """
    schedule = harness.require_ready(config, evidence)
    if (variant, seed) not in schedule:
        raise ValueError("run not in approved schedule")
    if (not isinstance(wall_budget, WallBudget) or wall_budget.max_runs > 9
            or wall_budget.per_run > config["gpu_seconds_per_run"]
            or wall_budget.total > config["gpu_seconds_total"]):
        raise ValueError("invalid wall budget")
    tokenizer_blob = _read_limited(tokenizer_path, 16 * 1024 * 1024)
    checkpoint_blob = _read_limited(checkpoint_path, 512 * 1024 * 1024)
    executable = Path(train_executable).resolve(strict=True)
    if _sha(_read_limited(executable, 512 * 1024 * 1024)) != config.get("train_executable_sha256"):
        raise ValueError("trainer executable hash mismatch")
    payload, payload_sha = prepare_run(config, manifest, train_documents,
                                       tokenizer_blob, checkpoint_blob,
                                       variant=variant, seed=seed, encode=encoder)
    package = json.loads(payload)
    if package["original_codepoints"] != config["train_codepoint_budget"]:
        raise ValueError("train codepoint budget mismatch")
    old_meta = metadata(checkpoint_blob)
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise FileExistsError("checkpoint output already exists")
    # Reserve before launching; never refund on failure or restart.
    if str(Path(ledger_path)) != config.get("ledger_path"):
        raise ValueError("ledger path differs from certified experiment")
    experiment_ledger.reserve(config, variant=variant, seed=seed, seconds=wall_budget.per_run)
    with tempfile.TemporaryDirectory(prefix=".p4-stage-", dir=destination.parent) as tmp:
        stage = Path(tmp)
        windows_path = stage / "windows.json"
        token_path = stage / "tokenizer.bin"
        checkpoint_in = stage / "input.mook"
        checkpoint_out = stage / "output.mook"
        for path, data in ((windows_path, payload), (token_path, tokenizer_blob),
                           (checkpoint_in, checkpoint_blob)):
            with open(path, "xb") as f:
                os.fchmod(f.fileno(), 0o600)
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
        env = os.environ.copy()
        env.update(harness.device_environment(config))
        env.update(P4_TOKENIZER_KIND=variant, P4_TOKENIZER_PATH=str(token_path),
                   P4_CHECKPOINT=str(checkpoint_in), P4_CHECKPOINT_OUT=str(checkpoint_out),
                   P4_WINDOWS_PATH=str(windows_path), P4_WINDOWS_SHA256=payload_sha,
                   P4_MANIFEST_SHA256=config["manifest_sha256"])
        result = wall_budget.run([str(executable)], cwd=str(stage), env=env)
        if result["timed_out"] or result["returncode"] != 0:
            raise RuntimeError("trainer failed: rc=%s timed_out=%s stderr=%s" % (result["returncode"], result["timed_out"], result.get("stderr", "")[:2048]))
        # Open only the staging file and validate before publishing. No attempt
        # is made to treat a timeout or missing file as a successful run.
        output = _read_limited(checkpoint_out, 512 * 1024 * 1024)
        new_meta = metadata(output)
        if (new_meta["tokenizer_version"] != _sha(tokenizer_blob)
                or new_meta["arch_version"] != "p4-gpt2-v1"
                or new_meta["vocab"] != old_meta["vocab"]
                or new_meta["context"] != old_meta["context"]
                or new_meta["architecture"] != old_meta["architecture"]
                or new_meta["step"] != old_meta["step"] + len(package["windows"])
                or new_meta["global_step"] != old_meta["global_step"] + len(package["windows"])):
            raise ValueError("checkpoint identity or step count mismatch")
        # Recheck original artifacts: a trusted immutable snapshot is still
        # required because before/after hashes do not prevent a transient swap.
        if (_sha(_read_limited(tokenizer_path, 16 * 1024 * 1024)) != _sha(tokenizer_blob)
                or _sha(_read_limited(checkpoint_path, 512 * 1024 * 1024)) != _sha(checkpoint_blob)
                or _sha(_read_limited(executable, 512 * 1024 * 1024)) != config["train_executable_sha256"]):
            raise ValueError("input snapshot changed")
        output_hash = _sha(output)
        publish_checkpoint(checkpoint_out, destination, output_hash)
        experiment_ledger.complete(config, variant=variant, seed=seed,
                                   checkpoint_path=destination, checkpoint_sha256=output_hash)
        return {"variant": variant, "seed": seed, "initial_checkpoint_sha256": _sha(checkpoint_blob),
                "checkpoint_sha256": output_hash, "payload_sha256": payload_sha,
                "tokenizer_sha256": _sha(tokenizer_blob), "manifest_sha256": config["manifest_sha256"],
                "windows": len(package["windows"]), "wall_seconds": result["wall_seconds"]}
