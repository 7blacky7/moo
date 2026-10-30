import hashlib, json, os, pathlib, subprocess
out = pathlib.Path("build/p5_ext05")
cache = {}
result = []
for name in ("A-11-0.0001", "B-11-0.0001", "A-11-0.0003"):
    data = json.loads((out / name / "result.json").read_text())
    reports = []
    for item in data["berichte"]:
        path = pathlib.Path(item["checkpoint_pfad"])
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != item["checkpoint_sha256"]:
            raise SystemExit("Checkpoint verändert")
        if digest not in cache:
            env = os.environ.copy()
            env.update(P5_CKPT=str(path), MOO_KI_GPU="0")
            run = subprocess.run(["compiler/target/p5-ext05-train-eval"], env=env, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
            values = [json.loads(line) for line in run.stdout.splitlines() if line.startswith("{")]
            if len(values) != 1 or values[0]["checkpoint_sha256"] != digest:
                raise SystemExit("Train-Bericht fehlt oder Hash abweichend")
            cache[digest] = values[0]
        reports.append(cache[digest])
    result.append({"lauf": name, "train_berichte": reports})
(out / "train_nachgeholt.json").write_text(json.dumps(result, ensure_ascii=False, indent=2))
print("EXT05 TRAIN NACHGEHOLT", len(result), len(cache))
