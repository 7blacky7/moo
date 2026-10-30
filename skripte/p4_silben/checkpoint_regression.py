"""Training-free initialization / checkpoint parity diagnostics."""
import base64
import hashlib
import json
import os
from pathlib import Path
from skripte.p4_silben.token_bytes import TokenTable
from skripte.p4_silben.wall_budget import WallBudget

ROOT = Path.cwd()
RUN = ROOT / "beispiele/p4_silben/run_sil11_paritaet_20261030"
OUT = Path(os.environ["P4_REGRESSION_DIR"])
BASE = ROOT / "beispiele/p4_silben/run_sil11_20261030_resume"
PROMPTS = ("Am Morgen ", "Die Kinder ", "Im Garten ")

def main():
    config = json.loads((BASE / "config.json").read_bytes())
    tokpath = Path(config["tokenizer_paths"]["char"])
    blob = tokpath.read_bytes()
    tokhash = hashlib.sha256(blob).hexdigest()
    if tokhash != config["tokenizer_sha256"]["char"]:
        raise ValueError("Tokenizer verändert")
    table = TokenTable("char", blob, tokhash)
    budget = WallBudget(max_runs=2, per_run=120, total=300)
    results = []
    for mode in ("cpu", "vulkan"):
        env = os.environ.copy()
        env.update(P4_TOKENIZER_KIND="char", P4_TOKENIZER_PATH=str(tokpath),
                   P4_CHECKPOINT_OUT=str(OUT / (mode + ".mook")), P4_SEED="11",
                   P4_DIM="32", P4_HEADS="4", P4_CONTEXT="64", P4_HIDDEN="128",
                   P4_GENERATION_PATH=str(RUN / "char-prompts.json"),
                   P4_DIAG_RESULT=str(OUT / (mode + ".json")),
                   MOO_KI_GPU="0" if mode == "cpu" else "1",
                   MOO_KI_GPU_ERZWINGEN="0" if mode == "cpu" else "1",
                   MOO_KI_GPU_STRIKT="0" if mode == "cpu" else "1")
        run = budget.run([str(OUT / "checkpoint_regression")], cwd=str(ROOT), env=env)
        if run["timed_out"] or run["returncode"] != 0:
            raise RuntimeError("Diagnose abgebrochen: %s %r" % (mode, run))
        data = json.loads((OUT / (mode + ".json")).read_bytes())
        cases = []
        for prompt, a, b in zip(PROMPTS, data["before"], data["after"], strict=True):
            ra = b"".join(table.tokens[int(i)] for i in a)
            rb = b"".join(table.tokens[int(i)] for i in b)
            cases.append({"prompt": prompt, "before_base64": base64.b64encode(ra).decode(),
                          "after_base64": base64.b64encode(rb).decode(), "equal": ra == rb,
                          "first_byte": next((i for i in range(min(len(ra),len(rb))) if ra[i] != rb[i]),
                                             min(len(ra),len(rb)) if len(ra) != len(rb) else None)})
        before, after = data["params_before"], data["params_after"]
        netz = data["params_netz"]
        result = {"mode": mode, "wall_seconds": run["wall_seconds"], "cases": cases,
                  "netz_tensors_equal": before == netz,
                  "netz_generation_equal": data["before"] == data["after_netz"],
                  "parameter_count_before": len(before), "parameter_count_after": len(after),
                  "all_tensor_shapes_and_json_hashes_equal": before == after,
                  "different_tensors": [i for i in range(max(len(before),len(after)))
                                        if i >= len(before) or i >= len(after) or before[i] != after[i]],
                  "gpu": data["gpu"], "checkpoint_sha256": hashlib.sha256((OUT / (mode + ".mook")).read_bytes()).hexdigest()}
        results.append(result)
    (OUT / "summary.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    if any(not r["all_tensor_shapes_and_json_hashes_equal"] or not all(c["equal"] for c in r["cases"]) for r in results):
        raise RuntimeError("Checkpoint-Parität verletzt")
    if any(not r["netz_tensors_equal"] or not r["netz_generation_equal"] for r in results):
        raise RuntimeError("netz.speichern-Parität verletzt")
    if results[1]["gpu"]["submits"] <= 0 or results[1]["gpu"]["cpu_fallbacks"] != 0:
        raise RuntimeError("GPU-Training nicht belegt")
if __name__ == "__main__":
    main()
