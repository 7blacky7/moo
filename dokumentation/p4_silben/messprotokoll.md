# P4: Daten und Messprotokoll, Entwurf v1

Stand 2026-10-29. Dieses Dokument und das Split-Werkzeug sind Vorbereitung;
sie sind kein ausgeführtes Experiment und kein versiegeltes Protokoll. Vor
Tokenizer- oder Modelltraining müssen SIL-10, die Rechte, ein überprüftes
Splitmanifest und die endgültige Protokollfassung freigegeben sein.

## Quelle und Rechte

Das bestehende `skripte/maerchen_download.sh` pinnt die Rohdatei von
[Project Gutenberg Nr. 77905](https://www.gutenberg.org/ebooks/77905) auf
SHA256 `3cc443c1f71bc23a6a33f566b8de3dca7c49604d81be1696d1aef5137c579e3f`
und den bearbeiteten Extrakt auf
`3770d821afd03adb39f1a1e042b87f7f4a4fb35e3c5add6ed17811d63c941e6a`.
Das sind Sollwerte; kein heutiger Messwert. Der Extrakt entfernt u. a.
Illustrationsmarkierungen und verändert Zeilenenden/Leerzeilen.

Der Gutenberg-Katalog bezeichnet die Ausgabe als in den USA gemeinfrei und
nennt die Brüder Grimm, die Herausgeberin M. Thilo-Luyken und die Illustratorin.
Die Illustrationen gehören nicht zum vorgesehenen Text; Umfang etwaiger eigener
Textbearbeitungen der Herausgeberin und ihr Todesdatum sind hier nicht belegt.
Die Gutenberg-Lizenz verweist für Nutzung außerhalb der USA auf örtliches Recht:
[License](https://www.gutenberg.org/policy/license),
[Permissions](https://www.gutenberg.org/policy/permission.html).
In Deutschland können geschützte Bearbeitungen und Sammlungen eigenständig
relevant sein ([UrhG §§ 3 und 4](https://www.gesetze-im-internet.de/urhg/BJNR012730965.html)),
neben [§ 64 UrhG](https://www.gesetze-im-internet.de/urhg/__64.html) und ggf.
[§ 70 UrhG](https://www.gesetze-im-internet.de/urhg/__70.html).
Die Rechte für den konkreten Extrakt und die beabsichtigte Nutzung in
Deutschland sind **offen**. Gutenberg-US-Status und frühes Erscheinungsjahr
allein klären sie nicht. Keine Verwendung für SIL-11 bis zur dokumentierten
Klärung; ggf. nachweislich nutzbare Alternative wählen. Als Kandidat bietet
Wikisource die [Ausgabe der Grimm-Märchen von 1857](https://de.wikisource.org/wiki/Kinder-_und_Haus-M%C3%A4rchen_Band_1_%281857%29)
mit ausgewiesener Vorlage und Korrekturstand; konkrete Revision, Textumfang,
redaktionelle Beiträge und Nutzungsbedingungen sind vor Nutzung zu prüfen.
Selbst verfasste Texte eignen sich für einen technischen Probelauf; ihre
geringe Vielfalt begrenzt Aussagen über allgemeine Modellqualität.

## Split und Siegel

Neun vollständige Geschichten sind laut ursprünglichem Downloadskript enthalten.
Für einen gemischten Korpus gibt es zusätzlich `split_multigenre.py`: mindestens
zwei Textarten mit jeweils mindestens drei unabhängigen Dokumentgruppen;
deterministische Zuordnung je Textart in Train/Val/Test, zusammengehörige
Gruppen stets im gleichen Split. Für jeden Eintrag ist `test_eligible` explizit
anzugeben; bei einem bereits betrachteten Text und seiner ganzen Duplikatgruppe
muss es falsch sein. Die bei der Recherche angesehene Wikisource-Geschichte
`Der Froschkönig oder der eiserne Heinrich (1857)` darf nicht in den Testsplit.
Ungesehene Testtexte werden nicht manuell geprüft. Quellenrechte, Genres,
Dokumentgrenzen und Ähnlichkeitsgruppen müssen unabhängig belegt werden;
gleichartige SHA256-Prüfung erkennt nur exakte Duplikate. Sofern eine Textart
zu wenig unabhängige Gruppen für alle drei Splits enthält, bricht der Splitter
ab. Quellrevisionen und Herkunft je Dokument zusätzlich im Boundary- bzw.
Quellmanifest festhalten. `split_manifest.py` ist die ältere 7/1/1-Variante.
Vor dem
Training Dokumentgrenzen und etwaige Duplikatgruppen unabhängig prüfen, ohne
den späteren Testtext für Entwicklung oder Modellauswahl zu betrachten.
Der Splitter `skripte/p4_silben/split_manifest.py` erwartet ein gesondert
geprüftes JSON mit neun IDs und lückenlosen Halbintervallen in Byteoffsets.
Er überprüft UTF-8-Grenzen, Abdeckung, gepinnten Extrakthash und identische
Duplikate; er kann weder semantische Vollständigkeit der Geschichten noch
ähnliche Duplikate selbst beweisen. Bis beide Prüfungen dokumentiert sind,
ist kein Manifest für Training freigegeben.

Der feste Seed und SHA256-Rang ordnen sieben Dokumente Train, eines Val, eines
Test zu. Jeder Split wird in Quellreihenfolge ohne eingefügte Bytes gebildet;
Dokument- und Split-Bytes, Codepoints und SHA256 stehen im Manifest. Ein
Manifest wird ausschließlich neu erstellt und nicht überschrieben. Der
Manifesthash ist SHA256 der UTF-8-JSON-Kanondarstellung ohne Feld
`manifest_sha256`, mit sortierten Schlüsseln und Trennzeichen `,` und `:`.
Für eine Alternative wird ihr vorab festgelegter SHA256 explizit über
`--expected-sha256` angegeben. Dieser Parameter beweist keine Nutzungsrechte;
Quellenrevision, Extraktion, Rechteprüfung und Hash vor dem Split festhalten.
Zum Nachstellen Quell- und Boundary-Hash, Werkzeugversion und Manifesthash
dokumentieren. Tokenizer und Vokabulare ausschließlich auf Train fitten,
Hyperparameter nur auf Val wählen. Testinhalt und -metriken bleiben bis zur
einmaligen Endbewertung unangesehen; Integrität darf mechanisch geprüft werden.

## Vorab festzulegende Evaluation

* A: feste Zeichen mit Bytefallback, B: vorhandenes Byte-BPE, C: Silben mit
  Bytefallback; gleiche Rohbytes und Dokumente, getrennte Artefakte,
  Modellzustände und Checkpoints. Keine stillen Normalisierungen.
* Pro Variante mindestens drei vorab dokumentierte Seeds. Summe der NLL aller
  bewerteten Ziel-IDs einschließlich Fallback, geteilt durch die Anzahl der
  tatsächlich abgedeckten Original-Unicode-Codepoints; zusätzlich bits/byte
  als NLL / (ln(2) × Originalbytes). Dies bewertet den deterministischen
  Tokenpfad, nicht die marginalisierte Textwahrscheinlichkeit. Keine direkten
  Token-Loss- oder Perplexity-Vergleiche.
* Vor erster Ausführung BOS/EOS, erstes Ziel, Zielmaske, Kontext,
  Sliding-Stride, Dokumentgrenzen und Abdeckung der Originalbytes festlegen.
  Jede Zielposition genau einmal bewerten; CE-Mittelwert mit der Anzahl
  aktiver Ziele multiplizieren und erst dann summieren. Bei Mehrbyte-Fallback
  nur vollständig zugeordnete Originalbereiche im Nenner zählen.
* Tatsächliche Gesamtparameter, Nicht-Embedding-/Nicht-Kopf-Parameter,
  Vokabulargrößen, Kontext in Tokens und Originalzeichen, FLOPs, Zeit und
  Speicher ausweisen. Möglichst gleiche Gesamtparameter und identische
  Rohtextprompts; gleiche Textmenge belegt keine gleichen FLOPs.
* Maximal neun Modellläufe (3 × 3), je maximal zehn GPU-Minuten, insgesamt
  einschließlich Evaluation höchstens zwei GPU-Stunden. Ressourcen- und
  Abbruchgrenzen, Val-Auswahlbudget und P3-GPU-Koordination vorab festlegen.
  Kein Training vor grünem SIL-10; Abweichungen als neue Protokollversion
  vor Testentsiegelung dokumentieren.

Ein dokumentweiser Evaluationskern liegt in `skripte/p4_silben/evaluator.py`:
Er prüft lückenlose Token-Byte-Offsets, berechnet Sliding-Window-NLL aus
Logits und erfasst jede Ziel-ID genau einmal. `evaluate_bound_document` prüft
zusätzlich SHA256 von Dokument, Tokenizer und Checkpoint sowie Tokenbytes.
Der Scorer erhält je Ziel nur das vorausgehende Tokenpräfix und dessen
Position; er verwendet Byte-ID 0 als BOS nur am Dokumentanfang. Das ist nur
vergleichbar, wenn alle Modelle mit derselben BOS-Policy trainiert werden.
Der Kontext umfasst höchstens die vereinbarte Tokenanzahl inklusive BOS;
1 <= Stride < Kontext, EOS ist ausgeschlossen. Er muss nachweislich den
geprüften Checkpoint verwenden. Bei Stride > 1
kann das erste Ziel im Block weniger Kontext als bei Stride 1 erhalten;
der Kern meldet Summe, Minimum und Maximum der Kontexttoken. Über Dokumente
werden NLL, Bytes und Codepoints vor dem Teilen summiert. Kausalität und
Bindung an das laufende Modell muss der Adapter zusätzlich nachweisen.
`harness.py` prüft die Form der A/B/C-Konfiguration und bleibt gesperrt.
Der konkrete Moo-Modelladapter, Hashprüfung, GPU-Zeitüberwachung und eine
ausführbare End-to-End-Messung sind noch nicht implementiert oder geprüft.

## Pflichtprüfungen nach Implementierung

Stufe 1 lokal über `mise run <task>`: Split-Reproduktion, fehlerhafte
Grenzen/Hashes/UTF-8, fehlende/duplizierte Ziele, NLL- und Stride-Orakel,
Tokenizer-/Checkpoint-Regressionen und ASan/UBSan für native Teile.
Stufe 2 bei Reproduzierbarkeit: frisch materialisierter einzelner Workspace
mit Manifest- und Hashvergleich, koordiniert und anschließend gestoppt.
Stufe 3 bei relevanten Windows- oder ABI-Änderungen: native Windows-VM mit
versiegeltem Snapshot, Hashabgleich und echten Plattformtests. Umgebung,
Befehl, Hash, Ergebnis und ausgelassene Fälle dokumentieren.
