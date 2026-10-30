"""EXT-08: reuse the EXT-05 B trainer; keep GPU budgets in a shared ledger."""
import contextlib, datetime, fcntl, hashlib, json, math, os, pathlib, shutil, signal, subprocess, tempfile, time

root = pathlib.Path.cwd()
base = root / "beispiele/p4_silben/run_sil14_grimm36k/full.mook"
expected = "2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d"
binary_path = root / "compiler/target/p5-ext08-train"
ledger_path = root / "build/p5_gpu_ledger.tsv"
ledger_lock_path = root / "build/p5_gpu_ledger.lock"
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def now_utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")
@contextlib.contextmanager
def lock_file(path, timeout=1800):
    with path.open("a+") as handle:
        started = time.monotonic()
        while True:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if time.monotonic() - started >= timeout:
                    raise TimeoutError(f"Sperre nicht erhalten: {path}")
                time.sleep(0.2)
        try:
            yield time.monotonic() - started
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)
def budget():
    with lock_file(ledger_lock_path):
        rows = []
        if ledger_path.exists():
            for line in ledger_path.read_text().splitlines():
                parts = line.split("\t")
                if len(parts) != 6:
                    raise ValueError("GPU-Ledger hat ungueltige Zeile")
                sec = float(parts[5])
                if not math.isfinite(sec) or sec < 0:
                    raise ValueError("GPU-Ledger hat ungueltige Dauer")
                rows.append((parts[1], sec))
        return sum(sec for _, sec in rows), sum(sec for task, sec in rows if task in ("P5-T8", "EXT-08"))
def append_ledger(run_id, begin, started):
    with lock_file(ledger_lock_path):
        end = now_utc()
        seconds = time.monotonic() - started + 0.5
        with ledger_path.open("a") as file:
            file.write(f"terra-p4-inventar\tP5-T8\t{run_id}\t{begin}\t{end}\t{seconds:.6f}\n")
            file.flush()
            os.fsync(file.fileno())
def terminate_handler(signum, frame):
    raise SystemExit(f"Signal {signum}")
signal.signal(signal.SIGTERM, terminate_handler)
if digest(base) != expected:
    raise SystemExit("Basischeckpoint stimmt nicht")
if digest(root / "beispiele/p5/daten/ext05_train_paare.json") != "b2aa8cc9b19e68a14117db8e772e5fdb1490aca631763372299462c0231ad529":
    raise SystemExit("Trainingspaare veraendert")
if digest(root / "beispiele/p5/daten/ext05_normaltext.txt") != "71e77e722845bc603dc40127cc2d3eaca1b7b5fc1649a232ee146b503f9f822c":
    raise SystemExit("Normaltext veraendert")
binary_sha256 = digest(binary_path)
smoke = os.environ.get("P5_EXT08_SMOKE") == "1"
out = (pathlib.Path(tempfile.mkdtemp(prefix="p5-ext08-smoke-", dir=root / "build"))
       if smoke else root / "build/p5_ext08")
out.mkdir(parents=True, exist_ok=True)
fixture = smoke and os.environ.get("P5_EXT08_LEDGER_TEST") == "1"
if fixture:
    ledger_path = out / "fixture_ledger.tsv"
    ledger_lock_path = out / "fixture_ledger.lock"
    ledger_path.touch()
if not smoke and not ledger_path.is_file():
    raise SystemExit("Gemeinsames GPU-Ledger fehlt; kein Produktionsstart")
