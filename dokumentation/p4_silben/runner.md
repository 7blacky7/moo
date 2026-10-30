# P4-Experimentrunner

Der laufgebundene Zertifikat-Pin in `approval_pin.py` steht nach Abschluss
des dokumentierten Versuchs wieder auf `None`. Das Gate bleibt damit für
weitere Läufe geschlossen. Der Moo-Trainingsadapter hat keine eigene
Hartsperre und darf nur über den geprüften Orchestrator aufgerufen werden.

Nach den erforderlichen Freigaben wird der Start über
`uv run python -m skripte.p4_silben.run_experiment CONFIG CERTIFICATE --create-ledger`
vorbereitet. Die Option legt ein noch nicht vorhandenes Ledger exklusiv an;
sie darf nicht zum Zurücksetzen eines begonnenen Experiments verwendet werden.
Ohne diese Option muss das vorhandene Ledger bereits vorliegen.

Die zertifizierte JSON-Konfiguration bindet neben den Feldern aus
`harness.validate_config` die vollständigen Pfade und Hashes: Manifest und
Trainingsdokumente (`manifest_path`, `train_documents`), Tokenizerpfade
und -hashes je Variante, Encoderpfad und -hashes, Initialcheckpointpfade und
-hashes je `variante:seed`, Trainerpfad und -hash, absolute eindeutige
`output_checkpoints` und `output_reports`, `ledger_path`, Kontext,
Stride und Codepoint-Budget. Für die Evaluation sind `eval_seconds_per_run`, Python-Interpreterpfad/-hash,
Eval-Skriptpfad/-hash sowie je Split `eval_input_path` und `eval_input_sha256`
erforderlich. `output_reports` enthält eindeutige Ziele je `variante:seed:split`;
`validation_decision` enthält die vorab festgelegte Entscheidung. Der Zertifikat-Hash der gesamten
Konfiguration verwendet UTF-8-JSON mit sortierten Schlüsseln, ohne Leerzeichen
und ohne NaN. Das Zertifikat muss die tatsächlichen Rechte-, Snapshot- und
Testbelege referenzieren; ein Hashfeld allein beweist sie nicht. Das Gate
prüft getrennte Produkt- und Experiment-Dateimanifeste samt Pflichtpfaden;
die Stufenbelege tragen ihre jeweiligen tatsächlichen Snapshotmarker und
Komponentenhashes. Der Pin ist eine lokale Vertrauensgrenze, keine Signatur.
Der zertifizierte Gerätemodus `vulkan_forced_strict` setzt die GPU-Variablen
für Trainer und Evaluator; nicht jede Operation ist dadurch GPU-resident.

Die Eval-Eingabe enthält das vollständige versiegelte Manifest, `split`
(`val` oder `test`), genau dessen Dokument-ID/Pfad-Zuordnung,
`tokenizers` je Variante mit Pfad und SHA256, Encoder- und Scorerpfade und
deren SHA256 sowie `context` und `stride`. Optional wählt
`scorer_mode: batch` das dazu passende kompilierte Batch-Programm;
Standard ist der Einzelscorer. Eval-Eingaben selbst, ausführbare Dateien und
ihre Abhängigkeiten müssen im freigegebenen Snapshot liegen. Der Worker
kontrolliert Dokument-, Split-, Tokenizer- und Checkpointidentität und schreibt
keine Dokumenttexte in den Report.

Der Orchestrator sperrt parallele Instanzen, reserviert Laufzeit vor jedem
Worker und setzt nach Checkpoint-Publikation einen Abschlussnachweis.
Evaluation akzeptiert ausschließlich einen dazu passenden Checkpoint. Zuerst führt `--phase val` alle Trainings und Val-Auswertungen aus. Danach
fixiert `--freeze-validation` die vorregistrierte Entscheidung und alle
Val-Berichtshashes. Erst `--phase test` prüft diese Fixierung und führt die
Endbewertung aller vorab festgelegten Seeds aus. Testwerte dürfen nicht zur
Nachwahl verwendet werden. Reservierte Zeit wird bei Fehler oder Absturz
nicht zurückerstattet. Die dokumentierte Fortsetzung nach dem ersten
Fehlversuch bindet zusätzlich das unveränderte vorherige Ledger und seine
Konfiguration per Hash; 600 Sekunden Altreservierung und 300 Sekunden
Vorlaufreserve bleiben außerhalb des neuen Ledgerlimits berücksichtigt.

Bei einem Abbruch nach Checkpoint-Publikation, aber vor gültigem
Abschlussnachweis, bleibt der Lauf uneindeutig: kein automatischer Neustart,
kein Überschreiben und keine Evaluation. Ledger, Checkpoint, Prozessende,
Dateihashes und Verzeichniszustand sind unabhängig zu prüfen; eine sichere
automatische Wiederherstellung ist nicht implementiert. Entsprechendes gilt
für einen eventuell schon publizierten Report nach fehlgeschlagenem
Verzeichnis-fsync.

Die Wallzeit begrenzt die GPU-Nutzung nur unter den Voraussetzungen einer
einzelnen isolierten GPU, nicht abgekoppelter Worker und beendeter GPU-Arbeit
vor Prozessende. Manuelle Fremdprozesse, Austausch oder Rücksetzen des
Ledgerverzeichnisses und abgekoppelte Kinder sind durch die Hashkette nicht
verhindert. Die Verantwortlichen müssen diese Voraussetzungen vor Freigabe
nachweisen. Lokale Tests sind Pflicht; ein frischer Workspace ist für
Reproduzierbarkeit erforderlich, und relevante Windows-/ABI-Änderungen
erfordern den nativen Windows-VM-Test für den exakten Snapshot.
