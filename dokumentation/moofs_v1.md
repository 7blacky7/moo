# MooFS v1 – Datenträgerformat und CoW-Vertrag

Status: lesender Kern P11-T16, schreibender CoW-Kern P11-T17 und Vollprüfung P11-T18.
Diese Spezifikation definiert Format und Veröffentlichungsprotokoll. Ein MooFS-Volume ist eine
eigenständig lesbare Blockfolge, kein von einem Array abhängiges Streifenformat.

## Einheiten und Byteordnung

Ein Dateisystemblock umfasst immer 4096 Bytes. Blocknummern und Inodes sind
vorzeichenlose 64-Bit-Zahlen. Alle Mehrbytefelder auf Platte sind Little Endian,
alle Strukturen gepackt. Transportsektoren dürfen 512, 1024, 2048 oder 4096 Bytes
umfassen; Dateisystemblock n beginnt bei Transport-LBA n × 4096 / Sektorgröße.
Der vorhandene synchrone Vertrag `FatTransport` wird wiederverwendet; sein
historischer Name bedeutet keine FAT-Abhängigkeit des Datenträgerformats.

Der Besitzer serialisiert Volumezugriffe und hält Transport/Puffer gültig.
Die lesende Implementierung schreibt beim Einhängen nichts auf das Medium.

## Gemeinsamer Blockkopf

Jeder belegte Block beginnt mit diesem 64-Byte-Kopf:

| Offset | Bytes | Feld |
|---:|---:|---|
| 0 | 8 | Magie 0x003153464F4F4D, Bytes „MOOFS1\\0\\0“ |
| 8 | 4 | Art: 1 Superblock, 2 Blatt, 3 innerer Knoten, 4 Dateidaten |
| 12 | 4 | Ebene, Blatt/Daten/Superblock = 0 |
| 16 | 8 | Eigene Dateisystemblocknummer |
| 24 | 8 | Generation, mindestens 1 |
| 32 | 4 | Anzahl der Einträge beziehungsweise gültigen Datenbytes |
| 36 | 4 | Flags, v1 = 0 |
| 40 | 4 | CRC32C des gesamten Blocks, dieses Feld als vier Nullbytes |
| 44 | 4 | Reserviert = 0 |
| 48 | 8 | Eigentümer-Inode bei Dateidaten, sonst 0 |
| 56 | 8 | Logischer Datenblockindex bei Dateidaten, sonst 0 |

CRC32C verwendet das reflektierte Castagnoli-Polynom 0x82F63B78, Startzustand
0xFFFFFFFF und abschließendes XOR 0xFFFFFFFF. „123456789“ ergibt 0xE3069283.
Das Prüfverfahren ist eine Fehlererkennung, keine kryptografische Authentisierung.

Unbelegte Bytes im verwendeten Block sind null. Prüfsumme und eigene Identität
werden vor der Interpretation von Längen, Zeigern oder Daten geprüft.
Eine referenzierte Blockgeneration darf die gewählte Superblockgeneration nicht
übersteigen. Blatt- und innere Knoten verwenden ausschließlich positive Generationen.

## Superblock-Paar

Die Kopien liegen in Block 0 und 1. Der Kopf hat Art 1, Ebene/Anzahl = 0.
Nach dem Kopf:

| Offset | Bytes | Feld |
|---:|---:|---|
| 64 | 4 | Formatversion = 1 |
| 68 | 4 | Merkmale = 0, unbekannte Merkmale sind ein Fehler |
| 72 | 8 | Anzahl Dateisystemblöcke des Volumes |
| 80 | 8 | Wurzelblock des gemeinsamen B+-Baums |
| 88 | 4 | Erwartete CRC32C dieses Wurzelblocks |
| 92 | 4 | Wurzelebene, 0–8 |
| 96 | 8 | Bisher höchstes belegtes Blockende, exklusiv |
| 104 | 8 | Volume-Kennung, untere Hälfte |
| 112 | 8 | Volume-Kennung, obere Hälfte |
| 120 | 3976 | Null |

