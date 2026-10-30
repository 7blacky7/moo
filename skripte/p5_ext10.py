"""EXT-10: einmalige Auswertung; --val-check und --self-test beruehren keinen Test."""
import argparse
import datetime
import decimal
import hashlib
import json
import math
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path.cwd()
OUT = ROOT / "build/p5_ext10"
TEST_HASH = "0830afb0290ee0d010754e9f345692cbdc18cddfb4e72ed07318be0651a3cf50"
VAL_HASH = "1fc768f2fcb8956d75f30d99c62b5e65978d1158d501764d68dc6ef8b35fcff2"
HOLD_HASH = "d057c8fbc0392bddfdd584cd9075e7219e61e0f55f411da3a4099025169075a8"
VAL_DOC_HASH = "c7ed332fa3d5324f0fbbcf5fab79127c88e5e370893673cd724c56aedc2ffaf7"
TOK_HASH = "20ef4b48638359a42141de61ff30d38be7f723c7fe8c612506e9d67f59b62eed"
BASE = ROOT / "beispiele/p4_silben/run_sil14_grimm36k/full.mook"
BASE_HASH = "2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d"

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def exclusive_lock(path, payload):
    data = json.dumps(payload, ensure_ascii=False, indent=2).encode() + b"\n"
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    dir_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(dir_fd)
    finally:
        os.close(dir_fd)

def from_groups(groups):
    if not isinstance(groups, list) or any(type(x) is not int or x < 0 or x >= 10000 for x in groups):
        raise ValueError("Festkommagruppen ungueltig")
    return sum(x * 10000 ** i for i, x in enumerate(groups))

def bits(numerator, denominator, byte_count):
    if numerator < 0 or denominator <= 0 or byte_count <= 0:
        raise ValueError("Ungueltige NLL")
    with decimal.localcontext() as ctx:
        ctx.prec = 50
        value = decimal.Decimal(numerator) / decimal.Decimal(denominator * byte_count) / decimal.Decimal(2).ln()
        return {"bits_pro_byte_dezimal": str(value),
                "nll_zaehler": str(numerator), "nll_nenner": str(denominator),
                "bytes": byte_count, "basis": 2}

def checked_manifest(path):
    data = json.loads(path.read_text())
    if not isinstance(data, list) or not data:
        raise ValueError("Nichtleere feste Checkpointliste erforderlich")
    seen = set()
    for item in data:
        if not isinstance(item, dict) or set(item) != {"name", "pfad", "sha256"}:
            raise ValueError("Checkpointliste: name, pfad, sha256 erforderlich")
        if not all(isinstance(item[k], str) and item[k] for k in item):
            raise ValueError("Checkpointliste enthaelt leere Werte")
        if item["name"] in seen:
            raise ValueError("Doppelter Modellname")
        seen.add(item["name"])
        checkpoint = pathlib.Path(item["pfad"]).resolve()
        if not checkpoint.is_relative_to(ROOT) or checkpoint.suffix != ".mook":
            raise ValueError("Checkpointpfad muss eine Projektdatei mit .mook-Endung sein")
        if digest(checkpoint) != item["sha256"]:
            raise ValueError("Checkpoint-Hash stimmt nicht: " + item["name"])
    return data

