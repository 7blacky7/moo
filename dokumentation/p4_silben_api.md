# Deutscher Silben-Tokenizer (P4, abgeschlossen, experimentell)

Diese Dokumentation beschreibt die native API und den Stand des kleinen
synthetischen Machbarkeitsversuchs. SIL-10 wurde für diesen Versuch mit den
unten genannten Einschränkungen abgenommen. Die Ergebnisse belegen keine
allgemeine Sprach- oder Modellqualität.

## API

| Deutsch | Englischer Alias | Funktion |
| --- | --- | --- |
| `silben_trainiere(korpus, limit, muster)` | `syllable_train` | Erstellt ein Tokenizer-Artefakt aus Korpus und vollständigem Inhalt der Musterdatei. `limit` ist eine ganzzahlige Vokabularobergrenze von 256 bis 65536. |
| `silben_kodiere(tok, text)` | `syllable_encode` | Gibt einen eindimensionalen Tensor mit Token-IDs zurück. |
| `silben_dekodiere(tok, ids)` | `syllable_decode` | Rekonstruiert die Tokenbytes aus einer ID-Liste oder einem eindimensionalen Tensor. |
| `silben_grenzen(tok, text)` | `syllable_boundaries` | Gibt klassifizierte Segmentspannen als Liste zurück; Details unten. |
| `silben_speichern(tok, pfad)` | `syllable_save` | Speichert das Artefakt und kann eine bestehende Datei ersetzen. |
| `silben_laden(pfad)` | `syllable_load` | Lädt und validiert ein Artefakt. |
| `silben_info(tok)` | `syllable_info` | Liefert `version`, `vokab`, `hash` und `hash64`. |
| `silben_hash(tok)` | `syllable_hash` | SHA-256 der vollständigen Artefaktbytes als Hextext. |
| `silben_hash64(tok)` | `syllable_hash64` | FNV-1a-64 der Artefaktbytes als Hextext; kein kryptografischer Hash. |

`tok` bezeichnet das geladene oder erzeugte Artefakt. IDs 0–255 stehen
für einzelne Bytes; weitere IDs gehören ausschließlich zu diesem Artefakt.
Muster und Einträge sind im Artefakt enthalten. Andere Artefakte können andere
IDs zuordnen; Tokenlisten deshalb nicht mit einem anderen Artefakt dekodieren.

## Text und Grenzen

Korpus und zu kodierender beziehungsweise zu segmentierender Text müssen
nichtleeres, gültiges UTF-8 ohne NUL sein; ungültige Eingaben und beschädigte
Artefakte werden zurückgewiesen. Die aktuelle Text-/Artefaktgrenze beträgt
16 MiB, die Musterdateigrenze 1 MiB. Nicht im Vokabular vorhandene Segmente
werden durch Byte-IDs dargestellt. Der Roundtrip eines gültigen Eingabetextes
mit demselben Artefakt erhält die ursprünglichen Bytes.

`silben_grenzen` gibt pro Segment `[start, ende, art]` zurück: Start ist
inklusive, Ende exklusiv, beide sind **Byte-Offsets** im ursprünglichen UTF-8.

- `0`: Literal, etwa nicht als Musterwort behandelter Inhalt.
- `1`: durch Muster ermitteltes Wortsegment, ein orthografischer Kandidat.
- `2`: Bytefallback; die Grenze kann Codepoints und Grapheme teilen.

Die Spannen beschreiben die Segmentierung, nicht zwingend einzelne erzeugte
Token-IDs. Sie sind keine Garantie für linguistisch korrekte Silben. Insbesondere
können Muster und Wörterbuch von erwarteten Trennungen abweichen. Die API
begrenzt die Ausgabe auf 100000 Spannen.

`silben_dekodiere` prüft ID-Form und Wertebereich, validiert aber die aus
beliebigen IDs zusammengesetzten Bytes nicht erneut als UTF-8. Nur der
Roundtrip zuvor gültiger Eingaben ist als UTF-8-Erhaltung zu verstehen.

