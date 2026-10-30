# P5 Validierungs- und Checkpoint-Protokoll v1

Stand: 30.10.2026. Dieses Protokoll und die unten gehashten Dateien sind
vor den P5-Vergleichsläufen eingefroren. Änderungen erfordern eine neue
Protokollversion und neue, nicht mit v1 vermischte Ergebnisse. Der Dry-Run
unten ist eine technische Basismessung und kein Trainingsvergleich.

## Daten und Isolation

Die 100 Lehrerpaare aus `daten/pilot_lehrer_v2.json` (SHA-256
`42f3f98b8a7004c0fa8ded7b94642701870edbb0c37a477c6895846be6b5c629`)
werden dokumentweise nach identischem Modell-Prompt gruppiert und zu
70 Train-, 15 Val- und 15 separaten Pilot-Testpaaren aufgeteilt. Ein Prompt
kommt nur in einem Split vor. Seed 271828, deterministische LCG-Mischung
und die anschließende Auswahl ganzer Gruppen auf genau 70/15/15 werden in
`skripte/p5_split_pruefen.py` reproduziert; der Test prüft zusätzlich
ID-Überschneidungen, Prompt-Lecks und identische Korrekturen über die
Splitgrenzen.

| Artefakt | Inhalt | SHA-256 |
|---|---|---|
| `daten/split_train.json` | 70 Train-IDs | `97b4762f84914b9a62f25006ef441f99cf7020e668c754c9ee0503f20720ee0e` |
| `daten/split_val.json` | 15 Val-IDs | `1fc768f2fcb8956d75f30d99c62b5e65978d1158d501764d68dc6ef8b35fcff2` |
| `daten/split_test.json` | 15 Pilot-Test-IDs | `0830afb0290ee0d010754e9f345692cbdc18cddfb4e72ed07318be0651a3cf50` |
| `daten/train_korrekturen.txt` | Train-NLL-Dokument | `d3504252784c2702d959b3aa74ed7f48810652d29d4ef23d5c410074c29d7c44` |
| `daten/val_korrekturen.txt` | Val-NLL-Dokument | `c7ed332fa3d5324f0fbbcf5fab79127c88e5e370893673cd724c56aedc2ffaf7` |

Die beiden Textdokumente bestehen aus den Korrekturen in aufsteigender
Aufgaben-ID-Reihenfolge, getrennt durch zwei Zeilenumbrüche, mit einem
abschließenden Zeilenumbruch. Diese Dokumente sind fest; ihre Verkettung
führt Kontext über Paargrenzen hinweg und misst nicht den maskierten
bedingten SFT-Korrekturverlust. Sie dienen als vergleichbare
Next-Token-NLL, nicht als alleiniger Nachweis für Korrekturfähigkeit.

Nur die 70 Trainpaare fließen in P5-Updates ein. Die 15 Valpaare dienen
zur regelmäßigen Messung und Auswahl. Die 15 Pilot-Testpaare bleiben bis
zur abschließenden Auswertung ungenutzt. Zusätzlich bleibt der bestehende
P4-Holdout mit 7.221 Codepoints und SHA-256
`d057c8fbc0392bddfdd584cd9075e7219e61e0f55f411da3a4099025169075a8`
bis zur einmaligen Abschlussevaluation des ausgewählten Modells
unangetastet; er dient nie zur Auswahl oder Abstimmung. Er ist gegenüber
P5 zurückgehalten, aber kein Nachweis für Unabhängigkeit von allen
Vorerfahrungen des Ausgangsmodells. Die P4-Train-Datei
(SHA-256 `7eae7b9bcac91995b9d74b4f0fe5f31957201f7c42ce9d63e16c01984a5a0770`)
darf bei vorab identisch festgelegter Beimischung nur als Trainingsmaterial
dienen, nicht als unabhängige Val- oder Testmenge.

## Mess- und Auswahlregeln

