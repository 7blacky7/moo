# P2-T54: native Vergleichsmessungen

Das freigegebene Leistungsziel ist **≤ 1,30× C -O2 je Last**, gegen GCC
und Clang, jeweils auf CPU 28 und CPU 0. Grundlage ist der Zielentscheid
zu P2-T54/P2-T127 vom 19.11.2026. Maßgeblich sind fünf alternierende
Paare mit bytegleichen Ergebnissen; die Auswertung zeigt jeden Quotienten.
Eine Zielüberschreitung bleibt ein sichtbarer Messbefund. Die historischen
JSON-Feldnamen `pending_proposed_limit` und `meets_proposed_limit` bleiben
für bestehende Auswerter erhalten; die Grenze ist inzwischen freigegeben.

## P2-T127: Restberechnung nach dem Entrollen

Die Compiler-Kanonisierung nutzt für konstantes unsigned `d != 0` und
`c & !((d & -d) - 1) == 0` die Identität
`(x | c) % d == (x % d) | c`. Gemeinsame Teilausdrücke werden danach mit
EarlyCSE zusammengeführt. Signed-Rest, dynamische Teiler und nicht passende
Masken bleiben unverändert. Pointer-Alignment und Inbounds-Annahmen bleiben
unverändert. Das Testziel `mise -C skripte/p2/zahlen run modulo-codegen`
prüft 108.195 C++-Orakelfälle, drei echte Compiler-Objektziele und einen
einzigen Rest im skalaren x86-Paar.

Drei vollständige Wiederholungen auf dem lokalen Messrechner, jeweils fünf
alternierende C/Moo-Paare je Last, ergeben folgende Medianquotienten
(Moo/C, Minimum bis Maximum der drei Suite-Läufe):

| CPU | C-Compiler | Füllen | Rauschen |
| --- | --- | --- | --- |
| 28 | Clang | 0,901–0,953 | 0,998–1,068 |
| 0 | Clang | 0,888–0,932 | 0,907–1,145 |
| 28 | GCC | 0,607–0,658 | 0,976–1,045 |
| 0 | GCC | 0,647–0,796 | 0,886–1,054 |

Alle 84 Lastquotienten liegen bei höchstens 1,283; alle Ergebnisbytes sind
identisch. Die Füllen-Ausgangsmessung desselben Protokolls lag gegen Clang
bei 1,559 (CPU 28) und 1,662 (CPU 0). Quellen von Rechenkern, C-Referenz und
Messrahmen sind unverändert. Compiler-SHA256:
`476074b6a7b5fb8d3d9283f9006853bc7536a3dc48e4a0157576ed006b26fac5`.

Der historische Rauschen/GCC-Ausreißer von 1,35 auf CPU 0 ist in den drei
Wiederholungen nicht reproduzierbar. Der Moo-Rauschen-Maschinencode vor und
nach der Änderung ist identisch. Zusammen mit der Streuung der Rohwerte
spricht das für Messrauschen; eine Codegen-Verbesserung für Rauschen wird
daraus nicht abgeleitet.

Lokale Rohbelege: `build/p2-t127/matrix-v2-full.json`,
`matrix-v2-summary.json`, `basis.json` und `gates/*rauschen*.asm`.
Die einzelnen Suite-Verzeichnisse liegen im isolierten Prüfstand
`build/p2-t127/gate/build/p2-bench/native-suite/run.*/`.
Synapse-Testjobs: `0d19a3f5` (12 Suites), `f8d1d94c` (Auswertung),
`2c04f88f` (Rauschen-ASM), `1d24796c` (Modulo-Gate).


```bash
CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p7-t25/target \
MOO_COMPILER=/home/blacky/dev/moo/build/p7-t25/target/release/moo-compiler \
mise run p2-bench-native suite
```

Jeder Lauf speichert Quellen-Snapshot, IR, Objekt, Prüfsummen, Rohwerte und
`result.json` unter `build/p2-bench/native-suite/run.*/`.
`MOO_BENCH_CPU` wählt bei Bedarf einen verfügbaren CPU-Kern.
`MOO_BENCH_SNAPSHOT_REF` ergänzt die dem Compiler zugeordnete Revision.

## Lasten

