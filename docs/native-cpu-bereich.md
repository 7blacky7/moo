# Gebundene CPU-Bereiche

`@cpu_bereich` markiert einen normalen `@nativ`-Funktionskörper.
Er darf geschützte `@simd`-Funktionen und CRC32C wiederholt aufrufen.
Der Dekorator hat keine Argumente; `@simd`, IRQ und nackte Körper werden
als eigene aufgerufene Funktionen außerhalb des Bereichsdeklarators definiert.

Auf x86-64 Linux sichert der äußere Bereich die Affinität des ausführenden
Threads und bindet ihn an seinen aktuellen zugelassenen Kern. Erst danach
wählt er CPU-/OS-Merkmale einschließlich XCR0. Profil und SSE4.2-Freigabe
liegen nur für diese Lebensdauer im Threadspeicher. Verschachtelte Bereiche
übernehmen diese Freigabe. Andere Threads besitzen eigene Bereiche.
Es gibt keinen prozessweiten Cache des zuerst beobachteten Prozessors.
Der Bindungsanbieter verwendet eine Affinitätsmaske für bis zu 1024 CPUs;
passt die Umgebung nicht, bleibt der normale Guardpfad aktiv.
Kompatible eigene Extern-C-Deklarationen der Linux-Affinitätsfunktionen sind
erlaubt; widersprechende ABI-Signaturen werden diagnostiziert.

Ein eigener Frame im vorhandenen nativen Cleanup-Stack stellt bei Return,
nativen Fehlern und compilerseitigem Abbruch zuerst die alte Freigabe und
anschließend die vorherige Thread-Affinität wieder her. Benutzer-Cleanup läuft
davor. Scheitert die Wiederherstellung, werden verbleibende Frames abgearbeitet
und der Prozess mit Diagnose beendet; ein inkonsistenter Bereich kehrt nicht
normal zurück.

Wenn die Plattform keine sichere Bindung anbietet oder die Bindung scheitert,
bleiben die bisherigen CPUID-/OS-Guards aktiv. Bare, UEFI und Windows benutzen
vorerst diesen Rückfall. Insbesondere wird deren bestehende XSAVE-Sicherung
nicht durch eine Annahme ersetzt.

Während des Bereichs gehören Affinität und Registerzustandsfreigabe dem
Bereich. Unsichere Fremd-/Assembleraufrufe dürfen sie nicht verändern.
Neu erzeugte Systemthreads erben die momentan gebundene Affinität gemäß
Plattformvertrag. CPU-Hotplug und Änderungen der virtuellen CPU-Merkmale
während eines Bereichs gehören nicht zum unterstützten Vertrag.

Die öffentlichen Längen-, Typ-, Fehler- und Aliasprüfungen bleiben je Aufruf
erhalten. Ein Bereich bietet nur den Beleg für die wiederverwendete ISA-Auswahl.
Die einmalige Bindung lohnt sich insbesondere um eine größere Rechenaufgabe,
statt um jeden einzelnen kleinen Baustein.

Regression: `mise run -C skripte/p11/array-q cpu-bereich`.
Der Messadapter aktiviert einen äußeren Bereich vor den Zeitmessungen für
beide Vergleichsarme; das wird im Ergebnisprotokoll ausgewiesen.
