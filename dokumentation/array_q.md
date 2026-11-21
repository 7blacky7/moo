# P/Q-Parität in Moo

Die Seitenrechnung liegt in `stdlib/array_gf256.moos` und
`stdlib/array_q.moos`, der P/Q-v3-Basissuperblock in
`stdlib/array_pq_format.moos`. Der physische Schreib-/Rebuildträger
verwendet diese Bausteine; sein eigener persistenter Intent gehört zu
`array_pq_rebuild*`.

## Körper und stabile Gewichte

Alle Byteprodukte verwenden GF(2^8) mit dem Polynom
x^8 + x^4 + x^3 + x^2 + 1, also 0x11D. Addition ist XOR.
Das Gewicht des Datenmitglieds an Slot s ist c(s) = 2^s.
Der Slot gehört zum Layout und bleibt beim Ersatz einer Platte erhalten.
Die Reihenfolge der übergebenen Quellenliste beeinflusst die Parität nicht.

Für jedes Byte einer gemeinsamen 4096-Byte-Seite gilt:

- P = XOR aller Datenbytes.
- Q = XOR aller Produkte c(s) × Datenbyte(s).
- Bei Änderung eines Datenbytes: Q_neu = Q_alt XOR c(s) × (alt XOR neu).

Eine kleinere Datenplatte trägt hinter ihrem Ende null bei. Der
Transportträger lässt ihre Quelle für diese Seite weg. Eine Ausfallmaske
bezieht sich auf die verbleibenden tatsächlichen Quellenslots.

`array_gf256_mal` rechnet skalar mit acht Reduktionsschritten.
`ArrayGf256` enthält Logarithmen und eine verdoppelte Exponententabelle;
`array_gf256_starten` initialisiert sie exklusiv. Die Tabelle wird
anschließend nur gelesen und darf von getrennten Arbeitskontexten
gemeinsam geliehen werden. `array_gf256_invers(0)` liefert 0;
die geprüften Rekonstruktionsaufrufe verwenden nur nachweislich
von null verschiedene Nenner.

## Geprüfte Seiten-API

`ArrayQQuelle { slot: u64, bytes: puffer[u8] }` leiht eine Datenseite.
`ArrayQArbeitsraum` enthält zwei 4096-Byte-Arrays für die Residuen.
Die API übernimmt keinen Datenbesitz und reserviert keinen Heap.
Alle beteiligten Bereiche einschließlich Arbeitsraum sind gültig,
disjunkt und bis zur Rückkehr geliehen.

| Aufruf | Wirkung |
|---|---|
| `array_q_beitrag(q, daten, slot, modus, t)` | Q XOR Gewicht × Quelle |
| `array_q_aktualisieren(q, alt, neue_bytes, slot, modus, t)` | Inkrementelles Q |
| `array_q_erzeugen(p, q, daten, modus, t)` | Beide Paritäten aus der Quellenliste |
| `array_q_retten(p, q, daten, fehlt, fehlt_p, fehlt_q, modus, t, w)` | Bis zu zwei bekannte Ausfälle rekonstruieren |

Alle liefern `ergebnis[void, BlockFehler]`. Die Byteansichten müssen
gleich lang und höchstens 4096 Bytes groß sein; Länge null ist erlaubt.
Eine Quellenliste enthält 1 bis 16 eindeutige Slots aus 0 bis 15.
Das physische Layout erlaubt maximal 14 Datenplatten plus P und Q.
`fehlt` ist eine Bitmaske der stabilen Datenslots, keine Maske der
Listenpositionen. P und Q haben eigene boolesche Ausfallkennzeichen.
Bei einem Ausfall ist die jeweilige Ansicht ein Ausgabebereich.
Gesunde Quellen und gesunde Parität bleiben erhalten.

Modus 0 verwendet skalare Multiplikation, Modus 1 die initialisierte
Tabelle t und Modus 2 `@simd`. In Modus 0/2 darf t null sein.
Die SIMD-Variante prüft CPU und Betriebssystemzustand und wählt einen
sicheren Rückfallpfad. `simd_gf256_mal` führt auch mit GFNI denselben
0x11D-Körper aus: Der bestehende Compiler wechselt die Basis zum
GFNI-Körper 0x11B und danach zurück.

