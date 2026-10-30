"""Freeze instrumented P4 runs, score each test report once, summarise."""
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import time
from skripte.p4_silben.token_bytes import TokenTable
from skripte.p4_silben.wall_budget import WallBudget

ROOT=Path.cwd()
BASE=ROOT/"beispiele/p4_silben/run_sil11_20261030_resume"
OUT=ROOT/"beispiele/p4_silben/run_sil11_fix_20261030"
INST=OUT/"instrumentierte"
SCHEDULE=[(v,s) for v in ("char","bpe","syllable") for s in (11,23,47)]
def read(p): return json.loads(Path(p).read_bytes())
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,obj):
    with Path(p).open("x",encoding="utf-8") as f:
        json.dump(obj,f,sort_keys=True,ensure_ascii=False,allow_nan=False,indent=2)
        f.flush(); os.fsync(f.fileno())
def diag(v,s):
    return OUT/"bpe47_instrumentiert" if (v,s)==("bpe",47) else INST/(v+"-"+str(s))
def ckpt(v,s):
    return diag(v,s)/("bpe-47.mook" if (v,s)==("bpe",47) else "checkpoint.mook")
def used():
    return 495.0+sum(read(p)["charged_seconds"] for p in INST.glob("account-*.json"))
def snapshots():
    stamp=read(OUT/"start.json")
    config=read(BASE/"config.json")
    if sha(BASE/"config.json")!=stamp["config_sha256"] or sha(ROOT/"compiler/runtime/moo_nn_easy.c")!=stamp["artifacts"]["runtime_source"]:
        raise ValueError("snapshot changed")
    for name in ("encode","logits_batch","train_paritaet"):
        if sha(OUT/"bin"/name)!=stamp["artifacts"][name]: raise ValueError("binary changed")
    return config,stamp
def verify_records(frozen):
    if frozen["schedule"]!=[list(x) for x in SCHEDULE]: raise ValueError("schedule differs")
    for v,s in SCHEDULE:
        key=v+"-"+str(s); d=diag(v,s); entry=frozen["runs"][key]
        for p,k in ((d/"result.json","result_sha256"),(d/"validation.json","validation_sha256"),(ckpt(v,s),"checkpoint_sha256")):
            if sha(p)!=entry[k]: raise ValueError("frozen artifact changed: "+str(p))
