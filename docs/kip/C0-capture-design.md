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

Der FD ist nonblocking. `kamera_frame` verwendet ausschließlich den in §8.1 normierten, auf `mapped_count` beschränkten Drain: während des Drains erfolgt kein QBUF. Alle gehaltenen Indizes werden danach genau einmal requeued; nur der letzte Buffer wird kopiert/konvertiert. Drops sind Teil dieses Vertrags. V4L2-`DQBUF` allein verspricht nicht automatisch den neuesten Frame. Ein Treiberbuffer darf nie Eigentum des Frames werden.

### 2.3 Verhandlung und Ausgabe

Vor Streaming sind verpflichtend:

1. `VIDIOC_QUERYCAP`: Capture + Streaming; MPLANE wird in v1 klar abgelehnt.
2. `ENUM_FMT`, `ENUM_FRAMESIZES`, `ENUM_FRAMEINTERVALS`.
3. Auswahl ausschließlich nach §8.2: bei vollständig angegebenen Parametern Exact Match oder Unsupported; Closest-Auswahl nur für ausgelassene Parameter mit festen Referenzen und Tie-Breakern.
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

- `MOO_KAMERA` und `MOO_MIKRO` werden als eigene `MooTag`-Werte ergänzt. Beide Payload-Structs haben `int32_t refcount` als erstes Feld, starten bei 1, stehen in `is_heap_type` und besitzen zentrale `moo_release`-Dispatch-Fälle zu idempotenten `moo_kamera_free`/`moo_mikro_free`. Builtin-Argumente sind borrowed, Rückgaben +1 owning; Aliase folgen derselben Regel.
- `build.rs` setzt getrennt `MOO_HAS_V4L2` und `MOO_HAS_ALSA`, nachdem pkg-config Header **und** Linkbibliotheken erfolgreich gefunden hat. Kamera-C-Datei wird nur unter `MOO_HAS_V4L2` gegen `libv4l2`/`libv4lconvert` gebaut; Mikro-C-Datei nur unter `MOO_HAS_ALSA` gegen `libasound`.
- Die Build-Erkennung wird zusätzlich als Cargo-Compile-Time-Konfiguration (`cargo:rustc-cfg=moo_has_v4l2` beziehungsweise `moo_has_alsa`, inklusive `cargo:rustc-check-cfg`) an das Compiler-Binary propagiert. Die separate native Final-Link-Stufe in `compiler/src/main.rs` ergänzt unter `#[cfg(moo_has_v4l2)]` zwingend `-lv4l2 -lv4lconvert` und unter `#[cfg(moo_has_alsa)]` zwingend `-lasound`. Damit erhalten vom Moo-Compiler erzeugte Executables dieselben Backend-Bibliotheken wie `libmoo_runtime.a`; Stub-Builds ergänzen keine dieser Argumente. Cross-/WASM-/Bare-Object-Ausgabe linkt weiterhin nicht und propagiert folglich keine Host-Bibliotheken.
- Runtime-Bindings und Builtin-Symbole existieren in jedem Build. Ohne Feature werden immer gebaute Stub-Dateien gelinkt, die dieselben ABI-Signaturen haben und eine klare Plattform-/Abhängigkeitsmeldung werfen; dadurch gibt es keine unresolved symbols.
- Linux mit fehlenden Development-Libraries baut erfolgreich mit Stubs und gibt eine Build-Warnung plus Laufzeitdiagnose. Nicht-Linux verwendet dieselben Stubs. Packaging deklariert die Runtime-Abhängigkeiten nur für aktivierte native Backends.
- Native PipeWire, Windows Media Foundation, macOS AVFoundation und Capture-Threads sind Folgephasen ohne Änderung der Grundbegriffe.

## 8. Normative P0-Details

### 8.1 Beschränkter Latest-Drain

Nach erfolgreichem poll werden **ohne zwischenzeitliches QBUF** höchstens so viele Buffer dequeuet, wie der Stream erfolgreich gemappt und initial gequeued hat (`mapped_count`). Der Drain endet früher bei `EAGAIN` oder Deadline. Alle dequeueten Indizes werden lokal gehalten; ein Index darf pro Aufruf höchstens einmal gehalten werden.

