"""Single BPE-47 instrumented rerun, with exact F32 weights and logit rows."""
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import time
from skripte.p4_silben.checkpoint_metadata import metadata
from skripte.p4_silben.token_bytes import TokenTable
from skripte.p4_silben.wall_budget import WallBudget

ROOT = Path.cwd()
OUT = ROOT / "beispiele/p4_silben/run_sil11_fix_20261030"
DIAG = OUT / "bpe47_instrumentiert"
BASE = ROOT / "beispiele/p4_silben/run_sil11_20261030_resume"
def sha(raw): return hashlib.sha256(raw).hexdigest()
def save(path, obj):
    with Path(path).open("x",encoding="utf-8") as f:
        json.dump(obj,f,sort_keys=True,ensure_ascii=False,indent=2,allow_nan=False)
def tensors(path):
    raw = Path(path).read_bytes()
    n = int.from_bytes(raw[:8], "little")
    if n < 1 or n > len(raw)-8: raise ValueError("bad header")
    h = json.loads(raw[8:8+n])
    data = raw[8+n:]
    out = {}
    for name, entry in h.items():
        if not (name.startswith("p") and name[1:].isdigit()): continue
        if entry["dtype"] != "F32": raise ValueError("not F32")
        a,b = entry["data_offsets"]
        if a < 0 or b < a or b > len(data) or (b-a) % 4: raise ValueError("bad tensor offsets")
        out[name] = {"shape":entry["shape"],"sha256":sha(data[a:b]),"raw":data[a:b]}
    if not out: raise ValueError("no tensors")
    return out
def top3(row, allowed):
    return [{"id":i,"logit":row[i]} for i in sorted(allowed,key=lambda j:(row[j],-j),reverse=True)[:3]]