## Identität und Checkpoints

Den SHA-256 des tatsächlich geladenen Artefakts als `tokenizer_version`
im Modell-Checkpoint speichern und beim Laden denselben erwarteten Wert sowie
die passende `arch_version` an `checkpoint_laden` übergeben. Das Beispiel
`beispiele/p4_silben_demo.moos` zeigt getrennte Tokenizer und Checkpoints
sowie diese Identitätsprüfung. Ein Hashvergleich bindet die Artefaktbytes;
er ersetzt keine Prüfung von Herkunft, Trainingsdaten oder Modellqualität.
`silben_speichern` ist kein exklusiver Nicht-Überschreiben-Vertrag.

## Musterlizenz

Die mitgelieferten deutschen Muster liegen in
`compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat`; der zugehörige
MIT-Lizenztext mit Urheber- und Erlaubnisvermerk steht in
`compiler/runtime/data/silben/LICENSE.data`. Bei Weitergabe der Muster
oder wesentlicher Teile die dortigen Lizenzbedingungen beachten.

## Vorhandene Tests und offene Stufen

Aus dem Projektwurzelverzeichnis über `mise run <task>`:

- `test-p4-silben`: nativer Silbenkern unter ASan und UBSan.
- `test-p4-char-fixed`: nativer Zeichenkern unter ASan und UBSan.
- `test-p4-moos-api`: öffentliche `.moos`-Schnittstelle mit konstruierten Fallback-Artefakten; benötigt den vorhandenen Release-Compiler.
- `test-p4-preflight`: vorbereitende Prüfungen ohne den ausgeschlossenen Tokenizer-Trainingsfall.
- `test-p4-regression`: native Dataset-, BPE-, Shard- und Checkpoint-Regressionen.
- `test-p4-split`: synthetische Split- und Evaluatorprüfungen.
- `test-p4-synthetic-manifest`: synthetisches Manifest temporär erzeugen und mit der Projektfassung vergleichen.
- `test-p4-train-compile`: Trainingsadapter nur kompilieren, nicht ausführen; vorhandener Release-Compiler erforderlich.

Einige dieser Tests erzeugen synthetische Tokenizer-Artefakte, insbesondere
`test-p4-silben`, `test-p4-char-fixed`, `test-p4-regression`, `test-p4-split`,
`test-p4-demo` und `test-p4-alias`. Vor erneutem Ausführen gilt das jeweils aktuelle
Trainingsverbot; die Auflistung selbst ist keine Ausführungsfreigabe. Tests mit synthetischen Texten belegen keine allgemeine
Sprachqualität. Von 209 positiven sprachlichen Teilorakeln trafen 208 zu.
Die eine Abweichung „Hofen“ ist ein bekannter Musterfehler, kein Testfehler:
Nach §110 des amtlichen Regelwerks kommt ein einzelner Konsonant zwischen
Vokalen auf die neue Zeile, also „Ho-fen“. Das Muster `.ho4f5en` im
Musterbestand zielt auf Komposita wie „Hof-ende“ und erzwingt deshalb
„Hof-en“. Die Erwartung bleibt unverändert, der Test meldet die Abweichung
als `REFERENZ_ABWEICHUNG`. Eine Ausnahmeliste ist nicht vorgesehen, weil sie
die Regelidentität des Artefakts ändern würde. Die Fälle bilden keinen
vollständigen Goldstandard.

Der Referenzkatalog (209 Fälle in `compiler/runtime/tests/test_silben_asan.c`)
ist nach §86–90 und §110 des Regelwerks zusammengestellt. Die Texte und
Kataloge dienen nur der internen Entwicklung und werden nicht veröffentlicht.
Eine weitergehende Quellen- und Nutzungsrechtsprüfung je Einzelfall ist
deshalb nach Nutzerentscheidung vom 30.10.2026 nicht erforderlich.

