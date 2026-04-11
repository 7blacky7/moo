# ============================================================
# moo MIDI-File-Parser + Renderer (.mid → .wav)
#
# Kompilieren: moo-compiler compile midi_player.moo -o midi_player
# Starten:     ./midi_player
#
# Flow:
#   1. Baut eine Test-MIDI (/tmp/test.mid) mit einer C-Dur
#      Tonleiter und drei Akkord-Noten (Polyphonie-Test)
#   2. Parst die MIDI komplett (Header + alle Tracks + Events)
#   3. Merged alle Tracks zu einer Zeitlinie
#   4. Rendert zu /tmp/rendered.wav mit Sinus-Oszillator +
#      Envelope, polyphon durch Addition
# ============================================================

konstante SR auf 44100
konstante TWO_PI auf 6.28318530717958
konstante PI auf 3.14159265358979

# --- Sinus via Taylor (mit Range-Reduktion) ---
funktion my_cos(x):
    setze v auf x
    solange v > PI:
        setze v auf v - 2 * PI
    solange v < (0 - PI):
        setze v auf v + 2 * PI
    setze x2 auf v * v
    gib_zurück 1 - x2 / 2 + x2 * x2 / 24 - x2 * x2 * x2 / 720

funktion my_sin(x):
    gib_zurück my_cos(x - 1.5707963267948966)

# Freq aus MIDI-Notennummer: 440 * 2^((n-69)/12)
funktion note_freq(n):
    gib_zurück 440 * (2 ** ((n - 69) / 12))

# --- Big-Endian Byte-Reader ---
funktion u16be(bs, off):
    gib_zurück bs[off] * 256 + bs[off + 1]

funktion u32be(bs, off):
    gib_zurück bs[off] * 16777216 + bs[off + 1] * 65536 + bs[off + 2] * 256 + bs[off + 3]

# Variable-Length-Quantity: bis zu 4 bytes, MSB=continue
# Gibt [wert, bytes_gelesen]
funktion read_vlq(bs, off):
    setze v auf 0
    setze n auf 0
    solange n < 4:
        setze b auf bs[off + n]
        setze v auf v * 128 + (b % 128)
        setze n auf n + 1
        wenn b < 128:
            gib_zurück [v, n]
    gib_zurück [v, n]

# --- Big-Endian Writer (fuer Test-MIDI) ---
funktion push_u32be(liste, v):
    liste.hinzufügen(boden(v / 16777216) % 256)
    liste.hinzufügen(boden(v / 65536) % 256)
    liste.hinzufügen(boden(v / 256) % 256)
    liste.hinzufügen(v % 256)

funktion push_u16be(liste, v):
    liste.hinzufügen(boden(v / 256) % 256)
    liste.hinzufügen(v % 256)

funktion push_vlq(liste, v):
    wenn v < 128:
        liste.hinzufügen(v)
        gib_zurück nichts
    wenn v < 16384:
        liste.hinzufügen(128 + boden(v / 128))
        liste.hinzufügen(v % 128)
        gib_zurück nichts
    wenn v < 2097152:
        liste.hinzufügen(128 + boden(v / 16384) % 128)
        liste.hinzufügen(128 + boden(v / 128) % 128)
        liste.hinzufügen(v % 128)
        gib_zurück nichts
    liste.hinzufügen(128 + boden(v / 2097152) % 128)
    liste.hinzufügen(128 + boden(v / 16384) % 128)
    liste.hinzufügen(128 + boden(v / 128) % 128)
    liste.hinzufügen(v % 128)

funktion push_ascii(liste, s):
    setze bs auf bytes_zu_liste(s)
    setze i auf 0
    solange i < bs.länge():
        liste.hinzufügen(bs[i])
        setze i auf i + 1

