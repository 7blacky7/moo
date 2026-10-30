"""EXT-09: pairwise DPO/combined training with exact validation and GPU ledger."""
import datetime
import fcntl
import hashlib
import json
import math
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import time
from fractions import Fraction

ROOT = pathlib.Path.cwd()
OUT = ROOT / "build/p5_ext09"
BASE = ROOT / "beispiele/p4_silben/run_sil14_grimm36k/full.mook"
LEDGER = ROOT / "build/p5_gpu_ledger.tsv"
LEDGER_LOCK = ROOT / "build/p5_gpu_ledger.lock"
GPU_LOCK = ROOT / "build/p5_gpu.lock"
BASE_HASH = "2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d"
VAL_HASH = "1fc768f2fcb8956d75f30d99c62b5e65978d1158d501764d68dc6ef8b35fcff2"
TOK_HASH = "20ef4b48638359a42141de61ff30d38be7f723c7fe8c612506e9d67f59b62eed"
RATES = ("0.0001", "0.0003")
VARIANTS = ("DPO", "KOMB")
BETA = "0.1"
TRAINER = ROOT / "compiler/target/p5-ext09-train"
DATA = {ROOT / "beispiele/p5/daten/ext05_train_paare.json": "b2aa8cc9b19e68a14117db8e772e5fdb1490aca631763372299462c0231ad529", ROOT / "beispiele/p5/daten/ext05_normaltext.txt": "71e77e722845bc603dc40127cc2d3eaca1b7b5fc1649a232ee146b503f9f822c", ROOT / "beispiele/p4_silben/run_sil14_grimm36k/tokenizer.msf": TOK_HASH}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def totals(rows):
    own = total = 0.0
    for row in rows:
        if len(row) != 6:
            raise ValueError("Ledgerzeile muss sechs Spalten haben")
        seconds = float(row[5])
        if not math.isfinite(seconds) or seconds < 0:
            raise ValueError("Ledgersekunden ungueltig")
        total += seconds
        if row[1] == "EXT-09":
            own += seconds
    return own, total

def score(report):
    groups = report["nll_fix30_gruppen_basis10000"]
    if not groups or any(not isinstance(x, int) or x < 0 or x >= 10000 for x in groups):
        raise ValueError("Fehlerhafte Festkommagruppen")
    value = sum(x * 10000 ** i for i, x in enumerate(groups))
    if report["bytes"] != 696 or report["paare"] != 15:
        raise ValueError("Val-Abdeckung falsch")
    return Fraction(value, report["bytes"])

def choose(rows):
    if not rows:
        raise ValueError("Leere Auswahl")
    return min(rows, key=lambda r: (score(r), r["schritt"], r["checkpoint_sha256"], r["rate"]))

def self_test():
    assert totals([["a", "EXT-09", "x", "s", "e", "12"], ["b", "EXT-05", "y", "s", "e", "7"]]) == (12, 19)
    try:
        totals([["a", "EXT-09", "x", "s", "e", "nan"]])
        raise AssertionError("NaN akzeptiert")
    except ValueError:
        pass
    a = {"nll_fix30_gruppen_basis10000":[1,1], "bytes":696, "paare":15, "schritt":2, "checkpoint_sha256":"b", "rate":"0.0003"}
    b = dict(a, nll_fix30_gruppen_basis10000=[0,1], checkpoint_sha256="a")
    assert choose([a,b]) is b
    print("EXT09 LEDGER UND EXAKTE AUSWAHL OK")

def rows_locked():
    if not LEDGER.is_file():
        raise RuntimeError("Gemeinsames GPU-Ledger fehlt")
    return [line.split("\t") for line in LEDGER.read_text().splitlines() if line]

def with_ledger(fn):
    with LEDGER_LOCK.open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return fn()

def append(row):
    with LEDGER.open("a") as f:
        f.write("\t".join(map(str,row)) + "\n")
        f.flush()
        os.fsync(f.fileno())

def utc(t):
    return datetime.datetime.fromtimestamp(t, datetime.timezone.utc).isoformat()

def stop(signum, _frame):
    raise InterruptedError("Signal " + str(signum))

