# Moo-Langtextvergleich – Auswertung und vollständige Textproben

**Stand: 30. Oktober 2026. Alle 60 Inferenzproben abgeschlossen; kein neues Training.**

## Mein Ergebnis

**Die längeren Texte bestätigen noch keine überzeugende Verbesserung des Deutschen durch das kurze Mischtraining.** Sampling erzeugt abwechslungsreichere Fortsetzungen, doch in der qualitativen Sichtung bleiben Wortbildungsfehler, fehlerhafte Satzanschlüsse und fehlender Zusammenhang deutlich. Mehr Abwechslung ist hier nicht gleichbedeutend mit besserer Grammatik.

Original, SGD, Adam und AdamW wurden mit denselben drei Satzanfängen getestet. Pro Probe entstanden 200 neue Tokens: Greedy einmal und Top-k=20 bei Temperatur 0,6 beziehungsweise 0,9 jeweils mit Seeds 11 und 23. Das ergibt 15 Proben pro Modell, 60 insgesamt. Die vollständigen Ausgaben folgen unten, einschließlich Leerraum, Wiederholungen und Fehlern.

### Konkrete Beobachtungen

**Adam und AdamW sind bei allen 15 entsprechenden Proben byte-identisch.** Dies wurde anhand der SHA-256-Werte der erzeugten Bytefolgen geprüft, nicht nur anhand des sichtbaren Texts. Unterschiedliche Checkpoint-Hashes stehen im Rohbericht. In diesem Versuch gibt es daher keinen sichtbaren Textqualitätsvorteil von AdamW gegenüber Adam.

**Die längeren Ausgaben zeigen Unterschiede, die der kurze Test verdeckt hat.** SGD und Original hatten zuvor dieselben drei 30-Token-Fortsetzungen. Bei 200 Tokens weicht nun die Fortsetzung von „Der König “ ab; die beiden anderen Greedy-Fortsetzungen bleiben identisch. Das ist eine Verhaltensänderung, noch kein Qualitätsgewinn.

**Greedy gerät teilweise in lange Leerzeichenfolgen.** Besonders sichtbar ist das bei Original und SGD nach „Im Schloss “ sowie bei Adam und AdamW nach „Der König “. Die hohen Greedy-Wiederholungswerte werden deshalb teilweise durch Leerraum geprägt; sie dürfen nicht als reine Wortwiederholungsrate interpretiert werden.

**49 von 60 Proben sind gültiges UTF-8; 11 sind es nicht.** Alle zwölf Greedy-Proben waren gültig. Bei Temperatur 0,6 waren 20 von 24 Proben gültig, bei 0,9 waren es 17 von 24. In der Textansicht werden ungültige Bytes als `\xNN` angezeigt. Sie wurden weder entfernt noch in vermeintlich richtige Wörter verwandelt. Die exakten Bytefolgen sind zusätzlich im Projekt gesichert. Der Befund betrifft die frei generierten Ausgaben und beweist für sich keinen Fehler beim Encode/Decode-Roundtrip des Tokenizers.

| Modell | Gültige UTF-8-Proben | Ungültige Proben |
|---|---:|---:|
| Original | 12 / 15 | 3 |
| SGD | 13 / 15 | 2 |
| Adam | 12 / 15 | 3 |
| AdamW | 12 / 15 | 3 |

## Einordnung

Die Modelle bilden erkennbare Märchenwörter und gelegentlich verständliche kurze Wendungen. In diesen längeren Proben entsteht aber kein zuverlässig zusammenhängender, grammatisch korrekter Absatz. Das ist meine qualitative Bewertung der hier abgedruckten Texte, keine automatische Grammatikmessung. Ich leite daraus weder einen allgemeinen Sieg eines Optimierers noch ein Scheitern der Silbenidee ab.

Es wurde nur die Ausgabeauswahl verändert, nicht das Modell weitertrainiert. Drei bekannte Prompts und zwei Sampling-Seeds ersetzen keinen unabhängigen Sprachbenchmark. Das Kontextfenster bleibt bei 64 Tokens, auch wenn 200 neue Tokens erzeugt werden. Für einen weiteren Entwicklungsversuch wären die auffälligen Leerzeichenfolgen und die ungültigen Byteausgaben konkrete Prüfstellen; hier wurde daran nichts repariert.

## Nachweis und Ablage

Synapse-Job: `847fc96f-e12c-4b97-bdfc-c0d7c9b1a838`, regulär beendet mit Exit-Code 0. Inferenzlauf am 30.10.2026 von 18:32:47 bis 18:33:27 UTC, entsprechend etwa 19:32–19:33 Uhr in Deutschland. Alle vier Checkpoints sowie Tokenizer, Sampler und Eingabekonfiguration wurden auf Identität geprüft; die Checkpoints blieben unverändert.

Die Originaldaten liegen im Moo-Projekt unter `build/p5_langtext_20261030T183247_434841/`. Der folgende maschinell erzeugte Rohbericht wurde beim Export bytegenau gegen seine Projekt-SHA-256 geprüft: `91620af407e677e90f984b54360c5a4e56085dc2f1e116aac4ffdf30756d42ad`. Alle Texte sind tatsächlich von Moo erzeugt; die Beurteilung in diesem Vorspann stammt von mir.

---