def invoke(binary, env, prefix):
    run = subprocess.run([str(binary)], env=env, text=True, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    if run.returncode:
        raise RuntimeError(f"Evaluator {binary.name} RC {run.returncode}: {run.stdout}")
    lines = [line.removeprefix(prefix) for line in run.stdout.splitlines() if line.startswith(prefix)]
    if len(lines) != 1:
        raise ValueError("Evaluatorausgabe fehlt")
    return json.loads(lines[0])

def measure(item, split, document, expected_split, expected_doc):
    env = os.environ.copy()
    for key in ("MOO_KI_GPU_ERZWINGEN", "MOO_KI_GPU_STRIKT", "P5_EXT10_SPLIT", "P5_EXT10_DUMMY", "P5_EXT10_DUMMY_PILOT", "P5_EXT10_DUMMY_SPLIT"):
        env.pop(key, None)
    env.update(MOO_KI_GPU="0", P5_CKPT=str(pathlib.Path(item["pfad"]).resolve()),
               P5_EXT05_SPLIT=split, P5_EXT10_DOK=str(document))
    if split == "test":
        env["P5_EXT10_SPLIT"] = "test"
    pair = invoke(ROOT / "compiler/target/p5-ext10-paar", env,
                  "EXT05_PAAR_TEST " if split == "test" else "EXT05_PAAR_VAL ")
    pref = invoke(ROOT / "compiler/target/p5-ext10-praef", env, "EXT09_PRAEF_VAL ")
    doc = invoke(ROOT / "compiler/target/p5-ext10-dokument", env, "EXT10_DOK ")
    for report in (pair, pref, doc):
        if report["checkpoint_sha256"] != item["sha256"] or report["tokenizer_sha256"] != TOK_HASH:
            raise ValueError("Checkpoint-/Tokenizeridentitaet falsch")
    if (pair["split_sha256"] != expected_split or pref["split_sha256"] != expected_split or
        pair["paare"] != 15 or pref["paare"] != 15 or doc["dokument_sha256"] != expected_doc):
        raise ValueError("Split-/Dokumentidentitaet falsch")
    pbits = bits(from_groups(pair["nll_fix30_gruppen_basis10000"]), 2**30, pair["bytes"])
    frac = from_groups(doc["nll_bruch_basis10000_2_48"])
    if not 0 <= frac < 2**48 or type(doc["nll_ganz"]) is not int:
        raise ValueError("Dokument-NLL ungueltig")
    dbits = bits(doc["nll_ganz"] * 2**48 + frac, 2**48, doc["bytes"])
    if (pref["bewertbar"] + len(pref["nicht_bewertbar"]) != 15 or
        len(set(pref["nicht_bewertbar"])) != len(pref["nicht_bewertbar"])):
        raise ValueError("Praeferenzabdeckung ungueltig")
    return {"name": item["name"], "checkpoint_sha256": item["sha256"],
            "paar": pbits, "praeferenz": {"korrekt": pref["korrekt"],
            "gleich": pref["gleich"], "paare": pref["paare"], "bewertbar": pref["bewertbar"],
            "nicht_bewertbar": pref["nicht_bewertbar"]}, "dokument": dbits}

def self_test():
    with tempfile.TemporaryDirectory(prefix="p5-ext10-dummy-", dir=ROOT / "build") as tmp:
        lock = pathlib.Path(tmp) / "dummy.lock"
        exclusive_lock(lock, {"test": "dummy", "liste": ["a"]})
        try:
            exclusive_lock(lock, {"test": "nochmal"})
        except FileExistsError:
            pass
        else:
            raise AssertionError("Zweiter Lauf muss scheitern")
        assert json.loads(lock.read_text())["liste"] == ["a"]
        fake = pathlib.Path(tmp) / "dummy.mook"
        fake.write_bytes(b"dummy-checkpoint")
        manifest = pathlib.Path(tmp) / "manifest.json"
        entry = {"name": "dummy", "pfad": str(fake), "sha256": digest(fake)}
        manifest.write_text(json.dumps([entry]))
        assert checked_manifest(manifest) == [entry]
        fake.write_bytes(b"changed")
        try:
            checked_manifest(manifest)
        except ValueError:
            pass
        else:
            raise AssertionError("Checkpoint-Drift akzeptiert")
        pilot = pathlib.Path(tmp) / "pilot.json"
        split = pathlib.Path(tmp) / "split.json"
        examples = [{"aufgabe_id": f"dummy-{i}", "prompt": "Hallo", "akzeptiert": True,
                     "aufgabe": "Korrigiere", "original": "Haus", "korrektur": "Haus" if i == 0 else "Baum",
                     "begruendung": "Dummy", "art": "dummy"} for i in range(15)]
        pilot.write_text(json.dumps({"beispiele": examples}))
        split.write_text(json.dumps({"ids": [p["aufgabe_id"] for p in examples]}))
        env = os.environ.copy()
        env.update(MOO_KI_GPU="0", P5_CKPT=str(BASE), P5_EXT10_DUMMY="1",
                   P5_EXT10_DUMMY_PILOT=str(pilot), P5_EXT10_DUMMY_SPLIT=str(split))
        env.pop("P5_EXT10_SPLIT", None)
        result = invoke(ROOT / "compiler/target/p5-ext10-praef", env, "EXT09_PRAEF_VAL ")
        assert result["paare"] == 15 and result["bewertbar"] == 14
        assert result["nicht_bewertbar"] == ["dummy-0"]
        del examples[1]["korrektur"]
        pilot.write_text(json.dumps({"beispiele": examples}))
        try:
            invoke(ROOT / "compiler/target/p5-ext10-praef", env, "EXT09_PRAEF_VAL ")
        except RuntimeError as error:
            assert "Praeferenztext fehlt" in str(error)
        else:
            raise AssertionError("Fehlender Text akzeptiert")
        assert from_groups([9999, 42]) == 429999
        assert bits(4, 2, 2)["nll_zaehler"] == "4"
        try:
            from_groups([10000])
        except ValueError:
            pass
        else:
            raise AssertionError("Ungueltige Gruppe akzeptiert")
    print("EXT10 DUMMY SPERRE UND PRAEZISION OK")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--val-check", action="store_true")
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--manifest", type=pathlib.Path)
    args = parser.parse_args()
    if sum((args.self_test, args.val_check, args.execute)) != 1:
        parser.error("Genau einen Modus angeben")
    if args.self_test:
        self_test()
        return
    if args.val_check:
        item = {"name": "basis-val", "pfad": str(BASE), "sha256": BASE_HASH}
        if digest(BASE) != BASE_HASH:
            raise ValueError("Basischeckpoint falsch")
        doc = ROOT / "beispiele/p5/daten/val_korrekturen.txt"
        if digest(doc) != VAL_DOC_HASH:
            raise ValueError("Val-Dokument falsch")
        result = measure(item, "val", doc, VAL_HASH, VAL_DOC_HASH)
        print(json.dumps(result, ensure_ascii=False))
        return
    if args.manifest is None:
        parser.error("--execute benoetigt feste --manifest-Liste")
    models = checked_manifest(args.manifest)
    OUT.mkdir(parents=True, exist_ok=True)
    lock = OUT / "test_ausgewertet.lock"
    exclusive_lock(lock, {"zeit_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                          "checkpointliste": models, "pilot_test_sha256": TEST_HASH,
                          "p4_holdout_sha256": HOLD_HASH,
                          "manifest_sha256": digest(args.manifest)})
    # Testdaten werden erst NACH dauerhaftem Erstellen der exklusiven Sperre geoeffnet.
    test_file = ROOT / "beispiele/p5/daten/split_test.json"
    holdout = ROOT / "beispiele/p4_silben/run_sil14_grimm36k/holdout.txt"
    if digest(test_file) != TEST_HASH or digest(holdout) != HOLD_HASH:
        raise ValueError("Testdatenidentitaet falsch; Sperre bleibt bestehen")
    report = {"status": "unvollstaendig", "split_sha256": TEST_HASH,
              "holdout_sha256": HOLD_HASH, "modelle": []}
    def persist():
        tmp = OUT / "ergebnis.json.tmp"
        with tmp.open("w") as f:
            f.write(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, OUT / "ergebnis.json")
    persist()
    for item in models:
        report["modelle"].append(measure(item, "test", holdout, TEST_HASH, HOLD_HASH))
        persist()
        print("EXT10 MODELL FERTIG", item["name"], flush=True)
    report["status"] = "abgeschlossen"
    persist()
    print("EXT10 AUSWERTUNG FERTIG")

if __name__ == "__main__":
    main()
