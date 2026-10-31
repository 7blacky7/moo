# Frame-Statistik ohne Grafik-Kontext

`test_frame_statistik(frame, x, y, breite, hoehe)` und der Alias
`test_frame_statistics` messen ein unveraendertes RGBA8-`MOO_FRAME`.
Fenster vorher explizit mit `test_frame_grab` aufnehmen; ein gespeichertes BMP
kann mit `frame_lade_bmp` geladen werden. Die Statistik selbst erzeugt keinen
Capture, Postprozess, Swap oder GPU-Aufruf.

Die ROI beginnt oben links. Koordinaten und Groessen sind endlich und
ganzzahlig; das Rechteck [x,x+breite) mal [y,y+hoehe) muss nichtleer und
vollstaendig im Bild liegen. Falsche Typen, NaN/Inf, Bruchwerte, ungueltige
Frames und Grenzen werfen einen Moo-Fehler. Es gibt kein stilles Cropping.
Vierzahlige/falsche Argumentlisten werden bereits vom Compiler abgewiesen.

Das Ergebnis ist ein Dict:

- `pixelzahl`: Anzahl ROI-Pixel.
- `rot`, `gruen`, `blau`, `alpha`: jeweils `min`, `max`,
  `mittelwert`, `varianz` (Populationsvarianz, Division durch N) und
  `histogramm` (Liste mit 256 exakten Haeufigkeiten).
- `any_rgb255`: Pixel mit mindestens einem RGB-Kanal gleich 255.
- `all_rgb_le5`: Pixel mit allen drei RGB-Kanaelen kleiner/gleich 5.

Min, Max und Mittelwert sind Ausgabecodewerte (0 bis 255), die Varianz
hat die Einheit Codewert zum Quadrat. Histogramm, Pixelzahl und gemeinsame
RGB-Zaehler zaehlen Pixel. Keine dieser Groessen ist lineare physikalische Luminanz.
Alpha wird als eigener Kanal gemessen; die beiden gemeinsamen RGB-Zaehler
ignorieren Alpha. BMP-Speicherung verliert Alpha: RGB-Replays aus BMP sind
getrennt von RGBA-Doppelcaptures zu beurteilen.

Beispiel: ein Kanal [0,0,255,255] ergibt N=4, Mittelwert=127.5,
Varianz=16256.25, Min=0, Max=255, Histogramm[0]=2, Histogramm[255]=2.
Eine konstante ROI hat Varianz 0.

Der C11-Kern braucht O(Pixelzahl + 4*256) Zeit und konstante 4*256
64-Bit-Zaehler. Die Pixelzahl ist auf 2^53-1 begrenzt, sodass alle
ausgegebenen Counts als Moo-Zahl exakt bleiben. Summen passen dadurch
in uint64; die Varianz wird aus dem Histogramm um den Mittelwert berechnet,
ohne ueberlaufende Quadratsummen oder Differenz grosser nahezu gleicher Werte.
Keine OS-API; auch bei Windows-long-double=double bleibt die Formel gueltig
(uebliche Fliesskomma-Rundung). Bestehende region/diff APIs bleiben unveraendert.

## Dedizierte Pruefung

`mise run test-gfx-roi` baut den Release-Compiler und fuehrt beide CPU-Tests
mit ASan/UBSan fuer den C-Kern aus. Der Linux-Testwrapper oeffnet kein Fenster.
Nur fuer einen bereits separat gebauten, koordinierten Snapshot kann
`GFX_SKIP_BUILD=1 mise run test-gfx-roi` den Build ueberspringen; dieser Lauf
ersetzt keinen Releasebuild. Compiler- und Testquellen-Hashes werden gegen
Aenderungen waehrend des Tests geprueft.

`compiler/runtime/tests/test_gfx_frame_statistics.c` prueft den echten
Frame-Kern mit kleinen Owning-Container-Stubs, unabhängiger Pixelreferenz,
allen Teilrechtecken eines 5x3-Bilds, Padding, Alpha, gemeinsamen Counts
und ungueltigen Eingaben inklusive NaN/Inf. Optionales Argument ist der
Ausgabepfad fuer ein 2x2-BMP aus dem bestehenden Moo-Saver.

`compiler/tests/gfx_frame_statistics.moos` nutzt echte Moo-Container,
Sprachbindungen, beide Aliase, BMP-Lader und Frame-Diff. Die Umgebungsvariable
`GFX_ROI_FIXTURE` zeigt auf das vom C-Harness erzeugte BMP. Kein Fenster noetig.
Die Testausfuehrung und ASan/UBSan- sowie Regel21-Gates werden im
Synapse-Ergebnis dokumentiert.
