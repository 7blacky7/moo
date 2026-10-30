"""Evaluate each selected EXT-08 run using the shared frozen P5 probes."""
import hashlib, json, math, os, pathlib, subprocess, sys, time
root = pathlib.Path.cwd()
smoke_dir = os.environ.get("P5_EXT08_SMOKE_DIR")
smoke = bool(smoke_dir)
out = pathlib.Path(smoke_dir) if smoke else root / "build/p5_ext08"
partial = os.environ.get("P5_EXT08_PARTIAL") == "1"
selection_file = "selection_partial_runs.json" if partial else "selection_runs.json"
selected = json.loads((out / selection_file).read_text())
if (partial and not selected) or (not partial and len(selected) != (3 if smoke else 12)):
    raise SystemExit("Ungueltige Anzahl Val-Laufauswahlen")
if smoke:
    selected = selected[:1]
cache = {}
env = os.environ.copy()
env.update(MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0", MOO_KI_GPU_STRIKT="0")
tasks = (
    ("train", ["compiler/target/p5-ext08-train-eval"]),
    ("grammatik", ["compiler/target/p5-ext08-grammatik"]),
    ("forgetting", ["compiler/target/p5-ext08-forgetting"]),
    ("sampling", [sys.executable, "skripte/p5_sampling_json.py", "compiler/target/p5-ext08-sampling"]),
)
for item in selected:
    report = item["sieger"]
    path = pathlib.Path(report["checkpoint_pfad"])
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != report["checkpoint_sha256"]:
        raise SystemExit("Ausgewaehlter Checkpoint veraendert")
    if digest in cache:
        continue
    target = out / ("quality_partial" if partial else "quality") / digest
    target.mkdir(parents=True, exist_ok=True)
    env["P5_CKPT"] = str(path)
    cache[digest] = str(target)
    for name, cmd in tasks:
        log_path = target / (name + ".log")
        if log_path.exists():
            try:
                record = json.loads(log_path.read_text())
            except (ValueError, OSError) as error:
                raise SystemExit(f"Vorhandenes Qualitaetslog ungueltig: {log_path}") from error
            if record.get("checkpoint_sha256") != digest:
                raise SystemExit(f"Qualitaetslog gehoert nicht zum Checkpoint: {log_path}")
            print("QUALITY_REUSE", digest, name, flush=True)
            continue
        print("QUALITY", digest, name, flush=True)
        start = time.monotonic()
        run = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log_path.write_text(run.stdout)
        print("QUALITY_DONE", name, run.returncode, round(time.monotonic()-start, 2), flush=True)
        if run.returncode:
            raise SystemExit(f"Qualitaetsmessung {name} fehlgeschlagen")
        record = json.loads(run.stdout)
        if record.get("checkpoint_sha256") != digest:
            raise SystemExit(f"Qualitaetsmessung gehoert nicht zum Checkpoint: {log_path}")
(out / ("quality_partial_manifest.json" if partial else "quality_manifest.json")).write_text(json.dumps(cache, indent=2))
rows = []
seen_seeds = {"sgd": set(), "adam": set(), "adamw": set()}
for item in selected:
    report = item["sieger"]
    optimizer, seed, rate = item["variante"], item["seed"], report["rate"]
    run_path = out / f"{optimizer}-{seed}-{rate}" / "result.json"
    run = json.loads(run_path.read_text())
    digest = report["checkpoint_sha256"]
    target = pathlib.Path(cache[digest])
    gram = json.loads((target / "grammatik.log").read_text())
    forgetting = json.loads((target / "forgetting.log").read_text())
    train = json.loads((target / "train.log").read_text())
    sampling = json.loads((target / "sampling.log").read_text())
    val_doc = next((r for r in run["berichte"] if r["checkpoint_sha256"] == digest), None)
    if val_doc is None:
        raise SystemExit("Val-Dokumentbericht fuer Sieger fehlt")
    exact = sum(int(v) * (10000 ** i) for i, v in enumerate(report["nll_fix30_gruppen_basis10000"]))
    seen_seeds[optimizer].add(seed)
    rows.append({
        "optimierer": optimizer, "seed": seed, "rate": rate, "checkpoint_schritt": report["schritt"],
        "checkpoint_sha256": digest, "val_paar_nll_fix30_gruppen": report["nll_fix30_gruppen_basis10000"],
        "val_paar_bits_pro_byte": exact / (2 ** 30) / report["bytes"] / math.log(2),
        "val_dokument_bits_pro_byte_gerundet": val_doc["nll_pro_byte"] / math.log(2),
        "train_bits_pro_byte_gerundet": train["bits_pro_byte"],
        "grammatik_richtig": gram["richtig"], "grammatik_anzahl": gram["anzahl"],
        "forgetting_bits_pro_byte_gerundet": forgetting["bits_pro_byte"],
        "sampling_anzahl": len(sampling["ausgaben"]),
        "sampling_utf8_gueltig": sum(bool(x["utf8_gueltig"]) for x in sampling["ausgaben"]),
        "schritte_pro_sekunde_effektiv": run["schritte_pro_sekunde"],
        "max_rss_kib": run["max_rss_kib"], "gpu": run["gpu"],
        "qualitaetslogs": str(target)
    })
missing = {name: sorted({11, 23, 47} - seeds) for name, seeds in seen_seeds.items()}
summary = {"status": "unvollstaendig" if partial else "smoke" if smoke else "vollstaendig",
           "lr_auswahl_seed": 11, "fehlende_seeds": missing,
           "rangfolge_aus_streuung_abgeleitet": False,
           "gpu_vram_peak": None, "gpu_vram_hinweis": "GPU-Speicherspitze wird nicht erhoben",
           "laeufe": rows}
summary_name = "summary_partial.json" if partial else "summary.json"
(out / summary_name).write_text(json.dumps(summary, ensure_ascii=False, indent=2))
print("EXT08 QUALITY COMPLETE", len(selected), len(cache), flush=True)
