import hashlib, json, math, os, pathlib, statistics, subprocess
out = pathlib.Path("build/p5_ext05")
manifest = json.loads((out / "conditional_manifest.json").read_text())
rates = json.loads((out / "chosen_rates.json").read_text())
selection = json.loads((out / "selection.json").read_text())
tasks = ("p5-train-nll-baseline", "p5-grammatik-baseline", "p5-forgetting-baseline", "p5-sampling-baseline")
rows = []
for group in manifest:
    variant, seed = group["variante"], group["seed"]
    run = json.loads((out / f"{variant}-{seed}-{rates[variant]}" / "result.json").read_text())
    for step in (100, 154):
        candidates = [r for r in group["berichte"] if r["rate"] == rates[variant] and r["schritt"] == step]
        if len(candidates) != 1:
            raise SystemExit(f"Checkpoint fehlt oder doppelt: {variant} {seed} {step}")
        pair = candidates[0]
        path = pathlib.Path(pair["checkpoint_pfad"])
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != pair["checkpoint_sha256"]:
            raise SystemExit("Checkpoint-Hash abweichend")
        docs = [r for r in run["berichte"] if r["checkpoint_sha256"] == digest]
        if len(docs) != 1:
            raise SystemExit("Dokumentmetrik fehlt oder doppelt")
        target = out / "quality" / digest
        target.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, MOO_KI_GPU="0", P5_CKPT=str(path.resolve()))
        results = {}
        for task in tasks:
            log, ok = target / (task + ".log"), target / (task + ".ok")
            if not (log.exists() and ok.exists()):
                print("QUALITY", variant, seed, step, task, flush=True)
                proc = subprocess.run(["mise", "run", task], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                log.write_text(proc.stdout)
                if proc.returncode:
                    raise SystemExit(f"{task}: RC {proc.returncode}")
                ok.write_text("RC=0\n")
            values = [json.loads(line) for line in log.read_text().splitlines() if line.startswith("{")]
            if len(values) != 1 or values[0]["checkpoint_sha256"] != digest:
                raise SystemExit("Qualitätsbericht ungültig")
            results[task] = values[0]
        fix = sum(v * 10000**i for i, v in enumerate(pair["nll_fix30_gruppen_basis10000"]))
        grammar = results["p5-grammatik-baseline"]
        samples = results["p5-sampling-baseline"]["ausgaben"]
        rows.append(dict(variante=variant, seed=seed, schritt=step, checkpoint_sha256=digest,
            val_paar_bits_pro_byte=fix / 2**30 / pair["bytes"] / math.log(2),
            val_dokument_bits_pro_byte=docs[0]["nll"] / docs[0]["bytes"] / math.log(2),
            train_bits_pro_byte=results["p5-train-nll-baseline"]["bits_pro_byte"],
            grammatik_richtig=grammar["richtig"], grammatik_gesamt=grammar["anzahl"],
            forgetting_bits_pro_byte=results["p5-forgetting-baseline"]["bits_pro_byte"],
            sampling_gueltig=sum(bool(s["utf8_gueltig"]) for s in samples), sampling_gesamt=len(samples)))
rows.sort(key=lambda r: (r["seed"], r["schritt"], r["variante"]))
selected = json.loads((out / "bericht.json").read_text())["ergebnisse"]
fields = ("val_paar_bits_pro_byte", "val_dokument_bits_pro_byte", "train_bits_pro_byte", "grammatik_richtig", "forgetting_bits_pro_byte", "sampling_gueltig")
def stats(items):
    if len(items) != 3:
        raise SystemExit("Drei Seeds erforderlich")
    return {k: {"mittelwert": statistics.mean(r[k] for r in items), "min": min(r[k] for r in items), "max": max(r[k] for r in items)} for k in fields}
summary = {v + "_154": stats([r for r in rows if r["variante"] == v and r["schritt"] == 154]) for v in ("A", "B")}
summary["B_sieger"] = stats([r for r in selected if r["variante"] == "B"])
result = {"feste_schritte": rows, "statistik": summary}
(out / "nachtrag.json").write_text(json.dumps(result, ensure_ascii=False, indent=2))
print("NACHTRAG_COMPLETE", len(rows), flush=True)