Die Kennung darf nicht vollständig null sein. Wurzel und alle referenzierten
belegten Blöcke liegen zwischen 2 einschließlich und dem Belegungsende
ausschließlich. Das Belegungsende darf die Volumegröße nicht überschreiten.
Die Transportkapazität muss mindestens die deklarierte Volumegröße umfassen.
Freie Blöcke unterhalb des Belegungsendes dürfen wiederverwendet werden, sobald
keine gültige Superblockwurzel sie mehr referenziert; das Ende ist kein Freizähler.

Beide Kopien werden unabhängig geprüft. Unter gültigen Kopien gewinnt die
höhere Generation. Gleich alte Kopien müssen Wurzel, CRC, Höhe, Volumegröße und
Belegungsende gemeinsam haben. Unterschiedliche Volume-Kennungen sind immer
ein Formatfehler. Ist genau eine Kopie ungültig, wird das Volume als degradiert
lesbar. Sind beide ungültig, wird der konkrete Fehler der ersten Kopie gemeldet.
Eine gültig veröffentlichte Generation mit beschädigtem Wurzelblock wird
abgelehnt; es erfolgt kein stiller Rückfall auf ältere Dateiinhalte.

mkfs schreibt zunächst Daten und Baumknoten, führt Flush aus und veröffentlicht
danach Kopie 0 und Kopie 1 jeweils mit Flush. Eine Neuformatierung ist kein
stromausfallsicherer Ersatz eines bestehenden Volumes. T17 definiert das
CoW-Protokoll für Änderungen eines bereits gültigen Volumes.

## Gemeinsamer B+-Baum

Schlüssel sind Tripel aus objekt, art, index, jeweils u64. Sie werden in dieser
Reihenfolge numerisch und streng aufsteigend verglichen. objekt ist die Inode
oder die Eltern-Inode. art 1 bezeichnet Inodes, 2 Extents, 3 Verzeichniseinträge.
Andere Arten sind in v1 ungültig.

Ein Blatt (Kopfart 2, Ebene 0) enthält 1–12 Records zu je 320 Bytes ab Offset 64:

| Recordoffset | Bytes | Inhalt |
|---:|---:|---|
| 0 | 24 | Schlüssel objekt/art/index |
| 24 | 8 | a |
| 32 | 8 | b |
| 40 | 8 | c |
| 48 | 8 | d |
| 56 | 256 | Name beziehungsweise Nullbytes |
| 312 | 8 | Reserviert = 0 |

Ein innerer Knoten (Kopfart 3, Ebene mindestens 1) enthält 1–100 Einträge zu
je 40 Bytes: Schlüsseltripel (24), Kindblock (8), erwartete Kind-CRC32C (4),
reserviert null (4). Der Schlüssel ist das exakte größte Schlüsseltripel des
Kindunterbaums. Kindebenen müssen um genau eins abnehmen. Jede Kindspanne
beginnt oberhalb der vorherigen oberen Schranke und endet an ihrer eigenen.
Die maximale Ebene 8 begrenzt auch die Laufzeit bei fehlerhaften Zyklen.
Es gibt keine ungeschützten Blattverkettungen.

### Inodes

Schlüssel (inode, 1, 0), a = Dateilänge, b = Art (1 Datei, 2 Verzeichnis),
c = Linkanzahl 1, d = 0, Name null. Verzeichnislänge ist 0.
Wurzel-Inode ist immer 1 und muss ein Verzeichnis sein.
v1 besitzt weder Hardlinks noch Symlinks, Zeitstempel oder ACLs.

### Extents

Schlüssel (inode, 2, erster logischer Datenblock), a = erster physischer
Datenblock, b = Anzahl zusammenhängender Datenblöcke, c/d = 0, Name null.
Die Länge ist positiv; logischer und physischer Bereich müssen ohne Überlauf
darstellbar sein. Leere Dateien haben keine Extents. Die belegten Dateibytes
haben keine Löcher. Die erste Implementierung von mkfs erzeugt einen Extent
je nichtleerer Datei; der Leser kann mehrere Extents auswerten.

### Verzeichnisse

Schlüssel (eltern-inode, 3, Eintragsindex), a = Kind-Inode, b = Namenslänge,
c = Kindart, d = 0. Namenslänge 1–255; die übrigen Namensbytes sind null.
Namen sind unveränderte Bytes, ohne NUL oder Schrägstrich. „.“ und „..“ sind
keine gespeicherten Einträge. mkfs verbietet gleiche Namen unter demselben
Elternteil sowie Zyklen. Kind-Inode und dessen Art werden beim Lesen geprüft.
Aufrufende können Namen als UTF-8 interpretieren; es erfolgt keine Faltung.

