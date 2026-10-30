# Maskiertes SFT und Korrekturtraining

Die nativen Funktionen `trainiere_sft(forward, daten, konfig)` und
`trainiere_korrekturen(forward, daten, konfig)` führen pro Schritt
Forward, maskierte Kreuzentropie, Backpropagation und Optimiererschritt
aus. `forward(ids)` ist eine Moo-Funktion mit genau einem Argument; sie
liefert Logits der Form [Sequenzlänge, Vokabular]. Der Callback muss die
trainierbaren Parameter aus `konfig["params"]` verwenden und selbst die
Modellarchitektur einschließlich Residualverbindungen ausführen.

`daten` enthält `tokenizer_id` und eine nichtleere Liste `beispiele`.
Jedes Fenster enthält Listen `input_ids`, `target_ids`, `loss_mask`
von voller `sequenzlaenge`, außerdem `nutzlaenge` und `ziel_start`.
Das Ziel ist um ein Token verschoben. Die Maske ist genau vor `ziel_start`
und ab `nutzlaenge` null, dazwischen eins. Die Positionslänge muss zum
Modell passen; dies muss der Aufrufer anhand der Positionsschicht sicherstellen.

`konfig` enthält `tokenizer_id`, `sequenzlaenge`, `vokab`, `params`
und `schritte`. Bei neuem Optimierer kommen `optimierer` (sgd, adam,
adamw), `rate` und optional `momentum` bzw. `decay` hinzu. Für Resume
wird stattdessen `opt` mit den zum Modell gehörigen Parametern übergeben.
Das Ergebnis enthält `opt`, `verluste` und `schritte`. Die Tokenizer-ID
muss aus dem tatsächlich verwendeten Tokenizer stammen und mit dem beim
Checkpoint-Laden geprüften Wert übereinstimmen; zwei gleiche frei gewählte
Strings beweisen keine Tokenizeridentität.

Korrekturfenster benötigen zusätzlich `akzeptiert: wahr`, `aufgabe`,
`original`, `korrektur` und `begruendung`. `konfig["encoder"]` muss
als einstelliger Moo-Callback den verwendeten Tokenizer aufrufen. Die
Runtime prüft anhand dieses Encoders den Bezug zwischen Text und Tokens.
Für den Pilot erzeugt `korrektur_fenster_aufteilen(paar, encoder, sequenzlaenge, pad)`
aus dem ursprünglichen Modell-Prompt und der korrigierten Fortsetzung
volle Fenster mit halber Überlappung. Das fehlerhafte Original bleibt
Metadatum. Ein Zieltoken wird innerhalb der vom Adapter erzeugten Fenster
genau einmal maskiert. Fenster enthalten `prompt` und `fenster_offset`;
die Runtime überprüft beide sowie die kanonische Zielgrenze anhand des
Encoders. Die Funktion liefert eine Liste, die als `daten["beispiele"]`
verwendet wird. `art` kennzeichnet freie Korrekturen.

Die bestehende Funktion `korrektur_fenster` bleibt für ein einzelnes
Fenster verfügbar; sie kodiert `aufgabe + "\\n" + original + "\\n"`
und Korrektur getrennt und weist Überlänge zurück. Die Bestätigung
`akzeptiert` ersetzt keine inhaltliche Prüfung von Original und Korrektur.

Vollständige synthetische Nutzung und Negativfälle:
`beispiele/p5/sft_test.moos`. Ein Beispiel mit SIL-14, voller
Positionslänge und separatem Checkpoint liegt in
`beispiele/p5/korrektur_sil14_test.moos`. Nach GPU-Updates vor direktem
Hostzugriff die Runtime-Sicherungsfunktionen bzw. Checkpoint-Funktionen
verwenden. Checkpoint-Zielpfade müssen vom Basischeckpoint getrennt sein.

Testaufgaben: `mise run test-p5-sft`, `mise run test-p5-sft-sanitize`
und `mise run test-p5-sil14-sft`. Strikte Vulkanläufe werden über den
koordinierten GPU-Lock ausgeführt.
