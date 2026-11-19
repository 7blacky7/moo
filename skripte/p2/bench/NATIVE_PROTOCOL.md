# P2-T54: native Vergleichsmessungen

Das Leistungsziel **≤ 1,3× C** steht als Vorschlag zur Abstimmung. Die
Auswertung zeigt den Quotienten je Last. Eine Zielüberschreitung wird als
Messergebnis protokolliert.

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
