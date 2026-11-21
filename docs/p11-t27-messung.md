# P11-T27: Leistungslücke zu ISA-L

Stand: 21.11.2026. Eigene Produktbatches: 35509/35510, 35512–35517,
35522/35524/35526 und 35533–35535. Das Ziel für P/Q-Erzeugung,
Zwei-Daten-Rettung und CRC32C ist in den unten genannten Fünfer-Medianen
auf Host und N100 erfüllt. Die historische Ausgangsmessung steht in
[p11-t26-messung.md](p11-t26-messung.md).

## Ergebnis und Verfahren

Durchsatz in dezimalen MB/s; jeweils fünf Läufe, Median, keine Auswahl des
schnellsten Einzellaufs. Host: Ryzen 5950X, CPU 28. N100: vorhandene
Windows-/WSL-Testumgebung, CPU 2. Beide Vergleichsarme laufen im selben äußeren
[CPU-Bereich](native-cpu-bereich.md); dessen einmalige Einrichtung liegt wie
ISA-Ls Tabellenaufbau außerhalb der Zeitmessungen. Öffentliche Moo-Prüfungen
bleiben bei jedem Aufruf aktiv. ISA-L-raw erhält gültige Pointer und Tabellen.

| Arbeit | Host Moo | Host ISA-L | N100 Moo | N100 ISA-L |
|---|---:|---:|---:|---:|
| P/Q erzeugen | 48959.528 | 33835.107 | 21725.716 | 18826.910 |
| Zwei Daten retten | 42690.616 | 32844.076 | 23650.966 | 20972.588 |
| CRC32C | 33986.697 | 17171.344 | 22367.342 | 18051.647 |
| Vollständiger MooFS-Scrub | 2242.159 | 1972.504 | 709.067 | 729.332 |

Für die drei Zielarbeiten entspricht das auf dem Host ungefähr
1.45×/1.30×/1.98× und auf dem N100 1.15×/1.13×/1.24× ISA-L.
Der vollständige N100-Scrub bleibt langsamer. Dessen Vergleichsarm ist derselbe
MooFS-Prüfer mit ISA-L-CRC-Backend, kein eigenständiges C-Dateisystem.

P/Q und Rettung: 14 × 4096 Byte Eingaben, 4093 Runden, 57344 logische Datenbytes
je Runde. CRC: 65521 Runden mit 4096 Eingabebytes. Vollscrub: 127 Runden,
679936 tatsächlich geprüfte Blockbytes in einem 2-MiB-Abbild.
Ergebnisbytes werden gegen unabhängige Referenzen geprüft; 49 Erzeugungs-
und 49 Rettungs-Aliasfälle bewahren zusätzlich die bisherige Schreibfolge.
WSL-/Lastschwankungen bleiben in den fünf einzelnen Zeiten im JSON sichtbar.
Ohne Hardwarezähler wird kein prozentualer Kostenanteil einer einzelnen
Instruktion behauptet.

Reproduktion:

```sh
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
mise run p4-build-compiler
mise run -C skripte/p11/array-q cpu-bereich
mise run -C skripte/p11/array-q vergleich
mise run -C skripte/p11/array-q vergleich-n100
```

Der letzte Task überträgt das identische Messprogramm, die gepinnte
ISA-L-Bibliothek und die vorhandenen N100-Orakel. Host-/N100-Ergebnisse
liegen unter `build/p11/array-q/t26/*-t27-anfang.*`.
ISA-L 2.32.1 ist ausschließlich Referenz; Produktcode wird nicht dadurch ersetzt.

## Ursachen und Produktänderungen

Die verworfene T26-Fusion zeigte in `build/p11/array-q/t26/moo-fusion1.s`
bei 17344ff einen 200-Byte-Rahmen und bei 17555–17695 verschachtelte
Bytekopier-/Aufbauschleifen statt direkter Vektorzugriffe. Der neue Pfad
verwendet vorhandene native Array-/Pufferlayouts und echte geprüfte
SIMD-Loads/-Stores. Eine einmal geprüfte 128-Byte-Teilansicht mit festen
Offsets 0/32/64/96 lässt LLVM redundante Einzelguards falten.

Neu bzw. erweitert:

- Native feste Arrays geliehener Ansichten, typgerechte Aggregate-Nullinitialisierung
  bei Deklaration und `pufferansicht(view[,start,anzahl])`. Die Fluchtprüfung
  verfolgt auch Herkunft aus Arrayliteralen; Elementtypen und echte Grenzen
  bleiben erhalten. Siehe [native-simd-puffer.md](native-simd-puffer.md).
- `array_q_summen_fusion` verarbeitet P und Q im selben Quellpass;
  `array_q_zwei_fusion` berechnet beide fehlenden Daten direkt aus P/Q und
  gesunden Quellen. Koeffizienten und GF-Inversen sind mathematische Tabellen.
- Die disjunkte Erzeugung beginnt im Register statt mit separatem Nullpass
  und Wiederladen. Die erste Quelle dient als Anfangsakkumulator; Faktor 1
  ist die Identität. Vorhandene Recovery-Deltas werden weiter geladen.
  Der Aliasguard steht vor Ausgabeschreibzugriffen; überlappende Fälle behalten
  den früheren Pfad. Es gibt keine Sonderbehandlung der Benchmarkdimensionen.
