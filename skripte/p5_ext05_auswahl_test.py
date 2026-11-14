import json, os, pathlib, subprocess, tempfile
root = pathlib.Path.cwd()
base = root / "beispiele/p4_silben/run_sil14_grimm36k/full.mook"
report = {"checkpoint_pfad": str(base),
          "checkpoint_sha256": "2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d",
          "tokenizer_sha256": "20ef4b48638359a42141de61ff30d38be7f723c7fe8c612506e9d67f59b62eed",
          "split_sha256": "1fc768f2fcb8956d75f30d99c62b5e65978d1158d501764d68dc6ef8b35fcff2",
          "paare": 15, "bytes": 696, "codepoints": 681, "tokens": 100,
          "schritt": 0, "rate": "0.0001"}
high = dict(report, nll_fix30_gruppen_basis10000=[2])
low = dict(report, nll_fix30_gruppen_basis10000=[1])
reports = [dict(entry, rate=rate) for rate in ("0.0001", "0.0003", "0.001") for entry in (high, low)]
data = [{"variante": v, "seed": s, "berichte": [dict(x) for x in reports]}
        for s in (11, 23, 47) for v in ("A", "B")]
with tempfile.TemporaryDirectory(dir="build", prefix="p5-select-test-") as tmp:
    path = pathlib.Path(tmp) / "manifest.json"
    env = os.environ.copy()
    env.update(P5_EXT05_MANIFEST=str(path), P5_EXT05_PER_RUN="1", MOO_KI_GPU="0")
    path.write_text(json.dumps(data))
    def select():
        return subprocess.run(["compiler/target/p5-ext05-auswahl"], env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    ok = select()
    rows = [json.loads(x.removeprefix("EXT05_AUSWAHL ")) for x in ok.stdout.splitlines()
            if x.startswith("EXT05_AUSWAHL ")]
    if ok.returncode or len(rows) != 6 or any(x["sieger"]["nll_fix30_gruppen_basis10000"] != [1] for x in rows):
        raise SystemExit("Exakte Auswahl fehlgeschlagen: " + ok.stdout)
    data[0]["berichte"][0]["checkpoint_sha256"] = "0" * 64
    path.write_text(json.dumps(data))
    if select().returncode == 0:
        raise SystemExit("Veränderter Checkpoint nicht abgelehnt")
print("EXT05 AUSWAHL TEST OK: 6 Entscheidungen, Hashabweichung abgelehnt")
