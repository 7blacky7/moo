import hashlib, json, os, pathlib, shutil, signal, subprocess, sys, time
root = pathlib.Path.cwd()
base = root / "beispiele/p4_silben/run_sil14_grimm36k/full.mook"
expected = "2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d"
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
if digest(base) != expected:
    raise SystemExit("Basischeckpoint stimmt nicht")
out = root / "build/p5_ext05"
out.mkdir(parents=True, exist_ok=True)
start = time.monotonic()
for seed in (11, 23, 47):
    for rate in (0.0001, 0.0003, 0.001):
        for variant in ("A", "B"):
            run = out / f"{variant}-{seed}-{rate}"
            run.mkdir(exist_ok=False)
            shutil.copyfile(base, run / "base.mook")
            if digest(run / "base.mook") != expected:
                raise SystemExit("Kopie stimmt nicht")
            env = os.environ.copy()
            env.update(P5_EXT05_DIR=str(run), P5_EXT05_VARIANTE=variant,
                       P5_EXT05_SEED=str(seed), P5_EXT05_RATE=str(rate),
                       MOO_KI_GPU="1", MOO_KI_GPU_ERZWINGEN="1", MOO_KI_GPU_STRIKT="1")
            remaining = 7200 - (time.monotonic() - start)
            if remaining <= 0:
                raise SystemExit("EXT-05 Zeitbudget ausgeschoepft")
            cmd = ["flock", "-w", "1800", "build/p5_gpu.lock", "compiler/target/p5-ext05-train"]
            print("START", variant, seed, rate, flush=True)
            process = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, start_new_session=True)
            try:
                output, _ = process.communicate(timeout=min(1800, remaining))
            except BaseException:
                os.killpg(process.pid, signal.SIGKILL)
                output, _ = process.communicate()
                (run / "run.log").write_text(output)
                raise
            (run / "run.log").write_text(output)
            print(output, flush=True)
            if process.returncode:
                raise SystemExit(f"Lauf fehlgeschlagen: {process.returncode}")
            lines = [s[len("EXT05_RESULT "):] for s in output.splitlines() if s.startswith("EXT05_RESULT ")]
            if len(lines) != 1:
                raise SystemExit("Ergebnis fehlt")
            (run / "result.json").write_text(json.dumps(json.loads(lines[0]), ensure_ascii=False, indent=2))
            if digest(base) != expected:
                raise SystemExit("Basischeckpoint wurde veraendert")
print("EXT05 RUNS COMPLETE", round(time.monotonic()-start, 2), flush=True)
