# Nativer VFS und MoOS-Dateideskriptoren

`stdlib/vfs.moos` enthält die nativen Schnittstellen `Dateisystem` und
`Datei`, die Mounttabelle und den Zustand einer geöffneten Datei.
`ds_ram.moos` und `ds_fat_vfs.moos` implementieren denselben Vertrag.
Die Dateisystemlogik ist Moo; der FAT-Adapter verwendet den vorhandenen
FAT32-Kern aus `ds_fat.moos`.

## Eigentum und Sperren

Ein Dateisystem wird als geliehener `ptr[dyn Dateisystem]` eingehängt. Sein
Objekt und seine Speichermedien müssen bis zum erfolgreichen Aushängen
gültig bleiben. `VfsDatei` liegt beim Aufrufer und wird vor dem Öffnen mit
Null initialisiert. Einen offenen Dateizustand nicht kopieren: genau ein
Schließaufruf konsumiert seinen Treibergriff und seine Mountreferenz.

Auch ein fehlgeschlagener Schließaufruf konsumiert den Griff. Beispielsweise
meldet FAT einen fehlgeschlagenen Flush, lässt aber keinen offenen Griff
zurück. Wiederholtes Schließen ergibt EBADF. Ein fehlgeschlagenes Aushängen
behält dagegen den Mount; erst nach erfolgreichem Abgleich wird er entfernt.

Puffer werden ausschließlich bis zur Rückkehr des jeweiligen Aufrufs
geliehen. Der neutrale VFS besitzt keine versteckten Sperren; sein Besitzer
serialisiert alle Zugriffe auf Mounttabelle und Dateisysteme.
`beispiele/kernel/kern_vfs.moos` übernimmt dies im laufenden Kernplaner:
zuerst der schlafende Mutex der Prozess-FD-Tabelle, dann der globale
schlafende VFS-Mutex. Treiber dürfen während eines VFS-Aufrufs nicht erneut
in den VFS eintreten. Unter diesen Sperren darf Block-I/O schlafen.

## Pfade und Mounts

Pfade sind längengeprüfte Byteansichten ohne NUL-Terminierung. Ein NUL-Byte
ist EINVAL, ein leerer Pfad ENOENT. Absolute und relative Pfade werden ab
`/` ausgewertet. Wiederholte Schrägstriche sowie `.` und `..` werden vor
der Mountwahl lexikalisch normalisiert; `..` bleibt an der Wurzel stehen.
Es gibt noch kein prozessabhängiges Arbeitsverzeichnis und keine Symlinks.

Grenzen: 4095 Pfadbytes, 1020 Bytes je normalisierter Komponente und
16 Mounts. Der längste an einer Komponentengrenze passende Mount gewinnt:
`/boot` passt zu `/boot/KERN.ELF`, nicht zu `/booted`. Ein exakter Mountpfad
wird an den Treiber als `/` weitergegeben. Ein Mountpunkt und seine
Vorfahren lassen sich nicht löschen oder umbenennen. Aushängen ist bei
offenen Dateien oder darunter eingehängten Dateisystemen EBUSY.
Umbenennen über Mountgrenzen ist EXDEV.

## Dateioperationen

`vfs_oeffnen(tabelle, pfad, modus, datei)` füllt einen `VfsDatei`-Zustand.
Methoden werden über einen Zeiger auf diesen Zustand aufgerufen. Ein solcher
Zeiger kann auch als `ptr[dyn Datei]` verwendet werden.

| Öffnungsflag | Wert | Wirkung |
|---|---:|---|
| VFS_LESEN | 1 | Lesen und Verzeichnisauflistung erlauben |
| VFS_SCHREIBEN | 2 | Schreiben erlauben |
| VFS_ANLEGEN | 4 | Fehlende Datei anlegen |
| VFS_LEEREN | 8 | Beim Öffnen auf Länge null kürzen; benötigt Schreibrecht |
| VFS_VERZEICHNIS | 16 | Ziel muss ein Verzeichnis sein |
| VFS_EXKLUSIV | 32 | Zusammen mit ANLEGEN vorhandene Ziele ablehnen |
| VFS_ANHAENGEN | 64 | Vor jedem Schreiben an das aktuelle Dateiende gehen |

Mindestens LESEN oder SCHREIBEN muss gesetzt sein. Unbekannte Flags sind
EINVAL. Verzeichnisse lassen sich nicht als beschreibbare Dateien öffnen.
Lesen liefert die tatsächliche Bytezahl und am Ende null; Schreiben erhöht
die gemeinsame Dateiposition um die bestätigte Bytezahl. Die Positionierung
nimmt einen vorzeichenbehafteten Offset und Bezug 0=Anfang, 1=aktuell,
2=Ende; negative Ergebnisse und Überläufe sind Fehler.
Verzeichnisauflistung verwendet einen sichtbaren Eintragsindex und endet
mit ENOENT. `VfsEintrag` enthält maximal 1024 Namensbytes mit expliziter
Länge; `.` und `..` werden nicht ausgegeben.

