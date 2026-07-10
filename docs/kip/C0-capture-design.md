# KI-MULTI-C0 — Design: Kamera-/Mikrofon-Capture (Realtime-Eingabe)

**Status:** DESIGN (kein Code). Vor Implementierung (C1) GPT-Gegenreview PFLICHT.
**Autor:** claude-local, 2026-07-10. **Owner-Task:** d222221a.

Dieses Dokument legt den API-**Vertrag** und die Backend-/Test-Strategie für
Echtzeit-Capture fest. Ziel ist die durchgehende Pipeline
**Kamera → MOO_FRAME → Tensor (V1-Brücke) → Netz** und
**Mikrofon → Tensor → Spektrogramm (A1) → Conv-Netz (V2)**.

---

## 1. Recherche (2026-Stand)

### 1.1 Empirische Baseline (dieses Ziel-System, verifiziert)
- **Audio:** `PulseAudio on PipeWire 1.6.4` — PipeWire IST das Backend, Pulse
  nur Compat-Layer. `pw-cli`, `wpctl` nativ vorhanden.
- **Kamera:** `v4l2-ctl` vorhanden, **kein** `/dev/video*` (keine Webcam an
  diesem PC). `v4l2loopback.ko` liegt im Kernel-Tree (7.0.5-cachyos).
- **Konsequenz:** Der Fake-Device-Pfad (v4l2loopback) ist nicht nur CI-Pflicht,
  sondern hier auch für lokale Entwicklung zwingend.

### 1.2 Web-Recherche (ki-browser, 2026-07-10; Quellen zitiert)
- **PipeWire ist 2026 Default-Audioserver** auf Fedora, Ubuntu (22.10+), Debian
  12 Bookworm, Pop!_OS (22.04+). Bietet Kompatibilitätsschichten für PulseAudio,
  JACK **und ALSA** — bestehende Anwendungen laufen unverändert. Niedrigere
  Latenz als raw ALSA, besser dokumentiert. (Quellen: mehrere Distro-Audio-
  Stack-Übersichten & PipeWire-Wiki, abgerufen 2026-07-10.)
- **V4L2-Capture:** Standard ist **MMAP-Streaming** (`VIDIOC_REQBUFS` mit
  `V4L2_MEMORY_MMAP`, danach `VIDIOC_QBUF`/`VIDIOC_DQBUF`) — zero-copy,
  nur Puffer-Pointer werden getauscht. `VIDIOC_ENUM_FMT` listet Hardware-Formate
  **plus die von libv4l emulierten** Formate. **libv4l** stellt
  `v4l2_open/close/ioctl/read/mmap` bereit und konvertiert MJPEG/YUYV → RGB
  transparent. Praxis-Hinweis: generische USB-Webcams liefern bei FullHD oft
  **nur MJPEG**, nicht raw YUYV → Dekodierung/Konvertierung ist Pflicht.
  (Quellen: kernel.org V4L2-API-Doku, usb_cam-Pixelformat-System, abgerufen
  2026-07-10.)

---

## 2. Kamera-API (Vertrag)

Plattform-neutrale Signatur nach `moo_ui.h`-Muster: **gleiche API, Backend
wechselt** (Linux zuerst, Windows/macOS später identisch).

```
kamera_liste()                       -> Liste von {pfad, name}   # Enumeration
kamera_oeffnen(pfad?, breite?, hoehe?) -> MOO_KAMERA (opaker Heap-Handle)
kamera_frame(kamera)                 -> MOO_FRAME                # RGBA8, top-left
kamera_schliessen(kamera)            -> nichts
```

- **kamera_frame liefert MOO_FRAME** → fügt sich nahtlos in die V1-Brücke:
  `tensor_aus_frame(kamera_frame(k), "grau")` = Echtzeit-Vision in 2 Zeilen.
- **Format:** intern immer RGBA8 top-left (Frame-Konvention). Die MJPEG/YUYV→RGBA-
  Konvertierung macht **libv4l** (nicht selbst dekodieren). Fällt eine Kamera nur
  MJPEG, übernimmt libv4l die JPEG-Dekodierung.
- **Blockierend vs. non-blocking:** kamera_frame blockiert per Default bis zum
  nächsten Frame (`VIDIOC_DQBUF`), mit optionalem Timeout. Frame-Dropping siehe §4.
- **Backend Linux:** V4L2 über **libv4l** (`v4l2_open` etc.), MMAP-Streaming mit
  4 Puffern (üblicher Standard), `VIDIOC_S_FMT` für Format-Negotiation.
- **Fehler:** erklärende deutsche Fehler (kein Gerät, Format nicht verhandelbar,
  Gerät belegt) — Kinderleicht-Philosophie.

### 2.1 Opaker Heap-Typ MOO_KAMERA
Neuer refcounteter Heap-Typ (refcount erstes Feld, Muster MOO_GIF/MOO_FRAME):
hält fd, mmap-Puffer, Format, Dims. `moo_kamera_free` gibt mmap frei + `v4l2_close`.
Pixeldaten NIE als moo-Liste (opaker Block, Muster MOO_FRAME).

---

## 3. Mikrofon-API (Vertrag)

```
mikro_oeffnen(rate?, kanaele?)  -> MOO_MIKRO
mikro_lesen(mikro, n_samples)   -> { daten: Tensor[n], rate }   # f32 [-1,1), Mono
mikro_schliessen(mikro)         -> nichts
```

- **Rückgabe passt zu A1:** `spektrogramm(mikro_lesen(m, 16000)["daten"], 1024, 256)`
  = Echtzeit-Audio-Features. Stereo→Mono-Mittel wie `wav_lesen`.