Für Änderungen gilt: Stufe 1 lokal; Stufe 2 im frischen Workspace, wenn
Reproduzierbarkeit zu prüfen ist; Stufe 3 in der nativen Windows-VM bei
relevanten Windows- oder ABI-Änderungen. Ein gezielter Workspace-Nachtest
verglich 14 Produkt- und Testdateien einzeln und bestand Compiler-, Zeichen-,
Silben- und Moo-API-Tests. Die Git-Lockdatei musste dafür in eine separate
Testkopie übertragen werden. Der ursprüngliche Workspace-Lock wich ab; die
14 Dateien decken nicht sämtliche Buildabhängigkeiten ab.

Ein versiegelter nativer Windows-Snapshot bestand den Compiler-Build und die
Moo-API-Prüfung mit clang-cl und MSVC-SDK. Das belegt diese Toolchain und
Prüfpfade, keine vollständige Windows- oder Sanitizer-Matrix. Ein separater
Windows-Snapshot bestand später die Identitäts-, Padding- und CPU-Gradienten-
Tests; ihre erneute Workspace-Prüfung ist wegen einer fremd belegten Instanz
zurückgestellt. GPU-Backward-Parität und vollständige Modellgradienten sind
dadurch nicht nachgewiesen.

## Synthetischer Machbarkeitsvergleich

### Checkpoint-Fehler und Neulauf (30.10.2026)

Der ursprüngliche 3×3-Versuch ist **ungültig**. `checkpoint_speichern`
(`compiler/runtime/moo_nn_easy.c`) schrieb die Host-Kopie der Gewichte und
Optimizer-Momente, ohne sie nach GPU-residentem SGD zurückzuholen. Nach
Vulkan-Training enthielt jeder Checkpoint deshalb veraltete Gewichte; das
fensterweise Training (laden, trainieren, speichern) kam nicht voran, und
auch die Auswertung bewertete die veralteten Gewichte. Die alten Werte
(Zeichen 7,47, BPE 4,45, Silben 6,50 bits/byte) lagen entsprechend nahe an
untrainierten Modellen. Fix: vor dem Schreiben `moo_tensor_host_sichern`
für alle Parameter und Momente; Regressionstest
`beispiele/tests/p4_checkpoint_gpu_paritaet.moos` (Task
`p4-checkpoint-regression`) war vor dem Fix rot und ist danach auf CPU und
Vulkan grün.

Der Neulauf nach dem Fix verwendet denselben Korpus, dasselbe Splitmanifest
und dieselbe Konfiguration, Seeds 11/23/47, je Training höchstens 300 s,
Validierung vor Freeze und eine einmalige Testauswertung danach. Bei allen
neun Modellen sind die 48 Gewichtstensoren vor dem Speichern und nach dem
Laden bitgleich (SHA-256 der rohen F32-Bytes). Auf denselben 697
Test-Codepoints und 717 Bytes ergaben sich im Mittel folgende Werte; kleiner
ist besser. „Gleichverteilt“ ist der Wert eines Modells, das jedem Token
dieselbe Wahrscheinlichkeit gibt:

| Tokenizer | Bits pro Byte | Seeds 11 / 23 / 47 | Gleichverteilt |
| --- | ---: | --- | ---: |
| Zeichen | 4,8261 | 4,7425 / 4,8409 / 4,8949 | 7,8093 |
| Byte-BPE | 4,4087 | 4,3820 / 4,4113 / 4,4327 | 4,4127 |
| Silbenkandidaten | 5,7083 | 5,7547 / 5,6876 / 5,6826 | 6,6276 |