`vfs_anlegen`, `vfs_loeschen`, `vfs_umbenennen`, `vfs_stat` und
`vfs_abgleichen` arbeiten auf Pfaden beziehungsweise allen Mounts.
Der Dateisystemvertrag stellt zusätzlich eine Größenänderung für seine
offenen Griffe bereit.

## Treiber

RAMFS leiht einen achtbyte-ausgerichteten Bereich von 1 bis 2048 Seiten
à 4096 Bytes. Metadaten und 128 Inodeplätze einschließlich Wurzel liegen
im `RamDateisystem`-Objekt, Dateidaten in den geliehenen Seiten.
Namen enthalten höchstens 255 Bytes. 256 generationsgeprüfte Treibergriffe
verhindern die Wiederverwendung geschlossener Griffe. Ein noch offener,
gelöschter Inode lebt bis zum letzten Close. Rename kann ein vorhandenes
kompatibles Ziel ersetzen; offene Zielgriffe behalten den alten Inode.
Verzeichniszyklen und nichtleere Verzeichnisziele werden abgelehnt.
Vergrößern und Schreiben hinter dem Ende füllen neue Bytes mit null;
ENOSPC bei Größenänderung verändert weder Länge noch Inhalt.

FAT-VFS leiht ein geöffnetes `FatVolume`. Seine 256 generationsgeprüften
Griffe teilen bis zu 128 kanonische Inodezustände. So sehen mehrere offene
Dateien Größenänderungen und bleiben nach erfolgreichem Rename konsistent.
Der vorhandene UTF-16-LFN-Decoder liefert jetzt auch einen Cursoriterator;
der Adapter gibt Namen als UTF-8 aus. Der FAT-Kern behält Langnamen- und
Kurzalias-Suche sowie die bisherige Unicode-Faltung bei.

FAT löscht geöffnete Inodes mit EBUSY nicht und ersetzt beim Rename keine
vorhandenen Ziele (EEXIST). Rename auf denselben Inode ist erfolgreich ohne
Namensänderung. Rename verschiebt die Verzeichniseinträge und erhält die
Datenkette; bei Verzeichniswechsel wird `..` nachgeführt. FAT32 besitzt kein
Journal und garantiert hierfür keine Stromausfall-Atomizität. Ein Fehler
nach Beginn der Veröffentlichung schaltet das Volume auf nur-lesend.
Siehe auch [FAT32](fat32.md).

## MoOS-Prozessdeskriptoren

`BenutzerGruppe.bg_dateien` besitzt `KernVfsDateien`.
`BenutzerProzess.bp_dateien` und alle Fäden derselben Gruppe zeigen darauf.
Deskriptoren beginnen bei 3; 0–2 bleiben für die Konsolenkonvention frei.
61 offene Dateien je Prozessgruppe sind möglich. Unabhängiges Öffnen
erzeugt eine eigene Position; Fäden teilen den bestehenden Deskriptor
einschließlich Position.

`prozess_freigeben` schließt die Tabelle erst, nachdem
`adressraum_freigeben` den letzten aktiven Kontext ausgeschlossen hat.
Der Faden-Endepfad unter Gruppenspinsperre führt kein VFS-I/O aus.
Cleanup schließt auch nach einem Flushfehler alle restlichen Dateien und
bewahrt den ersten Fehler in `KernVfsDateien.letzter_fehler`.

## Datei-API im Benutzerraum

`stdlib/moos_datei.moos` stellt elf Funktionen mit `ergebnis[..., u32]`
bereit. Fehler enthalten einen positiven errno-Wert; die rohe ABI liefert
dessen Negation in RAX. Nummern 0–7 behalten ihren bisherigen Vertrag.

| Nummer | Funktion | Argumente |
|---:|---|---|
| 8 | moos_datei_oeffnen | Pfadzeiger, Pfadlänge, Öffnungsflags |
| 9 | moos_datei_lesen | Deskriptor, Zielzeiger, Kapazität |
| 10 | moos_datei_schreiben | Deskriptor, Quellzeiger, Länge |
| 11 | moos_datei_schliessen | Deskriptor |
| 12 | moos_datei_stat | Deskriptor, Metadatenzeiger, Metadatengröße |
| 13 | moos_datei_verzeichnis | Deskriptor, Index, Kopfzeiger, Kopfgröße, Namenszeiger, Namenskapazität |
| 14 | moos_datei_position | Deskriptor, vorzeichenbehafteter Offset, Bezug |
| 15 | moos_datei_abgleichen | Deskriptor |
| 16 | moos_datei_anlegen | Pfadzeiger, Pfadlänge, Verzeichnis (0 oder 1) |
| 17 | moos_datei_loeschen | Pfadzeiger, Pfadlänge |
| 18 | moos_datei_umbenennen | Quellzeiger, Quelllänge, Zielzeiger, Ziellänge |

