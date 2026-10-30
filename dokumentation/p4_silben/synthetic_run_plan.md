# Vorläufige Versuchsvorgabe: synthetischer Mehrgenre-Korpus

Die maschinenlesbare Vorgabe steht in `synthetic_run_plan.json`. Sie ist
ein **Vorregistrierungsentwurf**, keine ausführbare Runner-Konfiguration und
keine Trainingsfreigabe. Der Korpus ist ein technischer Smoke; seine kleinen,
synthetischen Texte erlauben keine Aussage über allgemeine Sprachqualität.

Drei Tokenizer (Zeichen, BPE, Silben) werden mit denselben Seeds 11, 23 und
47 verglichen. Jede Kombination startet bei Schritt 0 und erhält exakt einen
Durchlauf über die 2.150 Original-Codepoints des versiegelten Trainingssplits.
Die Varianten verwenden dieselbe Netzarchitektur mit Dimension 32, vier
Attention-Köpfen, Kontext 64, zwei Attention-Blöcken und Feedforward-Dimension
128. Optimizer ist SGD mit Lernrate 0,01 und Momentum 0,9. Kontext 64 und
Stride 32 gelten auch für die dokumentweise Evaluation. Jedes Dokument beginnt
mit BOS; EOS ist ausgeschlossen. Die Zielgröße 512 Token ist vorläufig:
Die tatsächlichen Vokabulargrößen und Parameterzahlen müssen nach Erzeugung
der Artefakte dokumentiert und auf Vergleichbarkeit geprüft werden; gleiche
Codepoint-Mengen garantieren keine gleichen Token-, Rechen- oder Parameterkosten.

Pro Training sind höchstens 600 Sekunden, pro Val- und Testauswertung je
60 Sekunden reserviert. Neun Kombinationen ergeben höchstens 6.480 reservierte
Sekunden und liegen damit unter 7.200 Sekunden für Training und Evaluation.
Eine Wallzeitobergrenze belegt das GPU-Budget nur bei isolierter GPU und
beendeter GPU-Arbeit beim Prozessende. Die bisherige Batch-Zeitmessung mit
einem winzigen 8-dimensionalen Modell ist keine Laufzeitgarantie für dieses
32-dimensionale Modell.

Zuerst alle Trainings und Validierungen, dann die dokumentierte unveränderte
Entscheidung für alle neun Läufe fixieren, erst danach jeden Testsplit einmal
bewerten. Keine Auswahl anhand von Testwerten. Aggregation erfolgt über gesamte
NLL und Originalbytes bzw. -codepoints. Beim Lesen oder Vorbereiten keine
Testtexte manuell einsehen.

Das Korpus-SHA256 und der kanonische Manifest-Hash sind im JSON festgehalten.
Der Split umfasst neun Train-, drei Val- und drei Testdokumente aus je drei
Genres. Dieser Abschnitt beschreibt den ursprünglichen Vorbereitungsstand:
Tokenizer, Checkpoints, ausführbare Dateien und Belege wurden für den danach
durchgeführten Versuch erzeugt und separat versiegelt. Ergebnisse und Grenzen
stehen in `dokumentation/p4_silben_api.md`; die laufgebundenen Artefakte liegen
im dort genannten privaten Laufverzeichnis. Der Zertifikat-Pin ist nach dem
Versuch wieder geschlossen; der Trainingsadapter besitzt keine eigene Hartsperre.
Ein weiterer Lauf benötigt seine eigenen gültigen Voraussetzungen und Budgets.

Vor Freigabe können die Dokumentdateien aus dem versiegelten Korpus mit
`uv run python -m skripte.p4_silben.materialize_documents MANIFEST KORPUS ABSOLUTES_ZIEL MANIFEST_SHA256`
in ein neues privates Verzeichnis geschrieben werden. Das Programm prüft
Hashes, Grenzen und Splitzuordnung und gibt nur den Mappingpfad aus. Das Ziel
darf noch nicht existieren; ein abgebrochener Lauf bleibt zur Prüfung liegen.
Die Testtexte werden dabei nicht angezeigt oder manuell betrachtet.

Danach die Rechte und die Ziel-Vokabularpolitik prüfen, exakten
Snapshot für Tokenizer- und Checkpointerzeugung festlegen, Artefakte erstellen
und unabhängig prüfen. Eine Pfadvorlage mit allen Feldern aus `runner.md`,
den Artefaktpfaden sowie `corpus_path` und `protocol_path` kann anschließend
mit `uv run python -m skripte.p4_silben.prepare_config VORLAGE AUSGABE`
in eine kanonische Runner-Konfiguration umgewandelt werden. Der Generator
berechnet vorhandene Hashes, kontrolliert Tokenizer, untrainierte Checkpoints,
Eval-Spezifikationen und freie Ausgabeziele; er öffnet kein Gate und stellt
kein Zertifikat aus. Das Ergebnis samt Snapshot und Zertifikat unabhängig
prüfen. Bei Crash keine Ledgerreservierung
zurücksetzen oder Ausgabe überschreiben; siehe `runner.md`.

Verfügbare Projektaufgaben laut `mise tasks`: `test-p4-preflight`,
`test-p4-moos-api`, `test-p4-train-compile`,
`test-p4-synthetic-manifest`, `test-p4-char-fixed` und
`test-p4-silben`. Das Vorhandensein dieser Aufgaben ersetzt keine
Ausführung auf dem später versiegelten Snapshot. Der isolierte Workspace- und
der relevante native Windows-Test bleiben gesonderte Pflichtstufen.
