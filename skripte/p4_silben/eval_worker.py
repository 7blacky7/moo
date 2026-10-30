"""Hash-bound, document-wise Moo evaluation worker for the P4 orchestrator."""
import hashlib
import json
import os
from pathlib import Path

from skripte.p4_silben.evaluator import aggregate_documents, evaluate_bound_document
from skripte.p4_silben.moo_encoder import MooEncoder
from skripte.p4_silben.moo_scorer import MooScorer, MooBatchScorer
from skripte.p4_silben.token_bytes import TokenTable


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    spec = json.loads(Path(os.environ["P4_EVAL_INPUT"]).read_bytes())
    variant = os.environ["P4_VARIANT"]
    seed = int(os.environ["P4_SEED"])
    manifest = spec["manifest"]
    unsigned = {k: v for k, v in manifest.items() if k != "manifest_sha256"}
    canonical = json.dumps(unsigned, sort_keys=True, ensure_ascii=False,
                           separators=(",", ":")).encode("utf-8")
    manifest_hash = sha(canonical)
    if manifest_hash != os.environ["P4_MANIFEST_SHA256"] or manifest.get("manifest_sha256") != manifest_hash:
        raise ValueError("manifest hash mismatch")
    split_name = spec["split"]
    if split_name not in ("val", "test"):
        raise ValueError("evaluation split required")
    split = manifest["splits"][split_name]
    ids = split["document_ids"]
    files = spec["documents"]
    if not ids or len(set(ids)) != len(ids) or set(files) != set(ids):
        raise ValueError("documents do not match sealed split")
    tok = spec["tokenizers"][variant]
    tokenizer_blob = Path(tok["path"]).read_bytes()
    tokenizer_hash = sha(tokenizer_blob)
    if tokenizer_hash != tok["sha256"]:
        raise ValueError("tokenizer hash mismatch")
    checkpoint_path = os.environ["P4_CHECKPOINT"]
    checkpoint_blob = Path(checkpoint_path).read_bytes()
    checkpoint_hash = sha(checkpoint_blob)
    if checkpoint_hash != os.environ["P4_CHECKPOINT_SHA256"]:
        raise ValueError("checkpoint hash mismatch")
    encoder = MooEncoder(spec["encoder_path"], variant, tok["path"],
                         executable_sha256=spec["encoder_sha256"], tokenizer_sha256=tokenizer_hash)
    mode = spec.get("scorer_mode", "single")
    if mode not in ("single", "batch"):
        raise ValueError("invalid scorer mode")
    scorer_type = MooBatchScorer if mode == "batch" else MooScorer
    scorer = scorer_type(spec["scorer_path"], variant, tok["path"], checkpoint_path,
                       executable_sha256=spec["scorer_sha256"], tokenizer_sha256=tokenizer_hash,
                       checkpoint_sha256=checkpoint_hash)
    table = TokenTable(variant, tokenizer_blob, tokenizer_hash)
    documents = manifest["documents"]
    by_id = {d["id"]: d for d in documents}
    if len(by_id) != len(documents):
        raise ValueError("duplicate manifest document ID")
    reports, joined = [], bytearray()
    for ident in ids:
        doc = by_id[ident]
        raw = Path(files[ident]).read_bytes()
        if (doc["split"] != split_name or sha(raw) != doc["sha256"]
                or len(raw) != doc["bytes"] or len(raw.decode("utf-8")) != doc["codepoints"]):
            raise ValueError("document identity mismatch")
        tokens, offsets = encoder(tokenizer_blob, raw)
        reports.append(evaluate_bound_document(
            raw, tokens, offsets, scorer, spec["context"], spec["stride"],
            token_bytes=table, tokenizer_blob=tokenizer_blob, checkpoint_blob=checkpoint_blob,
            expected_document_sha256=doc["sha256"], expected_tokenizer_sha256=tokenizer_hash,
            expected_checkpoint_sha256=checkpoint_hash))
        joined.extend(raw)
    report = aggregate_documents(reports)
    if (sha(joined) != split["sha256"] or report["bytes"] != split["bytes"]
            or report["codepoints"] != split["codepoints"]):
        raise ValueError("split identity mismatch")
    report.update(variant=variant, seed=seed, split=split_name, manifest_sha256=manifest_hash,
                  checkpoint_sha256=checkpoint_hash, tokenizer_sha256=tokenizer_hash)
    output = Path(os.environ["P4_EVAL_OUTPUT"])
    with output.open("x", encoding="utf-8") as f:
        json.dump(report, f, sort_keys=True, ensure_ascii=False, allow_nan=False)
        f.flush()
        os.fsync(f.fileno())


if __name__ == "__main__":
    main()