`moos_aufruf6` legt die Nummer in RAX und die sechs Argumente in
RDI, RSI, RDX, R10, R8 und R9. Nicht verwendete Argumente müssen null sein.
Der bisherige Wrapper `moos_aufruf` ergänzt vier Nullargumente.
Der Kernel rettet R10 vor dem Stackwechsel in einem GS-lokalen Feld;
`pro_kern_versatz(...)` liefert dessen geprüften konstanten Versatz für
einen Assembler-Eingang mit Constraint `"i"`.

`moos_datei_abi.moos` definiert die gemeinsamen Flags und Metadaten:
`MoosDateiStat` umfasst 16 Bytes (art/rechte als u32, bytes als u64).
Der 24-Byte-Verzeichniskopf enthält bytes und name_bytes als u64 sowie
art und reserviert als u32. Der Name liegt in einem separaten Puffer,
ohne NUL-Terminierung. Anders als die Kernel-VFS-Schnittstelle liefert
die Benutzer-API am Verzeichnisende einen Kopf mit art=0.
Ein zu kleiner Namenspuffer ergibt ERANGE (34).

Lesen und Schreiben übertragen höchstens 4096 Bytes pro Aufruf.
Aufrufer müssen kurze Transfers behandeln; ein erfolgreicher Read mit
null Bytes bedeutet EOF. Positionen sind auf I64_MAX begrenzt;
negative Ergebnisse sind EINVAL, Überläufe EOVERFLOW. Ein fehlgeschlagener
Seek verändert die Position nicht.

Pfad- und Datenpuffer werden in begrenzte Kernelpuffer kopiert.
Jede Kopie prüft Benutzeradressbereich, Überlauf, aktive CR3 und alle
betroffenen Seiteneinträge unter der Adressraumsperre. Kopieren zum
Benutzer verlangt zusätzlich Schreibrecht. Ungültige Adressen ergeben
EFAULT (14); Benutzerzeiger gelangen nicht an VFS oder Treiber.
Die Adressraumsperre bleibt während der Kopie gehalten und wird vor
schlafenden Dateisystemoperationen freigegeben. Ein Read prüft den
Zielbereich vor der Positionsänderung und erneut beim Kopieren.
Die Pufferreservierungen werden auch auf Fehlerpfaden freigegeben.

## Gezielte Prüfungen

- `mise -C skripte/p11/vfs run datei` beziehungsweise `datei-ein`:
  echte Ring-3-Datei-API mit vier beziehungsweise einer CPU, 1 MiB bytegleich,
  alle elf Operationen, sechs Argumente, Fehleradressen, Seek-Grenzen,
  geerbte Deskriptoren und Cleanup von 61 absichtlich offenen Griffen.
- `mise -C skripte/p11/vfs run faden`: bisherige Thread-/Futex-/TLS-ABI
  über denselben neuen Syscall-Einsprung auf vier CPUs.
- `mise -C skripte/p11/vfs run abi`: konstante Pro-Kern-Versätze und
  Assembler-Immediate-Eingänge einschließlich negativer Compile-Proben.

- `mise -C skripte/p11/vfs run ram`: Datei-/Pfad-/Mountverhalten,
  Seitenränder, Nullfüllung, offene gelöschte Dateien, erschöpfte Griffe,
  konsumierendes Close bei EIO und Bare-IR-Verifikation.
- `mise -C skripte/p11/vfs run fat`: Unicode-Verzeichnisnamen, gemeinsame
  offene Inodes, Rename ohne Datenverlust, Zyklen, RO-Remount und Bare-IR.
- `mise -C skripte/p11/vfs run kern`: bestehender Headless-UEFI-Smoke mit
  vier CPUs. Zwei Fäden lesen 1024 Wörter über dieselbe Dateiposition, jedes
  genau einmal. Prozessfreigabe konsumiert 61 Deskriptoren. Eine eigene,
  ausschließlich gelesene QEMU-NVMe-FAT-Platte enthält die tatsächlich
  gebootete private `KERN.ELF`; der Kern liest sie über `/boot/KERN.ELF`.
  Hostvergleich prüft Dateilänge, Streaming-CRC32 und unveränderte Bilddatei.

Die Ring-3-Proben verwenden standardmäßig KVM mit Host-CPU. Für TCG
`MOO_UEFI_KVM=0 MOO_UEFI_CPU=max` setzen: der Syscall-Pfad benötigt XSAVE.

Compilerpfad: `build/p2-t123/target/release/moo-compiler`; über
`MOO_COMPILER` für Hostproben überschreibbar. Das Cargo-Zielverzeichnis
ist absolut `/home/blacky/dev/moo/build/p2-t123/target`.