Argument-, Größen-, Slot-, Doppelbelegungs- und Ausfallzahlfehler werden
vor Ausgabeschreibzugriffen gemeldet. Mehr als zwei bekannte Ausfälle
oder Maskenbits ohne passende Quelle sind ein Fehler. E/A, Sperren und
die dauerhafte Veröffentlichung gehören zum Transportträger, nicht
zur Seitenrechnung. Die Hilfsfunktionen `array_q_xor`,
`array_q_kopieren` und `array_q_leeren` sind interne Bausteine nach
erfolgter Prüfung.

Für zwei fehlende Datenquellen A/B werden zunächst die gesunden Beiträge
von P und Q abgezogen (XOR). Anschließend:

`A = (Q_res XOR c(B) × P_res) × invers(c(A) XOR c(B))`

`B = P_res XOR A`

Fehlen Daten und P, genügt Q_res / c(A). Fehlen Daten und Q, genügt
P_res. Fehlen beide Paritäten, werden sie aus allen Daten neu erzeugt.
Diese Fälle setzen bekannte ausgefallene Mitglieder voraus; ein
unlokalisierter stiller Datenfehler wird damit nicht automatisch erkannt.

## P/Q-v3 auf dem Medium

`ArrayLayout` und die Identitätsfelder des P-Formats werden wiederverwendet.
Die neue Rolle Q hat den Wert 3. Genau ein P-Mitglied, genau ein Q-Mitglied
und mindestens ein Datenmitglied sind erforderlich. P und Q umfassen
jeweils mindestens so viele 4-KiB-Seiten wie die größte Datenplatte.

Die Basissuperblöcke liegen wie zuvor auf Metadatenseite 0 und 1.
Familienmagie, gepackter Header, 64-Byte-Mitgliedseinträge und CRC32C
entsprechen dem P-Format; das Versionsfeld enthält ausdrücklich 3.
P-v1 und P-Rebuild-v2 werden dadurch weder umgeschrieben noch implizit
als P/Q interpretiert. Die v3-Decoder weisen Version 1/2 und unbekannte
Versionen ab. Metadatenseiten 2 bis 9 sind für zwei Intentbanken mit
Kopf und D/P/Q-Schreibbildern reserviert.

`array_pq_layout_pruefen` liefert
`ArrayPQGrenzen { paritaet_p, paritaet_q, daten_max }`, jeweils u64.
`array_pq_superblock_bauen(bytes, layout, eigenes)` kodiert ein Mitglied.
`array_pq_superblock_lesen(bytes, layout)` liefert dessen Slot.
Der Decoder prüft CRC vor Interpretation und veröffentlicht das
Ausgabelayout erst nach vollständiger Prüfung. Headerreserve,
Serienpadding und ungenutzte Restbytes müssen null sein.
Identitäten sind über vollständige Seriennummern (1 bis 32 Bytes)
und GUIDs eindeutig. Unterschiede im letzten Serienbyte bleiben erhalten.

## Nutzung der Sprachmittel

Native `puffer[T]`-Felder leihen einen Deskriptor aus Zeiger und Länge.
Strukturkopien kopieren diesen Deskriptor und teilen dieselben Daten,
ohne Besitzer- oder Datenkopie. `pufferansicht(array)` beziehungsweise
`pufferansicht(array, start, anzahl)` leiht vorhandenen Array-Speicher
mit passendem Zahlen- oder nominalem Strukturelementtyp. Der englische
Alias ist `array_view`. Start und Anzahl werden vor dem Zugriff geprüft;
ein leerer Ausschnitt am Arrayende ist gültig. Eine Konstante wird nicht
als schreibbare Ansicht freigegeben. Die bestehende Leihprüfung verfolgt
lokale Quellen auch durch Strukturkopien und weist deren Flucht ab.

Eine reine Pointerweitergabe braucht auch in einem sicheren Bare-Körper
kein `unsicher`. Dereferenzierungen, rohe Speicherbereiche und die
bestehenden Hardwarezugriffe behalten ihre explizite Grenze.
Dynamisches Boxing geliehener Strukturfelder und C-Exporte by value
bleiben diagnostiziert.

## Gezielte Nachweise

`mise run -C skripte/p11/array-q gf` vergleicht alle 65536 Bytepaare
auf Skalar-, Tabellen- und SIMD-Pfad mit unabhängiger Python-
Polynommultiplikation und -division.

