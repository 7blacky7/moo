"""Summarise already evaluated test reports and uniform-token baselines."""
import json
import math
from pathlib import Path
from skripte.p4_silben.token_bytes import TokenTable
ROOT = Path.cwd()
OUT = ROOT / "beispiele/p4_silben/run_sil11_fix_20261030"
BASE = ROOT / "beispiele/p4_silben/run_sil11_20261030_resume"
def main():
    config = json.loads((BASE / "config.json").read_bytes())
    aggregate = {}
    old = {"char":7.468002629490655, "bpe":4.4469705419350944, "syllable":6.502980659397526}
    for variant in ("char", "bpe", "syllable"):
        blob = Path(config["tokenizer_paths"][variant]).read_bytes()
        table = TokenTable(variant, blob, config["tokenizer_sha256"][variant])
        reports = [json.loads((OUT / (variant + "-" + str(seed) + "-test.json")).read_bytes()) for seed in (11,23,47)]
        if len({(r["tokens"], r["bytes"]) for r in reports}) != 1:
            raise ValueError("inconsistent test token/byte counts")
        uniform = reports[0]["tokens"] * math.log2(len(table.tokens)) / reports[0]["bytes"]
        average = sum(r["bits_per_byte"] for r in reports) / len(reports)
        aggregate[variant] = {"bits_per_byte_mean":average, "uniform_bits_per_byte":uniform,
                              "vocabulary_size":len(table.tokens), "tokens":reports[0]["tokens"],
                              "bytes":reports[0]["bytes"], "old_invalid_bits_per_byte":old[variant],
                              "difference_from_uniform":average-uniform, "difference_from_invalid":average-old[variant]}
    with (OUT / "comparison.json").open("x", encoding="utf-8") as f:
        json.dump(aggregate, f, indent=2, ensure_ascii=False, allow_nan=False)
if __name__ == "__main__":
    main()
