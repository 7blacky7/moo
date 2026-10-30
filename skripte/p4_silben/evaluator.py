"""P4 teacher-forced document window scoring primitives.

The caller supplies the exact token ids and their byte boundaries and a scorer.
A scorer receives only the preceding token IDs within the window, plus the
target position; it must return one logit row, conditioned on BOS at document
start. The current and later targets are never passed. Model
adapter and immutable artifact/checkpoint verification are caller obligations.
No model, corpus, or training is run by importing this module.
"""
import hashlib
import math


def windows(count, context, stride):
    if type(count) is not int or count <= 0:
        raise ValueError("document must contain at least one token")
    if type(context) is not int or type(stride) is not int or not 1 <= stride < context:
        raise ValueError("require integer 1 <= stride < context")
    start = 0
    while start < count:
        end = min(start + stride, count)
        yield max(0, end - context), start, end
        start = end


def evaluate_document(raw, ids, offsets, scorer, context, stride):
    if not isinstance(raw, bytes) or not raw or b"\0" in raw:
        raise ValueError("nonempty UTF-8 without NUL required")
    codepoints = len(raw.decode("utf-8", errors="strict"))
    if not ids or len(offsets) != len(ids) + 1 or offsets[0] != 0 or offsets[-1] != len(raw):
        raise ValueError("token offsets must cover all original bytes")
    if any(type(x) is not int or x < 0 for x in ids):
        raise ValueError("token IDs must be nonnegative integers")
    if any(type(x) is not int for x in offsets) or any(a >= b for a, b in zip(offsets, offsets[1:])):
        raise ValueError("token offsets must be strictly increasing integers")
    # Each ID, including every byte-fallback ID, is scored exactly once.
    losses = []
    covered = [0] * len(ids)
    context_lengths = []
    for first, start, end in windows(len(ids), context, stride):
        for chunk_start in range(start, end, 32):
            requests = [(tuple(ids[first:pos]), pos)
                        for pos in range(chunk_start, min(chunk_start + 32, end))]
            if callable(getattr(scorer, "score_batch", None)):
                rows = scorer.score_batch(requests)
            else:
                rows = [scorer(prefix, pos) for prefix, pos in requests]
            if len(rows) != len(requests):
                raise ValueError("scorer row count mismatch")
            for (prefix, pos), row in zip(requests, rows):
                context_lengths.append(len(prefix))
                if not row or ids[pos] >= len(row):
                    raise ValueError("target outside logits vocabulary")
                if any(not isinstance(x, (int, float)) or isinstance(x, bool) or not math.isfinite(x) for x in row):
                    raise ValueError("non-finite or non-numeric logits")
                maximum = max(row)
                logsum_shifted = math.log(math.fsum(math.exp(x - maximum) for x in row))
                loss = (maximum - row[ids[pos]]) + logsum_shifted
                if not math.isfinite(loss) or loss < -1e-12:
                    raise ValueError("invalid negative log likelihood")
                losses.append(max(0.0, loss))
                covered[pos] += 1
    if any(n != 1 for n in covered):
        raise ValueError("each token must be evaluated exactly once")
    nll = math.fsum(losses)
    return {"nll": nll, "tokens": len(ids), "bytes": len(raw),
            "codepoints": codepoints, "nll_per_codepoint": nll / codepoints,
            "bits_per_byte": nll / (math.log(2) * len(raw)),
            "context_tokens_min": min(context_lengths),
            "context_tokens_max": max(context_lengths),
            "context_tokens_sum": sum(context_lengths),
            "eos_policy": "excluded"}



def aggregate_documents(reports):
    """Sum document NLL and denominators before normalization."""
    if not reports:
        raise ValueError("no documents")
    seen = set()
    nlls = []
    total_bytes = 0
    total_codepoints = 0
    total_tokens = 0
    for report in reports:
        ident = report.get("document_sha256")
        if not isinstance(ident, str) or len(ident) != 64 or ident in seen:
            raise ValueError("missing or duplicate document identity")
        seen.add(ident)
        nll = report.get("nll")
        byte_count, codepoints, tokens = (report.get("bytes"), report.get("codepoints"), report.get("tokens"))
        if (type(nll) not in (int, float) or not math.isfinite(nll) or nll < 0
                or any(type(n) is not int or n <= 0 for n in (byte_count, codepoints, tokens))):
            raise ValueError("invalid document totals")
        nlls.append(nll)
        total_bytes += byte_count
        total_codepoints += codepoints
        total_tokens += tokens
    nll = math.fsum(nlls)
    return {"nll": nll, "bytes": total_bytes, "codepoints": total_codepoints,
            "tokens": total_tokens, "documents": len(reports),
            "nll_per_codepoint": nll / total_codepoints,
            "bits_per_byte": nll / (math.log(2) * total_bytes)}


def evaluate_bound_document(raw, ids, offsets, scorer, context, stride, *,
                            token_bytes, tokenizer_blob, checkpoint_blob,
                            expected_document_sha256, expected_tokenizer_sha256,
                            expected_checkpoint_sha256):
    """Validate immutable identities and token bytes before scoring.

    An independently verified adapter must still prove that the scorer uses
    this checkpoint and implements causal teacher forcing.
    """
    for blob, expected, name in (
        (raw, expected_document_sha256, "document"),
        (tokenizer_blob, expected_tokenizer_sha256, "tokenizer"),
        (checkpoint_blob, expected_checkpoint_sha256, "checkpoint"),
    ):
        if (not isinstance(blob, bytes) or not isinstance(expected, str)
                or len(expected) != 64 or hashlib.sha256(blob).hexdigest() != expected):
            raise ValueError(name + " hash mismatch")
    scorer_identity = getattr(scorer, "expected", None)
    if (not isinstance(scorer_identity, tuple) or len(scorer_identity) != 3
            or scorer_identity[1:] != (expected_tokenizer_sha256, expected_checkpoint_sha256)):
        raise ValueError("scorer identity differs from tokenizer or checkpoint")
    if len(offsets) != len(ids) + 1 or not offsets or offsets[0] != 0 or offsets[-1] != len(raw):
        raise ValueError("invalid token coverage")
    for i, token in enumerate(ids):
        start, end = offsets[i:i + 2]
        if (type(start) is not int or type(end) is not int
                or start < 0 or end <= start or end > len(raw)):
            raise ValueError("invalid token byte boundaries")
        piece = token_bytes(tokenizer_blob, token)
        if not isinstance(piece, bytes) or piece != raw[start:end]:
            raise ValueError("token bytes differ from original")
    result = evaluate_document(raw, ids, offsets, scorer, context, stride)
    result.update({"document_sha256": expected_document_sha256,
                   "tokenizer_sha256": expected_tokenizer_sha256,
                   "checkpoint_sha256": expected_checkpoint_sha256})
    return result
