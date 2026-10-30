"""EXT-08: exact conditional validation using the shared EXT-05 evaluator."""
import hashlib, json, os, pathlib, subprocess
root = pathlib.Path.cwd()
smoke_dir = os.environ.get("P5_EXT08_SMOKE_DIR")
smoke = bool(smoke_dir)
lr_stage = os.environ.get("P5_EXT08_STAGE") == "lr"
partial = not lr_stage and os.environ.get("P5_EXT08_PARTIAL") == "1"
out = pathlib.Path(smoke_dir) if smoke else root / "build/p5_ext08"
seeds = (11,) if smoke or lr_stage else (11, 23, 47)
all_rates = ("0.0001", "0.0003")
chosen_path = out / "chosen_rates.json"
chosen = {} if smoke or lr_stage or (partial and not chosen_path.exists()) else json.loads(chosen_path.read_text())
if (chosen and (set(chosen) != {"sgd", "adam", "adamw"} or any(v not in all_rates for v in chosen.values()))) or (not (smoke or lr_stage or partial) and not chosen):
    raise SystemExit("Ungueltige festgelegte Lernraten")
steps = 2 if smoke else 154
cache_path = out / "pair_cache.json"
cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
manifest = []
run_manifest = []
for seed in seeds:
    for optimizer in ("sgd", "adam", "adamw"):
        rows = []
        if partial and seed != 11 and optimizer not in chosen:
            continue
        rates = (all_rates if os.environ.get("P5_EXT08_SMOKE_RATES") == "2" else ("0.0001",)) if smoke else all_rates if seed == 11 else (chosen[optimizer],)
        for rate in rates:
            result_path = out / f"{optimizer}-{seed}-{rate}/result.json"
            if partial and not result_path.exists():
                print("UNVOLLSTAENDIG", result_path, flush=True)
                continue
            result = json.loads(result_path.read_text())
            if (result["variante"] != "B" or result["optimierer"] != optimizer or result["seed"] != seed
                    or result["schritte"] != steps or (not smoke and result["zielpositionen"] != 4530)
                    or result["optimizer_start_t"] != 0 or result["optimizer_end_t"] != steps
                    or result["smoke"] != smoke):
                raise SystemExit(f"Ungueltiger Lauf: {result_path}")
            current = []
            for report in result["berichte"]:
                path = pathlib.Path(report["checkpoint_pfad"])
                actual = hashlib.sha256(path.read_bytes()).hexdigest()
                if actual != report["checkpoint_sha256"]:
                    raise SystemExit(f"Checkpoint veraendert: {path}")
                if actual not in cache:
                    env = os.environ.copy()
                    env.update(P5_CKPT=str(path), P5_EXT05_SPLIT="val", MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0", MOO_KI_GPU_STRIKT="0")
                    run = subprocess.run(["compiler/target/p5-ext08-paar-val"], env=env, text=True,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
                    matches = [line.removeprefix("EXT05_PAAR_VAL ") for line in run.stdout.splitlines()
                               if line.startswith("EXT05_PAAR_VAL ")]
                    if len(matches) != 1:
                        raise SystemExit(f"Paarmetrik fehlt: {path}")
                    cache[actual] = json.loads(matches[0])
                    cache_path.write_text(json.dumps(cache, ensure_ascii=False, indent=2))
                    print("EVAL", len(cache), actual, flush=True)
                score = dict(cache[actual])
                if (score["checkpoint_sha256"] != actual or score["paare"] != 15 or score["bytes"] != 696
                        or score["split_sha256"] != "1fc768f2fcb8956d75f30d99c62b5e65978d1158d501764d68dc6ef8b35fcff2"
                        or score["tokenizer_sha256"] != "20ef4b48638359a42141de61ff30d38be7f723c7fe8c612506e9d67f59b62eed"):
                    raise SystemExit(f"Paarmetrik ungueltig: {path}")
                score["checkpoint_pfad"] = str(path)
                score["schritt"] = 0 if path.name == "base.mook" else int(path.name.removeprefix("val_").removesuffix(".mook"))
                score["rate"] = rate
                current.append(score)
            current.sort(key=lambda r: (r["schritt"], r["checkpoint_sha256"], r["rate"]))
            rows.extend(current)
            run_manifest.append({"variante": optimizer, "seed": seed, "berichte": current})
        rows.sort(key=lambda r: (r["schritt"], r["checkpoint_sha256"], r["rate"]))
        if rows:
            manifest.append({"variante": optimizer, "seed": seed, "berichte": rows})
if not manifest:
    raise SystemExit("Keine vollstaendigen Laeufe fuer eine Val-Auswertung")
stage = "lr" if lr_stage else "partial" if partial else "smoke" if smoke else "final"
def select(entries, name):
    path = out / f"manifest_{stage}_{name}.json"
    path.write_text(json.dumps(entries, ensure_ascii=False, indent=2))
    env = os.environ.copy()
    env.update(P5_EXT05_MANIFEST=str(path), P5_EXT05_PER_RUN="0",
               MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0", MOO_KI_GPU_STRIKT="0")
    run = subprocess.run(["compiler/target/p5-ext08-auswahl"], env=env, text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
    (out / f"selection_{stage}_{name}.log").write_text(run.stdout)
    selected = [json.loads(line.removeprefix("EXT05_AUSWAHL ")) for line in run.stdout.splitlines()
                if line.startswith("EXT05_AUSWAHL ")]
    if len(selected) != len(entries):
        raise SystemExit("Auswahl hat falsche Zahl an Berichten")
    return selected
per_run = select(run_manifest, "runs")
selected = select(manifest, "combined")
if lr_stage:
    (out / "selection_lr.json").write_text(json.dumps(selected, ensure_ascii=False, indent=2))
elif partial:
    (out / "selection_partial_runs.json").write_text(json.dumps(per_run, ensure_ascii=False, indent=2))
    (out / "selection_partial.json").write_text(json.dumps(selected, ensure_ascii=False, indent=2))
    (out / "selection_partial_meta.json").write_text(json.dumps({"status": "unvollstaendig", "laeufe": len(per_run), "seed_optimierer": len(selected)}, indent=2))
else:
    (out / "selection_runs.json").write_text(json.dumps(per_run, ensure_ascii=False, indent=2))
    (out / "selection.json").write_text(json.dumps(selected, ensure_ascii=False, indent=2))
print("EXT08 CONDITIONAL SELECTION", stage, len(cache), len(per_run), len(selected), flush=True)