Der Pfadleser beginnt an Inode 1, erlaubt führende/wiederholte Schrägstriche,
hat eine Grenze von 4095 Pfadbytes und lehnt NUL, „.“ sowie „..“ ab.
Lexikalische Normalisierung übernimmt bei einer VFS-Anbindung der vorhandene VFS.

## Datenblöcke und Fehler

Dateidaten beginnen bei Offset 64, somit trägt ein Block höchstens 4032 Bytes.
Anzahl nennt die gültigen Bytes; alle Blöcke außer dem letzten sind voll.
Eigentümer und logischer Index müssen zum angeforderten Inode/Offset passen.
Der Rest eines letzten Blocks ist null und ebenfalls durch CRC32C geschützt.

Der Leser kopiert erst nach vollständiger Blockprüfung. Bei einem späteren
Fehler eines Mehrblock-Reads kann bereits ein geprüfter, korrekter Präfix im
Ausgabepuffer stehen; der Aufruf liefert dann ausschließlich einen Fehler,
keine erfolgreiche Gesamtbytezahl. Kein Byte aus dem beschädigten Block wird
ausgegeben. EOF liefert erfolgreich null Bytes.

`MooFsFehler { code: u32, grund: u32, block: u64 }` nennt die betroffene
Dateisystemblocknummer, auch bei Transportfehlern. CRC-Fehler enthalten
MOOFS_CRC, Formfehler MOOFS_FORMAT. Reservierte und unbekannte Formate werden
abgelehnt, nicht als alternative Daten interpretiert.

## mkfs-Werkzeug

`mise -C skripte/p11/moofs run mkfs -- NEUES_BILD BYTES [QUELLE [NAME]]`
erzeugt eine neue reguläre Bilddatei. BYTES muss ein Vielfaches von 4096 und
mindestens 16384 sein. Ein optionales Quelldokument wird unter NAME
(Standard daten.bin) in der Wurzel abgelegt; ohne Quelle entsteht ein leeres
Dateisystem. Vorhandene Ziele werden mit O_EXCL abgelehnt. Ein Fehler kann
eine unvollständige neue Bilddatei hinterlassen; diese wird nicht eingehängt.
Das Linux-Werkzeug erzeugt die Volume-Kennung mit getrandom. Der Besitzer
darf eine geliehene Quelldatei während der Formatierung nicht verändern.

Der Shell-Aufruf bindet Argumente an Umgebungsvariablen und startet das
Moo-Werkzeug. Sämtliche Formatierung, Prüfsummen und Baumlogik bleiben in Moo.

## API und Prüfungen

`moofs_oeffnen` wählt das Superblockpaar und prüft die Wurzel-Inode.
`moofs_inode`, `moofs_suchen` und `moofs_verzeichnis` liefern geprüfte
Inodes beziehungsweise Einträge. Verzeichnisaufrufe nehmen einen unteren
Eintragsindex; der nächste Index ist der gelieferte Index plus eins.
MOOFS_FEHLT bedeutet Verzeichnisende. `moofs_lesen` nimmt Inode, Byteoffset
und geliehenen Zielpuffer. `moofs_formatieren` nimmt den synchronen Transport,
ein geliehenes Quellenmanifest, die Volume-Kennung und einen vom Aufrufer
bereitgestellten `MooFsFormatSpeicher`. Der Arbeitsbereich soll wegen seiner
Größe im Kernel statisch oder explizit reserviert sein.

- `mise -C skripte/p11/moofs run host`: CRC32C-Orakel, Layoutgrößen,
  512-/4096-Byte-Sektoren, mehrere Baumblätter, Namen und Extents,
  Reads über Datenblock-/Extentgrenzen, leeres Volume, Generationen,
  Bitkipper und mit gültiger CRC versehene ungültige Metadaten.
- `mise -C skripte/p11/moofs run abbild`: Moo-mkfs auf einer neuen
  POSIX-Datei, 1 MiB bytegleich, vorhandenes Ziel unverändert,
  Datenbitkipper meldet Fehlercode und Blocknummer 3.
