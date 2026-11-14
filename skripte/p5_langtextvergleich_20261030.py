"""Erzeugt unveränderte längere Moo-Texte und einen Markdown-Vergleich.

Nur Python-Standardbibliothek. Start aus jedem Arbeitsverzeichnis möglich:
    python3 /pfad/zu/moo/skripte/p5_langtextvergleich_20261030.py
Alle Checkpoints werden ausschließlich gelesen. Neue Ausgaben liegen in
je einem exklusiven build/p5_langtext_<Zeitstempel>-Verzeichnis.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
MODELS = (
    ("Original", "beispiele/p4_silben/run_sil14_grimm36k/full.mook"),
    ("SGD", "build/p5_grimm_mix_20261030/mix-sgd.mook"),
    ("Adam", "build/p5_grimm_mix_20261030/mix-adam.mook"),
    ("AdamW", "build/p5_grimm_mix_20261030/mix-adamw.mook"),
)
TOKENIZER = ROOT / "beispiele/p4_silben/run_sil14_grimm36k/tokenizer.msf"
BINARY = ROOT / "build/p5_grimm_mix_20261030/langtext_sampler"
SOURCE = ROOT / "beispiele/p5/grimm_langtext_sampling_20261030.moos"
CONFIG = {
    "neue_tokens": 200,
    "prompts": ["Der König ", "Die Kinder ", "Im Schloss "],
    "modi": [
        {"name": "Greedy", "temperatur": 0, "top_k": 0, "seeds": [1]},
        {"name": "Top-k 20, T=0.6", "temperatur": 0.6, "top_k": 20, "seeds": [11, 23]},
        {"name": "Top-k 20, T=0.9", "temperatur": 0.9, "top_k": 20, "seeds": [11, 23]},
    ],
}


def sha256(path: Path) -> str:
    """Berechnet die Identität einer tatsächlich vorhandenen Datei."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    """Schreibt ausschließlich eine neue JSON-Datei ohne Überschreiben."""
    with path.open("x", encoding="utf-8") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2, allow_nan=False)
        handle.write("\n")


def parse_output(path: Path, expected_sha: str, tokenizer_sha: str) -> tuple[dict, list[dict]]:
    """Prüft vollständige JSONL-Ausgabe, Seeds, Tokenzahlen und Originalbytes."""
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    if not rows or rows[0].get("ereignis") != "start" or rows[-1].get("ereignis") != "fertig":
        raise ValueError("Inferenzlauf nicht vollständig: " + str(path))
    header = rows[0]
    if header.get("checkpoint_sha256") != expected_sha or header.get("tokenizer_sha256") != tokenizer_sha:
        raise ValueError("Checkpoint-/Tokenizer-Hash in Ausgabe weicht ab")
    expected = [(p, m["name"], s) for p in CONFIG["prompts"] for m in CONFIG["modi"] for s in m["seeds"]]
    samples = rows[1:-1]
    actual = [(r.get("prompt"), r.get("modus"), r.get("seed")) for r in samples]
    if actual != expected or not rows[-1].get("checkpoint_unveraendert"):
        raise ValueError("Probenreihenfolge oder Vollständigkeit unzulässig")
    for row in samples:
        ids, raw_values = row["token_ids"], row["rohbytes"]
        if len(ids) != CONFIG["neue_tokens"] or any(int(x) != x or not 0 <= x < 512 for x in ids):
            raise ValueError("Ungültige erzeugte Token-IDs")
        if any(int(x) != x or not 0 <= x <= 255 for x in raw_values):
            raise ValueError("Ungültige Rohbytes")
        raw = bytes(int(x) for x in raw_values)
        try:
            text = raw.decode("utf-8", errors="strict")
            valid = True
        except UnicodeDecodeError:
            text = raw.decode("utf-8", errors="backslashreplace")
            valid = False
        row["utf8_gueltig"] = valid
        row["bytes"] = len(raw)
        row["text_anzeige"] = text
        row["roh_sha256"] = hashlib.sha256(raw).hexdigest()
        grams = [tuple(ids[i:i + 4]) for i in range(len(ids) - 3)]
        row["wiederholte_token_4gramme_anteil"] = 1 - len(set(grams)) / len(grams)
    return header, samples


def display_text(text: str) -> str:
    """Erhält Text; macht ausschließlich problematische Steuerzeichen sichtbar."""
    return "".join(f"\\x{ord(c):02x}" if (ord(c) < 32 and c not in "\n\t") or ord(c) == 127 else c for c in text)


def markdown(manifest: dict, results: list[dict]) -> str:
    """Erstellt den vollständigen Bericht ohne Auswahl günstiger Textbeispiele."""
    lines = [
        "# Moo: Längere Textgenerierung nach dem Mischtraining",
        "",
        f"Durchgeführt: {manifest['gestartet_utc']} (UTC).",
        "",
        "## Versuchsaufbau",
        "",
        "Verglichen werden der vorhandene SIL-14-Checkpoint und die drei vorhandenen Mischtraining-Checkpoints (SGD, Adam, AdamW). Keine neuen Gewichtsänderungen.",
        "",
        "- Drei identische Satzanfänge: `Der König `, `Die Kinder `, `Im Schloss `.",
        "- Jeweils 200 neu erzeugte Tokens statt der bisherigen 30. Kein Abbruch am ersten Satzzeichen, kein Abschneiden nach 120 Bytes.",
        "- Greedy sowie Top-k=20 bei Temperatur 0,6 und 0,9; die beiden Sampling-Varianten jeweils mit Seeds 11 und 23.",
        "- Vier Modelle × drei Prompts × fünf Einstellungen/Seeds = 60 vollständige Fortsetzungen.",
        "- Inferenz seriell auf CPU. Kontextfenster bleibt 64 Tokens; längere Ausgabe erweitert nicht das Kontextfenster.",
        "- PRNG: derselbe deterministische LCG48271 wie im vorhandenen P5-Sampler. Seed wird je Fortsetzung zurückgesetzt.",
        "- Keine Rechtschreibkorrektur, kein Nachschreiben, keine Wiederholungsstrafe und kein Filtern einzelner Token-IDs.",
        "- Unzulässige UTF-8-Bytefolgen erscheinen in der Anzeige als `\\xNN`. Originalbytes und Token-IDs sind zusätzlich unverändert in den JSON-Dateien bzw. `.bin`-Dateien enthalten.",
        "",
        "## Messübersicht",
        "",
        "Der Anteil wiederholter Token-4-Gramme ist eine rein beschreibende Wiederholungskennzahl, keine Grammatik- oder Verständnisnote.",
        "",
        "| Modell | Modus | Proben | UTF-8 gültig | Mittlere Bytes | Wiederholte Token-4-Gramme |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for result in results:
        for mode in CONFIG["modi"]:
            group = [s for s in result["samples"] if s["modus"] == mode["name"]]
            mean_bytes = sum(s["bytes"] for s in group) / len(group)
            repetition = sum(s["wiederholte_token_4gramme_anteil"] for s in group) / len(group)
            valid = sum(s["utf8_gueltig"] for s in group)
            lines.append(f"| {result['modell']} | {mode['name']} | {len(group)} | {valid}/{len(group)} | {mean_bytes:.1f} | {100 * repetition:.1f} % |")
    lines += ["", "## Vollständige Originalausgaben", "", "Der Prompt steht getrennt darüber; der folgende Codeblock enthält ausschließlich die generierte Fortsetzung. Abgebrochene Wörter am Ende ergeben sich aus der festen Tokenobergrenze.", ""]
    number = 0
    for result in results:
        lines += [f"### {result['modell']} – Schritt {int(result['header']['schritt'])}", "", f"Checkpoint: `{result['checkpoint']}`", "", f"SHA-256: `{result['checkpoint_sha256']}`", ""]
        for sample in result["samples"]:
            number += 1
            lines += [
                f"#### Probe {number:02d}: {sample['modus']} · Seed {int(sample['seed'])}",
                "", f"**Prompt:** `{sample['prompt']}`  ",
                f"**Länge:** 200 Tokens / {sample['bytes']} Bytes · **UTF-8 gültig:** {'ja' if sample['utf8_gueltig'] else 'nein'}",
                "", "````text", display_text(sample["text_anzeige"]), "````", "",
            ]
    lines += [
        "## Grenzen der Aussage",
        "",
        "Dies ist ein beschreibender Inferenzvergleich mit drei bereits verwendeten Prompts und zwei Sampling-Seeds, kein neuer unabhängiger Qualitätsbenchmark. Temperatursampling und Top-k verändern die Auswahl vorhandener Modellvorhersagen; in diesem Versuch findet kein Training statt. Einzelne gelungene Sätze begründen keine allgemeine Verbesserung. Die 64-Token-Kontextgrenze gilt für alle Varianten. Die Prüfsummen der vier Checkpoints werden vor und nach dem Vergleich kontrolliert.",
        "", "## Reproduktion und Dateien", "",
        "- Moo-Programm: `beispiele/p5/grimm_langtext_sampling_20261030.moos`",
        "- Runner: `skripte/p5_langtextvergleich_20261030.py` (nur Python-Standardbibliothek)",
        f"- Laufverzeichnis: `{manifest['laufverzeichnis']}`",
        "- `manifest.json`: Eingabepfade, SHA-256, Konfiguration und ausführbare Datei.",
        "- `ergebnisse.json`: sämtliche Proben mit Token-IDs, Originalbytes und UTF-8-Prüfung.",
        "- `*.jsonl` und `*.stderr.log`: unveränderte Programmausgaben.",
        "- `rohtexte/*.bin`: exakte generierte Bytefolgen.",
        "- `abschluss.json`: Abschlussstatus und verifizierte unveränderte Eingabedateien.",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    """Führt alle Modelle seriell aus; scheitert bei Drift oder unvollständigen Daten."""
    paths = [ROOT / p for _, p in MODELS] + [TOKENIZER, BINARY, SOURCE]
    for path in paths:
        if not path.is_file():
            raise FileNotFoundError(path)
    original_hashes = {str(p.relative_to(ROOT)): sha256(p) for p in paths}
    now = datetime.now(timezone.utc)
    out = ROOT / "build" / ("p5_langtext_" + now.strftime("%Y%m%dT%H%M%S") + f"_{os.getpid()}")
    out.mkdir(mode=0o700, parents=False, exist_ok=False)
    (out / "rohtexte").mkdir()
    config_path = out / "konfiguration.json"
    write_json(config_path, CONFIG)
    manifest = {"gestartet_utc": now.isoformat(), "laufverzeichnis": str(out.relative_to(ROOT)), "config": CONFIG, "config_sha256": sha256(config_path), "eingaben_sha256": original_hashes, "backend": "CPU", "training": False}
    write_json(out / "manifest.json", manifest)
    print("RUN_DIR " + str(out.relative_to(ROOT)), flush=True)
    started = time.monotonic()
    results = []
    for name, relpath in MODELS:
        remaining = 900 - (time.monotonic() - started)
        if remaining < 10:
            raise TimeoutError("Gesamtbudget für Inferenz ausgeschöpft")
        env = os.environ.copy()
        env.update(MOO_KI_GPU="0", MOO_KI_GPU_ERZWINGEN="0", MOO_KI_GPU_STRIKT="0", P5_CKPT=str(ROOT / relpath), P5_TOKENIZER_PATH=str(TOKENIZER), P5_SAMPLING_CONFIG=str(config_path))
        output_path = out / (name.lower() + ".jsonl")
        print("START " + name, flush=True)
        with output_path.open("xb") as stdout, (out / (name.lower() + ".stderr.log")).open("xb") as stderr:
            run = subprocess.run([str(BINARY)], cwd=ROOT, env=env, stdout=stdout, stderr=stderr, timeout=min(240, remaining), check=False)
        if run.returncode != 0:
            raise RuntimeError(f"{name}: Exit-Code {run.returncode}; siehe {out}")
        header, samples = parse_output(output_path, original_hashes[relpath], original_hashes[str(TOKENIZER.relative_to(ROOT))])
        if header.get("config_sha256") != sha256(config_path):
            raise ValueError("Konfiguration beim Ausführen verändert")
        for i, sample in enumerate(samples):
            rawpath = out / "rohtexte" / f"{name.lower()}_{i + 1:02d}.bin"
            with rawpath.open("xb") as handle:
                handle.write(bytes(int(x) for x in sample["rohbytes"]))
        results.append({"modell": name, "checkpoint": relpath, "checkpoint_sha256": original_hashes[relpath], "header": header, "samples": samples})
        print(f"DONE {name}: {len(samples)} Proben, UTF8={sum(s['utf8_gueltig'] for s in samples)}/{len(samples)}", flush=True)
    for relative, old_hash in original_hashes.items():
        if sha256(ROOT / relative) != old_hash:
            raise RuntimeError("Eingabedatei verändert: " + relative)
    write_json(out / "ergebnisse.json", results)
    with (out / "vergleich.md").open("x", encoding="utf-8") as handle:
        handle.write(markdown(manifest, results))
    write_json(out / "abschluss.json", {"status": "vollstaendig", "modelle": len(results), "proben": sum(len(r["samples"]) for r in results), "sekunden": time.monotonic() - started, "eingaben_unveraendert": True, "bericht_sha256": sha256(out / "vergleich.md")})
    print("REPORT " + str((out / "vergleich.md").relative_to(ROOT)), flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FEHLER: {type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
        raise SystemExit(1)