- **BACKEND-ENTSCHEID (begründet):** **v1 = ALSA-Capture-API** (`snd_pcm_*`)
  gegen das Default-Device. Begründung: (a) ALSA ist Kernel-Teil, überall da;
  (b) PipeWire stellt 2026 auf allen Ziel-Distros die **ALSA-Compat-Schicht** →
  ein ALSA-Client spricht faktisch mit PipeWire, ohne PipeWire-spezifischen Code;
  (c) die ALSA-PCM-API ist deutlich kleiner/stabiler als die native
  libpipewire-Graph-API. **libpipewire (native Streams)** ist der spätere
  Upgrade-Pfad für niedrigste Latenz / Graph-Awareness — als Phase 2 mit
  identischer moo-Signatur (Backend wechselt, API bleibt).
- **MOO_MIKRO:** opaker Heap-Typ (snd_pcm_t*, rate, kanaele). `moo_mikro_free`
  = `snd_pcm_close`.

---

## 4. Realtime-Budget & Threading

- **Ziel-Latenz v1:** Kamera ≤ 1 Frame (~33 ms @ 30 fps) Ende-zu-Ende bis MOO_FRAME;
  Mikro-Blockgröße wählbar (Default 1024 Samples @ 16 kHz ≈ 64 ms).
- **Threading v1 (einfach):** **synchron/pull-basiert** — kamera_frame/mikro_lesen
  blockieren im aufrufenden moo-Thread bis Daten da sind. Kein eigener Capture-
  Thread in v1 (Komplexität vermeiden; die meisten Demos sind Pull-Loops).
- **Frame-Dropping:** V4L2-MMAP mit Ringpuffer; wenn der Consumer langsamer ist
  als die Kamera, wird beim nächsten DQBUF automatisch der neueste Puffer geliefert
  (ältere requeued/verworfen) → kein unbegrenztes Aufstauen.
- **Phase 2 (später):** optionaler Capture-Thread + `kanal` (bestehende
  moo-Threads/Channels) für entkoppeltes Producer/Consumer — NICHT v1.

---

## 5. Test-Strategie OHNE Hardware (CI-tauglich)

- **Kamera:** **v4l2loopback** erzeugt ein virtuelles `/dev/videoN`; ein
  deterministisches Testmuster (z.B. mit `ffmpeg`/`gst` in den Loopback gespeist
  oder direkt Frames geschrieben) füttert bekannte RGBA-Werte → `kamera_frame`
  muss exakt diese liefern. Gate-Skript startet/entfernt das Loopback-Device.
  Da dieser PC keine echte Kamera hat, ist der Loopback auch das **lokale** Gate.
- **Mikrofon:** ALSA **`snd-aloop`** (Loopback-Soundkarte) oder Datei-gefütterter
  PCM-Strom; ein bekanntes Sinus-Signal rein → `mikro_lesen` muss es bit-nah
  zurückliefern (Roundtrip-Check analog `synth`→`wav_lesen`).
- **ASan/UBSan:** Capture-Pfad leak-clean; NVIDIA-/Treiber-Rausch-Suppressions
  nach kip_g4-Muster nur mit Begründung.
- **Echte Hardware:** separates lokales Gate nur wo Gerät existiert (nicht in CI,
  nicht auf diesem PC).

---

## 6. Abgrenzung v1 vs. später

| Aspekt              | v1 (dieser Vertrag)                   | Später (Folge-Tasks)              |
|---------------------|---------------------------------------|-----------------------------------|
| Kamera-Plattform    | Linux V4L2 (libv4l)                   | Windows Media Foundation, macOS AVFoundation (identische Signatur) |
| Kamera-Geräte       | ein Gerät gleichzeitig                | mehrere parallel                  |
| Kamera-Format       | ein verhandeltes Format → RGBA8       | Format-Auswahl-API, Hardware-Timestamps |
| Mikro-Backend       | ALSA-Capture (via PW-Compat)          | native libpipewire (Latenz)       |
| Threading           | synchron/pull                         | Capture-Thread + moo-Channel      |

---

## 7. Neue opake Typen & Build (Vorschau für C1)

- Neue Heap-Typen `MOO_KAMERA`, `MOO_MIKRO` (refcount-Konvention, `moo_*_free`
  im Release-Dispatch — Struct-Feld nur durch kip-kern, Dispatch-Verdrahtung wie
  MOO_FRAME/MOO_GIF).
- Neue C-Dateien `moo_kamera.c` (libv4l) + `moo_mikro.c` (ALSA), plattform-gated
  im Build wie die 3D-Backends. Linkt `-lv4l2` bzw. `-lasound`.
- DE/EN-Builtins nach codegen-Muster; Doku moolang-docs + je ein getestetes
  Beispiel (ki_kamera_live.moo, ki_mikro_spektrum.moo) im selben Zug.

---

## 8. Offene Fragen für den GPT-Gegenreview

1. ALSA-Capture vs. direkt libpipewire für v1 — ist der Compat-Weg robust genug,
   oder verdient PipeWire-native schon v1 (Latenz/Sample-Genauigkeit)?
2. libv4l-Abhängigkeit akzeptabel, oder MJPEG-Dekodierung selbst (stb_image/
   turbojpeg) für weniger Laufzeit-Deps?
3. Synchron/pull für v1 ausreichend, oder ist der Capture-Thread schon v1 nötig
   (Frame-Drops bei langsamem Netz-Forward)?
4. Reichen v4l2loopback + snd-aloop für ein ehrliches CI-Gate, oder brauchen wir
   Datei-basierte Fake-Backends hinter der Backend-Vtable?
