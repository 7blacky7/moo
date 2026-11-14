# P2-T54: native Vergleichsmessungen – Entwurf

Stand: Vorschlag zur Abstimmung mit Grafik (Kanal p2-nativ, #35420).
Das Leistungsziel **≤ 1,3× C** ist ein Vorschlag und noch keine bestätigte
Akzeptanzgrenze. Für die folgenden nativen Messungen liegen noch keine
Ergebnisse vor.

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
Allokation, Prüfsumme und Aufräumen. Die dortigen Faktoren sind deshalb nicht
ohne Weiteres mit einer späteren reinen Kernzeit vergleichbar. Native Schleifen,
Puffer, Strukturwerte und weitere Aufrufpfade sind noch nicht vollständig
verfügbar; das vorbereitete native Summengate scheitert vor der Messung.
