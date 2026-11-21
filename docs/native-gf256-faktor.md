# GF256 mit gemeinsamem Vektorfaktor

In einer nativen `@simd`-Funktion berechnet
`simd_gf256_mal_faktor(a: u8x16, faktor: u8) -> u8x16`
beziehungsweise dieselbe Form mit `u8x32` die bahnweise Multiplikation
im endlichen Körper mit Polynom `0x11D`. Der Faktor gilt für alle Bahnen
und wird einmal ausgewertet. Beispiel: ein Byte 128 mal Faktor 2 ergibt 29.

Die bisherige Form `simd_gf256_mal(a, b)` für zwei gleiche Bytevektoren
bleibt unverändert. Sie erlaubt verschiedene Faktoren je Bahn.
Es gibt keine implizite Umwandlung eines skalaren Operanden in einen Vektor.
Faktoren anderer Ganzzahltypen müssen ausdrücklich nach `u8` konvertiert werden.
Eigene gleichnamige Funktionen behalten Vorrang.

Der Compiler nutzt bei GFNI eine aus dem Faktor abgeleitete GF(2)-Affine;
SSSE3 und AVX2 verwenden zwei Nibble-Shuffles und XOR. Beide Verfahren
haben denselben mathematischen Vertrag wie die skalaren acht Bitrunden.
`@simd("gfni128")` zerlegt 32 Bahnen in zwei XMM-Hälften.
`@simd("gfni")` erlaubt auch die 256-Bit-Variante. Die vorhandenen
CPU-/OS-Prüfungen, Rückfallvarianten und Zustandssicherungen bleiben aktiv.

Die konstanten Tabellen werden mit dem Faktor indiziert. Dieser Baustein
verspricht deshalb keine Konstantzeit bezüglich eines geheimen Faktors.
Für beliebige geheime Faktoren den Vertrag der gewählten Kryptooperation prüfen.

Regression: `mise run -C skripte/p11/array-q faktor`.
Sie prüft alle 65.536 Byte-/Faktorpaare pro Bahn und Profilobergrenze,
negative Typ-/Profilfälle, Bare-/UEFI-IR und die erzeugten ISA-Grenzen.
