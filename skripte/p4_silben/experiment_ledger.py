"""Experiment-scoped conservative wall-budget ledger; POSIX trusted storage only.

Not GPU telemetry, rollback protection, process isolation, or checkpoint recovery.
An interrupted reservation remains spent and can never be retried under the same ID.
"""
import fcntl
import hashlib
import json
import math
import os
import stat
from pathlib import Path

MAX_BYTES = 1024 * 1024

def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")

def config_hash(config):
    return hashlib.sha256(canonical(config)).hexdigest()

def header(config):
    from skripte.p4_silben.harness import validate_config
    schedule = validate_config(config)
    path = config.get("ledger_path")
    if not isinstance(path, str) or not Path(path).is_absolute() or str(Path(path)) != path:
        raise ValueError("absolute canonical ledger path required")
    return {"format": "moo-p4-experiment-ledger-v1", "config_sha256": config_hash(config),
            "schedule": [[v, s] for v, s in schedule],
            "per_run": math.floor(config["gpu_seconds_per_run"]),
            "total": math.floor(config["gpu_seconds_total"])}

def create(config):
    h = header(config)
    fd = os.open(config["ledger_path"], os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                 getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        data = canonical(h) + b"\n"
        while data:
            n = os.write(fd, data)
            if n <= 0:
                raise OSError("short header write")
            data = data[n:]
        os.fsync(fd)
    finally:
        os.close(fd)
    # The caller must fsync and retain control of the parent directory.
    parent = os.open(str(Path(config["ledger_path"]).parent), os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(parent)
    finally:
        os.close(parent)

def reserve(config, *, variant, seed, seconds, kind="train"):
    h = header(config)
    run = [kind, variant, seed]
    if kind not in ("train", "eval", "eval_val", "eval_test") or [variant, seed] not in h["schedule"] or type(seed) is not int:
        raise ValueError("run not in schedule")
    if type(seconds) not in (int, float) or not math.isfinite(seconds):
        raise ValueError("invalid reservation")
    amount = math.ceil(seconds)
    if amount <= 0 or amount > (h["per_run"] if kind == "train" else h["total"]):
        raise ValueError("run reservation exceeds limit")
    fd = os.open(config["ledger_path"], os.O_RDWR | getattr(os, "O_NOFOLLOW", 0))
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > MAX_BYTES:
            raise ValueError("invalid ledger")
        with os.fdopen(os.dup(fd), "rb") as stream:
            data = stream.read(MAX_BYTES + 1)
        if not data.endswith(b"\n"):
            raise ValueError("partial ledger")
        lines = data.splitlines()
        if not lines or lines[0] != canonical(h):
            raise ValueError("ledger belongs to another experiment")
        previous = hashlib.sha256(lines[0]).hexdigest()
        seen = set()
        total = 0
        for raw in lines[1:]:
            item = json.loads(raw)
            if not isinstance(item, dict) or set(item) != {"run", "seconds", "previous"}:
                raise ValueError("invalid ledger entry")
            old = item["run"]
            if (raw != canonical(item) or item["previous"] != previous
                    or not isinstance(old, list) or len(old) != 3
                    or old[0] not in ("train", "eval", "eval_val", "eval_test") or old[1:] not in h["schedule"]
                    or type(old[2]) is not int or tuple(old) in seen
                    or type(item["seconds"]) is not int or not 0 < item["seconds"] <= (h["per_run"] if old[0] == "train" else h["total"])):
                raise ValueError("corrupt ledger chain")
            seen.add(tuple(old))
            total += item["seconds"]
            if total > h["total"]:
                raise ValueError("ledger exceeds budget")
            previous = hashlib.sha256(raw).hexdigest()
        if tuple(run) in seen:
            raise ValueError("run already reserved")
        if kind in ("eval", "eval_val", "eval_test"):
            if ("train", variant, seed) not in seen:
                raise ValueError("evaluation requires a prior training reservation")
            verify_complete(config, variant=variant, seed=seed)
        if total + amount > h["total"]:
            raise RuntimeError("experiment budget exhausted")
        encoded = canonical({"run": run, "seconds": amount, "previous": previous})
        os.lseek(fd, 0, os.SEEK_END)
        data = encoded + b"\n"
        while data:
            n = os.write(fd, data)
            if n <= 0:
                raise OSError("short ledger write")
            data = data[n:]
        os.fsync(fd)
        return hashlib.sha256(encoded).hexdigest()
    finally:
        os.close(fd)



def _checkpoint_path(config, variant, seed):
    paths = config.get("output_checkpoints")
    path = paths.get(variant + ":" + str(seed)) if isinstance(paths, dict) else None
    if not isinstance(path, str) or not Path(path).is_absolute() or str(Path(path)) != path:
        raise ValueError("certified checkpoint path required")
    return path

def _marker_path(config, variant, seed):
    return config["ledger_path"] + "." + variant + "-" + str(seed) + ".complete"

def _checkpoint_sha(path):
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size > 512 * 1024 * 1024:
            raise ValueError("invalid checkpoint")
        digest = hashlib.sha256()
        while True:
            block = os.read(fd, 1024 * 1024)
            if not block:
                return digest.hexdigest()
            digest.update(block)
    finally:
        os.close(fd)

def complete(config, *, variant, seed, checkpoint_path, checkpoint_sha256):
    """Record only an already published, validated checkpoint; fail closed on crash."""
    h = header(config)
    if [variant, seed] not in h["schedule"] or type(seed) is not int:
        raise ValueError("run not in schedule")
    expected = _checkpoint_path(config, variant, seed)
    if str(Path(checkpoint_path)) != expected or _checkpoint_sha(expected) != checkpoint_sha256:
        raise ValueError("checkpoint does not match certified destination")
    marker = _marker_path(config, variant, seed)
    record = {"format": "moo-p4-complete-v1", "config_sha256": h["config_sha256"],
              "variant": variant, "seed": seed, "checkpoint_path": expected,
              "checkpoint_sha256": checkpoint_sha256}
    fd = os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                 getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        data = canonical(record) + b"\n"
        while data:
            count = os.write(fd, data)
            if count <= 0:
                raise OSError("short completion write")
            data = data[count:]
        os.fsync(fd)
    finally:
        os.close(fd)
    directory = os.open(str(Path(marker).parent), os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(directory)
    finally:
        os.close(directory)

def verify_complete(config, *, variant, seed):
    expected = _checkpoint_path(config, variant, seed)
    marker = _marker_path(config, variant, seed)
    fd = os.open(marker, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size > 8192:
            raise ValueError("invalid completion record")
        with os.fdopen(os.dup(fd), "rb") as stream:
            raw = stream.read(8193)
    finally:
        os.close(fd)
    record = json.loads(raw)
    if (raw != canonical(record) + b"\n" or not isinstance(record, dict)
            or set(record) != {"format", "config_sha256", "variant", "seed", "checkpoint_path", "checkpoint_sha256"}
            or record["format"] != "moo-p4-complete-v1"
            or record["config_sha256"] != config_hash(config)
            or record["variant"] != variant or type(record["seed"]) is not int or record["seed"] != seed
            or record["checkpoint_path"] != expected
            or record["checkpoint_sha256"] != _checkpoint_sha(expected)):
        raise ValueError("checkpoint completion record mismatch")
    return record