def train(variant, seed, rate, smoke=False):
    name = f"{variant}-{seed}-{rate}"
    run = OUT / ("smoke-" + name if smoke else name)
    result_file = run / "result.json"
    expected_steps = 2 if smoke else 154
    trainer_hash = digest(TRAINER)
    data_hashes = {str(path.relative_to(ROOT)): digest(path) for path in DATA}
    if any(data_hashes[str(path.relative_to(ROOT))] != expected for path, expected in DATA.items()):
        raise RuntimeError("Trainingsdaten oder Tokenizer geaendert")
    if result_file.exists():
        result = json.loads(result_file.read_text())
        def checked_ledger_ids():
            rows = rows_locked()
            totals(rows)
            return {r[2] for r in rows}
        ledger_ids = with_ledger(checked_ledger_ids)
        if (result["variante"] != variant or result["seed"] != seed or
            result["schritte"] != expected_steps or result.get("ledger_id") not in ledger_ids or
            result.get("trainer_sha256") != trainer_hash or result.get("daten_sha256") != data_hashes or
            float(result["rate"]) != float(rate) or float(result["beta"]) != float(BETA)):
            raise RuntimeError("Wiederaufnahme: Metadaten oder Ledger passen nicht")
        for report in result["berichte"]:
            if digest(pathlib.Path(report["checkpoint_pfad"])) != report["checkpoint_sha256"]:
                raise RuntimeError("Wiederaufnahme: Checkpoint-Hash falsch")
        return result
    if run.exists():
        n = 1
        while (OUT / (run.name + f"-abgebrochen-{n}")).exists():
            n += 1
        run.rename(OUT / (run.name + f"-abgebrochen-{n}"))
    run.mkdir(parents=True)
    if digest(BASE) != BASE_HASH:
        raise RuntimeError("Basischeckpoint geaendert")
    shutil.copyfile(BASE, run / "base.mook")
    if digest(run / "base.mook") != BASE_HASH:
        raise RuntimeError("Basiskopie fehlerhaft")
    env = os.environ.copy()
    for key in ("P5_EXT09_OPTIMIERER", "P5_EXT08_OPTIMIERER", "P5_EXT08_SMOKE", "P5_EXT09_SMOKE"):
        env.pop(key, None)
    env.update(P5_EXT05_DIR=str(run), P5_EXT09_VARIANTE=variant, P5_EXT05_SEED=str(seed),
               P5_EXT05_RATE=rate, P5_EXT09_BETA=BETA, MOO_KI_GPU="1",
               MOO_KI_GPU_ERZWINGEN="1", MOO_KI_GPU_STRIKT="1")
    if smoke:
        env["P5_EXT09_SMOKE"] = "1"
    print("WAIT_GPU", name, flush=True)
    with GPU_LOCK.open("a+") as gpu:
        fcntl.flock(gpu, fcntl.LOCK_EX)
        own, total = with_ledger(lambda: totals(rows_locked()))
        allowance = min(1800.0, 3600.0-own, 14400.0-total) - 10.0
        if allowance < 420:
            print("BUDGET_STOP", own, total, name, flush=True)
            return None
        start_wall, start_mono = time.time(), time.monotonic()
        process = None
        output = ""
        timed_out = False
        ledger_id = f"ext09-{name}-smoke-{int(smoke)}-pid-{os.getpid()}-{int(start_wall)}"
        try:
            process = subprocess.Popen([str(TRAINER)], env=env,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, start_new_session=True)
            output, _ = process.communicate(timeout=allowance)
        except subprocess.TimeoutExpired:
            timed_out = True
            if process is not None and process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                output, _ = process.communicate()
        except BaseException:
            if process is not None and process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                output, _ = process.communicate()
            raise
        finally:
            end_wall = time.time()
            seconds = time.monotonic()-start_mono
            with_ledger(lambda: append(("terra-p4-paritaet","EXT-09",ledger_id,utc(start_wall),utc(end_wall),f"{seconds:.6f}")))
            (run / "run.log").write_text(output)
        if timed_out:
            (run / "timeout.json").write_text(json.dumps({"status": "timeout_unvollstaendig", "ledger_id": ledger_id, "sekunden": seconds}, indent=2))
            print("TIMEOUT_UNVOLLSTAENDIG", name, flush=True)
            return "timeout"
        if process.returncode:
            raise RuntimeError(f"{name}: RC {process.returncode}")
    lines = [x.removeprefix("EXT09_RESULT ") for x in output.splitlines() if x.startswith("EXT09_RESULT ")]
    if len(lines) != 1:
        raise RuntimeError("Ergebnis fehlt: " + name)
    result = json.loads(lines[0])
    if (result["variante"] != variant or result["seed"] != seed or result["schritte"] != expected_steps or
        result["praeferenzpaare"] != 69 or result["gpu"]["submits"] < 1 or result["gpu"]["cpu_fallbacks"] != 0):
        raise RuntimeError("Trainingsergebnis ungueltig: " + name)
    result["ledger_id"] = ledger_id
    result["trainer_sha256"] = trainer_hash
    result["daten_sha256"] = data_hashes
    result_file.write_text(json.dumps(result,ensure_ascii=False,indent=2))
    if digest(BASE) != BASE_HASH:
        raise RuntimeError("Basischeckpoint nach Lauf geaendert")
    print("DONE", name, "seconds", round(seconds,2), flush=True)
    return result

