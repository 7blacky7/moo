import json, math, pathlib
out = pathlib.Path("build/p5_ext05")
chosen = json.loads((out / "selection.json").read_text())
if len(chosen["selected"]) != 6 or len(chosen["runs"]) != 8:
    raise SystemExit("Auswahl unvollständig")
rates = json.loads((out / "chosen_rates.json").read_text())
def report(log):
    lines = log.read_text().splitlines()
    values = [json.loads(line) for line in lines if line.startswith("{")]
    if len(values) != 1:
        raise SystemExit(f"Genau ein JSON-Bericht erwartet: {log}")
    return values[0]
rows = []
for item in chosen["selected"]:
    variant, seed, best = item["variante"], item["seed"], item["sieger"]
    digest = best["checkpoint_sha256"]
    quality = out / "quality" / digest
    results = {name: report(quality / (name + ".log")) for name in (
        "p5-train-nll-baseline", "p5-grammatik-baseline",
        "p5-forgetting-baseline", "p5-sampling-baseline")}
    if any(value["checkpoint_sha256"] != digest for value in results.values()):
        raise SystemExit("Qualitätsbericht mit anderem Checkpoint")
    rate = rates[variant]
    run = json.loads((out / f"{variant}-{seed}-{rate}" / "result.json").read_text())
    doc = [x for x in run["berichte"] if x["checkpoint_sha256"] == digest]
    if not doc:
        raise SystemExit("Val-Dokumentbericht für Sieger fehlt")
    nll_fix = sum(x * 10000**i for i, x in enumerate(best["nll_fix30_gruppen_basis10000"]))
    pair_bits = nll_fix / (2**30) / best["bytes"] / math.log(2)
    train = results["p5-train-nll-baseline"]
    grammar = results["p5-grammatik-baseline"]
    forget = results["p5-forgetting-baseline"]
    samples = results["p5-sampling-baseline"]["ausgaben"]
    rows.append({"variante": variant, "seed": seed, "rate_seed11": rate,
        "schritt": best["schritt"], "checkpoint_sha256": digest,
        "val_paar_bits_pro_byte": pair_bits,
        "val_dokument_bits_pro_byte": doc[0]["nll"] / doc[0]["bytes"] / math.log(2),
        "train_bits_pro_byte": train["bits_pro_byte"], "grammatik_richtig": grammar["richtig"],
        "grammatik_gesamt": grammar["anzahl"], "forgetting_bits_pro_byte": forget["bits_pro_byte"],
        "sampling_gueltig": sum(bool(x["utf8_gueltig"]) for x in samples),
        "sampling_gesamt": len(samples),
        "greedy": [{"prompt": x["prompt"], "text": x["text"], "rohbytes": x["rohbytes"]}
                   for x in samples if x["art"] == "greedy"]})
rows.sort(key=lambda x: (x["seed"], x["variante"]))
data = {"status": "vollstaendig", "rate_auswahl": "Seed 11, zwei Werte, bedingte Val-Paar-NLL",
        "trainingsfenster": 77, "schritte": 154, "zielpositionen": 4530,
        "testauswertung": "EXT-10 vorbehalten", "ergebnisse": rows,
        "globaler_val_sieger": chosen["overall"]}
(out / "bericht.json").write_text(json.dumps(data, ensure_ascii=False, indent=2))
print(json.dumps(data, ensure_ascii=False, separators=(",", ":")))
