"""Inference-only diagnostics for the stored BPE/47 parity discrepancy."""
import base64
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
from skripte.p4_silben.moo_scorer import MooBatchScorer
from skripte.p4_silben.token_bytes import TokenTable

ROOT = Path.cwd()
OUT = ROOT / "beispiele/p4_silben/run_sil11_fix_20261030"
BASE = ROOT / "beispiele/p4_silben/run_sil11_20261030_resume"
def sha(data): return hashlib.sha256(data).hexdigest()
def raw(table, ids): return b"".join(table.tokens[int(i)] for i in ids)
def request(prefix):
    return (prefix[-63:], len(prefix))
def main():
    start = time.monotonic()
    config = json.loads((BASE / "config.json").read_bytes())
    casefile = json.loads((OUT / "bpe-47-result.json").read_bytes())
    req = json.loads((OUT / "bpe-47-prompts.json").read_bytes())
    ckpt = OUT / "bpe-47.mook"
    cpblob = ckpt.read_bytes()
    if sha(cpblob) != casefile["checkpoint_sha256"]:
        raise ValueError("checkpoint changed")
    header_length = int.from_bytes(cpblob[:8], "little")
    header = json.loads(cpblob[8:8+header_length])
    tensors = {k:v for k,v in header.items() if k.startswith("p") and k[1:].isdigit()}
    dtype_counts = {}
    for t in tensors.values(): dtype_counts[t["dtype"]] = dtype_counts.get(t["dtype"], 0)+1
    tokpath = Path(config["tokenizer_paths"]["bpe"])
    blob = tokpath.read_bytes()
    tokhash = sha(blob)
    if tokhash != casefile["tokenizer_sha256"]:
        raise ValueError("tokenizer changed")
    table = TokenTable("bpe", blob, tokhash)
    scorer = OUT / "bin/logits_batch"
    expected = json.loads((OUT / "start.json").read_bytes())["artifacts"]["logits_batch"]
    if sha(scorer.read_bytes()) != expected:
        raise ValueError("scorer changed")
    os.environ["TMPDIR"] = str(OUT)
    tempfile.tempdir = str(OUT)
    outcomes = {}
    for mode in ("cpu", "vulkan"):
        os.environ.update(MOO_KI_GPU="0" if mode == "cpu" else "1",
                          MOO_KI_GPU_ERZWINGEN="0" if mode == "cpu" else "1",
                          MOO_KI_GPU_STRIKT="0" if mode == "cpu" else "1")
        score = MooBatchScorer(scorer, "bpe", tokpath, ckpt,
                               executable_sha256=expected, tokenizer_sha256=tokhash,
                               checkpoint_sha256=casefile["checkpoint_sha256"])
        prefixes = [list(x) for x in req["prompts"]]
        generated = [[] for _ in prefixes]
        allowed = req["allowed"]
        for _ in range(12):
            rows = score.score_batch([request(p) for p in prefixes])
            for i, row in enumerate(rows):
                token = max(allowed, key=lambda j:(row[j], -j))
                generated[i].append(token)
                prefixes[i].append(token)
        diagnostics = []
        for i, case in enumerate(casefile["cases"]):
            a, b = case["before_ids"], case["after_ids"]
            idx = next(j for j, (x,y) in enumerate(zip(a,b)) if x != y)
            prefix = list(req["prompts"][i]) + list(a[:idx])
            row = score.score_batch([request(prefix)])[0]
            top = sorted(allowed, key=lambda j:(row[j], -j), reverse=True)[:2]
            decoded = raw(table, generated[i])
            diagnostics.append({"prompt":case["prompt"], "divergent_token_index":idx,
                "previous_first_differing_byte":case["first_differing_byte"],
                "memory_token":a[idx], "previous_reload_token":b[idx],
                "common_prefix_token_ids":prefix,
                "top2":[{"id":j,"logit":row[j]} for j in top],
                "top2_gap":row[top[0]]-row[top[1]],
                "memory_token_logit":row[a[idx]], "previous_reload_token_logit":row[b[idx]],
                "generated_ids":generated[i], "generated_base64":base64.b64encode(decoded).decode(),
                "equals_previous_memory":decoded == base64.b64decode(case["before_base64"]),
                "equals_previous_reload":decoded == base64.b64decode(case["after_base64"])})
        outcomes[mode] = diagnostics
    result = {"checkpoint_sha256":sha(cpblob), "tokenizer_sha256":tokhash,
              "parameter_dtype_counts":dtype_counts, "parameter_tensors":len(tensors),
              "results":outcomes, "wall_seconds":time.monotonic()-start,
              "note":"Logits from loaded checkpoint at common prefixes, not the vanished in-memory model."}
    with (OUT / "bpe-47-diagnose.json").open("x", encoding="utf-8") as f:
        json.dump(result, f, sort_keys=True, ensure_ascii=False, indent=2, allow_nan=False)
if __name__ == "__main__":
    main()