- CRC verwendet vier unabhängige GPR-Ketten und generische Potenz-zwei-Kacheln
  1024/256/64/16/8 Byte mit mathematischer Faltung; serieller Rest unter 32 Byte.
  Standardpolynom, Seed-/XOR-Vertrag und Bereichsfehler bleiben erhalten.
- `@cpu_bereich` bindet unter Linux den aktuellen Thread, ermittelt erst
  danach CPU-/OS-Freigaben und hält sie nur für diese Lebensdauer in TLS.
  Bestehender nativer Cleanup stellt Freigabe und Affinität wieder her.
  Verschachtelung, andere Threads und fehlgeschlagene Bindung sind geprüft.
  Bare/UEFI/Windows behalten den bisherigen Guard-/XSAVE-Pfad.
  Kein prozessweiter Cache einer zuerst beobachteten CPU.

Codebelege aus code_intel (functions/file): `stdlib/array_q.moos:159`
(Summenfusion), `:282` (direkte Rettung),
`compiler/src/native_cpu_scope.rs:19/48` (Marker/Plattformvertrag),
`native_cleanup.rs:79` (TLS), `native_system.rs:2308` (Cleanup-Frame),
`native_simd_dispatch.rs:116` (Auswahl) und `native_crc32c.rs:32` (CRC-Backend).

Die ausgewählte GFNI256-ASM enthält direkte YMM-Loads, XOR und
`vgf2p8affineqb`; das vollständige ASM ist archiviert. Diese GF-Tabellen
haben kein Konstantzeitversprechen für geheime Faktoren.
Die bestehende Pure-Einzeiler→Systemcallee-Grenze ist separat als
Sprachproposal 8ae6ddc5 dokumentiert.

## Regression und Standbindung

| Prüfung | Ergebnis | Synapse-Job |
|---|---|---|
| CPU-Bereich | 4 TLS-Threads, Verschachtelung, CPUID-Zähler, Fallback, Affinität/Cleanup, Hosted-Fangpfad, 3 Negativfälle und Target-IR | 091f2de7, N100 ed2d3f5c |
| CRC | jeweils 9222 Fälle Auto/Fallback, Seeds/Splits, Guardpages, Bounds, Shadowing, 8 Negative, 4 Targets | e0b0ae35 |
| SIMD-Ansichten | Profile/Offsets/Guardpages/Alias/Bounds, Typen, Borrow, Cleanup, Target-IR | 8250b6ee |
| P/Q | 54 Seitenfälle, 4239 Ausfälle, 98 Aliasfälle | e40ee90a |
| Core | 137 PASS, 0 FAIL, 9 deklarierte SKIP | 3a8b8205 |
| UEFI/Kernel | vorhandener Headless-Smoke mit 4 CPUs und P-Array-Probe grün | 3a8b8205 |
| Legacy-IR streng | 576/576 IR, 0 Fehler, IDENTICAL, ohne Baseline-Ausnahmen | b1e3f5de |

Die gemeinsame 574er-Baseline blieb unverändert. Temporäre Erwartung
35540: 556 bestehende Fingerprints unverändert, 18 betroffene
Array-/MooFS-/SIMD-Importfälle aktualisiert, zwei bereits abgenommene
T16/T18-CLI-Einträge ergänzt; nur fünf Snapshot-Metadatenfelder übernommen.
Die Erwartungsdatei wird nach dem Gegenlauf entfernt, ihre Synapse-Version
bleibt nachvollziehbar. Kein Legacy-Quellbeispiel wurde dafür geändert.

Messcompiler SHA-256:
`7fe795d9b1af82623da494201ba204e4b86ed9ba7307a3bf647f8d8f09d27277`.
Default-Compiler der Core-/Kernel-/IR-Runde:
`6bba1d5f34c350a7c2784ae6ab4f22cf1892cfbaef3ab4f42cfeb61042a789a9`.
Dessen neu erzeugtes Messobjekt ist bytegleich mit dem gemessenen Objekt
(`cmp`, Job b0c598ac; SHA-256
`1b653b23633ac208ee71ca4523127e44716d949b725ce7139003b503bd211388`).

Weitere SHA-256:

- Host-JSON: `245aecc9091b7dc429664d26776be92114cb9d1dae77ce71e0214e62a9c5d1db`
- N100-Protokoll: `a20dd7a679bd6d508d8e677162f81c05bf5b1684ff95afca965342cdbc763b3c`
- ASM: `69387b3dc8e979b14b989a8c839b88941aead73fe4b2a1ed256a19b5f029190f`
- Messprogramm: `22b46a9e08e1ef27765818a7db8010c91906a99f13a1ae075ddcc6aaa622f0fa`
- ISA-L: `0e95c577003b384e9471a135c02354fab10f19a4259b4faa5ee002e8ae50ddec`
- Strenges IR-Ergebnis: `238ece2985bdca816e940c4da015cc8498f043fd38645a9daad4742d8fe6a0c3`
- KERN.ELF: `1788cc7a3ec3aec2db572bb159114d1e452b90b455138f742b8dda86819d70e1`
- Kernel-Serial: `e851953270b344ca3e66f49019793678016c572d52ba9d040ac7ce5b22b30c71`

Publikation erfolgt im koordinierten Meilenstein; diese Task führt keinen
eigenständigen Push aus.