def main():
    if len(sys.argv)<2: raise ValueError("freeze|test|summary required")
    action=sys.argv[1]
    lock=(INST/"lock").open("a"); fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    config,stamp=snapshots()
    if action=="freeze":
        if len(sys.argv)!=2: raise ValueError("freeze has no args")
        runs={}
        for v,seed in SCHEDULE:
            key=v+"-"+str(seed); d=diag(v,seed)
            r=read(d/"result.json"); val=read(d/"validation.json")
            if not r["weights_bit_identical"] or r["weight_count"]!=48 or r["variant"]!=v or r["seed"]!=seed:
                raise ValueError("weight parity missing: "+key)
            h=sha(ckpt(v,seed))
            if h!=r["checkpoint_sha256"] or val["checkpoint_sha256"]!=h or val["variant"]!=v or val["seed"]!=seed or val["split"]!="val" or val["manifest_sha256"]!=config["manifest_sha256"]:
                raise ValueError("validation identity mismatch: "+key)
            runs[key]={"result_sha256":sha(d/"result.json"),"validation_sha256":sha(d/"validation.json"),"checkpoint_sha256":h}
        charged=used()
        if 3600-charged < 9*125:
            raise RuntimeError("budget insufficient to reserve nine 120-s tests plus margins")
        write(INST/"freeze.json",{"schedule":SCHEDULE,"runs":runs,"charged_seconds":charged,
              "remaining_seconds":3600-charged,"test_reserved_seconds":9*120,
              "decision":"Alle Varianten und Seeds unverändert übernehmen; keine Testauswahl."})
    elif action=="test":
        if len(sys.argv)!=4: raise ValueError("test variant seed required")
        v=sys.argv[2]; seed=int(sys.argv[3])
        if (v,seed) not in SCHEDULE: raise ValueError("invalid test")
        frozen=read(INST/"freeze.json"); verify_records(frozen)
        key=v+"-"+str(seed); d=diag(v,seed)
        completed=sum(1 for p in INST.glob("account-*-test.json") if read(p)["status"]=="done")
        prior=used()
        if 3600-prior < (9-completed)*125:
            raise RuntimeError("budget insufficient for remaining test reports")
        account=INST/("account-"+key+"-test.json")
        attempt=INST/("attempt-"+key+"-test.json")
        write(attempt,{"prior_charge_seconds":prior,"freeze_sha256":sha(INST/"freeze.json")})
        started=time.monotonic(); status="failed"
        try:
            original=Path(config["eval_input_path"]["test"])
            if sha(original)!=config["eval_input_sha256"]["test"]: raise ValueError("test input changed")
            spec=read(original)
            spec["encoder_path"]=str(OUT/"bin/encode"); spec["encoder_sha256"]=stamp["artifacts"]["encode"]
            spec["scorer_path"]=str(OUT/"bin/logits_batch"); spec["scorer_sha256"]=stamp["artifacts"]["logits_batch"]
            specpath=INST/"test-input.json"
            if not specpath.exists(): write(specpath,spec)
            elif read(specpath)!=spec: raise ValueError("test spec changed")
            info=read(d/"result.json"); cp=ckpt(v,seed)
            script=Path(config["eval_script_path"])
            if sha(script)!=config["eval_script_sha256"] or sha(config["eval_executable_path"])!=config["eval_executable_sha256"]:
                raise ValueError("eval executable changed")
            env=os.environ.copy()
            env.update(MOO_KI_GPU="1",MOO_KI_GPU_ERZWINGEN="1",MOO_KI_GPU_STRIKT="1",
                       P4_EVAL_INPUT=str(specpath),P4_CHECKPOINT=str(cp),P4_EVAL_OUTPUT=str(d/"test.json"),
                       P4_VARIANT=v,P4_SEED=str(seed),P4_MANIFEST_SHA256=config["manifest_sha256"],
                       P4_CHECKPOINT_SHA256=info["checkpoint_sha256"],PYTHONPATH=str(ROOT),TMPDIR=str(d))
            result=WallBudget(max_runs=1,per_run=120,total=min(3600-prior-5,7200)).run(
                [config["eval_executable_path"],str(script)],cwd=str(ROOT),env=env)
            if result["timed_out"] or result["returncode"]!=0:
                raise RuntimeError("test failed: %r"%result)
            report=read(d/"test.json")
            if report["split"]!="test" or report["variant"]!=v or report["seed"]!=seed or report["checkpoint_sha256"]!=info["checkpoint_sha256"] or report["tokenizer_sha256"]!=info["tokenizer_sha256"] or report["manifest_sha256"]!=config["manifest_sha256"]:
                raise ValueError("test identity mismatch")
            if any(not math.isfinite(report[x]) or report[x]<0 for x in ("nll","nll_per_codepoint","bits_per_byte")):
                raise ValueError("invalid test metrics")
            status="done"
        finally:
            elapsed=time.monotonic()-started
            write(account,{"action":"test","variant":v,"seed":seed,"status":status,
                           "job_seconds":elapsed,"charged_seconds":elapsed+5,
                           "prior_charge_seconds":prior,"total_charge_seconds":prior+elapsed+5})
    elif action=="summary":
        if len(sys.argv)!=2: raise ValueError("summary has no args")
        frozen=read(INST/"freeze.json"); verify_records(frozen)
        aggregate={}
        old={"char":7.468002629490655,"bpe":4.4469705419350944,"syllable":6.502980659397526}
        for v in ("char","bpe","syllable"):
            blob=Path(config["tokenizer_paths"][v]).read_bytes()
            table=TokenTable(v,blob,config["tokenizer_sha256"][v])
            reports=[]
            for seed in (11,23,47):
                account=read(INST/("account-"+v+"-"+str(seed)+"-test.json"))
                if account["status"]!="done": raise ValueError("test incomplete")
                p=diag(v,seed)/"test.json"
                r=read(p)
                if r["checkpoint_sha256"]!=frozen["runs"][v+"-"+str(seed)]["checkpoint_sha256"] or r["split"]!="test":
                    raise ValueError("test report mismatch")
                reports.append(r)
            if len({(r["tokens"],r["bytes"]) for r in reports})!=1: raise ValueError("test denominator mismatch")
            uniform=reports[0]["tokens"]*math.log2(len(table.tokens))/reports[0]["bytes"]
            average=sum(r["bits_per_byte"] for r in reports)/3
            aggregate[v]={"bits_per_byte_mean":average,"uniform_bits_per_byte":uniform,
                          "vocabulary_size":len(table.tokens),"tokens":reports[0]["tokens"],"bytes":reports[0]["bytes"],
                          "old_invalid_bits_per_byte":old[v],"difference_from_uniform":average-uniform,
                          "difference_from_invalid":average-old[v],
                          "nll_per_codepoint_mean":sum(r["nll_per_codepoint"] for r in reports)/3}
        write(INST/"summary.json",{"aggregates":aggregate,"charged_seconds":used(),"freeze_sha256":sha(INST/"freeze.json")})
    else: raise ValueError("unknown action")
if __name__=="__main__": main()
