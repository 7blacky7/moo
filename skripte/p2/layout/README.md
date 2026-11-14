# P2: C-Layoutreferenz und Puffer-Testplan

`layout_ref.c` ist eine unabhängige C11-Referenz für x86_64 Linux SysV mit GCC/Clang. Sie prüft zehn Layouts per `_Static_assert` und gibt Größe, Alignment und Feldoffsets als Text aus. Packed- und Alignment-Attribute sind Compilererweiterungen; `PackedNested` packt nur das äußere Aggregat, nicht das innere. Dies ist kein Moo- oder Windows-ABI-Nachweis.

Beispiel: `cc -std=c11 -Wall -Wextra -Werror skripte/p2/layout/layout_ref.c -o skripte/p2/layout/layout_ref.bin`, anschließend die erzeugte Binärdatei starten. Vergleich mit dem späteren Moo-Compiler muss **dieselben** Typen, das Zieltriple und die Feldtypen verwenden; Größe, Alignment, Offsets und Arraystride einzeln vergleichen. Das Layout liefert kein Wireformat; Padding darf weder als Gleichheitsinhalt noch ungeprüft serialisiert werden. Packed-Felder benötigen geeignete unaligned Zugriffe. Kombiniertes packed und höheres Alignment wird laut ADR vorerst vom Moo-Frontend abgelehnt; diese Negativdiagnose ist kein C-Compile-Test.

## P2-T15: Testplan für zukünftige Puffer

- Leerer Puffer, gültige Randindizes, Index = Länge und größer, negativer Index; Längen-, Bytegrößen- und Offsetoverflow sowie Allokationsfehler; nach Fehler keine Teilmutation.
- View/Slice über Ende des ursprünglichen Owners hinaus, verschachtelte Views, Release aller Aliase in verschiedenen Reihenfolgen; Leak-/Use-after-free-Prüfung.
- Aktive Views und FFI-Pins sperren Längenänderung, Schrumpfen und Reallokation; Elementmutation weiterhin sichtbar. Nach letztem Release/Unpin Wachstum wieder möglich. Auch asynchrone FFI-Nutzung bis zum tatsächlichen Nutzungsende pinnen.
- Geteilte Pufferhandles und tiefe Kopie unterscheiden; aliasierende Views, Selbstzuweisung und Fehlercleanup; keine unbelegte noalias-Annahme. Zunächst mutable Puffer threadgebunden; Thread-Übergabe nur nach ausdrücklich sicherem Vertrag testen.
- `puffer[struktur]`: Arraystride und Alignment gegen C, verschachtelte Felder und Wertkopien, verwaltete Felder und Besitzerverträge explizit prüfen.
- Frame-/Tensor-Ansichten: Format, Stride, Owner-Lebensdauer und Host/Device-Kohärenz; GPU-dirty und bf16-Materialisierung, Schreib-/Invalidierungsregeln und Verfügbarkeit. Zero-copy nur bei nachgewiesener Kohärenz, nicht allein durch einmaliges Host-Sichern.
- ASan/LSan/UBSan und Fehlerpfade; Legacy-IR-Gate sowie spätere native Linux- und hashgebundene Windows-Tests. Ein ungeprüfter Zugriff ist eine explizite Operation im `unsicher`-Block und kein globales Abschalten sicherer Zugriffe.

P2-G0 ist entschieden; G1/G2 und die separate Compiler-Änderungssperre bleiben bestehen. Dies ist ein Testplan, keine Behauptung, dass Puffer bereits implementiert sind.