# --- Test-MIDI-Builder: eine C-Dur Tonleiter + ein C-Dur Akkord ---
funktion baue_test_midi(pfad):
    setze out auf []

    # MThd Header
    push_ascii(out, "MThd")
    push_u32be(out, 6)
    push_u16be(out, 0)    # format 0 (single track)
    push_u16be(out, 1)    # 1 track
    push_u16be(out, 480)  # 480 ticks per quarter

    # Build track body
    setze tr auf []
    # Set Tempo (500000 us/quarter = 120 BPM)
    push_vlq(tr, 0)
    tr.hinzufügen(0xFF)
    tr.hinzufügen(0x51)
    tr.hinzufügen(0x03)
    tr.hinzufügen(0x07)  # 0x07A120 = 500000
    tr.hinzufügen(0xA1)
    tr.hinzufügen(0x20)

    # Melodie: C-Dur Tonleiter (60..72 Ganzer Ton je Viertel)
    setze noten auf [60, 62, 64, 65, 67, 69, 71, 72]
    setze i auf 0
    solange i < noten.länge():
        # Note ON delta=0 (bei erstem) / 0 zwischen (da wir ON direkt nach OFF)
        push_vlq(tr, 0)
        tr.hinzufügen(0x90)
        tr.hinzufügen(noten[i])
        tr.hinzufügen(100)
        # Note OFF nach 480 ticks (1 Viertel = 0,5 s)
        push_vlq(tr, 480)
        tr.hinzufügen(0x80)
        tr.hinzufügen(noten[i])
        tr.hinzufügen(0)
        setze i auf i + 1

    # C-Dur Akkord (C, E, G) fuer 2 Viertel
    push_vlq(tr, 240)
    tr.hinzufügen(0x90)
    tr.hinzufügen(60)
    tr.hinzufügen(100)
    push_vlq(tr, 0)
    tr.hinzufügen(0x90)
    tr.hinzufügen(64)
    tr.hinzufügen(100)
    push_vlq(tr, 0)
    tr.hinzufügen(0x90)
    tr.hinzufügen(67)
    tr.hinzufügen(100)
    push_vlq(tr, 960)
    tr.hinzufügen(0x80)
    tr.hinzufügen(60)
    tr.hinzufügen(0)
    push_vlq(tr, 0)
    tr.hinzufügen(0x80)
    tr.hinzufügen(64)
    tr.hinzufügen(0)
    push_vlq(tr, 0)
    tr.hinzufügen(0x80)
    tr.hinzufügen(67)
    tr.hinzufügen(0)

    # End of Track meta-event
    push_vlq(tr, 0)
    tr.hinzufügen(0xFF)
    tr.hinzufügen(0x2F)
    tr.hinzufügen(0x00)

    # MTrk Header + Body
    push_ascii(out, "MTrk")
    push_u32be(out, tr.länge())
    setze k auf 0
    solange k < tr.länge():
        out.hinzufügen(tr[k])
        setze k auf k + 1

    datei_schreiben_bytes(pfad, out)

