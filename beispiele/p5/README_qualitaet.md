# P5: Qualitäts- und Forgetting-Baseline

Die Runner lesen standardmäßig `full.mook` des SIL-14-Basismodells.
Mit `P5_CKPT=<pfad>` wird ein anderer kompatibler Checkpoint gewählt;
Tokenizeridentität und Architektur werden beim Laden geprüft. Die hier
aufgeführten Aufgaben erzwingen CPU und enthalten kein Training.

- `mise run p5-sampling-baseline` erzeugt für sechs festgelegte Prompts
  je eine Greedy-Fortsetzung und drei Sampling-Fortsetzungen mit Seeds
  11/23/47, Temperatur 0,8 und Top-k 20. `lcg48271-v1` setzt den Seed
  für jeden Prompt neu. Maximal 120 generierte Bytes, Abbruch am ersten
  Satzende. JSON enthält Checkpoint-, Tokenizer- und Prompt-SHA, Rohbytes,
  Seed, Methode, Anzahl der Generierungsschritte und, nach strenger
  UTF-8-Prüfung in `skripte/p5_sampling_json.py`, Gültigkeitsstatus und
  gegebenenfalls Text. Ein abgebrochener Byte-Fallback bleibt als Rohbytes
  erhalten. Die Aufgabe schreibt keine Checkpoints.
- `mise run p5-grammatik-baseline` bewertet 80 synthetische Minimalpaare:
  je 20 Artikel/Kasus, Verbformen, Satzbezug und neue Komposita. Der
  Satzbezug setzt explizit genau eine gemeinte Person voraus und ist mit
  je zehn weiblichen und männlichen Personen balanciert. Automatisch zählt
  `nll(gut) < nll(schlecht)`; Gleichstand zählt nicht als richtig.
  Die API bewertet jeweils den vollständigen Text ab dem zweiten Token.
  Unterschiede in Zeichen-/Tokenlängen, unterschiedliche Tokenisierung
  und häufige Wortformen können das Ergebnis beeinflussen. Der Wert ist
  daher ein heuristisches Minimalpaar-Gate, keine kausale Fehleranalyse
  oder repräsentative Grammatikgenauigkeit.
- `mise run p5-forgetting-baseline` misst natürliche NLL auf dem festen
  Ausschnitt `daten/forgetting_grimm.txt` aus P4-`train.txt`, Zeilen
  4–11. Es ist eine bereits trainierte Stichprobe, kein unabhängiger
  Holdout. Der erste Token dient als Kontext; Bytes/Codepoints zählen nur
  den tatsächlich bewerteten Suffix. Bits/Byte = NLL/Bytes/ln(2).
  Vergleiche vor/nach Fine-Tuning müssen exakt denselben Dokument-,
  Tokenizer-, Architektur- und Kontextvertrag verwenden.
- `mise run p5-train-nll-baseline` misst analog auf dem eingefrorenen
  `train_korrekturen.txt` als `split=train`. Die Eval-API akzeptiert
  seit Batch 29865 diesen Split; `checkpoint_waehlen` bleibt val-only.
  Dies aktualisiert den API-Verfügbarkeitsstand im ansonsten unveränderten
  `val_protokoll.md`.

Eingefrorene Eingaben: Grammatikdaten SHA-256
`d89fea83a3a3c400bc17953d9fb9b6769dff951cfade33c62f48ebae03e329aa`,
Grimm-Stichprobe
`d905cebec8bdf31d5d4477f3b34973aa7f728bcb8748177b6b5f069a76d19206`,
Sampling-Konfiguration
`44b45262831453a97ca7e67307689d8ae4a0f3a9ab7b3810df3b6dd4d73d9b69`.
`daten/baseline_*.json` enthält die CPU-Dry-Runs und Modellhashes.
Ergebnisse aus anderer Datenversion oder einem anderen Tokenizer nicht
unkritisch vergleichen. Der P4-Holdout und der Pilot-Test bleiben für
ihre festgelegte Abschlussevaluation reserviert.
