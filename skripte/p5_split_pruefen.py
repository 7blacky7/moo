"""Reproduziert und prüft den eingefrorenen, promptgruppierten P5-Split."""
import hashlib
import json
from pathlib import Path

ROOT = Path("beispiele/p5/daten")
HASHES = {
    "pilot_lehrer_v2.json": "42f3f98b8a7004c0fa8ded7b94642701870edbb0c37a477c6895846be6b5c629",
    "split_train.json": "97b4762f84914b9a62f25006ef441f99cf7020e668c754c9ee0503f20720ee0e",
    "split_val.json": "1fc768f2fcb8956d75f30d99c62b5e65978d1158d501764d68dc6ef8b35fcff2",
    "split_test.json": "0830afb0290ee0d010754e9f345692cbdc18cddfb4e72ed07318be0651a3cf50",
    "val_korrekturen.txt": "c7ed332fa3d5324f0fbbcf5fab79127c88e5e370893673cd724c56aedc2ffaf7",
    "train_korrekturen.txt": "d3504252784c2702d959b3aa74ed7f48810652d29d4ef23d5c410074c29d7c44",
}
def checked(name):
    raw = (ROOT / name).read_bytes()
    if hashlib.sha256(raw).hexdigest() != HASHES[name]:
        raise ValueError("Hashabweichung: " + name)
    return raw
def choose(groups, target):
    solutions = {0: []}
    for index, (_, ids) in enumerate(groups):
        for total in sorted(solutions, reverse=True):
            new = total + len(ids)
            if new <= target and new not in solutions:
                solutions[new] = solutions[total] + [index]
    indexes = set(solutions[target])
    return ([g for i, g in enumerate(groups) if i in indexes],
            [g for i, g in enumerate(groups) if i not in indexes])
def main():
    dataset = json.loads(checked("pilot_lehrer_v2.json"))
    pairs = dataset["beispiele"]
    if len(pairs) != 100 or len({p["aufgabe_id"] for p in pairs}) != 100:
        raise ValueError("Pilot-ID-Anzahl")
    groups = {}
    for pair in pairs:
        if pair["akzeptiert"] is not True:
            raise ValueError("Nicht akzeptiertes Paar")
        groups.setdefault(pair["herkunft"]["prompt"], []).append(pair["aufgabe_id"])
    ordered = sorted(groups.items(), key=lambda item: item[0].encode("utf-16be"))
    state = 271828
    for i in range(len(ordered) - 1, 0, -1):
        state = (state * 1664525 + 1013904223) & 0xffffffff
        j = state % (i + 1)
        ordered[i], ordered[j] = ordered[j], ordered[i]
    train, remaining = choose(ordered, 70)
    val, test = choose(remaining, 15)
    expected = {k: sorted(i for _, ids in part for i in ids)
                for k, part in (("train", train), ("val", val), ("test", test))}
    for name in expected:
        value = json.loads(checked("split_" + name + ".json"))
        if value["ids"] != expected[name] or value["anzahl"] != len(expected[name]) or value["seed"] != 271828 or value["quelle_sha256"] != HASHES["pilot_lehrer_v2.json"]:
            raise ValueError("Split stimmt nicht: " + name)
    by_id = {p["aufgabe_id"]: p for p in pairs}
    canonical = ("\n\n".join(by_id[i]["korrektur"] for i in expected["val"]) + "\n").encode("utf-8")
    if canonical != checked("val_korrekturen.txt"):
        raise ValueError("Val-Dokument weicht vom Split ab")
    canonical_train = ("\n\n".join(by_id[i]["korrektur"] for i in expected["train"]) + "\n").encode("utf-8")
    if canonical_train != checked("train_korrekturen.txt"):
        raise ValueError("Train-Dokument weicht vom Split ab")
    for left, right in (("train", "val"), ("train", "test"), ("val", "test")):
        if set(expected[left]) & set(expected[right]):
            raise ValueError("ID-Leck")
        lp = {by_id[i]["herkunft"]["prompt"] for i in expected[left]}
        rp = {by_id[i]["herkunft"]["prompt"] for i in expected[right]}
        if lp & rp:
            raise ValueError("Prompt-Leck")
        lc = {by_id[i]["korrektur"] for i in expected[left]}
        rc = {by_id[i]["korrektur"] for i in expected[right]}
        if lc & rc:
            raise ValueError("Identische Korrektur über Splitgrenze")
    print("P5 SPLIT OK train=70 val=15 test=15 gruppen=90")
if __name__ == "__main__":
    main()
