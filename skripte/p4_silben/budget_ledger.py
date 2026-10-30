"""Crash-conservative reservation ledger for P4 wall-time budgets (POSIX).

Reservations are never refunded; a crash or short run consumes the full
reservation. This only accounts for trusted process wall time. It is NOT GPU
telemetry or enforcement against detached/unrelated processes. The enclosing
experiment remains fail-closed pending those independent guarantees.
"""
import fcntl
import hashlib
import json
import math
import os
import stat

MAX_BYTES = 1024 * 1024


def _canonical(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("utf-8")


def create(path):
    """Create a new immutable-header ledger; do not overwrite."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        with os.fdopen(fd, "wb") as f:
            header = {"format": "moo-p4-wall-ledger-v1", "runs": 9,
                      "per_run_seconds": 600, "total_seconds": 7200}
            f.write(_canonical(header) + b"\n")
            f.flush()
            os.fsync(f.fileno())
    except BaseException:
        try:
            os.unlink(path)
        except OSError:
            pass
        raise


def reserve(path, *, kind, seconds):
    """Durably reserve whole seconds under an exclusive advisory lock.

    kind=train spends one of nine runs with at most 600 s; eval spends only
    total budget. Returns the new chain SHA256. Caller must reserve BEFORE a
    worker is launched. Crash-damaged or non-canonical ledgers fail closed.
    """
    if kind not in ("train", "eval") or type(seconds) not in (int, float) or not math.isfinite(seconds):
        raise ValueError("invalid reservation")
    amount = math.ceil(seconds)
    if amount <= 0 or (kind == "train" and amount > 600) or amount > 7200:
        raise ValueError("reservation outside limits")
    flags = os.O_RDWR | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > MAX_BYTES:
            raise ValueError("invalid ledger file")
        with os.fdopen(os.dup(fd), "rb") as f:
            content = f.read(MAX_BYTES + 1)
        if not content.endswith(b"\n"):
            raise ValueError("partial ledger")
        lines = content.splitlines()
        header = {"format": "moo-p4-wall-ledger-v1", "runs": 9,
                  "per_run_seconds": 600, "total_seconds": 7200}
        if not lines or lines[0] != _canonical(header):
            raise ValueError("invalid ledger header")
        previous = hashlib.sha256(lines[0]).hexdigest()
        total = 0
        count = 0
        for raw in lines[1:]:
            entry = json.loads(raw)
            if not isinstance(entry, dict) or set(entry) != {"kind", "seconds", "previous"}:
                raise ValueError("invalid ledger entry")
            if (raw != _canonical(entry) or entry["previous"] != previous
                    or entry["kind"] not in ("train", "eval")
                    or type(entry["seconds"]) is not int or entry["seconds"] <= 0
                    or (entry["kind"] == "train" and entry["seconds"] > 600)):
                raise ValueError("corrupt ledger chain")
            total += entry["seconds"]
            count += entry["kind"] == "train"
            if total > 7200 or count > 9:
                raise ValueError("ledger exceeds ceiling")
            previous = hashlib.sha256(raw).hexdigest()
        if total + amount > 7200 or (kind == "train" and count >= 9):
            raise RuntimeError("budget exhausted")
        entry = {"kind": kind, "seconds": amount, "previous": previous}
        encoded = _canonical(entry)
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
