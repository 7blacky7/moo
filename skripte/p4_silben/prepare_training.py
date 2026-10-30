"""Prepare train-only teacher-forced windows from a sealed P4 manifest.

No model or tokenizer training. An orchestrator must obtain token IDs from the
pinned tokenizer, supply a trusted token_bytes implementation, bind the actual
checkpoint to that tokenizer, and run only after all experiment gates pass.
"""
import hashlib
import json


def _sha(blob):
    return hashlib.sha256(blob).hexdigest()



def prepare_train_set(manifest, expected_manifest_sha256, train_documents,
                      tokenizer_blob, expected_tokenizer_sha256,
                      encode, token_bytes, context, stride):
    """Prepare exactly the sealed train split, never validation or test.

    train_documents maps document ID to original bytes. encode returns
    (IDs, byte offsets) and must use the supplied pinned tokenizer blob.
    This function validates every emitted token byte and the split hash.
    """
    if not isinstance(manifest, dict) or not isinstance(train_documents, dict):
        raise ValueError("invalid manifest or train documents")
    split = manifest.get("splits", {}).get("train", {})
    expected_ids = split.get("document_ids")
    if (not isinstance(expected_ids, list) or not expected_ids
            or len(set(expected_ids)) != len(expected_ids)
            or set(train_documents) != set(expected_ids)):
        raise ValueError("documents must exactly match the sealed train split")
    results = []
    joined = bytearray()
    for doc_id in expected_ids:
        raw = train_documents[doc_id]
        if not isinstance(raw, bytes):
            raise ValueError("document bytes required")
        ids, offsets = encode(tokenizer_blob, raw)
        results.append(prepare_windows(manifest, expected_manifest_sha256, doc_id,
                                      raw, ids, offsets, tokenizer_blob,
                                      expected_tokenizer_sha256, token_bytes,
                                      context, stride))
        joined.extend(raw)
    total_bytes = sum(r["original_bytes"] for r in results)
    total_codepoints = sum(r["original_codepoints"] for r in results)
    if (total_bytes != split.get("bytes") or total_codepoints != split.get("codepoints")
            or _sha(joined) != split.get("sha256")):
        raise ValueError("train split hash or counts mismatch")
    return {"manifest_sha256": expected_manifest_sha256,
            "tokenizer_sha256": expected_tokenizer_sha256,
            "train_sha256": split["sha256"], "original_bytes": total_bytes,
            "original_codepoints": total_codepoints,
            "documents": results}


def prepare_windows(manifest, expected_manifest_sha256, doc_id, raw, ids,
                    offsets, tokenizer_blob, expected_tokenizer_sha256,
                    token_bytes, context, stride):
    if not isinstance(manifest, dict):
        raise ValueError("manifest required")
    unsigned = {k: v for k, v in manifest.items() if k != "manifest_sha256"}
    canonical = json.dumps(unsigned, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":")).encode("utf-8")
    calculated = _sha(canonical)
    if (manifest.get("manifest_sha256") != calculated
            or expected_manifest_sha256 != calculated):
        raise ValueError("manifest hash mismatch")
    docs = manifest.get("documents")
    if not isinstance(docs, list):
        raise ValueError("invalid manifest documents")
    matching = [d for d in docs if isinstance(d, dict) and d.get("id") == doc_id]
    if len(matching) != 1 or matching[0].get("split") != "train":
        raise ValueError("document must belong exclusively to train")
    doc = matching[0]
    if (not isinstance(raw, bytes) or not raw or b"\0" in raw
            or _sha(raw) != doc.get("sha256") or len(raw) != doc.get("bytes")):
        raise ValueError("document bytes or hash mismatch")
    codepoints = len(raw.decode("utf-8", errors="strict"))
    if codepoints != doc.get("codepoints"):
        raise ValueError("document codepoint count mismatch")
    if not isinstance(tokenizer_blob, bytes) or _sha(tokenizer_blob) != expected_tokenizer_sha256:
        raise ValueError("tokenizer hash mismatch")
    if (type(context) is not int or type(stride) is not int
            or not 1 <= stride < context <= 4096):
        raise ValueError("require 1 <= stride < context <= 4096")
    if (not isinstance(ids, (list, tuple)) or not ids or len(offsets) != len(ids) + 1
            or offsets[0] != 0 or offsets[-1] != len(raw)):
        raise ValueError("invalid token coverage")
    for i, token in enumerate(ids):
        start, end = offsets[i:i + 2]
        if (type(token) is not int or not 0 <= token < 65536
                or type(start) is not int or type(end) is not int
                or start < 0 or end <= start or end > len(raw)):
            raise ValueError("invalid token ID or boundaries")
        if token_bytes(tokenizer_blob, token) != raw[start:end]:
            raise ValueError("token bytes do not reconstruct document")
    batches = []
    start = 0
    while start < len(ids):
        end = min(start + stride, len(ids))
        first = max(0, end - context)
        # Byte ID 0 is reserved out-of-band BOS for valid NUL-free text.
        preceding = 0 if first == 0 else ids[first - 1]
        inputs = [preceding] + list(ids[first:end - 1])
        targets = list(ids[first:end])
        mask = [0] * (start - first) + [1] * (end - start)
        if len(inputs) != len(targets) or len(mask) != len(targets) or not any(mask):
            raise ValueError("invalid prepared batch")
        batches.append({"input_ids": inputs, "target_ids": targets, "loss_mask": mask,
                        "context_start": first, "target_start": start, "target_end": end})
        start = end
    if sum(sum(batch["loss_mask"]) for batch in batches) != len(ids):
        raise ValueError("target coverage mismatch")
    return {"document_id": doc_id, "document_sha256": doc["sha256"],
            "manifest_sha256": calculated, "tokenizer_sha256": expected_tokenizer_sha256,
            "original_bytes": len(raw), "original_codepoints": codepoints,
            "windows": batches}
