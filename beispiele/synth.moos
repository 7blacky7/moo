# ============================================================
# moo WAV-Synthesizer — generiert Sinus-Audio-Dateien
#
# Kompilieren: moo-compiler compile synth.moo -o synth
# Starten:     ./synth
#
# Erzeugt:
#   /tmp/a440.wav       1 s reiner 440 Hz Sinus
#   /tmp/tonleiter.wav  C-Dur Tonleiter C-D-E-F-G-A-H-C
#   /tmp/cmajor.wav     C-Dur Akkord (C+E+G gleichzeitig)
#   /tmp/entchen.wav    Alle meine Entchen (C-D-E-F-G-G-A-A-A-A-G)
# ============================================================

konstante SR auf 44100
konstante PI auf 3.14159265358979
konstante TWO_PI auf 6.28318530717958

# --- Sinus via Taylor-Reihe (4. Ordnung) mit Range-Reduktion ---
funktion my_cos(x):
    # Reduziere x auf [-PI, PI]
    setze v auf x
    solange v > PI:
        setze v auf v - TWO_PI
    solange v < (0 - PI):
        setze v auf v + TWO_PI
    setze x2 auf v * v
    gib_zurück 1 - x2 / 2 + x2 * x2 / 24 - x2 * x2 * x2 / 720

funktion my_sin(x):
    gib_zurück my_cos(x - 1.5707963267948966)

# --- Synth-API via Dict-State ---
funktion synth_neu():
    setze s auf {}
    s["samples"] = []
    gib_zurück s

# Fuegt einen reinen Sinus-Ton an: freq Hz, dauer Sekunden, amp 0..1
funktion synth_ton(s, freq, dauer, amp):
    setze n auf boden(dauer * SR)
    setze dt auf 1.0 / SR
    setze omega auf TWO_PI * freq
    setze t auf 0.0
    setze i auf 0
    setze sam auf s["samples"]
    solange i < n:
        sam.hinzufügen(amp * my_sin(omega * t))
        setze t auf t + dt
        setze i auf i + 1
    s["samples"] = sam

# Fuegt mehrere Frequenzen gleichzeitig an (Akkord)
funktion synth_akkord(s, freqs, dauer, amp):
    setze n auf boden(dauer * SR)
    setze dt auf 1.0 / SR
    setze sam auf s["samples"]
    setze i auf 0
    solange i < n:
        setze t auf i * dt
        setze summe auf 0.0
        setze j auf 0
        setze m auf freqs.länge()
        solange j < m:
            setze summe auf summe + my_sin(TWO_PI * freqs[j] * t)
            setze j auf j + 1
        sam.hinzufügen(amp * summe / m)
        setze i auf i + 1
    s["samples"] = sam

funktion synth_pause(s, dauer):
    setze n auf boden(dauer * SR)
    setze sam auf s["samples"]
    setze i auf 0
    solange i < n:
        sam.hinzufügen(0.0)
        setze i auf i + 1
    s["samples"] = sam

# ADSR-artige Envelope: lineares Attack und Release auf ALLE Samples
funktion synth_envelope(s, attack_s, release_s):
    setze sam auf s["samples"]
    setze n auf sam.länge()
    setze a auf boden(attack_s * SR)
    setze r auf boden(release_s * SR)
    setze i auf 0
    solange i < n:
        setze g auf 1.0
        wenn i < a:
            setze g auf i / a
        wenn i > (n - r):
            setze g auf (n - i) / r
        sam[i] = sam[i] * g
        setze i auf i + 1
    s["samples"] = sam

# --- WAV Writer ---
funktion push_le16(liste, v):
    setze x auf v
    wenn x < 0:
        setze x auf x + 65536
    liste.hinzufügen(x % 256)
    liste.hinzufügen(boden(x / 256) % 256)

funktion push_le32(liste, v):
    liste.hinzufügen(v % 256)
    liste.hinzufügen(boden(v / 256) % 256)
    liste.hinzufügen(boden(v / 65536) % 256)
    liste.hinzufügen(boden(v / 16777216) % 256)

funktion push_ascii(liste, s):
    setze bs auf bytes_zu_liste(s)
    setze i auf 0
    solange i < bs.länge():
        liste.hinzufügen(bs[i])
        setze i auf i + 1

