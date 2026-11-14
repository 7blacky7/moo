# P2-T33: Testkontexte und offene Fälle

Stand des Untersuchungsbefunds: Commit `fa6bdfe`, Compiler-SHA256
`57efe709787ee966efa39f6a95542ffebe104dbfe97fcecf1535d2ff175e2298`.
[Das Manifest](testmanifest.json) dokumentierte ursprünglich die 24 damals
fehlgeschlagenen Quellen. Neun korrigierte Spiele wurden daraus entfernt;
verbleiben sechs Profile und neun offene Fälle. Es ist keine vollständige
Aufzählung aller Dateien und keine Liste erfolgreich kompilierender Quellen. Der Shell-Runner `legacy_ir.sh --profiles` übernimmt nur
die sechs belegten IR-Profilargumente; DB-Kontexte werden nicht automatisch
zusammengesetzt oder übersprungen.
Unbekannte, ungeprüfte oder nicht erfolgreich kompilierende Fälle bleiben
offen; ein wiederholbarer Fehler ist kein grünes Gate.

## Profile und Datenbank-Kontexte

Sechs Boot-/Kernel-/UEFI-Quellen erzeugten mit den im Manifest angegebenen
Argumenten erfolgreich IR (Job `0f8927ca`). `--emit-ir` kehrt in
`compiler/src/main.rs:995–1000` vor der Objekt- und Linkerzeugung zurück:
Das ist weder ein Ziel-ABI- noch Boot-Nachweis. Die späteren Ziel- und
Plattformtests bleiben Pflicht. Bei künftiger Integration Compiler-Binary,
Quellen, importierte Dateien, Profil und Flags mit Hash festhalten.

Fünf der sieben DB-Benchmark-/Smoke-Dateien dokumentieren einen historischen
Zusammenbau aus einem fest angegebenen Zeilenpräfix von
`beispiele/domain/db/postgres_client.moos` und der jeweiligen Testdatei.
Die Zeilenanzahlen stehen als historische Angaben im Manifest. Beim Clientstand
SHA256 `26b1a97007d129b6ffa12c1981996ec679fb015d9026d52b45c92e3c340e93cc`
(Job 44e6d6b4) enden die Präfixe 592, 760 und 978 vor der erst in
`postgres_client.moos:983` definierten Methode `schliesse`, die die
betroffenen Benchmarks aufrufen. Der Pool-Präfix 1024 endet vor
`PgPool.schliesse_alle` in Zeile 1029, das der Pool-Benchmark aufruft.
Der Pipelining-Präfix 1024 endet innerhalb `PgPool.checkin`; ob dieser
unvollständige Teil für den Pipelining-Fall relevant ist, ist ungeprüft.
Die historischen Zeilenangaben sind somit keine belastbaren Eingabeverträge;
ihre vollständige, versionierte Ersetzung bleibt offen (Thought 3104a9b2).
Ein zusätzlicher manueller Versuch mit dem hashgebundenen Clientteil bis
Zeile 1034 und jedem der fünf Fragmente erzeugte jeweils erfolgreich IR
(5/5 RC0, Thought `f484f698`). Dies belegt keine Laufzeit- oder
Datenbankfunktion und macht die historischen Präfixlängen nicht gültig. Ein
versionsgebundener automatischer Kontext samt Importgraph bleibt offen.
Der vorhandene Handoff `beispiele/domain/db/bench/HANDOFF_2026-05.md:15–20`
beschreibt bereits das Ausschneiden vor dem `# Smoketest`-Marker. Dieser
Marker steht aktuell in `postgres_client.moos:1037`; seine Eindeutigkeit und
alle beteiligten Hashes müssen vor einer Automatisierung geprüft werden.

Die verbleibenden zwei Dateien benötigen ein benanntes Modul beziehungsweise
einen relativen Import. `main.rs:176–199` löst Pfadimporte relativ zum
Verzeichnis der importierenden Datei auf. Modulbereitstellung, Importpfad und
vollständiger Graph sind vor einem reproduzierbaren Lauf zu klären. Keine
Produktbeispiele umschreiben oder still ausschließen.

## Neun behobene Einzeilen-wenn-Fälle

Der User entschied, die Beispiele auf mehrzeilige Blöcke umzustellen und den
Parser beizubehalten. Batch 30837 setzte dies um; Batch 30842 benannte im
Spiel 2048 zusätzlich die lokale Variable mit reserviertem Schlüsselwort um.
Alle neun kompilierten und linkten erfolgreich (Thought fba5fd3d); ihre
grafische Laufzeit wurde dabei nicht geprüft. Die folgende Tabelle bewahrt die
historischen ersten Diagnosezeilen.

| Datei unter `beispiele/` | Erste gemeldete Zeile |
|---|---:|
| `coin_catcher.moos` | 45 |
| `doodle_jump.moos` | 68 |
| `game_2048.moos` | 21 |
| `mine_run.moos` | 45 |
| `pipe_puzzle.moos` | 32 |
| `pong_plus.moos` | 76 |
| `simon.moos` | 20 |
| `snake_plus.moos` | 212 |
| `tic_tac_toe.moos` | 220 |

Parser-Beleg: `compiler/src/parser.rs:219–226` verlangt nach dem Doppelpunkt
ein `Indent`. Die Diagnose stammt aus Job `ffab97de`; jede Datei kann
weitere Fehler enthalten, die nach einer Änderung erst sichtbar werden.

## Zwei offene Fälle

- `beispiele/domain/db/mini_db.moos`: `BTree` definiert laut Zeilen
  192–219 `put`, `get`, `scan` und `dump`. Die Quelle ruft dagegen
  `selbst.index.alle()` in Zeile 324 und im Test u. a. `baum.einfuegen`,
  `baum.suche` und `baum.alle` in Zeilen 434–447 auf. Der Compiler meldet
  zuerst „Methode 'alle' nicht gefunden“. Das ist ein belegter Namenskonflikt;
  ob die Aufrufe oder die API geändert werden sollen und ob danach weitere
  Probleme auftreten, ist offen.
- `compiler/tests/test_stdlib.moos:2–3,16` importiert `mathe` und
  `liste` und ruft `durchschnitt(zahlen)` auf. Beide vollständig gelesenen
  Module `stdlib/mathe.moos:1–55` und `stdlib/liste.moos:1–42` definieren
  keine solche Funktion; der Compiler meldet entsprechend eine fehlende
  Funktion (Job `3866bf90`). Dies ist eine Test-/Bibliotheksinkonsistenz,
  keine nachgewiesene Negativprobe. API-Semantik und Verhalten für leere
  Listen bleiben zu klären; Quellhashes und Belege stehen in Thought
  `a6828102`. Die frühere Index-Sichtbarkeitslücke ist inzwischen behoben.

## Baseline-Grenze

Die historischen Läufe waren wegen wechselnder Release-Reihenfolgen nicht
identisch. Spätere Aufnahmen zeigten deterministische IR-Ergebnisse; zuletzt
belegt Thought d1f92038 drei gleiche Aufnahmen, neun bestätigte Diagnosen und
zwei separat dreifach bytegleich geprüfte Nutzliteral-IRs. Das ist ein
manueller Nachweis nach der M1-Regel; der vorhandene Runner meldet weiterhin
RC1 und ist kein automatisiertes grünes Gate. Semantisch relevante Unterschiede
wie Release-Reihenfolge oder Nutzliterale dürfen nicht herausnormalisiert werden.
