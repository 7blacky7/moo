"""Prepare an A/B/C training-run payload without executing a model.

Callers must use a trusted native tokenizer encoder adapter; Moo must\nindependently validate the immutable tokenizer and checkpoint.
Execution remains prohibited by harness.require_ready until independent gates,
GPU accounting and immutable snapshot isolation have a concrete verifier.
"""
import hashlib
import json
from skripte.p4_silben.harness import validate_config
from skripte.p4_silben.checkpoint_metadata import metadata as checkpoint_metadata
from skripte.p4_silben.prepare_training import prepare_train_set
from skripte.p4_silben.token_bytes import TokenTable
from skripte.p4_silben.moo_encoder import MooEncoder


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def prepare_run(config, manifest, train_documents, tokenizer_blob, checkpoint_blob,
                *, variant, seed, encode):
    """Bind the exact train split, tokenizer and initial checkpoint.

    Returns deterministic payload bytes and its SHA256; no process is started.
    The built-in parser inspects the actual checkpoint bytes; runtime model\n    and optimizer reconstruction still require independent verification.
    """
    schedule = validate_config(config)
    if config["split"] != "train":
        raise ValueError("training runs require train split")
    if not isinstance(manifest, dict) or manifest.get("source_sha256") != config["corpus_sha256"]:
        raise ValueError("manifest corpus hash mismatch")
    if (variant, seed) not in schedule:
        raise ValueError("run not in pre-registered A/B/C schedule")
    if not isinstance(tokenizer_blob, bytes) or not isinstance(checkpoint_blob, bytes):
        raise ValueError("artifact bytes required")
    token_hash = sha(tokenizer_blob)
    ckpt_hash = sha(checkpoint_blob)
    expected_tokens = config.get("tokenizer_sha256", {})
    expected_ckpts = config.get("checkpoint_sha256", {})
    run_key = variant + ":" + str(seed)
    if (not isinstance(expected_tokens, dict) or expected_tokens.get(variant) != token_hash
            or not isinstance(expected_ckpts, dict) or expected_ckpts.get(run_key) != ckpt_hash):
        raise ValueError("tokenizer or checkpoint not pinned in run config")
    metadata = checkpoint_metadata(checkpoint_blob)
    if (not isinstance(metadata, dict) or metadata.get("tokenizer_version") != token_hash
            or metadata.get("arch_version") != "p4-gpt2-v1" or metadata.get("opt") is not True
            or metadata.get("step") != 0 or metadata.get("global_step") != 0):
        raise ValueError("checkpoint tokenizer, architecture or optimizer mismatch")
    encoders = config.get("encoder_sha256", {})
    if (type(encode) is not MooEncoder or encode.kind != variant
            or encode.tokenizer_sha256 != token_hash or not isinstance(encoders, dict)
            or encoders.get(variant) != encode.executable_sha256):
        raise ValueError("native encoder identity mismatch")
    token_bytes = TokenTable(variant, tokenizer_blob, token_hash)
    if (metadata["vocab"] != len(token_bytes.tokens) or type(config.get("context")) is not int
            or config["context"] > metadata["context"]):
        raise ValueError("checkpoint vocabulary or context does not match run")
    prepared = prepare_train_set(manifest, config["manifest_sha256"], train_documents,
                                 tokenizer_blob, token_hash, encode, token_bytes,
                                 config["context"], config["stride"])
    all_windows = []
    for doc in prepared["documents"]:
        all_windows.extend(doc["windows"])
    payload = {"format": "moo-p4-train-windows-v1", "variant": variant, "seed": seed,
               "manifest_sha256": prepared["manifest_sha256"], "tokenizer_sha256": token_hash,
               "initial_checkpoint_sha256": ckpt_hash, "train_sha256": prepared["train_sha256"],
               "original_bytes": prepared["original_bytes"],
               "original_codepoints": prepared["original_codepoints"], "windows": all_windows}
    encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True,
                         separators=(",", ":")).encode("utf-8")
    if len(encoded) > 16 * 1024 * 1024 or len(all_windows) > 100000:
        raise ValueError("training payload exceeds limits")
    return encoded, sha(encoded)
