# FAT32 in Moo

`stdlib/ds_fat.moos` enthält denselben nativen Dateisystemkern für Host und MoOS.
`ds_fat_formate` beschreibt die gepackten BPB-, FSInfo- und Verzeichniseinträge mit
expliziten Little-Endian-Feldern. Es gibt keine C-Dateisystemimplementierung.

## Transport und Lebensdauer

`FatTransport` bietet `geometrie()` und synchrone `uebertragen(art,lba,anzahl,puffer,bytes)`.
Operationen und Fehler entsprechen `nativ_block`. Ein Aufruf leiht den Puffer bis
zur Rückkehr. Der Besitzer hält Transport und Volume gültig und serialisiert
**alle** Operationen am Volume, einschließlich mehrerer Handles auf dieselbe Datei.
Handles nach Änderungen über einen anderen Handle neu suchen.

- `FatRam / fat_ram_einrichten`: geliehener zusammenhängender RAM-Bereich.
- `FatPosix / fat_posix_einrichten`: geliehener regulärer POSIX-Dateideskriptor;
  Größe wird über `lseek` geprüft, I/O über `pread/pwrite`, Persistenz über `fsync`.
  Kurze Transfers und EINTR werden behandelt; Verwerfen wird ausdrücklich abgelehnt.
- `FatKernTransport / fat_kern_transport_einrichten` in
  `beispiele/kernel/speicher_fat.moos`: vorhandene Blockkennung, auch GPT-Partition
  oder Blockcache. Anforderungen laufen weiterhin über `block_anfordern`.

## API

Alle falliblen Funktionen liefern `ergebnis[...,BlockFehler]`; mit `weitergeben`
propagieren oder mit `ist_erfolg/ergebnis_fehler` prüfen.

| Funktion | Verhalten |
|---|---|
| `fat_formatieren(transport,seriennummer)` | FAT32 neu formatieren, zwei FATs, Wurzelcluster2, BPB/FSInfo und Sicherungen 6/7 |
| `fat_oeffnen(volume,transport)` | BPB und Geometrie prüfen; schmutzige Medien nur lesend öffnen |
| `fat_anlegen(volume,dir,name_utf8,verzeichnis)` | Datei/Unterverzeichnis samt eindeutigem Kurzalias und LFN anlegen |
| `fat_suchen(volume,dir,name_utf8)` | Datei/Unterverzeichnis suchen; Wurzel über `volume.layout.wurzel` |
| `fat_lesen(volume,datei,offset,daten)` | Exakt die Pufferlänge lesen; außerhalb der Dateigröße Fehler |
| `fat_schreiben(volume,datei,offset,daten)` | Schreiben und bei Bedarf wachsen; Lücken werden genullt |
| `fat_datei_groesse(volume,datei,bytes)` | Wachsen/kürzen; überschüssige Cluster freigeben |
| `fat_loeschen(volume,datei)` | Datei oder leeres Verzeichnis samt LFN und Clusterkette löschen |
| `fat_synchronisieren(volume)` | Freie Cluster zählen, FSInfo/Sicherung schreiben, flushen und sauber markieren |

Namen sind UTF-8, auf Platte UTF-16 (höchstens 255 Codeeinheiten). Ungültiges UTF-8,
isolierte Surrogatcodepunkte, verbotene FAT-Zeichen sowie abschließender Punkt/Leerraum sind Fehler.
Suche verwendet Unicode 17-Simple-Upper, einschließlich Zeichen außerhalb des BMP.
Keine Unicode-Normalisierung und keine Mehrzeichen-Faltung (etwa ß zu SS).
Die verwendeten Unicode-Daten stehen unter der [Unicode-Lizenz](fat32-unicode-LICENSE.txt).
Ungültige LFN-Ketten werden ignoriert; der gültige Kurzalias bleibt suchbar.
Ohne Zeitquelle verwenden neu angelegte Einträge das Datum 1980-01-01.

Der Kern unterstützt logische Sektoren 512–4096 in Zweierpotenzen,
Cluster bis 32 KiB und eine oder zwei FATs. Der Formatierer verwendet zwei FATs.
Kleine Medien unterhalb der FAT32-Clustergrenze werden abgelehnt. Der aktive
FAT-/Spiegelmodus des BPB wird beim Öffnen beachtet, das reservierte obere FAT-Nibble
bleibt bei Änderungen erhalten. FSInfo wird nur als Hinweis behandelt.

## Fehler und Persistenz

Vor Änderungen werden Dirty-Bit und unbekannter FSInfo-Zähler persistent gemacht.
Bei einem I/O-Fehler sperrt das geöffnete Volume weitere Schreiboperationen.
Bei bereits begonnenen Änderungen meldet `fat_synchronisieren` dann einen Fehler.
Erst prüfen/reparieren und neu öffnen.
Zyklen, freie/defekte Cluster innerhalb einer Kette und Bereichsfehler liefern
einen Fehler; Freigaben prüfen die Kette vorab.

FAT32 besitzt hier kein Journal: ein abgebrochener Schreibaufruf kann Teiländerungen
oder verlorene Cluster hinterlassen. Ein erfolgreiches Synchronisieren ist die
Persistenzgrenze; es ersetzt keine atomare Mehrdateitransaktion.

## Reproduzierbare Abnahme

```sh
mise -C skripte/p11/fat32 run ram
mise -C skripte/p11/fat32 run fuzz
mise -C skripte/p11/fat32 run abbild
mise -C skripte/p11/fat32 run uefi
mise -C skripte/p11/fat32 run kern
```

Abbild prüft 512-/4096-Byte-Medien mit `fsck.vfat -n`, FAT-Spiegel/FSInfo und
`mcopy` in beiden Richtungen (1 MiB bytegleich). Die UEFI-Probe schreibt die EFI-App
über Moo in die Moo-formatierte ESP; OVMF muss sie selbst laden. Die Kernelprobe
nutzt vier CPUs und RAM → Blockregister → GPT-Partition → FAT-Kerneladapter.
Fuzzing mutiert BPB, UTF-8, LFN und Clusterketten und injiziert 80 I/O-Fehlerpunkte.
