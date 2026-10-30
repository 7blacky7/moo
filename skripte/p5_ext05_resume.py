import datetime, fcntl, hashlib, json, os, pathlib, shutil, signal, subprocess, time
root = pathlib.Path.cwd()
out = root / "build/p5_ext05"
base = root / "beispiele/p4_silben/run_sil14_grimm36k/full.mook"
ledger = root / "build/p5_gpu_ledger.tsv"
ledger_lock = root / "build/p5_gpu_ledger.lock"
gpu_lock = root / "build/p5_gpu.lock"
expected = "2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d"
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def utc(timestamp):
    return datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc).isoformat()
def rows_locked():
    if not ledger.exists():
        return []
    return [line.split("\t") for line in ledger.read_text().splitlines() if line]
def totals(rows):
    all_seconds = own_seconds = 0.0
    for row in rows:
        if len(row) != 6:
            raise SystemExit("Ungültige GPU-Ledgerzeile")
        try:
            seconds = float(row[5])
        except ValueError:
            raise SystemExit("Ungültige GPU-Ledgersekunden")
        if seconds < 0:
            raise SystemExit("Negative GPU-Zeit")
        all_seconds += seconds
        if row[1] == "EXT-05":
            own_seconds += seconds
    return own_seconds, all_seconds
def with_ledger(callback):
    with ledger_lock.open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return callback()
def append(row):
    with ledger.open("a") as f:
        f.write("\t".join(map(str, row)) + "\n")
        f.flush()
        os.fsync(f.fileno())
def historic():
    # Konservative Obergrenzen: vollständige Job-Wandzeit inklusive Wartezeit.
    old = [
        ("8b695888-42c4-4d52-8052-c1a04b7e5630-obergrenze", "2026-10-30T15:32:37+00:00", "2026-10-30T15:48:16+00:00", 939),
        ("bacd8f1e-a63a-42b9-9e7d-37be153035f2-obergrenze", "2026-10-30T15:49:09+00:00", "2026-10-30T15:51:30+00:00", 141)]
    def add_missing():
        existing = {r[2] for r in rows_locked() if len(r) == 6}
        for job, start, end, seconds in old:
            if job not in existing:
                append(("terra-p4-daten", "EXT-05", job, start, end, seconds))
        return totals(rows_locked())
    return with_ledger(add_missing)
def interrupted(signum, frame):
    raise InterruptedError("Signal " + str(signum))
signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)
def train(variant, seed, rate):
    name = f"{variant}-{seed}-{rate}"
    run = out / name
    if (run / "result.json").exists():
        result = json.loads((run / "result.json").read_text())
        if result["variante"] != variant or result["seed"] != seed or result["schritte"] != 154:
            raise SystemExit("Vorhandenes Ergebnis ungueltig: " + name)
        print("REUSE", name, flush=True)
        return
    if run.exists():
        index = 1
        archived = out / (name + f"-abgebrochen-{index}")
        while archived.exists():
            index += 1
            archived = out / (name + f"-abgebrochen-{index}")
        run.rename(archived)
    run.mkdir()
    if digest(base) != expected:
        raise SystemExit("Basischeckpoint veraendert")
    shutil.copyfile(base, run / "base.mook")
    if digest(run / "base.mook") != expected:
        raise SystemExit("Checkpoint-Kopie falsch")
    env = os.environ.copy()
    env.update(P5_EXT05_DIR=str(run), P5_EXT05_VARIANTE=variant, P5_EXT05_SEED=str(seed),
               P5_EXT05_RATE=rate, MOO_KI_GPU="1", MOO_KI_GPU_ERZWINGEN="1", MOO_KI_GPU_STRIKT="1")
    print("WAIT_GPU", name, flush=True)
    with gpu_lock.open("a+") as gpu:
        fcntl.flock(gpu, fcntl.LOCK_EX)
        own, total = with_ledger(lambda: totals(rows_locked()))
        allowance = min(1800.0, 4500.0 - own, 14400.0 - total)
        if allowance < 420:
            raise SystemExit(f"GPU-Restbudget fuer neuen Lauf zu klein: EXT-05={own}, P5={total}")
        start_wall = time.time()
        start_mono = time.monotonic()
        process = None
        output = ""
        try:
            process = subprocess.Popen(["compiler/target/p5-ext05-train"], env=env, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, start_new_session=True)
            output, _ = process.communicate(timeout=allowance)
        except BaseException:
            if process is not None:
                os.killpg(process.pid, signal.SIGKILL)
                output, _ = process.communicate()
            raise
        finally:
            end_wall = time.time()
            seconds = time.monotonic() - start_mono
            job = f"ext05-{name}-pid-{process.pid if process else 'startfehler'}"
            with_ledger(lambda: append(("terra-p4-daten", "EXT-05", job, utc(start_wall), utc(end_wall), f"{seconds:.6f}")))
            (run / "run.log").write_text(output)
        if process.returncode:
            raise SystemExit(f"{name}: RC {process.returncode}\n{output}")
    lines = [s.removeprefix("EXT05_RESULT ") for s in output.splitlines() if s.startswith("EXT05_RESULT ")]
    if len(lines) != 1:
        raise SystemExit(name + ": Ergebnis fehlt")
    (run / "result.json").write_text(json.dumps(json.loads(lines[0]), ensure_ascii=False, indent=2))
    if digest(base) != expected:
        raise SystemExit("Basischeckpoint veraendert")
    print("DONE", name, flush=True)
print("GPU_LEDGER_NACHTRAG", historic(), flush=True)
for rate in ("0.0001", "0.0003"):
    for variant in ("A", "B"):
        train(variant, 11, rate)
env = os.environ.copy()
env.update(P5_EXT05_SEED11_ONLY="1", P5_EXT05_SPLIT="val", P5_EXT05_MANIFEST=str(out / "conditional_manifest.json"), MOO_KI_GPU="0")
selection = subprocess.run(["mise", "run", "p5-ext05-conditional-auswahl"], env=env,
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
(out / "seed11_selection.log").write_text(selection.stdout)
lines = [line.removeprefix("EXT05_AUSWAHL ") for line in selection.stdout.splitlines()
         if line.startswith("EXT05_AUSWAHL ")]
if len(lines) != 2:
    raise SystemExit("Seed-11-Auswahl unvollständig")
rates = {x["variante"]: x["sieger"]["rate"] for x in map(json.loads, lines)}
if set(rates) != {"A", "B"} or any(x not in ("0.0001", "0.0003") for x in rates.values()):
    raise SystemExit("Seed-11-Raten ungültig")
(out / "chosen_rates.json").write_text(json.dumps(rates, indent=2))
print("CHOSEN_RATES", json.dumps(rates), flush=True)
for seed in (23, 47):
    for variant in ("A", "B"):
        train(variant, seed, rates[variant])
print("EXT05 EIGHT RUNS COMPLETE", with_ledger(lambda: totals(rows_locked())), flush=True)