def row(path):
    ts=tensors(path)
    raw=ts["p0"]["raw"]
    values=struct.unpack("<" + str(len(raw)//4) + "f",raw)
    if any(not math.isfinite(x) for x in values): raise ValueError("nonfinite logits")
    return values
def main():
    job_started = time.monotonic()
    stamp=json.loads((OUT/"start.json").read_bytes())
    # Conservative charge for completed C jobs: 277.812 + 186.362 < 470 s.
    remaining=3600-470
    if remaining <= 0: raise RuntimeError("Gesamtbudget erreicht")
    config=json.loads((BASE/"config.json").read_bytes())
    previous=json.loads((OUT/"bpe-47-result.json").read_bytes())
    tokenizer=Path(config["tokenizer_paths"]["bpe"])
    blob=tokenizer.read_bytes()
    if sha(blob)!=previous["tokenizer_sha256"]: raise ValueError("tokenizer changed")
    init=Path(config["initial_checkpoint_paths"]["bpe:47"])
    if sha(init.read_bytes())!=previous["initial_sha256"]: raise ValueError("initial checkpoint changed")
    windows=OUT/"bpe-47-windows.json"
    if sha(windows.read_bytes())!=previous["windows_sha256"]: raise ValueError("windows changed")
    reqpath=OUT/"bpe-47-prompts.json"
    req=json.loads(reqpath.read_bytes())
    table=TokenTable("bpe",blob,previous["tokenizer_sha256"])
    before=DIAG/"before.json"; after=DIAG/"after.json"; checkpoint=DIAG/"bpe-47.mook"
    save(DIAG/"attempt.json",{"started_monotonic":time.monotonic(),
         "remaining_seconds":remaining,"reason":"authorised single BPE-47 instrumented rerun"})
    env=os.environ.copy()
    env.update(MOO_KI_GPU="1",MOO_KI_GPU_ERZWINGEN="1",MOO_KI_GPU_STRIKT="1",
               MOO_KI_CKPT_BF16="0",MOO_KI_SPEICHERN_BF16="0",
               P4_TOKENIZER_KIND="bpe",P4_TOKENIZER_PATH=str(tokenizer),P4_CHECKPOINT=str(init),
               P4_CHECKPOINT_OUT=str(checkpoint),P4_WINDOWS_PATH=str(windows),
               P4_WINDOWS_SHA256=previous["windows_sha256"],P4_MANIFEST_SHA256=config["manifest_sha256"],
               P4_GENERATION_PATH=str(reqpath),P4_BEFORE_PATH=str(before),P4_AFTER_PATH=str(after),
               P4_DIAG_DIR=str(DIAG))
    result=WallBudget(max_runs=1,per_run=min(300,remaining),total=remaining).run(
        [str(DIAG/"train_paritaet_instrumentiert")],cwd=str(ROOT),env=env)
    if result["timed_out"] or result["returncode"]!=0:
        raise RuntimeError("instrumentierter Lauf fehlgeschlagen: %r"%result)
    v=json.loads(before.read_bytes()); n=json.loads(after.read_bytes())
    pre=tensors(DIAG/"gewichte-vor.mook"); post=tensors(DIAG/"gewichte-nach.mook")
    preinfo={k:{x:y for x,y in item.items() if x!="raw"} for k,item in pre.items()}
    postinfo={k:{x:y for x,y in item.items() if x!="raw"} for k,item in post.items()}
    same=preinfo==postinfo
    if len(pre)!=48 or len(post)!=48: raise ValueError("parameter count mismatch")
    if v["gpu"]["submits"]<=0 or v["gpu"]["cpu_fallbacks"]!=0: raise ValueError("GPU not established")
    if metadata(checkpoint.read_bytes())["step"] != metadata(init.read_bytes())["step"]+previous["windows"]:
        raise ValueError("step count mismatch")
    cases=[]
    for pi, (p, a, b) in enumerate(zip(previous["cases"],v["ids"],n["ids"],strict=True)):
        if len(a)!=12 or len(b)!=12: raise ValueError("wrong generated length")
        ra=b"".join(table.tokens[int(i)] for i in a); rb=b"".join(table.tokens[int(i)] for i in b)
        first_token=next((i for i,(x,y) in enumerate(zip(a,b)) if x!=y),None)
        first_byte=next((i for i in range(min(len(ra),len(rb))) if ra[i]!=rb[i]),
                        min(len(ra),len(rb)) if len(ra)!=len(rb) else None)
        trace=[]; maxdiff=0.0
        for j in range(12):
            x=row(DIAG/("vor-"+str(pi)+"-"+str(j)+".mook"))
            y=row(DIAG/("nach-"+str(pi)+"-"+str(j)+".mook"))
            if len(x)!=len(table.tokens) or len(y)!=len(x): raise ValueError("logit shape")
            common = first_token is None or j<=first_token
            delta=max(abs(t-u) for t,u in zip(x,y)) if common else None
            if common: maxdiff=max(maxdiff,delta)
            trace.append({"step":j,"common_prefix":common,"max_abs_difference":delta,
                          "before_top3":top3(x,req["allowed"]),"after_top3":top3(y,req["allowed"])})
        gap=None
        if first_token is not None:
            t=trace[first_token]
            gap=max(t["before_top3"][0]["logit"]-t["before_top3"][1]["logit"],
                    t["after_top3"][0]["logit"]-t["after_top3"][1]["logit"])
        numerisch=bool(ra!=rb and first_token is not None and maxdiff<=0.001 and gap<=0.002)
        def utf8(x):
            try: x.decode("utf-8"); return True
            except UnicodeDecodeError: return False
        cases.append({"prompt":p["prompt"],"equal":ra==rb,"first_byte":first_byte,"first_token":first_token,
                      "before_base64":base64.b64encode(ra).decode(),"after_base64":base64.b64encode(rb).decode(),
                      "before_utf8_valid":utf8(ra),"after_utf8_valid":utf8(rb),
                      "max_abs_logit_difference_common_prefix":maxdiff,"flip_top2_max_gap":gap,
                      "numerical_exception":numerisch,"steps":trace})
    accepted=same and all(c["equal"] or c["numerical_exception"] for c in cases)
    report={"variant":"bpe","seed":47,"training_ms":v["training_ms"],"wall_seconds":result["wall_seconds"],
            "gpu":v["gpu"],"checkpoint_sha256":sha(checkpoint.read_bytes()),"tokenizer_sha256":sha(blob),
            "weight_count":len(pre),"weights_bit_identical":same,"before_tensors":preinfo,"after_tensors":postinfo,
            "cases":cases,"accepted":accepted,"snapshot_intervention":"host materialisation after in-memory generation before checkpoint",
            "logits_format":"raw little-endian F32 safetensors; full row exported each step",
            "elapsed_since_original_start":time.monotonic()-stamp["monotonic"],
            "job_seconds":time.monotonic()-job_started, "previous_job_charge_upper_seconds":470,
            "job_charge_margin_seconds":5, "total_job_budget_seconds":3600}
    save(DIAG/"result.json",report)
    if not accepted: raise RuntimeError("instrumentiertes Paritaetskriterium nicht erfuellt")
if __name__=="__main__": main()