- `mise -C skripte/p11/moofs run kern`: bestehender UEFI-Smoke mit vier
  CPUs und drei separaten Starts. Der gemeinsame Moo-Leser prüft 1 MiB
  samt CRC32C auf einem readonly NVMe-Bild; der zweite Start erkennt den
  Bitkipper in Block 3 und gibt kein Byte aus diesem Block aus.
  Der dritte Start schreibt über NVMe: Anlegen, Umbenennen, Kürzen,
  Nullvergrößerung, Löschen, erneutes Anlegen und Abgleichen. Ein neuer
  Hostprozess liest anschließend die unveränderte 1-MiB-Datei und die neue
  12289-Byte-Datei bytegleich aus der privaten QEMU-Kopie.
  SHA-256 bestätigt unveränderte Medien der Leseprofile. Standard ist KVM; für TCG
  `MOO_UEFI_KVM=0 MOO_UEFI_CPU=max` setzen.

Das opt-in `MOO_UEFI_MOOFS_PROBE=1|bitkipper|schreiben` ist ausschließlich ein
privates Testprofil. Gerätekennung und Profilflag werden in der privaten
Kernel-ELF-Kopie gesetzt, der normale Kernelstand aktiviert es nicht.
Nur `schreiben` erhält eine beschreibbare private Bildkopie.

- `mise -C skripte/p11/moofs run schreiben`: Host- und Bare-IR-Prüfung der
  Mutatoren, Baumwachstum und Recycling, Datenblockgrenzen, Lückenfüllung,
  Kürzen/Vergrößern, Rename samt Ersatz und Zyklusverbot, Füllen bis ENOSPC
  und Teilblockkürzung trotz erschöpftem Datenplatz.
- `mise -C skripte/p11/moofs run crash`: 1000 echte SIGKILL gegen eigene
  Writerprozesse, gleichmäßig vor/nach allen Schreib-/Flush-Grenzen und nach
  Bestätigung. Jeder Lauf startet mit einer privaten Kopie; ein frischer
  Moo-Prozess inventarisiert beide Wurzeln und liest die stabile Datei sowie
  die vollständig alte oder neue geänderte Datei bytegleich. Zusätzlich
  E/A-Fehler vor/nach jeder Grenze und reale Teilschreibzugriffe auf Daten,
  Metadaten und Superblöcke. Bericht: `build/p11/moofs/crash-ergebnis.json`.
  SIGKILL prüft Prozessabbruch, keinen physischen Stromausfall.

## CoW-Checkpoints und Reserve

Der Schreiber inventarisiert beide gültigen Superblockgenerationen. Vor einer
weiteren Mutation darf er die ältere Kopie auf den bereits dauerhaft bestätigten
aktuellen Stand setzen. Erst nach dem Flush werden ausschließlich von dieser
älteren Kopie referenzierte Blöcke frei. Beim nächsten Commit bleibt der alte
aktuelle Baum bis hinter die neue Superblock-Veröffentlichung unberührt.
Es gibt keine API für das Aufbewahren historischer Snapshots.

mkfs lässt mindestens einen weiteren vollständigen Metadatensatz frei.
Datenallokationen erhalten eine Metadatenreserve für den erwarteten Baum plus
einen Datenblock für das Kürzen eines angeschnittenen Endblocks. Damit ist bei vollem Datenbereich
weiterhin ein CoW-Commit für Löschen oder Kürzen möglich. Ein Writer auf einem
älteren, ohne Reserve bis zur Kapazitätsgrenze formatierten Bild kann hierfür
ENOSPC melden. Das lesende Format bleibt unverändert kompatibel.

## Schreib-API und Lebensdauer

`moofs_schreiber_oeffnen` leiht Transport und explizite Arbeitsbereiche:
Recordarray, Array der Baumkinder und Belegungsbitmap. Die Bitmap benötigt
mindestens ceil(Volumeblöcke/8) Bytes. Das Kinderarray benötigt mindestens
ceil(Recordanzahl/12) Elemente. Die Recordkapazität begrenzt die Metadaten im
Arbeitsspeicher, nicht das Datenträgerformat. Zu kleine Arbeitsbereiche liefern
MOOFS_SPEICHER; ausgeschöpfter Plattenplatz liefert MOOFS_VOLL.

