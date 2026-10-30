import hashlib, json, os, pathlib, subprocess
root = pathlib.Path.cwd()
out = root / "build/p5_ext05"
cache = {}
manifest = []
expected_hash = None
seed11_only = os.environ.get("P5_EXT05_SEED11_ONLY") == "1"
chosen = None if seed11_only else json.loads((out / "chosen_rates.json").read_text())
for seed in ((11,) if seed11_only else (11, 23, 47)):
    for variant in ("A", "B"):
        rows = []
        rates = ("0.0001", "0.0003") if seed == 11 else (chosen[variant],)
        for rate in rates:
            result_path = out / f"{variant}-{seed}-{rate}/result.json"
            result = json.loads(result_path.read_text())
            if result["variante"] != variant or result["seed"] != seed or result["schritte"] != 154 or result["zielpositionen"] != 4530:
                raise SystemExit(f"Ungültiger Lauf: {result_path}")
            for report in result["berichte"]:
                path = pathlib.Path(report["checkpoint_pfad"])
                actual = hashlib.sha256(path.read_bytes()).hexdigest()
                if actual != report["checkpoint_sha256"]:
                    raise SystemExit(f"Checkpoint verändert: {path}")
                if actual not in cache:
                    env = os.environ.copy()
                    env.update(P5_CKPT=str(path), MOO_KI_GPU="0", P5_EXT05_SPLIT="val")
                    run = subprocess.run(["compiler/target/p5-ext05-paar-val"], env=env, text=True,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
                    matches = [line.removeprefix("EXT05_PAAR_VAL ") for line in run.stdout.splitlines()
                               if line.startswith("EXT05_PAAR_VAL ")]
                    if len(matches) != 1:
                        raise SystemExit(f"Paarmetrik fehlt: {path}")
                    score = json.loads(matches[0])
                    if (score["checkpoint_sha256"] != actual or score["paare"] != 15 or
                        score["split_sha256"] != "1fc768f2fcb8956d75f30d99c62b5e65978d1158d501764d68dc6ef8b35fcff2" or
                        score["tokenizer_sha256"] != "20ef4b48638359a42141de61ff30d38be7f723c7fe8c612506e9d67f59b62eed"):
                        raise SystemExit(f"Paarmetrik ungültig: {path}")
                    cache[actual] = score
                    print("EVAL", len(cache), actual, flush=True)
                score = dict(cache[actual])
                score["checkpoint_pfad"] = str(path)
                name = path.name
                score["schritt"] = 0 if name == "base.mook" else int(name.removeprefix("val_").removesuffix(".mook"))
                score["rate"] = rate
                rows.append(score)
        rows.sort(key=lambda r: (r["schritt"], r["checkpoint_sha256"], r["rate"]))
        manifest.append({"variante": variant, "seed": seed, "berichte": rows})
manifest_path = out / "conditional_manifest.json"
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2))
print("EXT05 CONDITIONAL MANIFEST", manifest_path, len(cache), flush=True)