`matrix` prüft 54 Seitenfälle: 4 oder 14 Datenquellen, drei Pfade und
Längen 0, 1, 15, 16, 31, 32, 33, 97, 4096. Alle 4239 Einzel- und
Zweier-Ausfallkombinationen müssen Originaldaten und Parität bytegleich
wiederherstellen. Python prüft neu erzeugtes und inkrementelles P/Q;
zusätzliche Argumentfehler dürfen keine Paritätsausgabe verändern.

`format` prüft alle fünf Mitgliedsslots und 27 negative CRC-/resealte
Formatfälle einschließlich Versionsgrenzen, Rollen, Kapazitäten,
Identitäten, Reserven und Fehleratomizität.

`bench` protokolliert fünf Messungen pro Pfad mit CPU-Affinität,
nichttrivialer Prüfsumme und ASM-Belegen für GFNI128/256.
Der Durchsatz zählt verarbeitete Eingabebytes, nicht sämtliche
Speichertransfers. `MOO_Q_BENCH_CPUS` wählt eine Liste von CPU-Nummern.
`winrm` verwendet den bestehenden SHA-geprüften SMB-Transfer und WinRM
für den N100-Test: 65536 Bytepaare je GFNI128/256, dieselbe Moo-
Ausfallmatrix gegen ein unabhängiges C++-Orakel und drei Messungen pro Pfad
auf CPU 2. Die C++-Datei ist ausschließlich Testglue und Orakel.

`mise run -C skripte/p11/sprache buffer-feld` prüft die neuen Sprachformen
auf Host, Bare und UEFI-IR sowie 20 negative Zielproben und vier tatsächlich
ausgelöste Bereichsfehler. Breite Legacy-/Core-/Kernel-Prüfungen erfolgen
am gemeinsamen Meilenstein.


## MooFS-Scrub und Topologieverwaltung

`array_pq_scrub_seite(r, seite, reparieren, w, a)` liefert die physische
Slotmaske der fehlerhaften Seiten. `ArrayPQScrubArbeit` hält eine zusätzliche
Originalzeile; der Besitzer stellt beide Arbeitsräume disjunkt und dauerhaft
bereit. Alle Mitglieder müssen aktiv und lesbar sein, ohne offenen Intent oder
laufenden Ersatz. Bereits degradierte Arrays verwenden zuerst den Rebuildpfad.

Der Scrub liest eine unveränderliche Zeile, prüft MooFS-CRC, Header und
physische Blocknummer aller Datenseiten und beide Paritätsgleichungen.
Vollständig genullte Seiten sind zulässig. Alte CoW-Seiten müssen wie belegte
Seiten eine gültige eigene Prüfsumme besitzen. Beliebige Fremdformate oder
nicht initialisierter freier Raum liefern keine Grundlage zur Reparatur.

Nach einem Widerspruch werden alle Einzel- und Zweierfehlerbilder
rekonstruiert. Ein Kandidat muss genau die benannten Seiten ändern,
alle Datenprüfsummen erfüllen und zu P und Q passen. Nur ein global
eindeutiger Kandidat wird angenommen; eine kleinere Fehleranzahl
ist kein Beweis. Beispielsweise können zwei vertauschte CRC-gültige
Superseiten wie ein Q-Einzelfehler aussehen. Eine intern gültig resealte
Datenseite kann mit einem Fehler beider Paritäten konkurrieren.
Solche Fälle bleiben ohne externe vertrauenswürdige Referenz unverändert.
CRC ist Fehlererkennung, keine Authentifizierung gegen absichtlich
konstruierte Prüfsummenkollisionen.

Im Modus `reparieren=falsch` bleibt das gesamte Medium unverändert.
Bei Mehrdeutigkeit oder mehr als zwei nicht zuordenbaren Fehlern folgt
`BLOCK_EA/310` ohne Reparatur. Erkannte Seiten werden im Schreibmodus
gezielt mit den bereits geprüften Bildern durch das vorhandene replizierte
Journal repariert. Daten kommen vor Parität; ein Neustart spielt einen
angefangenen Intent über den normalen Öffnungspfad ab.
Der vollständige referenzierte MooFS-Scrub aus `ds_moofs_pruefen`
bleibt zusätzlich für Baum-, Eigentümer-, Extent- und Namensprüfung zuständig.