Alle Läufe starten von einer lesenden Kopie des gleichen
SIL-14-Checkpoints `full.mook` (SHA-256
`2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d`),
mit Tokenizer-SHA-256
`20ef4b48638359a42141de61ff30d38be7f723c7fe8c612506e9d67f59b62eed`,
Architektur `grimm-silben-250k-v1`, Vokabular 512, Pad-ID 0 und voller
Positionslänge 64. Jeder Lauf schreibt neue Checkpoints in sein eigenes
Verzeichnis; der Basischeckpoint wird nicht überschrieben. Laufmanifest,
Seed, Optimiererzustand, Hyperparameter, Daten- und Checkpoint-Hashes sowie
Budget und Backend werden mitprotokolliert.

Vor dem ersten Update, alle 100 Optimiererschritte und nach dem letzten
Update werden Train- und Val-NLL auf den eingefrorenen Textdokumenten mit
derselben Tokenizer-/Kontextkonfiguration gemessen. Für beide gilt
`bits_pro_byte = nll / bytes / ln(2)` sowie
`nll_pro_codepoint = nll / codepoints`. Die API bewertet das erste Token
nicht; Bytes und Codepoints beziehen sich auf genau den verbleibenden
Suffix. Siehe `README_eval.md`. Bei unterschiedlichen Tokenizern sind
Token-NLL und Perplexity nicht direkt vergleichbar.

`evaluiere_modell` liefert Metriken und Daten-/Tokenizer-/Checkpoint-
Hashes; `checkpoint_waehlen` erhält ausschließlich kompatible
Val-Berichte und wählt die kleinste natürliche NLL pro bewertetem Byte,
äquivalent zu Bits/Byte. Berichte für jeden Lauf vorab nach Schritt,
danach Checkpoint-Hash aufsteigend sortieren: bei exakt gleichem Score
gewinnt der zuerst stehende Bericht. Train-Metriken, Textproben und
Testsätze dürfen nie in diese Auswahl eingehen. Die aktuelle Eval-API
akzeptiert nur `val` und `test`; eine korrekt als `train`
gekennzeichnete Train-Messung benötigt die angefragte additive Erweiterung.
Train-Daten dürfen nicht als Val etikettiert werden. Den Laufvergleich
erst nach dieser Erweiterung starten. Rundung durch JSON-Serialisierung ist
zu beachten; Auswahl möglichst aus ungerundeten Berichten im Speicher.

## Textproben und Ergebnis

Für jeden gemessenen Checkpoint dieselben drei unverfänglichen Prompts
verwenden: `Es war einmal`, `Der König sprach`,
`Im Wald stand`. Temperatur 0,8, Top-k 20, Seeds 11/23/47, höchstens
120 erzeugte Bytes, Stopp am ersten Satzende. Generator, Saatalgorithmus,
Promptbytes, Tokenizer, Modellhash, erzeugte Rohbytes und Einstellungen
müssen vor dem ersten Vergleich einheitlich festgelegt und protokolliert
werden; die Implementierung dieses gemeinsamen Sampling-Runners ist noch
nicht Bestandteil dieses Protokolls. Textbeispiele sind ergänzende
qualitative Beobachtungen, kein Auswahlkriterium.

Nach der Val-Auswahl wird der Pilot-Test und der P4-Holdout jeweils einmal
auf dem gewählten Checkpoint ausgewertet und die verwendete Hashidentität
dokumentiert. Keine Rückkopplung in die Auswahl. Bericht enthält
Train-/Val-Verläufe, Bits/Byte, NLL/Codepoint, Zeiten, GPU-Fallbacks,
feste Textproben, Holdout-Ergebnis und Grenzen. Die 97 frei rekonstruierten
Korrekturen der 100 Pilotpaare sind eine erhebliche Lehrer-Bias- und
Überkorrekturquelle; die Aufgabe muss zusätzlich mit passenden
Korrektur- und Vergessenstests untersucht werden.

## Technischer Dry-Run

`mise run test-p5-val-protokoll` reproduziert und prüft die Splits
und evaluiert die Basis nur auf dem oben eingefrorenen Val-Dokument.
Job `ba17cfe0-af3a-4c16-b7b7-2a84a17e4539`, CPU, RC 0:
NLL rund 1168,85 auf 722 bewerteten Bytes und 707 Codepoints,
rund 2,33558 Bits/Byte und 1,65325 NLL/Codepoint. Die JSON-Ausgabe ist
gerundet; es ist kein Training, keine Testauswertung und keine Aussage zur
Qualität gegenüber anderen Checkpoints.
