"""Subprocess scorer for the fixed p4-gpt2-v1 Moo architecture.

The compiled logits.moos executable validates the tokenizer embedded in the
checkpoint via checkpoint_laden's explicit expected SHA256. Each call receives
only the token prefix. The caller must provide immutable snapshot files and
verify that the checkpoint was trained with BOS ID 0 at every document start.
This reference implementation starts a process per token and is not an
efficient production evaluator.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess


def _hash_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


class MooScorer:
    def __init__(self, executable, kind, tokenizer, checkpoint, *,
                 executable_sha256, tokenizer_sha256, checkpoint_sha256,
                 timeout_seconds=30):
        if kind not in ("char", "bpe", "syllable"):
            raise ValueError("unknown tokenizer")
        self.executable = Path(executable).resolve(strict=True)
        self.tokenizer = Path(tokenizer).resolve(strict=True)
        self.checkpoint = Path(checkpoint).resolve(strict=True)
        self.expected = (executable_sha256, tokenizer_sha256, checkpoint_sha256)
        if type(timeout_seconds) not in (int, float) or not math.isfinite(timeout_seconds) or not 0 < timeout_seconds <= 600:
            raise ValueError("invalid timeout")
        self.kind = kind
        self.timeout = timeout_seconds
        self._verify()

    def _verify(self):
        for path, expected in zip((self.executable, self.tokenizer, self.checkpoint), self.expected):
            if not isinstance(expected, str) or len(expected) != 64 or _hash_file(path) != expected:
                raise ValueError("immutable input hash mismatch")

    def __call__(self, prefix, position):
        if type(position) is not int or position < 0 or len(prefix) > position or len(prefix) > 4096:
            raise ValueError("invalid prefix position")
        if any(type(t) is not int or not 0 <= t < 65536 for t in prefix):
            raise ValueError("invalid prefix token")
        self._verify()
        env = os.environ.copy()
        env.update(P4_TOKENIZER_KIND=self.kind, P4_TOKENIZER_PATH=str(self.tokenizer),
                   P4_CHECKPOINT=str(self.checkpoint), P4_POSITION=str(position),
                   P4_PREFIX_JSON=json.dumps(prefix, separators=(",", ":")))
        result = subprocess.run([str(self.executable)], env=env, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=self.timeout, check=True)
        self._verify()
        if len(result.stdout) > 4 * 1024 * 1024:
            raise ValueError("scorer output too large")
        row = json.loads(result.stdout)
        if not isinstance(row, list) or not 0 < len(row) <= 65536:
            raise ValueError("scorer must return a nonempty logits list")
        if any(type(x) not in (int, float) or not math.isfinite(x) for x in row):
            raise ValueError("invalid logits")
        return row



class MooBatchScorer(MooScorer):
    """Batch up to 32 independent causal prefixes per native model load."""
    def score_batch(self, requests):
        import tempfile
        if not isinstance(requests, list) or not 1 <= len(requests) <= 32:
            raise ValueError("invalid scorer batch")
        payload = []
        for prefix, position in requests:
            if type(position) is not int or position < 0 or len(prefix) > position or len(prefix) > 4096:
                raise ValueError("invalid prefix position")
            if any(type(t) is not int or not 0 <= t < 65536 for t in prefix):
                raise ValueError("invalid prefix token")
            payload.append({"prefix": list(prefix), "position": position})
        self._verify()
        with tempfile.TemporaryDirectory(prefix="moo-p4-score-") as tmp:
            path = Path(tmp) / "requests.json"
            path.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")
            env = os.environ.copy()
            env.update(P4_TOKENIZER_KIND=self.kind, P4_TOKENIZER_PATH=str(self.tokenizer),
                       P4_CHECKPOINT=str(self.checkpoint), P4_REQUESTS_PATH=str(path))
            result = subprocess.run([str(self.executable)], env=env, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    timeout=self.timeout, check=True)
        self._verify()
        if len(result.stdout) > 64 * 1024 * 1024:
            raise ValueError("scorer batch output too large")
        rows = json.loads(result.stdout)
        if not isinstance(rows, list) or len(rows) != len(requests):
            raise ValueError("scorer batch length mismatch")
        for row in rows:
            if (not isinstance(row, list) or not 0 < len(row) <= 65536
                    or any(type(x) not in (int, float) or not math.isfinite(x) for x in row)):
                raise ValueError("invalid batch logits")
        return rows
