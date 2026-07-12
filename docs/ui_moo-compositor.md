# Moo Surface-/Compositor-Protokoll

P016-O3 trennt gerasterte UI-Surfaces vom späteren Displayserver. Der Referenzkern ist eine deterministische, single-threaded C11-Zustandsmaschine ohne Toolkit, libc-Allokation, Syscalls, Uhr oder Hardwarezugriff.

## Schichten

- `moo_surface_core`: zeichnet in einen vom Aufrufer besessenen RGBA8888-Puffer.
- `moo_compositor_protocol`: versionierte Integer-Handles, Opcodes, Events und Featurebits; keine Pointer oder ABI-Rohserialisierung.
- `moo_compositor_core`: Clients, Buffer, Surfaces, atomare Commits, Damage, Stapelordnung, Frame-Events und Cursorzustand.
- `moo_compositor_raster`: partielle, deterministische RGBA-Komposition in einen caller-owned Ausgabepuffer.
- Der direkte Register-/View-Pfad ist der In-Process-Referenztransport. Spätere IPC-/Shared-Memory-Transporte und DRM-/GPU-Presenter verwenden denselben Vertrag und enthalten keine eigene Widget- oder Compositorlogik.

## Sicherheits- und Lebensdauervertrag

Handles kodieren Objekttyp, Generation und Slot. Jeder mutierende Clientaufruf prüft Typ, Generation und Eigentümer. Zerstörung und Slot-Wiederverwendung machen alte Handles dauerhaft ungültig; ein Client-Disconnect räumt seine Surfaces, Bufferreferenzen, Frame-Tokens und Cursorrechte deterministisch auf.

Buffer-Views sind intern geliehen. Der Client muss ihre Bytes unverändert und gültig halten, bis der Buffer nicht mehr gebunden ist und das `BUFFER_RELEASE`-Event geliefert wurde. Die Protokollgrenze transportiert niemals rohe Pointer. Eine spätere IPC-Schicht muss Felder einzeln in ein festgelegtes Wireformat dekodieren; C-Struct-Padding darf nicht übertragen werden.

Surface-Setter schreiben nur in Pending-State. Erst ein erfolgreicher Commit veröffentlicht Attach, Damage, ganzzahligen Scale-Faktor und Opacity gemeinsam. Ungültige oder fremde Handles, volle Tabellen und fehlerhafte Buffer lassen den sichtbaren Zustand unverändert.

Frame- und Eventtabellen besitzen feste Pro-Client-Quoten. Ein angenommener Frame-Token reserviert bereits seinen späteren Eventplatz; ein Client kann daher `present_done` oder `BUFFER_RELEASE` eines anderen Clients nicht durch eine volle eigene Queue blockieren.

## Darstellung

- RGBA8888, straight alpha, top-left.
- Rechtecke sind half-open.
- Ganzzahliger Buffer-Scale liegt in Version 1 zwischen 1 und 4.
- Stapelordnung ist stabil und serverseitig kontrolliert; Clients können nur ihre Inhalte committen.
- Damage enthält lokale Änderungen sowie automatisch alte und neue Bounds bei Bewegung, Resize, Z-Wechsel, Map/Unmap und Cursorwechsel.
- Vollständig deckende Opacity wird ausschließlich serverseitig aus allen tatsächlich gesampelten Alpha-Bytes validiert. Nur dann dürfen untere Damage-Bereiche sicher entfallen und Frame-Callbacks `OCCLUDED` melden.
- Jede Damage-Region wird auf den Hintergrund zurückgesetzt und anschließend back-to-front neu komponiert; der Cursor wird als oberste Plane gezeichnet.
- Frame-Tokens enden erst nach `present_done` und genau einmal, auch wenn eine Surface vollständig verdeckt war.

## Cursor, Clipboard und Drag-and-drop

Der Cursor ist eine eigene Softwareplane. Nur der Client mit Pointer-Fokus darf Bild und Hotspot wählen; globale Position und Z-Order bleiben Policy-Aufgaben.

Clipboard- und Drag-and-drop-Opcodes sind in Version 1 reserviert, ihre Featurebits sind aber nicht aktiv. Solche Requests liefern ohne Seiteneffekt `MOO_COMP_UNSUPPORTED`. Damit entsteht kein scheinbar sicherer globaler In-Process-Datenaustausch, bevor Fokus-, MIME-, Streaming-, Cancellation- und IPC-Isolation definiert sind.

## Gates

`mise run test-ui-moo-compositor` führt ausschließlich Headless-Prüfungen aus:

- Multi-Client-Komposition, Alpha, echte Overlap-Occlusion, Damage, Z, Resize/Scale und Cursor,
- 12.288 deterministische Differential-Presents gegen einen unabhängigen Referenzraster,
- stale, wrong-kind und cross-client Handles, Quoten/Backpressure sowie Disconnect-Aufräumen,
- atomare Fehlerpfade und Frame-Event-Reihenfolge,
- ASan und UBSan,
- freestanding Compile/Relocatable-Link mit leerem `nm -u`.

Das Gate öffnet kein Fenster und startet weder X11/Wayland/Xvfb noch QEMU. Ein echter DRM-/GPU-Presenter und IPC/Shared Memory sind ausdrücklich spätere Schichten.