Zeichen und Silben haben deutlich gelernt; das BPE-Modell liegt nur rund
0,004 bits/byte unter der Gleichverteilung und ist praktisch untrainiert.
Sein niedriger Absolutwert kommt allein daher, dass ein BPE-Token im Mittel
mehr Bytes abdeckt. Ursache ist vermutlich die geringe Trainingsmenge: BPE
erhielt 29 statt 71 Trainingsfenster und trainierte rund 10 s. Ein fairer
Tokenizervergleich ist mit diesem Aufbau deshalb nicht möglich. Der Testteil
war vor dem Neulauf bereits einmal eingesehen und ist kein unberührter
Holdout. Belege: Thought `c6f2413d-1878-454d-9519-6ad839e2ee84`, Laufordner
`beispiele/p4_silben/run_sil11_fix_20261030/`.

In 26 von 27 festen Fortsetzungen stimmten die Bytes direkt nach dem Training
und nach dem Neuladen überein. Bei BPE Seed 47 kippte eine Greedy-Wahl
(Top-2-Abstand 0,00026); die Logits beider Pfade wichen trotz bitgleicher
Gewichte um bis zu 0,046 ab.

**SIL-13, Ursache und Fix:** Die Positionsschicht (`fw_position` in
`compiler/runtime/moo_nn.c`) kopiert bei Inferenz mit kürzerer Sequenz als
ihrer Maximallänge die ersten Zeilen der Positionstabelle per `memcpy` aus
der Hostkopie. Nach GPU-residentem SGD war diese veraltet; bei voller Länge
läuft die Addition über den GPU-fähigen Pfad und war korrekt. Folge: Jede
Generierung direkt nach GPU-Training rechnete mit alten Positionsgewichten
(Diagnose: Abweichung ab der Positionsschicht, Logits bis 0,038 bei
Kontextlänge 12). Fix: vor der Kopie `moo_tensor_f32_sichern`. Seitdem sind
alle Schichtausgaben direkt nach dem Training, nach Host-Sicherung und nach
Neuladen bitgleich, auf CPU und Vulkan, bei Kontextlänge 12 und 64
(`mise run p4-sil13-regression`). Die Auswertung des Neulaufs lädt die
Checkpoints neu und ist davon nicht betroffen.

Derselbe Fehler wie bei `checkpoint_speichern` steckte auch in
`netz.speichern` (`moo_nn_speichern`); er ist ebenfalls behoben und im
Regressionstest `p4-checkpoint-regression` abgedeckt (vor dem Fix bei Vulkan
rot, danach grün).

### Ursprünglicher Versuch (ungültig, nur zur Nachvollziehbarkeit)

Der Versuch nutzte 15 eigens verfasste kurze Dokumente, getrennt in neun
Trainings-, drei Validierungs- und drei Testdokumente. Je Verfahren wurden
drei Seeds ausgewertet; der Testteil wurde erst nach dem Validierungs-Freeze
bewertet. Wegen des Checkpoint-Fehlers sind die folgenden Werte kein
Qualitätsvergleich:

| Tokenizer | Bits pro Byte | NLL pro Codepoint |
| --- | ---: | ---: |
| Zeichen | 7,4680 | 5,3250 |
| Byte-BPE | 4,4470 | 3,1709 |
| Silbenkandidaten | 6,5030 | 4,6369 |

Die Vokabulare umfassten 262, 499 beziehungsweise 512 Einträge und die Modelle
44.294, 59.699 beziehungsweise 60.544 Parameter. Ohne Token-Embedding
verbleiben 35.910, 43.731 beziehungsweise 44.160 Parameter; der Ausgabekopf
ist dabei enthalten. Ohne Token- und Positions-Embedding verbleiben 33.862,
41.683 beziehungsweise 42.112 Parameter.

