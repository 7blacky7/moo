import hashlib, json, os, pathlib, subprocess, time
root = pathlib.Path.cwd()
out = root / "build/p5_ext05"
env = os.environ.copy()
env["MOO_KI_GPU"] = "0"
env["P5_EXT05_PER_RUN"] = "1"
env["P5_EXT05_MANIFEST"] = str(out / "conditional_manifest.json")
selection = subprocess.run(["compiler/target/p5-ext05-auswahl"], env=env, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
lines = [line.removeprefix("EXT05_AUSWAHL ") for line in selection.stdout.splitlines()
         if line.startswith("EXT05_AUSWAHL ")]
if len(lines) != 6:
    raise SystemExit("Sechs Val-Auswahlen erforderlich")
selected = [json.loads(line) for line in lines]
run_lines = [line.removeprefix("EXT05_LAUF ") for line in selection.stdout.splitlines()
             if line.startswith("EXT05_LAUF ")]
overall_lines = [line.removeprefix("EXT05_GESAMT ") for line in selection.stdout.splitlines()
                 if line.startswith("EXT05_GESAMT ")]
if len(run_lines) != 8 or len(overall_lines) != 1:
    raise SystemExit("8 Lauf-Auswahlen und ein Gesamtmodell erforderlich")
runs = [json.loads(line) for line in run_lines]
(out / "selection.json").write_text(json.dumps({"selected": selected, "runs": runs, "overall": json.loads(overall_lines[0])}, ensure_ascii=False, indent=2))
cache = {}
tasks = ("p5-train-nll-baseline", "p5-grammatik-baseline",
         "p5-forgetting-baseline", "p5-sampling-baseline")
for item in runs + selected:
    report = item["sieger"]
    path = pathlib.Path(report["checkpoint_pfad"])
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != report["checkpoint_sha256"]:
        raise SystemExit("Ausgewählter Checkpoint verändert")
    if digest in cache:
        continue
    target = out / "quality" / digest
    target.mkdir(parents=True, exist_ok=True)
    env["P5_CKPT"] = str(path)
    cache[digest] = str(target)
    for task in tasks:
        if (target / (task + ".ok")).exists() and (target / (task + ".log")).exists():
            print("QUALITY_REUSE", digest, task, flush=True)
            continue
        print("QUALITY", digest, task, flush=True)
        start = time.monotonic()
        run = subprocess.run(["mise", "run", task], env=env, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (target / (task + ".log")).write_text(run.stdout)
        print("QUALITY_DONE", task, run.returncode, round(time.monotonic()-start, 2), flush=True)
        if run.returncode:
            raise SystemExit(f"Qualitätsmessung {task} fehlgeschlagen")
        (target / (task + ".ok")).write_text("RC=0\n")
(out / "quality_manifest.json").write_text(json.dumps(cache, indent=2))
print("EXT05 QUALITY COMPLETE", len(selected), len(cache), flush=True)
