"""Native Moo tokenizer encoder bridge for already-selected train documents.

The executable and tokenizer paths must belong to a sealed, isolated snapshot.
Before/after hashing detects drift but is not an adversarial TOCTOU guarantee.
The bridge never fits a tokenizer and does not read a validation/test split.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
from skripte.p4_silben.token_bytes import TokenTable


def _hash_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


class MooEncoder:
    def __init__(self, executable, kind, tokenizer, *,
                 executable_sha256, tokenizer_sha256, timeout_seconds=30):
        if kind not in ("char", "bpe", "syllable"):
            raise ValueError("unknown tokenizer")
        if type(timeout_seconds) not in (int, float) or not math.isfinite(timeout_seconds) or not 0 < timeout_seconds <= 600:
            raise ValueError("invalid timeout")
        self.executable = Path(executable).resolve(strict=True)
        self.tokenizer = Path(tokenizer).resolve(strict=True)
        self.executable_sha256 = executable_sha256
        self.tokenizer_sha256 = tokenizer_sha256
        self.kind = kind
        self.timeout = timeout_seconds
        self._verify()

    def _verify(self):
        if (_hash_file(self.executable) != self.executable_sha256
                or _hash_file(self.tokenizer) != self.tokenizer_sha256):
            raise ValueError("encoder or tokenizer hash mismatch")

    def __call__(self, blob, raw):
        if (not isinstance(blob, bytes) or hashlib.sha256(blob).hexdigest() != self.tokenizer_sha256
                or not isinstance(raw, bytes) or not 0 < len(raw) <= 1024 * 1024
                or b"\0" in raw):
            raise ValueError("invalid tokenizer or document")
        raw.decode("utf-8", errors="strict")
        self._verify()
        table = TokenTable(self.kind, blob, self.tokenizer_sha256)
        with tempfile.TemporaryDirectory(prefix="moo-p4-encode-") as tmp:
            path = Path(tmp) / "document.txt"
            path.write_bytes(raw)
            env = os.environ.copy()
            env.update(P4_TOKENIZER_KIND=self.kind, P4_TOKENIZER_PATH=str(self.tokenizer),
                       P4_TEXT_PATH=str(path))
            result = subprocess.run([str(self.executable)], env=env, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    timeout=self.timeout, check=True)
        self._verify()
        if len(result.stdout) > 8 * 1024 * 1024:
            raise ValueError("encoder output too large")
        decoded = json.loads(result.stdout)
        if not isinstance(decoded, list) or not decoded:
            raise ValueError("encoder returned no IDs")
        ids = []
        offsets = [0]
        for x in decoded:
            if type(x) not in (int, float) or not math.isfinite(x) or x != int(x):
                raise ValueError("encoder emitted invalid ID")
            token = int(x)
            piece = table(blob, token)
            pos = offsets[-1]
            if raw[pos:pos + len(piece)] != piece:
                raise ValueError("encoder IDs do not reconstruct input")
            ids.append(token)
            offsets.append(pos + len(piece))
        if offsets[-1] != len(raw):
            raise ValueError("encoder omitted original bytes")
        return ids, offsets