Auf dem gesamten Testsplit ergaben sich 697 Zeichen-, 353 BPE- und 528
Silbentokens, entsprechend durchschnittlich 1,0000, 1,9745 und 1,3201
Codepoints pro Token. Dieser Gesamtdurchschnitt ist keine direkte Messung
der von einem 64-Token-Fenster abgedeckten Codepoints. Gleiche Textmengen
und Tokenkontexte garantieren weder gleiche Rechenarbeit noch gleiche
Zeichenkontextabdeckung. Drei kurze Testdokumente und drei Seeds erlauben
keine verallgemeinerbare Qualitätsaussage. Tatsächliche GPU-Aktivität beim
Training wurde stichprobenartig beobachtet; gleichzeitige Desktop-Grundlast
begrenzt Zeitvergleiche. Die reservierten Budgets summierten sich einschließlich
Fehlstart und Vorlaufreserve auf 6480 von 7200 Sekunden; Reservierung ist
keine Messung der verbrauchten GPU-Zeit. Ein erster kurzer Trainingsstart
scheiterte; sein genauer Fehler und ein möglicher Optimizerfortschritt konnten
rückwirkend nicht belegt werden. Die anschließende Fortsetzung und ihre
Budgetkette sind getrennt dokumentiert.

Im ursprünglichen Versuch wurde vor dem Speichern keine In-Memory-Fortsetzung
erfasst. Der Paritätstest des Neulaufs hat diese Lücke geschlossen und dabei
den Checkpoint-Fehler aufgedeckt (siehe oben).

Ein nachträglicher CPU-Check verwendete drei erst nach der Testauswertung
festgelegte Prompts und erzeugte für jeden der neun Checkpoints zweimal je
zwölf Greedy-Tokens. Die Ausgaben der beiden Reloads stimmten jeweils überein.
Von 27 rohen Bytefortsetzungen waren 22 kein gültiges UTF-8: neun von neun
bei Zeichen, acht von neun bei BPE und fünf von neun bei Silben. Die Anzeige
nutzt für ungültige Bytes Ersatzzeichen; die Originalbytes sind im Report
Base64-kodiert erhalten. Dieser Befund gilt für die konkrete, sehr kleine
Versuchsanordnung. Der verlustfreie Roundtrip gültiger Eingaben ist eine
andere Eigenschaft als gültiges UTF-8 bei frei generierten Tokenfolgen.
Der Report `generation_posthoc.json` hat SHA-256
`e168f1e639a679a1943d3670a84ade23b06438d204dc5e1df26c8a1bc6a751b2`.

### Reproduktions- und Prüfbelege

Der synthetische Korpus hat SHA-256
`01c18cd29a07f21616fb7c0ecd7f85be6b4f7feb30c70e5976dcc7f739c23e0d`;
das kanonische Splitmanifest hat SHA-256
`8e74c14e607844e2172b49534edb131b07bada24320bc035554348d6ff98c80f`.
Die experimentbezogene Konfiguration hat SHA-256
`e8224b9e71cee9d4376af549437d193ea791507810d0505cd2a54ef5825d5667`.

Im privaten Laufverzeichnis `beispiele/p4_silben/run_sil11_20261030_resume/`
halten `config.json`, `certificate_candidate.json`, `budget.json`, der
Validierungs-Freeze, Checkpoints und die 18 Berichte den Versuch fest. Die
Zusammenfassung `sil11_summary.json` hat SHA-256
`1eb5d2cc2aa43eb7eb40ff2539503be0e94e3f7752cdff9fb53237c0b67d19a7`.
Die Dateien enthalten laufgebundene Pfade und müssen für eine spätere
Reproduktion samt Manifesten, Lizenzhinweisen und Umfeld erhalten werden.
Ein erneuter Modelllauf benötigt ein dafür gültiges Gate und ein separates
Budget; die Dokumentation ist keine pauschale Freigabe.

Für die Teststufen stehen unter anderem folgende nachvollziehbare Jobs:

- Lokal: Job `35a52aed-abf0-4b98-861c-8e94e829ee8f`; Zeichen- und Silbentests
  unter ASan/UBSan und Moo-API bestanden, 14-Dateien-Marker vor und nachher gleich.
