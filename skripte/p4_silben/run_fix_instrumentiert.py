"""Single BPE-47 instrumented rerun, with exact F32 weights and logit rows."""
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
from skripte.p4_silben.prepare_run import prepare_run
from skripte.p4_silben.moo_encoder import MooEncoder
import time
from skripte.p4_silben.checkpoint_metadata import metadata
from skripte.p4_silben.token_bytes import TokenTable
from skripte.p4_silben.wall_budget import WallBudget

ROOT = Path.cwd()
OUT = ROOT / "beispiele/p4_silben/run_sil11_fix_20261030"
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
def train(variant, seed, deadline):
    job_started = time.monotonic()
    remaining=deadline-job_started
    if remaining <= 0: raise RuntimeError("Gesamtbudget erreicht")
    config=json.loads((BASE/"config.json").read_bytes())
    stamp=json.loads((OUT/"start.json").read_bytes())
    if variant not in ("char","bpe","syllable") or seed not in (11,23,47):
        raise ValueError("invalid run")
    DIAG=OUT/"instrumentierte"/(variant+"-"+str(seed))
    DIAG.mkdir(mode=0o700)
    os.environ["TMPDIR"]=str(DIAG)
    tempfile.tempdir=str(DIAG)
    tokenizer=Path(config["tokenizer_paths"][variant])
    blob=tokenizer.read_bytes()
    tokhash=sha(blob)
    if tokhash!=config["tokenizer_sha256"][variant]: raise ValueError("tokenizer changed")
    key=variant+":"+str(seed)
    init=Path(config["initial_checkpoint_paths"][key])
    initial_blob=init.read_bytes()
    if sha(initial_blob)!=config["checkpoint_sha256"][key]: raise ValueError("initial checkpoint changed")
    manifest=json.loads(Path(config["manifest_path"]).read_bytes())
    documents={k:Path(v).read_bytes() for k,v in config["train_documents"].items()}
    encpath=OUT/"bin"/"encode"
    enchash=stamp["artifacts"]["encode"]
    if sha(encpath.read_bytes()) != enchash: raise ValueError("encoder changed")
    run_config=dict(config)
    run_config["encoder_sha256"]={v:enchash for v in config["variants"]}
    enc=MooEncoder(encpath,variant,tokenizer,executable_sha256=enchash,tokenizer_sha256=tokhash)
    payload,phash=prepare_run(run_config,manifest,documents,blob,initial_blob,variant=variant,seed=seed,encode=enc)
    windows=DIAG/"windows.json"
    with windows.open("xb") as f: f.write(payload)
    table=TokenTable(variant,blob,tokhash)
    prompts=("Am Morgen ","Die Kinder ","Im Garten ")
    req={"prompts":[list(enc(blob,p.encode())[0]) for p in prompts],
         "allowed":[i for i,p in enumerate(table.tokens) if p and b"\x00" not in p]}
    reqpath=DIAG/"prompts.json"
    save(reqpath,req)
    previous={"cases":[{"prompt":p} for p in prompts],"windows_sha256":phash,
              "windows":len(json.loads(payload)["windows"]),"tokenizer_sha256":tokhash}
    before=DIAG/"before.json"; after=DIAG/"after.json"; checkpoint=DIAG/"checkpoint.mook"
    save(DIAG/"attempt.json",{"started_monotonic":time.monotonic(),
         "remaining_seconds":remaining,"reason":"authorised instrumented P4 run"})
    env=os.environ.copy()
    env.update(MOO_KI_GPU="1",MOO_KI_GPU_ERZWINGEN="1",MOO_KI_GPU_STRIKT="1",
               MOO_KI_CKPT_BF16="0",MOO_KI_SPEICHERN_BF16="0",
               P4_TOKENIZER_KIND=variant,P4_TOKENIZER_PATH=str(tokenizer),P4_CHECKPOINT=str(init),
               P4_CHECKPOINT_OUT=str(checkpoint),P4_WINDOWS_PATH=str(windows),
               P4_WINDOWS_SHA256=previous["windows_sha256"],P4_MANIFEST_SHA256=config["manifest_sha256"],
               P4_GENERATION_PATH=str(reqpath),P4_BEFORE_PATH=str(before),P4_AFTER_PATH=str(after),
               P4_DIAG_DIR=str(DIAG))
    remaining=deadline-time.monotonic()
    if remaining <= 0: raise RuntimeError("Gesamtbudget erreicht")
    result=WallBudget(max_runs=1,per_run=min(300,remaining),total=remaining).run(
        [str(OUT/"bpe47_instrumentiert"/"train_paritaet_instrumentiert")],cwd=str(ROOT),env=env)
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
    accepted=same
    report={"variant":variant,"seed":seed,"training_ms":v["training_ms"],"wall_seconds":result["wall_seconds"],
            "gpu":v["gpu"],"checkpoint_sha256":sha(checkpoint.read_bytes()),"tokenizer_sha256":sha(blob),
            "weight_count":len(pre),"weights_bit_identical":same,"before_tensors":preinfo,"after_tensors":postinfo,
            "cases":cases,"accepted":accepted,"snapshot_intervention":"host materialisation after in-memory generation before checkpoint",
            "logits_format":"raw little-endian F32 safetensors; full row exported each step",
            "elapsed_since_original_start":time.monotonic()-stamp["monotonic"]}
    save(DIAG/"result.json",report)
    if not accepted: raise RuntimeError("F32-Gewichte sind nicht bitgleich")
    return report