`array_pq_scrub_starten` und `array_pq_scrub_schritt` führen maximal eine
Zeile je Schritt aus. `ArrayPQScrub` zählt geprüfte Zeilen, gefundene
Fehlerseiten und Reparaturen je Träger. Ändert normale I/O zwischen
Schritten den Journalstand, fordert der nächste Schritt mit
`BLOCK_NICHT_BEREIT/315` einen Neustart des Scrubs. So wird kein Ergebnis
über unterschiedliche Schreibstände hinweg als konsistent gemeldet.

### Aufnahme und Paritätswechsel

`array_pq_daten_hinzufuegen(r, quelle, w)` hängt eine Datenplatte am Ende
der Slotliste an; vorhandene Slots und GF-Koeffizienten bleiben stabil.
Die gesamte Datenkapazität des neuen Trägers wird genullt und per
Barriere bestätigt. Falls das Datenmaximum wächst, werden nur die bisher
ungenutzten Paritätsschwänze genullt und bestätigt. Beide Paritätsmedien
müssen vor dem ersten Schreibzugriff groß genug sein.

`array_pq_paritaet_tauschen(r, slot, quelle, w)` nimmt einen mindestens
gleich großen neuen P- oder Q-Träger auf. Die verwendete Parität wird
zuerst kopiert und der übrige neue Bereich genullt; erst nach bestätigter
Barriere wird die Identität gewechselt. Der alte Träger bleibt während
der Vorbereitung aktiv und unverändert. Beide Operationen erfordern ein
gesundes Array, eine neue eindeutige Identität, getrennte physische
Daten-/Metadatenbereiche und exklusiven Zugriff während des gesamten Aufrufs.

Die existierenden v3-Journalbanken veröffentlichen das neue Layout mit
steigender Generation und Folge. Basisseiten des neuen Mediums allein
aktivieren es nicht. Der Loader wählt den maßgeblichen Journalstand
und bindet dessen Identitäten, auch wenn alte und vorbereitete neue
Parität zugleich angeschlossen sind. Zulässige Geometrieübergänge sind
ausschließlich angehängte Daten-Slots und wachsende Paritätskapazität;
vorhandene Datenrollen und Datenkapazitäten bleiben fest.

Die Bibliothek leiht Transporte; sie kann deren tatsächliche physische
Überlappung nicht aus Seriennummern ableiten. Ihr Besitzer garantiert
unterschiedliche Medien und gültige Lebensdauer. Im Kern prüfen
`kern_array_pq_hinzufuegen` und `kern_array_pq_paritaet_tauschen`
zusätzlich die physischen und virtuellen Gerätekennungen und halten
dieselbe Mutex wie normale Block-I/O. Temporäre Transporte liegen
getrennt von aktiven Transporten. Virtuelle IDs vorhandener Datenplatten
bleiben stabil. Ein aufgenommenes Mitglied erhält eine neue normale
Blockkennung; nach einem vollen Blockregister lässt sich die Registrierung
mit `kern_array_pq_nachregistrieren` wiederholen.

### Gezielte Abnahme

`mise run -C skripte/p11/array scrub` nutzt denselben nativen Prüflauf
für Host und Bare-IR. Echte MooFS-Formatterseiten durchlaufen alle
Einzel-/Paarfehler in drei GF-Pfaden und beiden Sektorgrößen 512/4096.
Zusätzlich geprüft werden gültig resealte Daten, widersprüchliche
Dreifachfehler, vollständiger MooFS-Baumlauf, Nullaufnahme,
Paritätsvergrößerung und erneutes Öffnen mit vertauschter Quellreihenfolge.

Das Präfix-Ausfallmodell bricht vor jeder Schreib- und Barriereoperation
in einer D+P-Reparatur, Datenaufnahme sowie P- und Q-Wechsel ab und
verweigert danach sämtliche I/O bis zum Neustart. Anschließend müssen
Journal-Replay, Daten und Paritätsgleichungen wieder stimmen.
Dieses Modell prüft Operationspräfixe; echte Stromabschaltung,
geräteinterne Schreibumordnung und beliebige zerrissene Sektoren sind
damit nicht nachgewiesen.

Die Kernelprobe `kern_array_scrub_probe.moos` verwendet normale
registrierte RAM-Blockgeräte, den MooFS-Formatter und die
Verwaltungs-APIs unter KernMutex. Ihr Erfolgssignal lautet
`MOO-ARRAY SCRUB PRUEFUNG 0000000000000001`.
