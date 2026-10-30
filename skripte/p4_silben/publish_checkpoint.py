"""Exclusive POSIX publication of a validated checkpoint from private staging.

Only a trusted orchestrator may call this, after parsing the checkpoint and
checking its embedded tokenizer/architecture identities. The private staging
and output directories must not be writable by untrusted processes; all write
handles to the staging inode must be closed before publication. A failure after
link creation may leave a destination requiring hash/ledger-based recovery.
This module does not claim GPU, training, or test-gate validation.
"""
import hashlib
import os
import stat


def publish_checkpoint(staged, destination, expected_sha256):
    if os.name != "posix" or not hasattr(os, "O_NOFOLLOW"):
        raise RuntimeError("exclusive publication not supported on this platform")
    if (not isinstance(expected_sha256, str) or len(expected_sha256) != 64
            or any(c not in "0123456789abcdef" for c in expected_sha256)):
        raise ValueError("invalid checkpoint SHA256")
    fd = os.open(staged, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("checkpoint staging file must be a private regular file")
        h = hashlib.sha256()
        while True:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            h.update(block)
        if h.hexdigest() != expected_sha256:
            raise ValueError("checkpoint hash mismatch")
        os.fsync(fd)
        # Hard-link publication is same-filesystem and fails if destination
        # exists; never delete/replace an existing destination.
        os.link(staged, destination, follow_symlinks=False)
        published = os.lstat(destination)
        if (not stat.S_ISREG(published.st_mode) or
                (published.st_dev, published.st_ino) != (info.st_dev, info.st_ino)):
            # Trusted private directory is a precondition; mismatch is failure.
            os.unlink(destination)
            raise RuntimeError("staging file changed during publication")
        directory = os.path.dirname(os.path.abspath(destination))
        dfd = os.open(directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(dfd)
        finally:
            os.close(dfd)
        # Caller retains staging on failure and removes it after success.
        return expected_sha256
    finally:
        os.close(fd)
