"""EXT-09 CPU-Vergleich: seriell, vorab definierte Val-Auswahl."""
import fcntl
import json
import os
import pathlib
import shutil
import subprocess
import time

import p5_ext09 as common

ROOT = pathlib.Path.cwd()
OUT = ROOT / "build/p5_ext09_cpu"
RATES = ("0.0001", "0.0003")
VARIANTS = ("DPO", "KOMB")
pref_cache = {}

def train(variant, seed, rate, control=False):
    name = "V1-B-11-0.0003" if control else f"{variant}-{seed}-{rate}"
    run = OUT / name
    trainer = ROOT / ("compiler/target/p5-ext05-train" if control else "compiler/target/p5-ext09-train")
    hashes = {str(p.relative_to(ROOT)): common.digest(p) for p in common.DATA}
    if any(hashes[str(p.relative_to(ROOT))] != expected for p, expected in common.DATA.items()):
        raise RuntimeError("Datenidentitaet ungueltig")
    trainer_hash = common.digest(trainer)
    result_path = run / "result.json"
    if result_path.exists():
        result = json.loads(result_path.read_text())
        if (result.get("trainer_sha256") != trainer_hash or result.get("daten_sha256") != hashes or
            result.get("backend") != "cpu" or result["schritte"] != 154 or
            result["variante"] != ("B" if control else variant) or result["seed"] != seed or
            float(result["rate"]) != float(rate)):
            raise RuntimeError("Gespeicherter Lauf passt nicht")
        for report in result["berichte"]:
            if common.digest(pathlib.Path(report["checkpoint_pfad"])) != report["checkpoint_sha256"]:
                raise RuntimeError("Checkpoint veraendert")
        return result
    if run.exists():
        raise RuntimeError("Unvollstaendiger Lauf vorhanden: " + str(run))
    run.mkdir(parents=True)
    if common.digest(common.BASE) != common.BASE_HASH:
        raise RuntimeError("Basischeckpoint veraendert")
    shutil.copyfile(common.BASE, run / "base.mook")
    if common.digest(run / "base.mook") != common.BASE_HASH:
        raise RuntimeError("Basiskopie veraendert")
    env = os.environ.copy()
    for key in ("P5_EXT09_OPTIMIERER", "P5_EXT08_OPTIMIERER", "P5_EXT08_SMOKE",
                "P5_EXT09_SMOKE", "MOO_KI_GPU_ERZWINGEN", "MOO_KI_GPU_STRIKT"):
        env.pop(key, None)
    env.update(P5_EXT05_DIR=str(run), P5_EXT05_SEED=str(seed), P5_EXT05_RATE=rate,
               MOO_KI_GPU="0")
    if control:
        env["P5_EXT05_VARIANTE"] = "B"
    else:
        env.update(P5_EXT09_VARIANTE=variant, P5_EXT09_BETA=common.BETA)
    print("CPU_START", name, flush=True)
    started = time.monotonic()
    proc = subprocess.run([str(trainer)], env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    seconds = time.monotonic() - started
    (run / "run.log").write_text(proc.stdout)
    if proc.returncode:
        raise RuntimeError(f"{name}: RC {proc.returncode}")
    prefix = "EXT05_RESULT " if control else "EXT09_RESULT "
    lines = [line.removeprefix(prefix) for line in proc.stdout.splitlines() if line.startswith(prefix)]
    if len(lines) != 1:
        raise RuntimeError("Ergebnis fehlt: " + name)
    result = json.loads(lines[0])
    if (result["variante"] != ("B" if control else variant) or result["seed"] != seed or
        result["schritte"] != 154 or float(result["rate"]) != float(rate) or
        result["gpu"]["submits"] != 0):
        raise RuntimeError("Ungueltiges CPU-Ergebnis: " + name)
    result.update(trainer_sha256=trainer_hash, daten_sha256=hashes, backend="cpu", sekunden=seconds)
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2))
    print("CPU_DONE", name, round(seconds, 2), flush=True)
    return result

def evaluate(result, rate):
    reports = common.evaluate(result, rate)
    for report in reports:
        sha = report["checkpoint_sha256"]
        if sha not in pref_cache:
            pref_cache[sha] = common.preference(report)
        report["praeferenz"] = pref_cache[sha]
    return reports

def choose(rows):
    return min(rows, key=lambda r: (-r["praeferenz"]["korrekt"], common.score(r),
                                    r["schritt"], r["checkpoint_sha256"], r["rate"]))

def save(state):
    (OUT / "resultat.json").write_text(json.dumps(state, ensure_ascii=False, indent=2))

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / "serie.lock").open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        state = {"status": "unvollstaendig", "backend": "cpu", "laeufe": [],
                 "auswahl": [], "raten": {}, "auswahlregel":
                 "Praeferenz korrekt absteigend, exakte NLL pro Byte aufsteigend, Schritt, SHA, Rate"}
        seed11 = {}
        for rate in RATES:
            for variant in VARIANTS:
                result = train(variant, 11, rate)
                reports = evaluate(result, rate)
                state["laeufe"].append({"variante":variant, "seed":11, "rate":rate, "berichte":reports})
                seed11.setdefault(variant, []).extend(reports)
                save(state)
        for variant in VARIANTS:
            winner = choose(seed11[variant])
            state["raten"][variant] = winner["rate"]
            state["auswahl"].append({"variante":variant, "seed":11, "sieger":winner})
        save(state)
        for seed in (23, 47):
            for variant in VARIANTS:
                rate = state["raten"][variant]
                result = train(variant, seed, rate)
                reports = evaluate(result, rate)
                state["laeufe"].append({"variante":variant, "seed":seed, "rate":rate, "berichte":reports})
                state["auswahl"].append({"variante":variant, "seed":seed, "sieger":choose(reports)})
                save(state)
        control = train("B", 11, "0.0003", control=True)
        state["backend_kontrolle"] = evaluate(control, "0.0003")
        state["status"] = "cpu_raster_fertig_qualitaet_ausstehend"
        save(state)
        print("EXT09 CPU RASTER FERTIG", flush=True)

if __name__ == "__main__":
    main()