CACHE = {}
def evaluate(result, rate):
    reports = []
    for old in result["berichte"]:
        path = pathlib.Path(old["checkpoint_pfad"])
        sha = digest(path)
        if sha != old["checkpoint_sha256"]:
            raise RuntimeError("Checkpoint-Hash abweichend")
        if sha not in CACHE:
            env = os.environ.copy()
            env.update(P5_CKPT=str(path), P5_EXT05_SPLIT="val", MOO_KI_GPU="0")
            p = subprocess.run([str(ROOT / "compiler/target/p5-ext09-paar-val")],env=env,
                               text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
            hits=[x.removeprefix("EXT05_PAAR_VAL ") for x in p.stdout.splitlines() if x.startswith("EXT05_PAAR_VAL ")]
            if len(hits)!=1:
                raise RuntimeError("Val-Metrik fehlt")
            v=json.loads(hits[0])
            if v["checkpoint_sha256"] != sha or v["split_sha256"] != VAL_HASH or v["tokenizer_sha256"] != TOK_HASH:
                raise RuntimeError("Val-Identitaet stimmt nicht")
            score(v)
            CACHE[sha]=v
        v=dict(CACHE[sha])
        v["checkpoint_pfad"]=str(path)
        v["schritt"]=0 if path.name=="base.mook" else int(path.stem.removeprefix("val_"))
        v["rate"]=rate
        reports.append(v)
    return reports

def preference(report):
    path=pathlib.Path(report["checkpoint_pfad"])
    if digest(path)!=report["checkpoint_sha256"]:
        raise RuntimeError("Praeferenz-Checkpoint geaendert")
    env=os.environ.copy()
    env.update(P5_CKPT=str(path),MOO_KI_GPU="0")
    p=subprocess.run([str(ROOT / "compiler/target/p5-ext09-praef-val")],env=env,
                     text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
    hits=[x.removeprefix("EXT09_PRAEF_VAL ") for x in p.stdout.splitlines() if x.startswith("EXT09_PRAEF_VAL ")]
    if len(hits)!=1: raise RuntimeError("Praeferenzmetrik fehlt")
    v=json.loads(hits[0])
    if v["paare"]!=15 or v["checkpoint_sha256"]!=report["checkpoint_sha256"] or v["split_sha256"]!=VAL_HASH or v["tokenizer_sha256"]!=TOK_HASH:
        raise RuntimeError("Praeferenzmetrik ungueltig")
    return v

def save_state(state):
    (OUT / "resultat.json").write_text(json.dumps(state,ensure_ascii=False,indent=2))

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    if not LEDGER.is_file(): raise RuntimeError("Gemeinsames GPU-Ledger fehlt; kein Start")
    signal.signal(signal.SIGTERM,stop)
    signal.signal(signal.SIGINT,stop)
    with (OUT / "serie.lock").open("a+") as series:
        fcntl.flock(series,fcntl.LOCK_EX | fcntl.LOCK_NB)
        if os.environ.get("P5_EXT09_SMOKE")=="1":
            for variant in VARIANTS:
                result = train(variant,11,RATES[0],True)
                if result is None or result == "timeout": raise RuntimeError("Smoke unvollstaendig: " + (result or "budget"))
            print("EXT09 GPU SMOKE OK",flush=True)
            return
        state={"status":"unvollstaendig","laeufe":[], "auswahl":[], "fehlend":[], "beta":BETA,
               "hinweis":"69 Praeferenzpaare, 8 Wiederholungen pro 77 Feedbackupdates; V1 nutzt EXT-05 B."}
        rates={}
        seed11={}
        for rate in RATES:
            for variant in VARIANTS:
                result=train(variant,11,rate)
                if result is None or result == "timeout":
                    state["fehlend"].append(f"{variant}-11-{rate}: {result or 'budget'}")
                    save_state(state); return
                reports=evaluate(result,rate)
                state["laeufe"].append({"variante":variant,"seed":11,"rate":rate,"result":str(OUT/f"{variant}-11-{rate}/result.json"),"berichte":reports})
                seed11.setdefault(variant,[]).extend(reports)
                save_state(state)
        for variant in VARIANTS:
            winner=choose(seed11[variant]); rates[variant]=winner["rate"]
            state["auswahl"].append({"variante":variant,"seed":11,"sieger":winner,"praeferenz":preference(winner)})
        state["raten"]=rates
        save_state(state)
        for seed in (23,47):
            for variant in VARIANTS:
                rate=rates[variant]
                result=train(variant,seed,rate)
                if result is None or result == "timeout":
                    state["fehlend"].append(f"{variant}-{seed}: {result or 'budget'}")
                    save_state(state); return
                reports=evaluate(result,rate); winner=choose(reports)
                state["laeufe"].append({"variante":variant,"seed":seed,"rate":rate,"result":str(OUT/f"{variant}-{seed}-{rate}/result.json"),"berichte":reports})
                state["auswahl"].append({"variante":variant,"seed":seed,"sieger":winner,"praeferenz":preference(winner)})
                save_state(state)
        state["status"]="dpo_und_kombi_vollstaendig_v1_ausstehend"
        save_state(state)
        print("EXT09 DPO/KOMBI FERTIG; V1 und Sekundaermetriken separat auswerten",flush=True)

if __name__=="__main__":
    if "--self-test" in sys.argv: self_test()
    else: main()
