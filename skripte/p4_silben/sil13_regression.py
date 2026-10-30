"""SIL-13: Vorwaertsdurchlauf direkt nach GPU-SGD gegen Host-gesichert/Reload.

Kompiliertes diagnose_sil13 laeuft je Kontextlaenge (kurz und voll) auf CPU
und strikt Vulkan; alle Schichtausgaben muessen bitgleich sein.
"""
import json
import os
import subprocess
from pathlib import Path

ROOT = Path.cwd()
OUT = Path(os.environ["P4_REGRESSION_DIR"])
CONFIG = ROOT / "beispiele/p4_silben/run_sil11_20261030_resume/config.json"
PROMPTS = ROOT / "beispiele/p4_silben/run_sil11_paritaet_20261030/char-prompts.json"


def flat(x):
    if isinstance(x, list):
        return [v for r in x for v in flat(r)]
    return [x]


def main():
    tokpath = json.loads(CONFIG.read_bytes())["tokenizer_paths"]["char"]
    fehler = []
    for laenge in (12, 64):
        for mode in ("cpu", "vulkan"):
            g = "1" if mode == "vulkan" else "0"
            res = OUT / f"res_{mode}_{laenge}.json"
            env = os.environ.copy()
            env.update(P4_LEN=str(laenge), MOO_KI_GPU=g, MOO_KI_GPU_ERZWINGEN=g, MOO_KI_GPU_STRIKT=g,
                       P4_TOKENIZER_PATH=tokpath, P4_CHECKPOINT_OUT=str(OUT / f"ck_{mode}_{laenge}.mook"),
                       P4_GENERATION_PATH=str(PROMPTS), P4_DIAG_RESULT=str(res))
            subprocess.run([str(OUT / "diag")], cwd=ROOT, env=env, check=True, timeout=120)
            d = json.loads(res.read_bytes())
            if mode == "vulkan" and (d["gpu"]["submits"] <= 0 or d["gpu"]["cpu_fallbacks"] != 0):
                fehler.append(f"L={laenge} vulkan: GPU-Training nicht belegt")
            for vergleich in ("gesichert", "geladen"):
                for k in range(len(d["direkt"])):
                    a, b = flat(d["direkt"][k]), flat(d[vergleich][k])
                    if a != b:
                        diff = max(abs(p - q) for p, q in zip(a, b))
                        fehler.append(f"L={laenge} {mode} direkt~{vergleich} Stufe {k}: {diff:.3e}")
                        break
            print(f"L={laenge} {mode}: gpu={d['gpu']}")
    if fehler:
        raise RuntimeError("SIL-13-Paritaet verletzt:\n" + "\n".join(fehler))
    print("SIL-13 OK: alle Stufen bitgleich")


if __name__ == "__main__":
    main()
