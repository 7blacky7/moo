import hashlib, json, math, os, pathlib, subprocess, sys
root = pathlib.Path.cwd()
out = root / "build/p5_ext06"
out.mkdir(parents=True, exist_ok=True)
if len(sys.argv) < 3:
    raise SystemExit("Nutzung: Basis-SHA=Pfad Modell-SHA=Pfad [...]")
items = []
for arg in sys.argv[1:]:
    digest, path = arg.split("=", 1)
    path = pathlib.Path(path).resolve()
    if len(digest) != 64 or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit(f"Checkpoint-Hash falsch: {path}")
    items.append((digest, path))
if len({digest for digest, _ in items}) != len(items):
    raise SystemExit("Doppelte Checkpoint-SHA")
def json_log(path):
    lines = [json.loads(line) for line in path.read_text().splitlines() if line.startswith("{")]
    if len(lines) != 1:
        raise SystemExit(f"Genau ein JSON-Bericht erwartet: {path}")
    return lines[0]
def execute(task, path, target):
    env = dict(os.environ, MOO_KI_GPU="0", P5_CKPT=str(path))
    print("CPU", path.name, task, flush=True)
    p = subprocess.run(["mise", "run", task], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    target.write_text(p.stdout)
    if p.returncode:
        raise SystemExit(f"{task}: RC {p.returncode}")
    return json_log(target)
reports = {}
for digest, path in items:
    target = out / digest
    target.mkdir(exist_ok=True)
    diagnostic_bin = root / "compiler/target/p5-grammatik-diagnose"
    diagnostic_hash = hashlib.sha256(diagnostic_bin.read_bytes()).hexdigest()
    grammar_log, grammar_stamp = target / "grammatik.log", target / "grammatik.sha256"
    if grammar_log.exists() and grammar_stamp.exists() and grammar_stamp.read_text().strip() == diagnostic_hash:
        grammar = json_log(grammar_log)
    else:
        grammar = execute("p5-grammatik-diagnose", path, grammar_log)
        grammar_stamp.write_text(diagnostic_hash + "\n")
    if grammar["checkpoint_sha256"] != digest or len(grammar["items"]) != 80:
        raise SystemExit("Grammatikbericht ungültig")
    quality = {}
    for name in ("p5-forgetting-baseline", "p5-sampling-baseline"):
        cached = root / "build/p5_ext05/quality" / digest
        if (cached / (name + ".ok")).exists() and (cached / (name + ".log")).exists():
            report = json_log(cached / (name + ".log"))
        else:
            sources = (["beispiele/p5/forgetting_basis.moos"] if name == "p5-forgetting-baseline"
                       else ["beispiele/p5/sampling_runner.moos", "skripte/p5_sampling_json.py"])
            fingerprint = hashlib.sha256(b"".join((root / s).read_bytes() for s in sources)).hexdigest()
            log, stamp = target / (name + ".log"), target / (name + ".sha256")
            if log.exists() and stamp.exists() and stamp.read_text().strip() == fingerprint:
                report = json_log(log)
            else:
                report = execute(name, path, log)
                stamp.write_text(fingerprint + "\n")
        if report["checkpoint_sha256"] != digest:
            raise SystemExit("Qualitätsbericht mit falschem Checkpoint")
        quality[name] = report
    reports[digest] = (grammar, quality)
base = {x["id"]: x for x in reports[items[0][0]][0]["items"]}
def test_p(up, down):
    n = up + down
    return min(1.0, 2 * sum(math.comb(n, i) for i in range(min(up, down) + 1)) / 2**n) if n else 1.0
result = []
for digest, path in items:
    grammar, quality = reports[digest]
    all_items = grammar["items"]
    if set(x["id"] for x in all_items) != set(base):
        raise SystemExit("Itemmenge abweichend")
    metrics = {}
    for flag in ("roh_richtig", "token_richtig"):
        categories = {}
        for item in all_items:
            cat = item["kategorie"]
            categories.setdefault(cat, {"anzahl": 0, "richtig": 0})
            categories[cat]["anzahl"] += 1
            categories[cat]["richtig"] += int(item[flag])
        up = [x["id"] for x in all_items if not base[x["id"]][flag] and x[flag]]
        down = [x["id"] for x in all_items if base[x["id"]][flag] and not x[flag]]
        metrics[flag] = {"richtig": sum(int(x[flag]) for x in all_items), "anzahl": len(all_items),
            "kategorien": categories, "falsch_zu_richtig": up, "richtig_zu_falsch": down,
            "mcnemar_exakt_orientierung": test_p(len(up), len(down))}
    samples = quality["p5-sampling-baseline"]["ausgaben"]
    result.append({"checkpoint_sha256": digest, "checkpoint_pfad": str(path), "grammatik": metrics,
        "forgetting_bits_pro_byte": quality["p5-forgetting-baseline"]["bits_pro_byte"],
        "utf8_gueltig": sum(bool(x["utf8_gueltig"]) for x in samples), "utf8_gesamt": len(samples),
        "samples": samples})
report = {"status": "nicht validiert, deskriptiv", "basis_sha256": items[0][0],
    "mcnemar_hinweis": "nur Orientierung; abhängige Items, keine Signifikanzbehauptung",
    "empfehlung": "Span-konditionierte NLL mit balanciertem unabhängigem Satz und Positiv-/Negativkontrollen entwickeln.",
    "modelle": result}
(out / "vergleich.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
print("EXT06_COMPLETE", len(result), flush=True)