- Workspace: Thought `0f426818-ccaa-4953-b327-72bbc932e58f`; frischer
  14-Dateien-Vergleich und Tests in der separaten Testkopie.
- Windows: Archiv-SHA `c5a094c090f210e70a32cc29f341d9554b91e0be37a256016979b3ceb9c18bfa`,
  Compiler-Job `aade06d6-74b2-41f9-8a1b-ed89e3e77c13` und Moo-API-Job
  `67aa49f6-97a1-472c-b1e4-e711daf71361`; späterer Test-Snapshot-SHA
  `108f967ad5d5d285b0724479dc452981de63d40ebe42ad557f85898ab52737d6`.
- Ergebnisprüfung: Job `8bd78e28-2f2c-4dad-b1bd-bc31617c351a` prüfte die
  18 Report- und Checkpointbindungen, den Freeze, die 27 Ledgerpositionen
  und die Umrechnung der Testmetriken, ohne den Testtext auszugeben.

## Abschlussurteil (SIL-12)

Die Silbenvariante bleibt **experimentell** und wird nicht als Standard
aktiviert. Zeichentokenizer, Byte-BPE und bestehende Checkpoints bleiben
unverändert.

Begründung:

- Die API (Trainieren, Kodieren, Dekodieren, Grenzen, Speichern, Laden,
  Info, Hash) ist umgesetzt und durch native Tests unter ASan/UBSan, die
  Moo-API-Tests und die Regressionen abgedeckt.
- Im Neulauf nach dem Checkpoint-Fix lag die Silbenvariante mit 5,71
  bits/byte hinter dem Zeichentokenizer (4,83). Das BPE-Modell blieb
  praktisch untrainiert; ein Vorteil der Silben gegenüber BPE ist weder
  belegt noch widerlegt. Die Datenmenge erlaubt ohnehin keine allgemeine
  Aussage.
- Die orthografische Trennung folgt den TeX-Mustern und ist nicht in jedem
  Fall sprachlich korrekt (Beispiel „Hofen“).

Bekannte Grenzen:

- Generierungen direkt nach GPU-Training nutzten bis zum SIL-13-Fix veraltete
  Positionsgewichte; die Neulauf-Auswertung (über neu geladene Checkpoints)
  ist davon nicht betroffen.
- BPE erhielt bei gleicher Fensterkonfiguration deutlich weniger
  Trainingsschritte; ein rechenangeglichener Vergleich steht aus.
- Frei generierte Byte-Fortsetzungen sind häufig kein gültiges UTF-8.
- Der Workspace-Nachtest der nachträglich ergänzten Tests steht aus, solange
  die fremde Workspace-Instanz belegt ist.
- Private Laufdaten unter `beispiele/p4_silben/run_sil11_20261030_*` sind
  nicht versioniert und nicht gesichert.

Mögliche Folgearbeiten nur mit gesondertem Auftrag: größerer,
rechenangeglichener Vergleich auf echtem Korpus mit Paritätsprüfung,
Ausnahmeliste für bekannte Musterfehler, Gewichtsmigration oder weitere
Sprachvarianten. Die GPU-Residenz für SGD gibt es seit 10.07.2026 (Commit
`888f223`). Seitdem mit SGD auf der GPU trainierte und per
`checkpoint_speichern` oder `netz.speichern` gesicherte Modelle (etwa Grimm
unter `tests/local/`, `beispiele/ki_sprachmodell_gpu.moos`, multimodale
Beispiele) können veraltete Gewichte enthalten. Die Grimm-Checkpoints vom
28.10. haben je Stufe unterschiedliche Gewichte, sind also nicht vollständig
veraltet; eine teilweise Veraltung lässt sich ohne neues Training nicht
ausschließen. Weitere Stellen, die Gewichte direkt über `->data` lesen, sind
nicht vollständig geprüft.