Alle Aufrufe desselben Schreibers werden vom Besitzer serialisiert. Transport,
Arbeitsbereiche und Eingabepuffer bleiben gültig; sie überlappen einander nicht.
Die Arbeitsbereiche enthalten keine heimlichen Heap-Allokationen. Große Writer
gehören im Kernel in statischen oder ausdrücklich reservierten Speicher.

- `moofs_anlegen(w, eltern, name, ordner)` liefert eine neue Inodenummer.
- `moofs_schreiben(w, inode, offset, quelle)` ersetzt die angegebenen Bytes.
  Lücken bis zum neuen Offset werden mit Nullbytes materialisiert. Erfolg nennt
  die vollständige Quelllänge; bei ENOSPC bleibt die veröffentlichte Datei
  vollständig erhalten. Ein leerer Quellpuffer verändert die Datei nicht.
- `moofs_kuerzen(w, inode, bytes)` setzt die Länge; Vergrößern füllt mit null.
  Ein angeschnittener letzter Datenblock wird mit neuem Kopf/CRC geschrieben.
- `moofs_umbenennen(w, eltern, name, ziel_eltern, ziel)` verändert beide
  Verzeichniseinträge in einem Commit. Ersetzt werden Dateien beziehungsweise
  leere Verzeichnisse derselben Art. Gleicher Eintrag ist ein No-op;
  Elternzyklen werden abgewiesen.
- `moofs_loeschen` entfernt Datei oder leeres Verzeichnis samt Extents.
  Wurzel und nichtleere Verzeichnisse bleiben erhalten.
- `moofs_abgleichen` bestätigt eine Barriere und setzt beide Superkopien auf
  den aktuellen Stand. `moofs_statistik` nennt Gesamtblöcke, freie Blöcke,
  die darin enthaltene Reserve und die bestätigte Generation.
- Der bestehende Leser arbeitet auf `w.volume`. Inodenummern sind Identitäten
  vorhandener Dateien; nach Löschen darf dieselbe Nummer wieder vergeben
  werden. Ein darüberliegender Griff-/VFS-Adapter muss offene gelöschte
  Identitäten selbst schützen. Es gibt hier keine POSIX-Unlink-Offen-Garantie.

Jede erfolgreiche nichtleere Mutation ist bereits dauerhaft bestätigt:
neue Daten und vollständig neuer Metadatenbaum, Flush, neue ältere Superkopie
mit Generation + 1, Flush. Unveränderte Datenblöcke bleiben geteilt. Benachbarte
logische und physische Extents werden wieder zusammengefasst. Der Baum wird
aus dem geliehenen Recordarray neu aufgebaut; mkfs ist daran nicht beteiligt.

Scheitert das Schreiben neuer, noch unveröffentlichter Blöcke, kann das Inventar
den bisherigen Stand wiederherstellen. Ab der ersten Veröffentlichungsbarriere
bleibt der Schreiber bei unklarem E/A-Ergebnis gesperrt. Erst ein ausdrückliches
erneutes Öffnen inventarisiert den tatsächlich gültigen Stand. Eine misslungene
Mutation wird nicht automatisch erneut veröffentlicht. Die Garantie setzt einen
Transport voraus, der seine Flush-/Persistenzzusage einhält.

## VFS-Adapter

`ds_moofs_vfs` implementiert den bestehenden Vertrag `Dateisystem`.
`moofs_vfs_einrichten(fs, w)` leiht einen bereiten Schreiber für ein
beschreibbares Dateisystem; `moofs_vfs_lesend(fs, volume)` leiht nur den Leser
und weist Mutationen mit EROFS ab. Anschließend wird das Objekt als
`ptr[dyn Dateisystem]` über `vfs_einhaengen` eingehängt. Pfadnormalisierung,
Mountwahl und Datei-Offsets bleiben beim vorhandenen VFS.

Adapter, Volume, Transport und Writer-Arbeitsbereiche leben über den gesamten
Mount. Währenddessen erfolgen Mutationen ausschließlich über den Adapter.
Er verwaltet 256 generationengesicherte Griffe. Jeder Stat-Aufruf liest die
aktuelle Inodelänge, sodass mehrere Griffe dieselben Kürzungen und Schreibungen
sehen. Rename einer offenen Quelle erhält deren Inode; Löschen oder Ersetzen
eines offenen Inodes meldet EBUSY. Schließen konsumiert den Griff auch bei
einem Abgleichfehler. Es gibt keine POSIX-Unlink-Offen-Semantik.

