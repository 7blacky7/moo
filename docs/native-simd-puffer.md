# Sichere SIMD-Bytezugriffe

`simd_laden(u8x16, daten: puffer[u8], versatz: u64)` liefert 16 Bytes
als Wert. Mit `u8x32` liest derselbe Baustein 32 Bytes.
`simd_speichern(daten, versatz, wert)` schreibt einen `u8x16`- oder
`u8x32`-Wert zurück. Andere Puffer- und Vektortypen werden abgewiesen.

Beide Formen prüfen zuerst `versatz <= länge(daten)` und
`breite <= länge(daten) - versatz`. Sie verlangen keine besondere Ausrichtung.
Die Prüfung verwendet keine überlaufende Addition. Ein ungültiger Zugriff
geht über den vorhandenen nativen Fehlerpfad einschließlich Cleanup.
Ein unbehandelter Fehler an einem externen Einstieg bricht ab.
Die Bausteine versprechen keine atomaren Mehrbytezugriffe.

`@liest`-Puffer dürfen geladen, aber nicht beschrieben werden.
Ein geladener Vektor ist eine Kopie und hält keine Leihe auf den Puffer.
Die Lebensdauerverträge des Puffers gelten weiter. Überlappende Quellen
und Ziele einer Lade-/Speicherfolge beobachten den zuvor geladenen Wert.

Im Bare-/UEFI-Profil ist `@simd` erforderlich. Die vorhandene CPU-/OS-Prüfung
und Zustandssicherung bleiben aktiv. Das Skalarprofil benutzt Bytezugriffe;
`gfni128` bleibt auch bei 32 Bytes auf XMM beschränkt.
Eigene gleichnamige Funktionen behalten Vorrang. Ein eigener Strukturtyp
namens `u8x16` oder `u8x32` wird nicht als eingebauter Vektortyp behandelt.

```moo
@nativ
@simd
funktion block_kopieren(ziel: puffer[u8], quelle: puffer[u8]) -> void:
    setze block: u8x32 auf simd_laden(u8x32, quelle, 0)
    simd_speichern(ziel, 0, block)
```

Regression: `mise run -C skripte/p11/array-q simd-puffer`.


`pufferansicht(view)` übernimmt einen vorhandenen geliehenen Pufferdeskriptor.
`pufferansicht(view, start:u64, anzahl:u64)` leiht einen Teilbereich gleichen
Elementtyps; Zahlen- und nominale Strukturpuffer bleiben typidentisch.
Es gelten `start <= länge(view)` und `anzahl <= länge(view) - start`.
Der leere Endbereich ist gültig. Die Ansicht besitzt und kopiert keine Daten;
die Leihe darf ihren Speicher nicht überleben. Fehler benutzen Cleanup.
Für vier SIMD-Blöcke kann eine 128-Byte-Teilansicht einmal geprüft werden;
Offsets 0, 32, 64 und 96 sind dann statisch innerhalb dieser Ansicht.
Feste lokale Arrays dürfen geliehene `puffer[T]` speichern. Literale behalten
die Borrow-Herkunft; Rückgaben mit einer lokalen Leihe bleiben M-M002.
Explizite getypte Arraydeklarationen `setze a: [T; N] auf 0` nullen jedes Element.