gpu_lock_path = out / "fixture_gpu.lock" if fixture else root / "build/p5_gpu.lock"
start = time.monotonic()
optimizers = ("sgd", "adam", "adamw")
rates = (0.0001, 0.0003)
def run_one(seed, rate, optimizer):
    run = out / f"{optimizer}-{seed}-{rate}"
    result_path = run / "result.json"
    if result_path.exists():
        prior = json.loads(result_path.read_text())
        expected_steps = 2 if smoke else 154
        if (prior["variante"] != "B" or prior["optimierer"] != optimizer or prior["seed"] != seed
                or prior["rate"] != rate or prior["smoke"] != smoke or prior["schritte"] != expected_steps
                or prior["optimizer_start_t"] != 0 or prior["optimizer_end_t"] != expected_steps
                or prior["basis_sha256"] != expected or prior["binary_sha256"] != binary_sha256
                or len(prior["berichte"]) != (2 if smoke else 3)
                or len(prior["train_berichte"]) != (2 if smoke else 3)
                or (not smoke and prior["zielpositionen"] != 4530)
                or digest(run / "base.mook") != expected):
            raise SystemExit(f"Vorhandenes Ergebnis ungueltig: {run}")
        for report in prior["berichte"]:
            path = pathlib.Path(report["checkpoint_pfad"])
            if path.resolve().parent != run.resolve() or digest(path) != report["checkpoint_sha256"]:
                raise SystemExit(f"Checkpoint aus Vorlauf ungueltig: {path}")
        if not smoke:
            with lock_file(ledger_lock_path):
                entries = [line.split("\t") for line in ledger_path.read_text().splitlines()]
                if not any(len(row) == 6 and row[2] == prior["ledger_job_id"] and row[1] in ("P5-T8", "EXT-08") for row in entries):
                    raise SystemExit(f"Ledgerbeleg fuer Vorlauf fehlt: {run}")
        print("REUSE", optimizer, seed, rate, flush=True)
        return
    if run.exists():
        index = 1
        archived = out / (run.name + f"-abgebrochen-{index}")
        while archived.exists():
            index += 1
            archived = out / (run.name + f"-abgebrochen-{index}")
        run.rename(archived)
    run_id = f"p5t8-{os.getpid()}-{optimizer}-{seed}-{rate}"
    use_ledger = not smoke or fixture
    slot = lock_file(gpu_lock_path) if use_ledger else contextlib.nullcontext(0.0)
    with slot as wait_seconds:
        run_start = time.monotonic()
        begin = now_utc()
        total_used, own_used = budget() if use_ledger else (0.0, 0.0)
        available = min(1790.0, 3590.0 - own_used, 14390.0 - total_used)
        # EXT-05 observed ~7 minutes per full-length run.
        if not smoke and available < 420:
            (out / "budget_stop.json").write_text(json.dumps({"status": "unvollstaendig", "naechster_lauf": run_id, "ext08_verbraucht_s": own_used, "p5_verbraucht_s": total_used, "grund": "Restbudget unter 420 Sekunden"}, indent=2))
            raise SystemExit(f"GPU-Restbudget zu klein: EXT08={3600-own_used:.1f}s P5={14400-total_used:.1f}s")
        run = out / f"{optimizer}-{seed}-{rate}"
        run.mkdir(exist_ok=False)
        shutil.copyfile(base, run / "base.mook")
        if digest(run / "base.mook") != expected:
            raise SystemExit("Kopie stimmt nicht")
        env = os.environ.copy()
        env.update(P5_EXT05_DIR=str(run), P5_EXT05_VARIANTE="B",
                   P5_EXT05_SEED=str(seed), P5_EXT05_RATE=str(rate),
                   P5_EXT08_OPTIMIERER=optimizer, P5_EXT08_SMOKE="1" if smoke else "0",
                   MOO_KI_GPU="0" if smoke else "1",
                   MOO_KI_GPU_ERZWINGEN="0" if smoke else "1",
                   MOO_KI_GPU_STRIKT="0" if smoke else "1")
        print("START", run_id, "remaining", round(available, 2), flush=True)
        process = None
        try:
            with (run / "run.log").open("w") as log:
                process = subprocess.Popen(["compiler/target/p5-ext08-train"], env=env,
                                           stdout=log, stderr=subprocess.STDOUT,
                                           text=True, start_new_session=True)
                try:
                    while True:
                        pid, status, usage = os.wait4(process.pid, os.WNOHANG)
                        if pid:
                            process.returncode = os.waitstatus_to_exitcode(status)
                            break
                        if time.monotonic() - run_start > available:
                            raise TimeoutError("EXT-08 Laufzeitbudget erschoepft")
                        time.sleep(0.2)
                except BaseException as error:
                    os.killpg(process.pid, signal.SIGKILL)
                    os.wait4(process.pid, 0)
                    if not smoke and isinstance(error, TimeoutError):
                        (out / "budget_stop.json").write_text(json.dumps({"status": "unvollstaendig", "lauf": run_id, "grund": "Zeitlimit waehrend Lauf"}, indent=2))
                    raise
        finally:
            elapsed = time.monotonic() - run_start
            if use_ledger:
                append_ledger(run_id, begin, run_start)
    output = (run / "run.log").read_text()
    print(output, flush=True)
    if process.returncode:
        raise SystemExit(f"Lauf fehlgeschlagen: {process.returncode}")
    lines = [s[len("EXT05_RESULT "):] for s in output.splitlines() if s.startswith("EXT05_RESULT ")]
    if len(lines) != 1:
        raise SystemExit("Ergebnis fehlt")
    result = json.loads(lines[0])
    expected_steps = 2 if smoke else 154
    if result["variante"] != "B" or result["seed"] != seed or result["schritte"] != expected_steps:
        raise SystemExit("Laufmetadaten ungueltig")
    if result["optimizer_art"] != optimizer or result["optimizer_start_t"] != 0 or result["optimizer_end_t"] != expected_steps:
        raise SystemExit("Optimiererzustand ist nicht frisch oder Schrittzaehler falsch")
    if not smoke and result["zielpositionen"] != 4530:
        raise SystemExit("Zielbudget ungueltig")
    if digest(binary_path) != binary_sha256:
        raise SystemExit("Trainer-Binaerdatei waehrend des Laufs veraendert")
    result.update(optimierer=optimizer, weight_decay=0.01 if optimizer == "adamw" else 0,
                  initialzustand="frisch", laufzeit_sekunden=elapsed,
                  lock_wartezeit_sekunden=wait_seconds, ledger_job_id=run_id,
                  schritte_pro_sekunde=result["schritte"] / elapsed,
                  basis_sha256=expected, binary_sha256=binary_sha256,
                  korrekturdaten_sha256="b2aa8cc9b19e68a14117db8e772e5fdb1490aca631763372299462c0231ad529",
                  normaltext_sha256="71e77e722845bc603dc40127cc2d3eaca1b7b5fc1649a232ee146b503f9f822c",
                  max_rss_kib=usage.ru_maxrss, smoke=smoke)
    (run / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2))
    if digest(base) != expected:
        raise SystemExit("Basischeckpoint wurde veraendert")
