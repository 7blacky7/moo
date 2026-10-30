"""Read and structurally validate metadata of a moo v2 checkpoint.

The runtime must still load the same immutable checkpoint using explicit
expected tokenizer/architecture hashes; this parser is not a replacement
for runtime tensor/optimizer reconstruction or model-shape validation.
"""
import json
import math


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _json(data):
    return json.loads(data, object_pairs_hook=_unique,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite JSON")))


def metadata(blob):
    if not isinstance(blob, bytes) or len(blob) < 9 or len(blob) > 512 * 1024 * 1024:
        raise ValueError("invalid checkpoint size")
    n = int.from_bytes(blob[:8], "little")
    if not 0 < n <= 64 * 1024 * 1024 or n > len(blob) - 8:
        raise ValueError("invalid checkpoint header size")
    header = _json(blob[8:8+n].decode("utf-8", errors="strict"))
    if not isinstance(header, dict) or not isinstance(header.get("__metadata__"), dict):
        raise ValueError("checkpoint metadata missing")
    meta = header["__metadata__"]
    if not isinstance(meta.get("moo_arch"), str) or not isinstance(meta.get("moo_ckpt"), str):
        raise ValueError("checkpoint metadata fields missing")
    architecture = _json(meta["moo_arch"])
    ckpt = _json(meta["moo_ckpt"])
    if not isinstance(architecture, list) or len(architecture) != 14 or not isinstance(ckpt, dict):
        raise ValueError("invalid checkpoint architecture")
    if ckpt.get("format_version") != 2 or not isinstance(ckpt.get("opt"), dict):
        raise ValueError("v2 checkpoint and optimizer required")
    types = ["embedding", "position", "layernorm", "attention", "layernorm",
             "dicht", "dicht", "layernorm", "attention", "layernorm",
             "dicht", "dicht", "layernorm", "dicht"]
    if any(not isinstance(layer, dict) or layer.get("typ") != typ
           for layer, typ in zip(architecture, types)):
        raise ValueError("unexpected model layer sequence")
    emb, pos, head = architecture[0], architecture[1], architecture[13]
    dim, vocab, max_context = emb.get("dim"), emb.get("vokab"), pos.get("max")
    if (type(dim) is not int or not 1 <= dim <= 4096 or
            type(vocab) is not int or not 256 <= vocab <= 65536 or
            type(max_context) is not int or not 2 <= max_context <= 4096 or
            pos.get("dim") != dim or pos.get("art") != "gelernt" or
            head.get("ein") != dim or head.get("aus") != vocab or
            head.get("aktivierung") != "keine"):
        raise ValueError("model embedding, position or output dimensions invalid")
    for i in (2, 4, 7, 9, 12):
        if architecture[i].get("dim") != dim:
            raise ValueError("layernorm dimension mismatch")
    for i in (3, 8):
        layer = architecture[i]
        heads = layer.get("koepfe")
        if (layer.get("dim") != dim or type(heads) is not int or heads < 1 or
                heads > 16 or dim % heads or layer.get("kv_koepfe") != heads or
                layer.get("maske") != "causal" or layer.get("fenster") != 0 or
                layer.get("rope") != 0):
            raise ValueError("attention configuration mismatch")
    for a, b in ((5, 6), (10, 11)):
        first, second = architecture[a], architecture[b]
        hidden = first.get("aus")
        if (first.get("ein") != dim or type(hidden) is not int or not 1 <= hidden <= 65536 or
                first.get("aktivierung") != "gelu" or second.get("ein") != hidden or
                second.get("aus") != dim or second.get("aktivierung") != "keine"):
            raise ValueError("feedforward configuration mismatch")
    opt = ckpt["opt"]
    if opt.get("art") not in ("sgd", "adam", "adamw"):
        raise ValueError("unsupported optimizer")
    segments = []
    payload_size = len(blob) - 8 - n
    for name, tensor in header.items():
        if name == "__metadata__":
            continue
        if not isinstance(tensor, dict):
            raise ValueError("invalid tensor descriptor")
        dtype, shape, offsets = tensor.get("dtype"), tensor.get("shape"), tensor.get("data_offsets")
        if dtype not in ("F32", "BF16") or not isinstance(shape, list) or not shape:
            raise ValueError("unsupported tensor dtype/shape")
        if any(type(x) is not int or x <= 0 for x in shape):
            raise ValueError("invalid tensor dimension")
        if (not isinstance(offsets, list) or len(offsets) != 2
                or any(type(x) is not int for x in offsets)):
            raise ValueError("invalid offsets")
        begin, end = offsets
        size = math.prod(shape) * (4 if dtype == "F32" else 2)
        if begin < 0 or end < begin or end > payload_size or end - begin != size:
            raise ValueError("tensor size/offset mismatch")
        segments.append((begin, end))
    if not segments:
        raise ValueError("checkpoint has no tensors")
    segments.sort()
    cursor = 0
    for begin, end in segments:
        if begin != cursor:
            raise ValueError("tensor data overlaps or has gaps")
        cursor = end
    if cursor != payload_size:
        raise ValueError("checkpoint has trailing data")
    step, global_step = ckpt.get("schritt"), ckpt.get("global_schritt")
    if (type(step) not in (int, float) or type(global_step) not in (int, float)
            or not math.isfinite(step) or not math.isfinite(global_step)
            or step < 0 or global_step < 0 or int(step) != step or int(global_step) != global_step):
        raise ValueError("invalid checkpoint step counters")
    return {"tokenizer_version": ckpt.get("tokenizer_version"),
            "arch_version": ckpt.get("arch_version"), "opt": True,
            "step": int(step), "global_step": int(global_step),
            "vocab": vocab, "context": max_context, "architecture": architecture}