# --- MIDI Parser ---
# Gibt {format, division, tracks: [[event, ...], ...]} zurueck
# Event = {tick, typ, note?, vel?, us_per_qn?}
funktion parse_midi(bs):
    wenn bs[0] != 77 oder bs[1] != 84 oder bs[2] != 104 oder bs[3] != 100:
        zeige "Kein MThd"
    setze hdr_len auf u32be(bs, 4)
    setze format auf u16be(bs, 8)
    setze ntracks auf u16be(bs, 10)
    setze division auf u16be(bs, 12)
    zeige "  MThd: format=" + text(format) + " tracks=" + text(ntracks) + " div=" + text(division)

    setze pos auf 8 + hdr_len
    setze tracks auf []

    setze ti auf 0
    solange ti < ntracks:
        # "MTrk"
        wenn bs[pos] != 77 oder bs[pos + 1] != 84 oder bs[pos + 2] != 114 oder bs[pos + 3] != 107:
            zeige "Kein MTrk an pos=" + text(pos)
        setze tlen auf u32be(bs, pos + 4)
        setze tstart auf pos + 8
        setze tend auf tstart + tlen

        setze events auf []
        setze tp auf tstart
        setze running_status auf 0
        setze tick auf 0

        solange tp < tend:
            setze dr auf read_vlq(bs, tp)
            setze dt auf dr[0]
            setze tp auf tp + dr[1]
            setze tick auf tick + dt

            setze first auf bs[tp]
            setze status auf first
            wenn first < 128:
                setze status auf running_status
            sonst:
                setze tp auf tp + 1
            setze running_status auf status

            setze hi auf boden(status / 16)

            wenn status == 0xFF:
                # Meta-Event
                setze mt auf bs[tp]
                setze tp auf tp + 1
                setze lr auf read_vlq(bs, tp)
                setze llen auf lr[0]
                setze tp auf tp + lr[1]
                wenn mt == 0x51 und llen == 3:
                    setze us auf bs[tp] * 65536 + bs[tp + 1] * 256 + bs[tp + 2]
                    setze ev auf {}
                    ev["tick"] = tick
                    ev["typ"] = "tempo"
                    ev["us_per_qn"] = us
                    events.hinzufügen(ev)
                setze tp auf tp + llen
            sonst:
                wenn status == 0xF0 oder status == 0xF7:
                    setze lr auf read_vlq(bs, tp)
                    setze tp auf tp + lr[1] + lr[0]
                sonst:
                    wenn hi == 0x8 oder hi == 0x9:
                        setze note auf bs[tp]
                        setze vel auf bs[tp + 1]
                        setze tp auf tp + 2
                        setze ev auf {}
                        ev["tick"] = tick
                        setze typ auf "off"
                        wenn hi == 0x9 und vel > 0:
                            setze typ auf "on"
                        ev["typ"] = typ
                        ev["note"] = note
                        ev["vel"] = vel
                        events.hinzufügen(ev)
                    sonst:
                        wenn hi == 0xA oder hi == 0xB oder hi == 0xE:
                            setze tp auf tp + 2
                        sonst:
                            wenn hi == 0xC oder hi == 0xD:
                                setze tp auf tp + 1

        tracks.hinzufügen(events)
        setze pos auf tend
        setze ti auf ti + 1

    setze erg auf {}
    erg["format"] = format
    erg["division"] = division
    erg["tracks"] = tracks
    gib_zurück erg

# --- Zeitlinien-Builder: ticks → seconds, unter Beachtung von Tempo-Wechseln ---
# Pro Event: absolute Zeit in Sekunden zuweisen
funktion tick_zu_sekunden(tracks, division):
    # Sammle alle events mit Track-Index, sortiere nach tick
    setze alle auf []
    setze ti auf 0
    solange ti < tracks.länge():
        setze evs auf tracks[ti]
        setze i auf 0
        solange i < evs.länge():
            setze e auf evs[i]
            e["track"] = ti
            alle.hinzufügen(e)
            setze i auf i + 1
        setze ti auf ti + 1

    # Einfache Insertion-Sort nach tick (Format 0 hat eh nur 1 Track)
    setze n auf alle.länge()
    setze i auf 1
    solange i < n:
        setze j auf i
        solange j > 0 und alle[j - 1]["tick"] > alle[j]["tick"]:
            setze tmp auf alle[j - 1]
            alle[j - 1] = alle[j]
            alle[j] = tmp
            setze j auf j - 1
        setze i auf i + 1

    # Tempo-Map: start mit default 500 000 us/quarter
    setze us_pqn auf 500000
    setze last_tick auf 0
    setze last_sec auf 0.0
    setze ausgabe auf []
    setze i auf 0
    solange i < n:
        setze e auf alle[i]
        setze t auf e["tick"]
        setze sec_per_tick auf (us_pqn / 1000000) / division
        setze sec auf last_sec + (t - last_tick) * sec_per_tick
        e["sec"] = sec
        wenn e["typ"] == "tempo":
            setze us_pqn auf e["us_per_qn"]
            setze last_tick auf t
            setze last_sec auf sec
        sonst:
            ausgabe.hinzufügen(e)
        setze i auf i + 1
    gib_zurück ausgabe

