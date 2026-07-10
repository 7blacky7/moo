# KI-Audio: FFT, Spektrogramm und WAV

Moo kann Audiodaten ohne zusätzliche Bibliothek als Tensor laden und in Merkmale für neuronale Netze umwandeln. Die Funktionen laufen auf der CPU, benötigen keine Audio-Hardware und sind bewusst nicht Teil von Autograd.

## Schnellstart

```moo
setze audio auf wav_lesen("ton.wav")
setze bild auf spektrogramm(audio["daten"], 1024, 256)
zeige audio["rate"]
zeige bild.form()
```

Das vollständige, deterministische Beispiel steht in `beispiele/ki_audio.moo`. Es erzeugt eine Tonleiter als WAV, liest sie zurück, berechnet Spektrogramme und trainiert einen Noten-Klassifikator. Start:

```bash
compiler/target/release/moo-compiler run beispiele/ki_audio.moo
```

## Funktionen

### `wav_lesen(pfad)` / `wav_read(path)`

Liest eine RIFF/WAVE-Datei und liefert ein Wörterbuch:

```moo
{"daten": Tensor[n], "rate": 44100}
```

Unterstützt wird unkomprimiertes 16-bit PCM mit einem oder zwei Kanälen. Stereo wird durch Mittelung beider Kanäle in Mono umgewandelt. Die Samples liegen als f32 im Bereich `[-1, 1)` vor. Unbekannte Formate, beschädigte Chunks und unvollständige Daten erzeugen eine erklärende Fehlermeldung.

### `fft(t)` / `spektrum(t)` / `spectrum(t)`

Berechnet die reelle FFT eines 1D-Tensors. Die Länge wird mit Nullen auf die nächste Zweierpotenz erweitert. Das Ergebnis hat die Form `[n/2+1, 2]`; die letzte Dimension enthält Real- und Imaginärteil.

```moo
setze komplex auf fft(samples)
```

### `spektrum_betrag(t)` / `magnitude_spectrum(t)`

Liefert das einseitige Betragsspektrum mit Form `[n/2+1]`.

### `spektrogramm(t, fenster, schritt)` / `spectrogram(t, window, hop)`

Teilt einen 1D-Audiotensor in überlappende Fenster, multipliziert jedes Fenster mit einer Hann-Funktion und berechnet `|STFT|`. Das Ergebnis hat die Form `[frames, bins]`, wobei `bins = nächste_zweierpotenz(fenster) / 2 + 1`.

```moo
setze merkmale auf spektrogramm(samples, 1024, 256)
```

Für Signale, die kürzer als ein Fenster sind, entsteht ein einzelner, mit Nullen aufgefüllter Frame. Ein unvollständiger Rest am Ende wird nicht als zusätzlicher Frame ausgegeben.

## Genauigkeit und Grenzen

- FFT und STFT rechnen intern in double und speichern Ergebnisse als f32-Tensor.
- Der Runtime-Harness vergleicht die FFT mit einer naiven DFT bei Fehler kleiner `1e-6` und prüft Parseval.
- Die Funktionen sind Feature-Extraktion und Dateidekodierung, daher nicht differenzierbar und nicht im Op-/Autograd-Register.
- WAV-Float, 24/32-bit PCM und komprimierte Codecs sind noch nicht unterstützt.