Die sieben Fälle der dynamischen Baseline bleiben als Ausgangspunkt erhalten:

| Fall | Eingabe / Wiederholungen |
| --- | --- |
| Schleifensumme | 10 Mio. Summanden |
| Vektoraddition | 10 Mio. f32-Elemente |
| Matrix × Vektor | 1 Mio. mat4×vec4-Operationen |
| Füllen | 10 Mio. Elemente |
| Strukturdaten | 100.000 Punkte |
| Builtin-Aufruf | 10 Mio. Aufrufe |
| Rauschen | 512×512, gleiche Koordinaten und Seed |

Zusätzlich wird der Grafikfall mit 100.000 indizierten Dreiecken als eigener
Fall abgestimmt. Seine konkrete Datenverteilung, Kernoperation und Prüfsumme
werden vor der Messung mit Grafik festgelegt.

## Vergleich und Ablauf

- Für den nativen Moo/C-Vergleich dieselben Datentypen, Eingangsdaten und
  Rechenschritte verwenden. Dynamisches Moo als getrennte Vergleichsspalte
  kennzeichnen, wenn es Listen und double statt nativer f32-Puffer nutzt.
- Allokation, Datenaufbereitung und gegebenenfalls Upload getrennt vom
  wiederholten Rechenkern messen und beschreiben. CPU- und GPU-Arbeit nicht
  vermischen; die C-CPU-Referenz belegt keine GPU-Leistung.
- Vor der Zeitmessung Ergebnisse anhand einer definierten Prüfsumme bzw. eines
  vollständigen Vergleichs mit dokumentierter Float-Toleranz prüfen. Das
  Ergebnis muss beobachtbar bleiben; Optimierungen und eventuell gemeinsam
  aufgerufene Runtime-Funktionen in den Ergebnissen offenlegen.
- Einen verfügbaren CPU-Kern pinnen, je Variante einmal aufwärmen und danach
  fünf Messungen je Variante in abwechselnder C/Moo-Reihenfolge durchführen.
  Median, Minimum, Maximum und alle Einzelwerte je Seite sowie den Quotienten
  der Mediane berichten. Timeout, Ergebnisvalidierung und Hashprüfung müssen
  einen fehlerhaften oder veränderten Lauf als Fehler markieren.
- Compiler- und Quellenhashes vor/nach dem Lauf, CPU-Modell, gewählten Kern,
  Plattform, Compiler-/C-Compiler-Version, Optimierungsflags und Zeitpunkt im
  JSON unter `build/p2-bench/` festhalten. C-Referenz mindestens `-O2`;
  abweichende Optimierungen und Float-Regeln explizit ausweisen.

Die vorhandene dynamische Baseline misst Prozesslaufzeit einschließlich
## Messrahmen

C -O2 und Moo-Bare-O2 teilen sich einen Host-Messrahmen und dieselbe C-ABI.
C-Referenzen liegen in einer eigenen Übersetzungseinheit. Allokation,
Initialisierung und Vergleiche liegen außerhalb des Kernintervalls.
Das Aufwärmen prüft das vollständige Ergebnis byteweise. Danach wechseln
C und Moo in fünf Paaren die Startreihenfolge. Die zuletzt erzeugte Ausgabe
muss dieselbe Prüfsumme wie im Aufwärmlauf haben.

Die Summanden liegen als vorab aufgebaute u64-Daten vor. Der Kern muss die
Eingabe lesen, sodass ein Optimierer die Summe der Induktionsvariablen
nicht durch eine geschlossene Formel ersetzen kann. Matrix × Vektor
schreibt alle vier f32-Komponenten. Strukturdaten liest ein typisiertes
Punkt-Array. Füllen und Vektoraddition schreiben einen f32-Puffer.
Der Aufruftest benutzt dieselbe Schleife und je ein C-ABI-Betragssymbol.
Rauschen misst zwei Schleifen um dieselbe Seed-parametrisierte
`moo_noise_perlin2`-Primitive. Alle diese Details gelten für beide Seiten.

Die alte dynamische Baseline misst die Prozesszeit inklusive Aufbau und
Abbau. Die Einzelproben `summe` und `puffer` messen zusätzlich einen
dynamisch/nativen Moo-Einstieg. Ihre Ergebnisse sind eigene Messreihen.
