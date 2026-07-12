# P016-O1: socketloser Moo-Level-Test der finalen Surface-Builtins.
# Kein importiere ui, kein Fenster, kein GTK/SDL/Xvfb.

importiere ui_moo_surface

setze ergebnis auf {}
ergebnis["fehler"] = 0

funktion pruef(name, ok):
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        ergebnis["fehler"] = ergebnis["fehler"] + 1
    gib_zurück ok

funktion rgba_ist(pixel, r, g, b, a):
    wenn pixel == nichts:
        gib_zurück falsch
    gib_zurück pixel["rot"] == r und pixel["gruen"] == g und pixel["blau"] == b und pixel["alpha"] == a

# Konstruktor, Clear und kanonischer FNV-1a-Hashvektor.
setze klein auf surface_new(2, 1)
pruef("create-2x1", klein != nichts)
pruef("surface-typ", typ_von(klein) == "Oberflaeche")
pruef("surface-text", text(klein) == "<Oberflaeche>")
pruef("clear-transparent-gruen", surface_clear(klein, 0, 255, 0, 0))
pruef("rect-rot-pixel0", surface_rect(klein, 0, 0, 1, 1, 255, 0, 0, 255))
pruef("hash-kanonisch", surface_hash(klein) == "e2337428034aea61")
pruef("pixel0-rot", rgba_ist(surface_read_pixel(klein, 0, 0), 255, 0, 0, 255))
pruef("pixel1-alpha0", rgba_ist(surface_read_pixel(klein, 1, 0), 0, 255, 0, 0))
pruef("oob-pixel-nichts", surface_read_pixel(klein, 2, 0) == nichts)
pruef("create-nullbreite", surface_new(0, 1) == nichts)

# Integergenaues Straight-Alpha source-over.
setze alpha_bild auf surface_new(1, 1)
surface_clear(alpha_bild, 10, 20, 30, 255)
surface_rect(alpha_bild, 0, 0, 1, 1, 110, 120, 130, 128)
pruef("alpha-opaque-dst", rgba_ist(surface_read_pixel(alpha_bild, 0, 0), 60, 70, 80, 255))
surface_clear(alpha_bild, 0, 0, 255, 128)
surface_rect(alpha_bild, 0, 0, 1, 1, 255, 0, 0, 128)
pruef("alpha-partial-dst", rgba_ist(surface_read_pixel(alpha_bild, 0, 0), 170, 0, 85, 192))

# Half-open Rechtecke und verschachtelte Clip-Intersection.
setze clip_bild auf surface_new(8, 8)
surface_clear(clip_bild, 0, 0, 0, 0)
pruef("clip-aussen-push", surface_clip_push(clip_bild, 1, 1, 6, 6))
pruef("clip-innen-push", surface_clip_push(clip_bild, 3, 0, 4, 4))
surface_rect(clip_bild, 0, 0, 8, 8, 255, 0, 0, 255)
pruef("clip-innen-links", rgba_ist(surface_read_pixel(clip_bild, 3, 1), 255, 0, 0, 255))
pruef("clip-innen-rechts", rgba_ist(surface_read_pixel(clip_bild, 6, 3), 255, 0, 0, 255))
pruef("clip-halfopen-x", rgba_ist(surface_read_pixel(clip_bild, 7, 2), 0, 0, 0, 0))
pruef("clip-halfopen-y", rgba_ist(surface_read_pixel(clip_bild, 4, 4), 0, 0, 0, 0))
pruef("clip-innen-pop", surface_clip_pop(clip_bild))
surface_line(clip_bild, 0, 5, 7, 5, 0, 0, 255, 255)
pruef("clip-restauriert-innen", rgba_ist(surface_read_pixel(clip_bild, 1, 5), 0, 0, 255, 255))
pruef("clip-restauriert-rechts", rgba_ist(surface_read_pixel(clip_bild, 6, 5), 0, 0, 255, 255))
pruef("clip-restauriert-aussen", rgba_ist(surface_read_pixel(clip_bild, 0, 5), 0, 0, 0, 0))
pruef("clip-root-pop", surface_clip_pop(clip_bild))
pruef("clip-underflow-falsch", surface_clip_pop(clip_bild) == falsch)

