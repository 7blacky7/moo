# KI-MULTI-C0 — Capture-Vertrag für Kamera und Mikrofon

**Status:** DESIGN V2 — P0-Befunde aus `verification-ki-multi-c0-gpt` eingearbeitet.  
**Owner-Task:** `d222221a`. C1 darf erst nach unabhängigem GO dieses Vertrags starten.

Ziel sind robuste, plattformneutrale Pipelines:

- Kamera → `MOO_FRAME` → `tensor_aus_frame` → Netz
- Mikrofon → Tensor → `spektrogramm` → Netz

## 1. Verbindliche v1-Entscheidungen

- Linux-Kamera: V4L2-MMAP über libv4l2/libv4lconvert.
- Keine eigene MJPEG-Dekodierung.
- Linux-Audio: libasound. `default` ist nur ein Default-Gerät und nicht garantiert PipeWire.
- Synchrones Pull ohne Capture-Thread.
- Kamera verwendet ausdrücklich **latest-frame-Semantik**.
- Alle blockierenden Aufrufe besitzen eine monotone Deadline.
- Deterministische Backend-Injection ist das Pflichtgate; Loopback-Geräte sind ein zusätzliches Integrationsgate.

## 2. Öffentliche Kamera-API

```moolang
kamera_liste() -> Liste<{pfad, name, id?}>
kamera_oeffnen(pfad?, breite?, hoehe?, fps?) -> MOO_KAMERA
kamera_frame(kamera, timeout_ms?) -> MOO_FRAME
kamera_schliessen(kamera) -> nichts
```

Englische Aliase: `camera_list`, `camera_open`, `camera_frame`, `camera_close`.

### 2.1 Timeout

- Standard: 1000 ms.
- `timeout_ms == 0`: nicht blockieren; sofort Frame oder Timeout-Fehler.
- Negative, nicht-endliche oder zu große Werte werden erklärt abgelehnt.
- Deadline basiert auf monotoner Zeit und gilt für poll + drain + Konversion zusammen.
- Timeout ist von Busy, Permission, Unsupported, Disconnect und Closed unterscheidbar.
- Kein API-Pfad darf unbegrenzt blockieren.

### 2.2 Latest-frame-Semantik

Der FD ist nonblocking. `kamera_frame` pollt bis zur Deadline und dequeuet danach alle sofort verfügbaren Buffer. Ältere Buffer werden unmittelbar requeued; nur der neueste wird kopiert/konvertiert. Drops sind Teil dieses Vertrags. V4L2-`DQBUF` allein verspricht nicht automatisch den neuesten Frame.

Jeder erfolgreich dequeuete Buffer wird auf **jedem** Pfad requeued, nachdem die Daten in einen eigenen `MOO_FRAME` kopiert wurden. Ein Treiberbuffer darf nie Eigentum des Frames werden.

### 2.3 Verhandlung und Ausgabe

Vor Streaming sind verpflichtend:

1. `VIDIOC_QUERYCAP`: Capture + Streaming; MPLANE wird in v1 klar abgelehnt.
2. `ENUM_FMT`, `ENUM_FRAMESIZES`, `ENUM_FRAMEINTERVALS`.
3. Exact Match, wenn möglich; sonst dokumentierte nächste unterstützte Größe/FPS.
4. `S_FMT`-Rückgabe ist Wahrheit: pixelformat, width, height, bytesperline, sizeimage validieren.
5. FPS mit `S_PARM`/ `G_PARM` setzen und tatsächlichen Wert speichern.
6. MMAP- und Konversionsfähigkeit des gewählten libv4l-Pfads prüfen.

libv4l muss ein tatsächlich unterstütztes RGB24 oder BGR24 liefern. Moo ergänzt Alpha=255 beziehungsweise swizzelt BGR→RGBA. RGBA-Ausgabe durch libv4l wird nicht vorausgesetzt. Ausgabe ist immer RGBA8, top-left, kompakter eigener Frame-Stride.

Alle Rechnungen wie `width*height*4`, `bytesperline*height` und `sizeimage` erfolgen overflow-sicher und unter konfigurierten Caps.

## 3. Öffentliche Mikrofon-API

```moolang
mikro_oeffnen(rate?, kanaele?, geraet?) -> MOO_MIKRO
mikro_lesen(mikro, n_samples, timeout_ms?) -> {daten: Tensor[n], rate}
mikro_schliessen(mikro) -> nichts
```

Englische Aliase: `microphone_open`, `microphone_read`, `microphone_close`.

- Standardgerät: `"default"`; expliziter Gerätename ist möglich.
- Standardtimeout: 1000 ms; `0` und ungültige Werte wie bei Kamera.
- `n_samples` bezeichnet mono Output-Samples, nicht interleaved Eingabewerte.
- Internes Format v1: S16_LE, interleaved.
- Gewünschte und tatsächlich ausgehandelte Rate/Kanäle/Period/Buffer werden getrennt gehalten.
- Rückgabe-`rate` ist immer die tatsächliche Rate.
- Stereo→Mono: arithmetisches Mittel in ausreichend breiter Zwischenrepräsentation.
- v1 resampelt nicht stillschweigend. Kann die gewünschte Rate nicht exakt gesetzt werden, wird die tatsächliche Rate zurückgegeben.

`snd_pcm_readi` läuft bis exakt `n_samples` Output-Samples oder bis zur Deadline. Short Reads sind normal. Behandelt werden begrenzt:

- `EINTR`: wiederholen.
- `EAGAIN`: poll/wait bis zur Restdeadline.
- XRUN `EPIPE`: `snd_pcm_recover`/prepare.
- Suspend `ESTRPIPE`: begrenzter Resume-/Prepare-Versuch.
- Disconnect oder erschöpftes Recovery-Budget: Handle wird BROKEN und liefert erklärenden Fehler.

Timeout nach bereits gelesenen Teildaten liefert keinen stillen kürzeren Tensor; der Aufruf scheitert atomar.

## 4. Handle-Lifecycle

Beide opaken Heap-Typen besitzen:

```
OPEN -> STREAMING -> BROKEN -> CLOSED
```

- `schliessen` ist idempotent.
- Ressourcenfelder werden nach Freigabe sofort auf Sentinel/NULL gesetzt.
- Der Destruktor schließt nur noch vorhandene Ressourcen.
- Operationen nach Close liefern `Closed Handle`.
- Gleichzeitiges read/frame/close auf demselben Handle ist in v1 nicht threadsicher und wird nicht unterstützt.
- `moo_throw` unwindingt nicht: Cleanup erfolgt vollständig **vor** throw und danach unmittelbar return.

Kamera-Cleanup rückwärts: STREAMOFF, alle erfolgreich gemappten Buffer munmap, fd close. Jeder partielle Fehler von Open bis STREAMON nutzt denselben zustandsbewussten Cleanup.

Audio-Cleanup: laufenden Zugriff abbrechen/drop, PCM schließen, Felder nullen. Partielle hw-/sw-param-Fehler dürfen nichts leaken.

## 5. Fehlerklassen

Mindestens unterscheidbar und auf Deutsch erklärt:

- Timeout
- Gerät belegt oder keine Berechtigung
- Nicht unterstütztes Format
- Gerät getrennt / BROKEN
- XRUN/Suspend nicht wiederherstellbar
- Geschlossener Handle
- Treiber lieferte ungültige Dimensionen/Strides

## 6. Testarchitektur

### 6.1 Deterministisches Pflichtgate

C1 erhält interne injizierbare Adapter/Vtables für V4L2-, poll/clock- und ALSA-Operationen. Kein öffentliches Fake-Backend.

Fault-Matrix:

- jeder Initialisierungsschritt schlägt einzeln fehl
- S_FMT verändert Format oder Dimensionen
- ungültiges bytesperline/sizeimage und Overflow
- QBUF/DQBUF/STREAMON/STREAMOFF-Fehler
- mehrere fertige Frames: latest wird gewählt, ältere requeued
- Timeout vor Daten und nach Audio-Teildaten
- Short Reads, EINTR, EAGAIN, XRUN, Suspend, Disconnect
- wiederholtes Open/Close und Close nach BROKEN/Timeout
- Alias/Refcount-Lifecycle

Alle laufen unter ASan und UBSan.

### 6.2 Linux-Integrationsgate

Auf kontrolliertem privilegiertem Runner:

- v4l2loopback mit verlustfreiem Raw-Testmuster
- snd-aloop mit bekanntem PCM-Signal
- optional eigene PipeWire-`default`-Route

MJPEG-Vergleiche verwenden Toleranz beziehungsweise Referenz derselben Decoderstrecke. Audio über Mixer/Resampler verwendet Alignment plus numerische Toleranz/SNR, nicht pauschale Bitgleichheit.

### 6.3 Hardware-Smoke

Vor Release mindestens eine echte MJPEG-UVC-Kamera und ein Mikrofon. Hotplug/Disconnect wird manuell geprüft.

## 7. Build und Plattformen

- Linux feature-detection für libv4l2/libv4lconvert und libasound.
- Fehlende Development- oder Runtime-Libraries ergeben klare Build-/Laufzeitdiagnosen.
- Nicht-Linux stellt dieselben Builtins bereit, antwortet bis zu späteren Backends aber mit einer klaren Plattformmeldung.
- Native PipeWire, Windows Media Foundation, macOS AVFoundation und Capture-Threads sind Folgephasen ohne Änderung der Grundbegriffe.

## 8. C1-Abnahmekriterien

C1 ist erst fertig, wenn:

- alle Fault-Injection-Gates ASan/UBSan-grün sind
- Timeout und Latest-Vertrag nachweisbar sind
- partieller Cleanup und idempotentes Close bewiesen sind
- tatsächliche Kamera-/Audioformate zurückgespiegelt werden
- Beispiele `ki_kamera_live.moo` und `ki_mikro_spektrum.moo` existieren
- moolang-docs DE/EN-Aliase, Timeout, Drops und Fehlerklassen dokumentiert
- unabhängiger Subagent die Implementierung adversarial mit GO bewertet

## 9. Review-Entscheidung

Die vier ursprünglichen Fragen sind geschlossen:

1. ALSA v1: GO mit explizitem Gerät, echter Negotiation und Deadline.
2. libv4l: GO; eigene MJPEG-Dekodierung NO-GO.
3. synchron/pull: GO; latest wird durch drain/requeue implementiert.
4. Loopbacks allein: NO-GO; Pflichtgate ist deterministische Injection, Loopbacks ergänzen es.

Dieser V2-Vertrag erfüllt die P0-Auflagen aus `verification-ki-multi-c0-gpt` und wartet auf unabhängiges Re-Review.