with lock_file(out / ".runner.lock", timeout=1):
    if smoke:
        smoke_rates = rates if os.environ.get("P5_EXT08_SMOKE_RATES") == "2" else (0.0001,)
        for rate in smoke_rates:
            for optimizer in optimizers:
                run_one(11, rate, optimizer)
        if os.environ.get("P5_EXT08_REPLAY_SMOKE") == "1":
            for rate in smoke_rates:
                for optimizer in optimizers:
                    run_one(11, rate, optimizer)
        if fixture:
            total, own = budget()
            if total != own or total <= 0 or len(ledger_path.read_text().splitlines()) != len(optimizers) * len(smoke_rates):
                raise SystemExit("Fixture-Ledger falsch")
            print("EXT08 LEDGER FIXTURE OK", round(total, 3), flush=True)
    else:
        for rate in rates:
            for optimizer in optimizers:
                run_one(11, rate, optimizer)
        env = os.environ.copy()
        env.pop("P5_EXT08_SMOKE_DIR", None)
        env.update(P5_EXT08_STAGE="lr", MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0", MOO_KI_GPU_STRIKT="0")
        evaluation = subprocess.run(["mise", "run", "p5-ext08-eval"], env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=1800, check=True)
        (out / "lr_eval.log").write_text(evaluation.stdout)
        selections = json.loads((out / "selection_lr.json").read_text())
        chosen = {row["variante"]: row["sieger"]["rate"] for row in selections}
        if set(chosen) != set(optimizers) or any(v not in ("0.0001", "0.0003") for v in chosen.values()):
            raise SystemExit("Ungueltige LR-Auswahl")
        (out / "chosen_rates.json").write_text(json.dumps(chosen, indent=2))
        for seed in (23, 47):
            for optimizer in optimizers:
                run_one(seed, float(chosen[optimizer]), optimizer)
    print("EXT08 RUNS COMPLETE", str(out), round(time.monotonic()-start, 2), flush=True)