funktion synth_speichern(s, pfad):
    setze sam auf s["samples"]
    setze n auf sam.länge()
    setze data_size auf n * 2  # 16-bit mono
    setze file_size auf 36 + data_size

    setze out auf []
    push_ascii(out, "RIFF")
    push_le32(out, file_size)
    push_ascii(out, "WAVE")

    # fmt chunk
    push_ascii(out, "fmt ")
    push_le32(out, 16)              # chunk size
    push_le16(out, 1)               # PCM
    push_le16(out, 1)               # mono
    push_le32(out, SR)              # sample rate
    push_le32(out, SR * 2)          # byte rate
    push_le16(out, 2)               # block align
    push_le16(out, 16)              # bits per sample

    # data chunk
    push_ascii(out, "data")
    push_le32(out, data_size)

    # Samples als int16 LE
    setze i auf 0
    solange i < n:
        setze v auf sam[i]
        # Clip
        wenn v > 1:
            setze v auf 1.0
        wenn v < (0 - 1):
            setze v auf 0 - 1.0
        setze iv auf boden(v * 32767)
        push_le16(out, iv)
        setze i auf i + 1

    datei_schreiben_bytes(pfad, out)

# --- Frequenzen der C-Dur-Tonleiter (4. Oktave) ---
konstante C auf 261.63
konstante D auf 293.66
konstante E auf 329.63
konstante F auf 349.23
konstante G auf 392.00
konstante A auf 440.00
konstante H auf 493.88
konstante C2 auf 523.25

# ============================================================
# Test 1: Reiner 440 Hz Sinus 1 Sekunde
# ============================================================
zeige "=== moo WAV-Synthesizer ==="
zeige ""
zeige "Test 1: 440 Hz Sinus 1s → /tmp/a440.wav"
setze s1 auf synth_neu()
synth_ton(s1, 440, 1.0, 0.7)
synth_envelope(s1, 0.02, 0.05)
synth_speichern(s1, "/tmp/a440.wav")
zeige "  samples=" + text(s1["samples"].länge()) + "  (erwartet 44100)"

# ============================================================
# Test 2: C-Dur Tonleiter
# ============================================================
zeige ""
zeige "Test 2: C-Dur Tonleiter → /tmp/tonleiter.wav"
setze s2 auf synth_neu()
setze noten auf [C, D, E, F, G, A, H, C2]
setze i auf 0
solange i < noten.länge():
    synth_ton(s2, noten[i], 0.3, 0.5)
    synth_pause(s2, 0.03)
    setze i auf i + 1
synth_envelope(s2, 0.01, 0.05)
synth_speichern(s2, "/tmp/tonleiter.wav")
zeige "  samples=" + text(s2["samples"].länge())

# ============================================================
# Test 3: C-Dur Akkord (C+E+G)
# ============================================================
zeige ""
zeige "Test 3: C-Dur Akkord → /tmp/cmajor.wav"
setze s3 auf synth_neu()
synth_akkord(s3, [C, E, G], 2.0, 0.9)
synth_envelope(s3, 0.05, 0.3)
synth_speichern(s3, "/tmp/cmajor.wav")
zeige "  samples=" + text(s3["samples"].länge())

# ============================================================
# Test 4: Alle meine Entchen
# ============================================================
zeige ""
zeige "Test 4: Alle meine Entchen → /tmp/entchen.wav"
setze s4 auf synth_neu()
# C D E F G G | A A A A G  | A A A A G | F F F F E E | D D D D C
# Dauer schematisch: Viertel=0.4s, Halb=0.8s
setze melodie auf [
    C, 0.4,
    D, 0.4,
    E, 0.4,
    F, 0.4,
    G, 0.8,
    G, 0.8,
    A, 0.4,
    A, 0.4,
    A, 0.4,
    A, 0.4,
    G, 1.2,
    A, 0.4,
    A, 0.4,
    A, 0.4,
    A, 0.4,
    G, 1.2,
    F, 0.4,
    F, 0.4,
    F, 0.4,
    F, 0.4,
    E, 0.8,
    E, 0.8,
    D, 0.4,
    D, 0.4,
    D, 0.4,
    D, 0.4,
    C, 1.6
]
setze i auf 0
solange i < melodie.länge():
    synth_ton(s4, melodie[i], melodie[i + 1] * 0.9, 0.5)
    synth_pause(s4, melodie[i + 1] * 0.1)
    setze i auf i + 2
synth_envelope(s4, 0.01, 0.1)
synth_speichern(s4, "/tmp/entchen.wav")
zeige "  samples=" + text(s4["samples"].länge())

zeige ""
zeige "=== Fertig ==="
