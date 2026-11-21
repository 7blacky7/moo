# Native CRC32C

`crc32c_aktualisieren(zustand: u32, daten: puffer[u8]) -> u32`
berechnet einen unkomplementierten CRC32C-Zwischenstand mit dem reflektierten
Castagnoli-Polynom `0x82F63B78`. Ein leerer Puffer liefert den Startzustand.
Der Puffer wird nur gelesen; der Aufrufer hält seine geliehenen Daten gültig.

Die Form `crc32c_aktualisieren(zustand, daten, start: u64, anzahl: u64)`
liest einen geprüften Teilbereich. Sie verlangt `start <= länge(daten)`
und `anzahl <= länge(daten) - start`. Ein leerer Bereich am Ende ist gültig.
Ein Bereichsfehler benutzt denselben nativen Fehler-/Cleanup-Pfad wie
Byte- und Indexzugriffe; ein unbehandelter Fehler am externen Einstieg bricht ab.
Die Vollpufferform erzeugt keinen Bereichsfehler.

Aus `nativ_crc32c` importieren Anwendungen gewöhnlich:
- `crc32c(daten)`: Standardstart `0xFFFFFFFF`, Ergebnis abschließend XOR `0xFFFFFFFF`.
- `crc32c_fortsetzen(zustand, daten)`: gleicher Zwischenzustandsvertrag wie der Builtin.
- `crc32c_byte(zustand, wert)`: byteweise Referenz, gleiche Rechnung.

Teilupdates müssen den unkomplementierten Zwischenstand weiterreichen.
Für die ASCII-Bytes `123456789` liefert die Standardform `0xE3069283`.
Eigene Funktionen namens `crc32c_aktualisieren` behalten Vorrang.

Auf x86-64 verwendet der Compiler vier unabhängige CRC32-Ganzzahlketten
mit mathematischer Faltung und generischen Potenz-zwei-Kacheln; der serielle
Rest bleibt unter 32 Byte. Es werden keine XMM-/YMM-/ZMM-Register benötigt.
Außerhalb eines gültigen [CPU-Bereichs](native-cpu-bereich.md) bleibt die
SSE4.2-Prüfung per CPUID aktiv. Innerhalb des gebundenen Bereichs gilt dessen
threadlokale Freigabe. Ohne Merkmal sowie auf anderen Zielen läuft der
portable Tabellenpfad. Es gibt keinen prozessweiten Erst-CPU-Cache.
CRC32C ist eine Fehlerprüfsumme; die Tabellenpfade haben datenabhängige
Adressen und versprechen keine Konstantzeit.

MooFS benutzt sichere Teilbereiche und vier logische Nullbytes für das
Prüfsummenfeld bei Offset 40; das Eingabeabbild bleibt unverändert.

Gezielte Regression: `mise run -C skripte/p11/array-q crc32c`.
Vergleichsmessung: `mise run -C skripte/p11/array-q vergleich`;
die ISA-L-Bibliothek ist ausschließlich Messreferenz.