Die vorhandene VfsFehler-Struktur transportiert MooFS-Code und Grund in
`grund/status`. Die volle u64-Blocknummer ist nur im direkten MooFsFehler
verfügbar; der Adapter verändert das bestehende VFS-ABI nicht.
Der Test `run schreiben` prüft auch den Adapter über das normale VFS.

Der 64-Bit-Kern importiert `nativ_memmove` als generischen C-ABI-Träger für
von LLVM optimierte Aggregatverschiebungen. Der Träger kopiert überlappende
Bereiche in der richtigen Richtung; volatile Bytezugriffe verhindern einen
rekursiven Libcall. Das zugehörige Bare-Testobjekt läuft mit Linux-Teststart
auf dem Host und prüft beide Überlappungsrichtungen, Identität und Länge null.

## Vollprüfung und Online-Scrub (P11-T18)

`ds_moofs_pruefen` verwendet dieselben Superblock-, Knoten-, Record- und
Datenvalidatoren wie der Leser. `moofs_pruefung_starten` leiht Transport,
Recordbereich und Bitmap; `moofs_pruefung_schritt(s, budget)` liefert
`ergebnis[bool, MooFsFehler]`: wahr nach vollständigem Durchlauf, falsch
für einen noch laufenden Durchlauf. Budget null ist ein Argumentfehler.
Der Prüfer schreibt und repariert nichts und stoppt am ersten Befund.

Anders als der degradierte Mount verlangt die Vollprüfung zwei gültige
Superblöcke. Sie durchläuft beide veröffentlichten Bäume samt Dateidaten,
auch die ältere CoW-Generation. Gemeinsame Blöcke zwischen Generationen
sind erlaubt, doppelte Referenzen innerhalb einer Wurzel nicht. Die Bitmap
wird deshalb je Wurzel neu aufgebaut. Freie, von keiner Wurzel erreichbare
Blöcke werden nicht als Dateisysteminhalt interpretiert.

Zusätzlich zu CRC, Kopf, Generation, Kindgrenzen und reservierten Bytes
prüft sie die vollständige Dateiabdeckung, logische Extentfolge,
physische Mehrfachbelegung, Inodezuordnung, eindeutige Namen und
Verzeichnisziele mit passender Art. Jeder Inode außer Wurzel 1 hat
genau einen Verzeichniseingang und muss über eine azyklische Elternkette
mit der Wurzel verbunden sein. Wurzel 1 hat keinen Eingang.

Der Aufrufer stellt mindestens einen Recordplatz je Blattrecord der
größeren der beiden Wurzeln sowie `ceil(Transportkapazität / 4096 / 8)`
Bitmapbytes bereit. Zu kleiner Arbeitsraum meldet `MOOFS_SPEICHER`.
Prüfzustand, Arbeitsbereiche und Transport dürfen einander nicht
überlagern und bleiben bis Ende oder Abbruch gültig.

Ein Arbeitsschritt verarbeitet höchstens einen 4096-Byte-Block mit bis
zu zwölf Blattrecords, einen Metadatenrecord, ein Namenspaar oder eine
Elternkante. Das Löschen der Bitmap ist auf 4096 Bytes je Schritt begrenzt.
Namensvergleich und binäre Inodesuche bleiben durch Format beziehungsweise
Arbeitsraum begrenzt. Das Budget zählt solche Arbeitseinheiten, keine
Mikrosekunden und keine harte Echtzeitgarantie. Doppelte Namen werden
paarweise innerhalb eines Verzeichnisses geprüft.

Nach einem Fehler enthalten `fehler.block` und `fund` die Blocknummer
und das zugehörige Schlüsseltripel. `problem_block` nennt bei einer
Mehrfachbelegung zusätzlich den doppelt referenzierten physischen Block;
`kopie` bezeichnet den geprüften Superblock. CRC-/E/A-Fehler behalten ihre
vorhandenen Codes. Zusätzliche Formatgründe:

