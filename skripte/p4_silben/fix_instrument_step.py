"""Run exactly one instrumented train OR validation step under a shared budget."""
import fcntl
import json
import math
import os
from pathlib import Path
import sys
import time
from skripte.p4_silben.run_fix_instrumentiert import train
from skripte.p4_silben.wall_budget import WallBudget

ROOT=Path.cwd()
BASE=ROOT/"beispiele/p4_silben/run_sil11_20261030_resume"
OUT=ROOT/"beispiele/p4_silben/run_sil11_fix_20261030"
INST=OUT/"instrumentierte"
def read(p): return json.loads(Path(p).read_bytes())
def write(p,obj):
    with Path(p).open("x",encoding="utf-8") as f:
        json.dump(obj,f,sort_keys=True,ensure_ascii=False,allow_nan=False)
        f.flush(); os.fsync(f.fileno())
def sha(p):
    import hashlib
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    if len(sys.argv)!=4: raise ValueError("expected train|val, variant, seed")
    action, variant, seed_s=sys.argv[1:]
    seed=int(seed_s)
    if action not in ("train","val") or variant not in ("char","bpe","syllable") or seed not in (11,23,47):
        raise ValueError("invalid step")
    key=variant+"-"+str(seed)
    if action=="train" and key=="bpe-47": raise ValueError("existing instrumented rerun must be reused")
    started=time.monotonic()
    lock=(INST/"lock").open("a")
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    prior=495.0
    accounts=sorted(INST.glob("account-*.json"))
    for path in accounts:
        prior+=read(path)["charged_seconds"]
    remaining=3600-prior
    if remaining<=5: raise RuntimeError("Gesamtbudget erschöpft")
    deadline=started+remaining-5
    marker=INST/("attempt-"+key+"-"+action+".json")
    account=INST/("account-"+key+"-"+action+".json")
    write(marker,{"prior_charge_seconds":prior,"started_monotonic":started,"deadline":deadline})
    status="failed"
    try:
        config=read(BASE/"config.json")
        stamp=read(OUT/"start.json")
        if sha(BASE/"config.json")!=stamp["config_sha256"] or sha(ROOT/"compiler/runtime/moo_nn_easy.c")!=stamp["artifacts"]["runtime_source"]:
            raise ValueError("snapshot changed")
        if sha(OUT/"bin/logits_batch")!=stamp["artifacts"]["logits_batch"]:
            raise ValueError("scorer changed")
        if action=="train":
            report=train(variant,seed,deadline)
            if not report["weights_bit_identical"]: raise RuntimeError("F32 weights differ")
        else:
            diag=OUT/"bpe47_instrumentiert" if key=="bpe-47" else INST/key
            info=read(diag/"result.json")
            if not info["weights_bit_identical"]: raise ValueError("weight equality missing")
            ck=diag/("bpe-47.mook" if key=="bpe-47" else "checkpoint.mook")
            if sha(ck)!=info["checkpoint_sha256"]: raise ValueError("checkpoint changed")
            original=Path(config["eval_input_path"]["val"])
            if sha(original)!=config["eval_input_sha256"]["val"]: raise ValueError("val input changed")
            expected=read(original)
            expected["encoder_path"]=str(OUT/"bin/encode")
            expected["encoder_sha256"]=stamp["artifacts"]["encode"]
            expected["scorer_path"]=str(OUT/"bin/logits_batch")
            expected["scorer_sha256"]=stamp["artifacts"]["logits_batch"]
            spec=OUT/"val-input.json"
            if read(spec)!=expected: raise ValueError("val spec changed")
            script=Path(config["eval_script_path"])
            if sha(script)!=config["eval_script_sha256"] or sha(config["eval_executable_path"])!=config["eval_executable_sha256"]:
                raise ValueError("eval program changed")
            report_path=diag/"validation.json"
            env=os.environ.copy()
            env.update(MOO_KI_GPU="1",MOO_KI_GPU_ERZWINGEN="1",MOO_KI_GPU_STRIKT="1",
                       P4_EVAL_INPUT=str(spec),P4_CHECKPOINT=str(ck),P4_EVAL_OUTPUT=str(report_path),
                       P4_VARIANT=variant,P4_SEED=str(seed),P4_MANIFEST_SHA256=config["manifest_sha256"],
                       P4_CHECKPOINT_SHA256=info["checkpoint_sha256"],PYTHONPATH=str(ROOT),TMPDIR=str(diag))
            left=deadline-time.monotonic()
            if left<=0: raise RuntimeError("Gesamtbudget erschöpft")
            outcome=WallBudget(max_runs=1,per_run=min(120,left),total=left).run(
                [config["eval_executable_path"],str(script)],cwd=str(ROOT),env=env)
            if outcome["timed_out"] or outcome["returncode"]!=0:
                raise RuntimeError("Validierung fehlgeschlagen: %r"%outcome)
            report=read(report_path)
            if report["split"]!="val" or report["variant"]!=variant or report["seed"]!=seed or report["checkpoint_sha256"]!=info["checkpoint_sha256"] or report["tokenizer_sha256"]!=info["tokenizer_sha256"] or report["manifest_sha256"]!=config["manifest_sha256"]:
                raise ValueError("validation identity mismatch")
            if any(not math.isfinite(report[x]) or report[x]<0 for x in ("nll","nll_per_codepoint","bits_per_byte")):
                raise ValueError("invalid metric")
        status="done"
    finally:
        elapsed=time.monotonic()-started
        write(account,{"action":action,"variant":variant,"seed":seed,"status":status,
                       "job_seconds":elapsed,"charged_seconds":elapsed+5,
                       "prior_charge_seconds":prior,"total_charge_seconds":prior+elapsed+5})
if __name__=="__main__": main()