Nur der letzte erfolgreich dequeuete Buffer wird konvertiert/kopiert. Danach werden alle gehaltenen Buffer genau einmal requeued. Ältere Buffer müssen nicht kopiert werden. Auf jedem Fehlerpfad erfolgt best-effort-Requeue aller gehaltenen Buffer. Nicht behebbarer DQBUF-, QBUF- oder Disconnect-Fehler setzt BROKEN und startet zustandsbewussten Cleanup. Überschreitet Drain oder Konversion die Deadline, wird ein eventuell erzeugter Frame verworfen und Timeout geliefert.

### 8.2 Deterministische Negotiation

Für jeden angegebenen Parameter verlangt Kamera-v1 standardmäßig Exact Match. Ausgelassene Parameter erhalten vor der Kandidatenbewertung feste Referenzen: `rw=640`, `rh=480`, `rfps=30`; diese Werte sind Auswahlpräferenzen, keine behaupteten Ergebnisse. Sind alle drei Parameter angegeben und Exact Match fehlt, wird Unsupported geliefert. Bei mindestens einem ausgelassenen Parameter wählt Moo deterministisch:

1. kleinste normierte **L1-Distanz** `abs(w-rw)/rw + abs(h-rh)/rh + abs(fps-rfps)/rfps`;
2. Tie-Breaker: kleinere Pixelzahl;
3. danach höhere FPS;
4. danach numerisch kleinerer FourCC.

Diskrete, stepwise und continuous Bereiche werden zuerst in overflow-sicher begrenzte Kandidaten für Requested-Wert, beide Nachbarn und Grenzen überführt. Ein Treiberergebnis außerhalb enumerierter/angegebener Bereiche wird abgelehnt.

Audio-v1 akzeptiert tatsächlich nur 1 oder 2 Kanäle. Andere Kanalzahlen sind Unsupported. `daten` ist Tensor f32. S16_LE wird exakt als `sample / 32768.0f` nach [-1,1) skaliert. Mono wird direkt skaliert. Stereo wird zuerst in int32 als `left + right` summiert und dann durch `65536.0f` geteilt; es gibt keine Integer-Rundung vor f32.

### 8.3 Audio-Recovery

Pro `mikro_lesen` sind maximal **3 Recovery-Versuche insgesamt** erlaubt, zusätzlich begrenzt durch die Restdeadline. EINTR zählt nicht als Recovery, EAGAIN wartet nur auf die Restdeadline. EPIPE und ESTRPIPE verbrauchen je einen Versuch.

Tritt XRUN oder Suspend nach bereits gelesenen Samples auf, werden alle Teildaten verworfen. Nach erfolgreichem Recover beginnt der komplette Block erneut innerhalb derselben ursprünglichen Deadline. Vor- und Nachfehler-Samples werden nie als kontinuierlicher Block kombiniert. Ist kein vollständiger Block mehr rechtzeitig lesbar, schlägt der Aufruf atomar fehl.

### 8.4 Vollständige Lifecycle-Übergänge

Zulässig sind ausschließlich:

- OPEN→STREAMING
- OPEN→BROKEN
- OPEN→CLOSED
- STREAMING→BROKEN
- STREAMING→CLOSED
- BROKEN→CLOSED
- CLOSED→CLOSED

Kamera erreicht STREAMING erst nach erfolgreichem STREAMON. Audio erreicht STREAMING nach erfolgreicher hw/sw-Konfiguration und prepare; der erste Read ändert den Zustand nicht erneut. frame/read sind nur in STREAMING erlaubt. close ist in jedem Zustand erlaubt und idempotent. Alle anderen Operationen in BROKEN/CLOSED liefern den passenden erklärenden Fehler ohne Backendzugriff.

### 8.5 Fehlerwert-Ownership und Return-Gate