# --- Renderer ---
funktion render(ev_liste):
    # Matche NOTE_ON/OFF zu (note, start_sec, end_sec)
    setze noten auf []
    setze offen auf {}
    setze i auf 0
    solange i < ev_liste.länge():
        setze e auf ev_liste[i]
        setze k auf "n" + text(e["note"])
        wenn e["typ"] == "on":
            setze o auf {}
            o["start"] = e["sec"]
            o["vel"] = e["vel"]
            offen[k] = o
        wenn e["typ"] == "off":
            setze o auf offen[k]
            wenn o != nichts:
                setze n auf {}
                n["note"] = e["note"]
                n["start"] = o["start"]
                n["end"] = e["sec"]
                n["vel"] = o["vel"]
                noten.hinzufügen(n)
        setze i auf i + 1

    # Finde Dauer
    setze max_sec auf 0.0
    setze i auf 0
    solange i < noten.länge():
        wenn noten[i]["end"] > max_sec:
            setze max_sec auf noten[i]["end"]
        setze i auf i + 1

    setze total auf boden((max_sec + 0.5) * SR)
    zeige "  Render: " + text(noten.länge()) + " Noten, " + text(max_sec) + " s, " + text(total) + " samples"

    # Mix-Buffer (Floats)
    setze mix auf []
    setze i auf 0
    solange i < total:
        mix.hinzufügen(0.0)
        setze i auf i + 1

    # Addiere jede Note als Sinus mit Envelope
    setze i auf 0
    solange i < noten.länge():
        setze n auf noten[i]
        setze freq auf note_freq(n["note"])
        setze start_s auf boden(n["start"] * SR)
        setze end_s auf boden(n["end"] * SR)
        setze len_s auf end_s - start_s
        setze amp auf n["vel"] / 127 * 0.3
        setze omega auf TWO_PI * freq
        setze dt auf 1.0 / SR
        # Attack/Release
        setze atk auf 200
        setze rel auf 400
        wenn atk > len_s / 4:
            setze atk auf boden(len_s / 4)
        wenn rel > len_s / 4:
            setze rel auf boden(len_s / 4)
        setze k auf 0
        solange k < len_s:
            setze g auf 1.0
            wenn k < atk:
                setze g auf k / atk
            wenn k > (len_s - rel):
                setze g auf (len_s - k) / rel
            setze s auf g * amp * my_sin(omega * k * dt)
            mix[start_s + k] = mix[start_s + k] + s
            setze k auf k + 1
        setze i auf i + 1

    gib_zurück mix

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

funktion wav_schreiben(pfad, samples):
    setze n auf samples.länge()
    setze data_size auf n * 2
    setze file_size auf 36 + data_size
    setze out auf []
    push_ascii(out, "RIFF")
    push_le32(out, file_size)
    push_ascii(out, "WAVE")
    push_ascii(out, "fmt ")
    push_le32(out, 16)
    push_le16(out, 1)
    push_le16(out, 1)
    push_le32(out, SR)
    push_le32(out, SR * 2)
    push_le16(out, 2)
    push_le16(out, 16)
    push_ascii(out, "data")
    push_le32(out, data_size)
    setze i auf 0
    solange i < n:
        setze v auf samples[i]
        wenn v > 1:
            setze v auf 1.0
        wenn v < (0 - 1):
            setze v auf 0 - 1.0
        push_le16(out, boden(v * 32767))
        setze i auf i + 1
    datei_schreiben_bytes(pfad, out)

# --- Main ---
zeige "=== moo MIDI-Player ==="
zeige ""
zeige "Schritt 1: Test-MIDI bauen"
baue_test_midi("/tmp/test.mid")
zeige "  /tmp/test.mid geschrieben"

zeige ""
zeige "Schritt 2: MIDI parsen"
setze bs auf datei_lesen_bytes("/tmp/test.mid")
zeige "  Groesse: " + text(bs.länge()) + " bytes"
setze mid auf parse_midi(bs)

setze anzahl_ev auf 0
setze i auf 0
solange i < mid["tracks"].länge():
    setze anzahl_ev auf anzahl_ev + mid["tracks"][i].länge()
    setze i auf i + 1
zeige "  Total events: " + text(anzahl_ev)

zeige ""
zeige "Schritt 3: Zeitlinie bauen"
setze ev_liste auf tick_zu_sekunden(mid["tracks"], mid["division"])
zeige "  Noten-Events: " + text(ev_liste.länge())

zeige ""
zeige "Schritt 4: Rendern"
setze samples auf render(ev_liste)

zeige ""
zeige "Schritt 5: WAV schreiben"
wav_schreiben("/tmp/rendered.wav", samples)
zeige "  /tmp/rendered.wav geschrieben"

zeige ""
zeige "=== Fertig ==="