| Grund | Befund |
|---:|---|
| 700 | Referenz außerhalb des belegten Bereichs |
| 701 | Physischer Block innerhalb einer Wurzel mehrfach verwendet |
| 702 | Zugehöriger Inode fehlt |
| 703 | Zu große Baumtiefe |
| 704 | Datenabdeckung stimmt nicht mit Dateilänge überein |
| 705 | Wurzel-Inode fehlt oder ist ungültig |
| 706 | Extentfolge, Typ oder arithmetischer Bereich ungültig |
| 707 | Verzeichnisrecord gehört zu keinem Verzeichnis |
| 708 | Zielart, Eingangszahl oder Wurzelverknüpfung ungültig |
| 709 | Doppelter Name im selben Verzeichnis |
| 710 | Zyklus der Elternverknüpfungen |
| 711 | Inode ohne Verbindung zur Wurzel |

`moofs_pruefung_online` leiht zusätzlich das lebende Volume. Start und
jeder Schritt laufen unter derselben Besitzersperre wie sämtliche
Writeroperationen. Zwischen Schritten darf der Writer arbeiten. Ändern
sich Generation, Wurzel oder Volumeidentität, meldet der nächste Schritt
`MOOFS_PRUEF_NEUSTART` (0x4000D); der Besitzer beginnt den Durchlauf neu.
Bei dauernden Mutationen ist ein Abschluss nicht garantiert.

Der Kernelträger `speicher_moofs_scrub` führt diesen Vertrag im vorhandenen
Fadenplaner aus. `moofs_scrub_starten` leiht Schreiber, einen schlafenden
`KernMutex`, Dienst und Arbeitsbereiche. Alle anderen Writerzugriffe
verwenden denselben Mutex. Pro Budget hält der Dienst die Sperre,
gibt sie über natives `aufräumen` auch bei Fehlern frei und wechselt
danach den Faden. Ein Generationswechsel startet automatisch neu.
`fortschritt` und `neustarts` sind atomare Zähler. Start und Kontrolle
desselben Dienstes serialisiert der Besitzer.

`status` hat die Werte 1 laufend, 2 vollständig geprüft, 3 Fehler und
4 abgebrochen. Nach `moofs_scrub_abbrechen` wartet der Besitzer weiterhin
auf einen terminalen Status mit Acquire-Laden. Erst dann darf er geborgte
Bereiche freigeben oder den Dienst erneut starten. Ein terminaler
Release-Store ist der letzte Zugriff des Arbeiters auf den Dienst.

### Hosted-Prüfwerkzeug

`mise run -C skripte/p11/moofs pruefen -- BILD [RECORDKAPAZITAET]`
öffnet das Bild ausschließlich lesend. Standard sind 4096 Recordplätze;
die Bitmap wird aus der Bildgröße berechnet. Erfolg liefert Exit 0 mit
Generation und Zählern. Ein Befund liefert Exit 1 mit Code, Grund,
Blocknummer, Superblockkopie, Inode, logischem Index und gegebenenfalls
zweitem physischem Block. Das Werkzeug repariert keine Daten.

Gezielte Gates: `pruef-test` prüft gültige und resealte beschädigte
Strukturen mit 512-/4096-Byte-Transport, einen dreistufigen Baum mit
600 Dateien, beide CoW-Wurzeln, Generationsneustart, Arbeitsraum und
E/A-Fehler. `pruef-abbild` prüft die echte CLI samt lesendem Bildzugriff
und unveränderten SHA256-Werten. Das Kernelprofil `kern`/Modus
`schreiben` enthält den Scrub während einer echten CoW-Mutation und
eine native Mehr-CPU-Cleanup-Probe.

## Implementierungsgrenzen

Reader und Formate gehören nach stdlib/ds_moofs*.moos; CRC32C nach
stdlib/nativ_crc32c.moos. Der Formatierer leiht sein Manifest und den
Arbeitsbereich, übernimmt keine Quellen und ist nicht der Mount-Schreibpfad.
Die erste mkfs-Implementierung beschränkt ihr Manifest auf 128 Einträge.
Diese Grenze steht nicht im Datenträgerformat. Kernel und hosted verwenden
denselben Moo-Leser und dieselben CRC-Prüfungen.