Fehlermeldungen werden vor Cleanup als eigener refcounteter `MOO_ERROR`-Wert materialisiert und dürfen keine Zeiger auf Treiber-, mmap-, ALSA- oder Handle-Puffer behalten. Der Fehlerpfad besitzt +1 owning. `moo_throw(error)` konsumiert diese Referenz und ersetzt die globale owning Referenz, wobei ein vorheriger Fehler released wird. `moo_get_error()` liefert +1 retained an Catch-Code. Liefert die borrowed String-Konvertierung einen neuen String, released der Codegen den retained Error-Temp sofort und transferiert den neuen String in die Catch-Variable. War der geworfene Wert bereits `MOO_STRING`, gibt die Konvertierung denselben borrowed Wert zurück; dann wird der Retain aus `moo_get_error()` ohne Zwischen-Release direkt in die Catch-Variable transferiert. Der Catch-String lebt anschließend gemäß normaler Codegen-Lifetime weiter. `moo_try_leave()` released die globale Referenz beim Verlassen des äußersten Try. Unbehandelter Fehler wird nach Ausgabe vor `exit` released. Ablauf: Error erzeugen → Ressourcen-Cleanup → genau ein `moo_throw(error)` → unmittelbar Return.

Das Pflichtgate ersetzt/instrumentiert `moo_throw` als zurückkehrende, konsumierende Funktion und beweist je Fault-Punkt: Cleanup vor Throw, genau ein Throw, Ownership genau einmal übertragen/freigegeben, unmittelbarer Return und keine weitere Zustandsmutation.

### 8.6 Konkrete Caps und Checked Arithmetic

Feste v1-Defaults:

- `MOO_CAPTURE_MAX_WIDTH = 8192`
- `MOO_CAPTURE_MAX_HEIGHT = 8192`
- `MOO_CAPTURE_MAX_FRAME_BYTES = 256 MiB`
- `MOO_CAPTURE_MAX_BUFFERS = 16`
- `MOO_CAPTURE_MAX_AUDIO_SAMPLES = 16_777_216`
- `MOO_CAPTURE_MAX_TIMEOUT_MS = 60_000`

`timeout_ms` liegt in 0..60000; `n_samples` in 1..MAX_AUDIO_SAMPLES; tatsächliche Kanäle in 1..2. Vor Backendzugriff oder Allokation werden REQBUFS.count, jede Mapping-Länge, width×height×4, bytesperline×height, sizeimage und n_samples×channels×sizeof(int16_t) mit checked size_t/uint64-Arithmetik validiert. REQBUFS-Antwort 0 oder >16, Mapping-Länge 0 oder >MAX_FRAME_BYTES und jede Cap-/Overflow-Verletzung liefern Bounds/Invalid-Argument ohne Allokation oder weiteren Backendzugriff.

## 9. C1-Abnahmekriterien

C1 ist erst fertig, wenn:

- alle Fault-Injection-Gates ASan/UBSan-grün sind
- Timeout und Latest-Vertrag nachweisbar sind
- partieller Cleanup und idempotentes Close bewiesen sind
- tatsächliche Kamera-/Audioformate zurückgespiegelt werden
- Beispiele `ki_kamera_live.moo` und `ki_mikro_spektrum.moo` existieren
- moolang-docs DE/EN-Aliase, Timeout, Drops und Fehlerklassen dokumentiert
- unabhängiger Subagent die Implementierung adversarial mit GO bewertet

## 10. Review-Entscheidung

Die vier ursprünglichen Fragen sind geschlossen:

1. ALSA v1: GO mit explizitem Gerät, echter Negotiation und Deadline.
2. libv4l: GO; eigene MJPEG-Dekodierung NO-GO.
3. synchron/pull: GO; latest wird durch drain/requeue implementiert.
4. Loopbacks allein: NO-GO; Pflichtgate ist deterministische Injection, Loopbacks ergänzen es.

Dieser V2-Vertrag erfüllt die P0-Auflagen aus `verification-ki-multi-c0-gpt` und wartet auf unabhängiges Re-Review.