# Linienbreite im Moo-Surface-Adapter: dominant-axis, zentriert und ohne Doppelblend.
setze linien_k auf uim_surface_wurzel(14, 14)
setze linien_z auf uim_surface_handle(linien_k)
pruef("surface-roundrect-cap-ehrlich", uim_backend_surface()["faehigkeiten"]["rechteck_rund"] == falsch)
uim_surface_leeren(linien_k, 0, 0, 0, 0)
pruef("linie-farbe-opak", _uim_farbe(linien_k, linien_z, [200, 100, 50, 255]))
pruef("linie-vertikal-width3", _uim_zf_linie(linien_k, linien_z, 5, 2, 5, 8, 3))
pruef("linie-vertikal-links", rgba_ist(uim_surface_pixel(linien_k, 4, 5), 200, 100, 50, 255))
pruef("linie-vertikal-mitte", rgba_ist(uim_surface_pixel(linien_k, 5, 5), 200, 100, 50, 255))
pruef("linie-vertikal-rechts", rgba_ist(uim_surface_pixel(linien_k, 6, 5), 200, 100, 50, 255))
pruef("linie-vertikal-aussen", rgba_ist(uim_surface_pixel(linien_k, 3, 5), 0, 0, 0, 0))

uim_surface_leeren(linien_k, 0, 0, 0, 0)
pruef("linie-farbe-halb", _uim_farbe(linien_k, linien_z, [200, 100, 50, 128]))
pruef("linie-diagonal-width3", _uim_zf_linie(linien_k, linien_z, 2, 2, 8, 8, 3))
setze linien_pixel auf 0
setze nur_einmal_geblendet auf wahr
setze scan_y auf 0
solange scan_y < 14:
    setze scan_x auf 0
    solange scan_x < 14:
        setze scan_pixel auf uim_surface_pixel(linien_k, scan_x, scan_y)
        wenn scan_pixel["alpha"] != 0:
            setze linien_pixel auf linien_pixel + 1
            wenn rgba_ist(scan_pixel, 200, 100, 50, 128) == falsch:
                setze nur_einmal_geblendet auf falsch
        setze scan_x auf scan_x + 1
    setze scan_y auf scan_y + 1
pruef("linie-diagonal-21-pixel", linien_pixel == 21)
pruef("linie-diagonal-kein-doppelblend", nur_einmal_geblendet)

# Primitive: Diagonale, Kreis und abgerundetes Rechteck.
setze formen auf surface_new(16, 16)
surface_clear(formen, 0, 0, 0, 0)
surface_line(formen, 0, 0, 4, 4, 1, 2, 3, 255)
pruef("linie-start", rgba_ist(surface_read_pixel(formen, 0, 0), 1, 2, 3, 255))
pruef("linie-mitte", rgba_ist(surface_read_pixel(formen, 2, 2), 1, 2, 3, 255))
pruef("linie-ende", rgba_ist(surface_read_pixel(formen, 4, 4), 1, 2, 3, 255))
surface_circle(formen, 8, 8, 2, 9, 8, 7, 255)
pruef("kreis-zentrum", rgba_ist(surface_read_pixel(formen, 8, 8), 9, 8, 7, 255))
pruef("kreis-kardinal", rgba_ist(surface_read_pixel(formen, 10, 8), 9, 8, 7, 255))
surface_roundrect(formen, 1, 10, 7, 5, 2, 4, 5, 6, 255)
pruef("roundrect-zentrum", rgba_ist(surface_read_pixel(formen, 4, 12), 4, 5, 6, 255))
pruef("roundrect-aussenecke", rgba_ist(surface_read_pixel(formen, 1, 10), 0, 0, 0, 0))

# Snapshot ist eine tiefe Momentaufnahme und nutzt bestehendes Frame-Diff.
setze vor auf surface_snapshot_to_frame(formen)
setze hash_vor auf surface_hash(formen)
surface_rect(formen, 15, 15, 1, 1, 200, 100, 50, 255)
setze nach auf surface_snapshot_to_frame(formen)
setze differenz auf test_frame_diff(vor, nach)
pruef("snapshot-frame", vor != nichts und nach != nichts)
pruef("snapshot-tief", differenz["geaenderte_pixel"] == 1)
pruef("hash-aendert", surface_hash(formen) != hash_vor)

wenn ergebnis["fehler"] == 0:
    zeige "P016-O1-SURFACE-PRIMITIVES-OK"
sonst:
    zeige "P016-O1-SURFACE-PRIMITIVES-FEHLER " + text(ergebnis["fehler"])
    wirf "P016-O1 Surface-Primitivvertrag verletzt"
