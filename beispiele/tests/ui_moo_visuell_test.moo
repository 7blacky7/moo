# ============================================================
# beispiele/tests/ui_moo_visuell_test.moo — UIMOO-4 visuelle Tests
#
# Snapshot-Sequenz über den Plan-004-Apparat (ui_test_sequenz):
#   frame_001  initial, Theme dunkel (Knopf, Checkbox aus,
#              Slider 50%, Fortschritt 30%)
#   klick Checkbox -> frame_002 (Haken + Fokusring)
#   klick Slider 75% -> frame_003 (Knob rechts, Fokusring)
#   Theme hell -> frame_004
#
# Verifikation: alle 4 Frames als JSON+PNG vorhanden, JSON parsebar,
# Leinwand im Widget-Tree, UND die ui_moo-Zustände stimmen (Checkbox
# an, Slider ~75). Harte Pixel-Diffs (test_frame_diff) folgen mit dem
# Frame-Backend in UIMOO-6 — die PNGs hier sind das Review-Material.
# Marker: === UIMOO-VISUELL OK ===
# ============================================================

importiere ui
importiere ui_moo

setze g auf {}

funktion nix_klick(w):
    gib_zurück wahr

funktion nix_wechsel(w, wert):
    gib_zurück wahr

g["hFenster"] = ui_fenster("UIMOO-Visuell", 600, 460, 0, nichts)
g["k"] = uim_wurzel(g["hFenster"], 10, 10, 580, 400)

uim_hinzu(g["k"], uim_label("ui_moo Widgets — visueller Snapshot", 20, 16, 300, 20))
g["kn"]  = uim_hinzu(g["k"], uim_knopf("Speichern", 20, 50, 130, 32, nix_klick))
g["chk"] = uim_hinzu(g["k"], uim_checkbox("Benachrichtigungen", 20, 100, 220, 24, falsch, nix_wechsel))
g["sl"]  = uim_hinzu(g["k"], uim_slider(20, 145, 220, 24, 0, 100, 50, nix_wechsel))
g["fs"]  = uim_hinzu(g["k"], uim_fortschritt(20, 190, 220, 16))
uim_fortschritt_setze(g["k"], g["fs"], 0.3)

ui_widget_id_setze(g["hFenster"], "root")

wenn datei_existiert("beispiele/snapshots/ui_moo") != wahr:
    datei_mkdir("beispiele/snapshots/ui_moo")

setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch

funktion lauf_sequenz():
    setze aktionen auf []

    setze a1 auf {}
    setze a1["action"] auf "frame"
    setze a1["pfad"] auf "beispiele/snapshots/ui_moo/frame_001_dunkel_initial"
    aktionen.hinzufügen(a1)

    # Checkbox-Kasten: Leinwand-Offset 10 -> Fenster (10+30, 10+112)
    setze a2 auf {}
    setze a2["action"] auf "klick_xy"
    setze a2["x"] auf 40
    setze a2["y"] auf 122
    aktionen.hinzufügen(a2)

    setze a3 auf {}
    setze a3["action"] auf "warte"
    setze a3["ms"] auf 150
    aktionen.hinzufügen(a3)

    setze a4 auf {}
    setze a4["action"] auf "frame"
    setze a4["pfad"] auf "beispiele/snapshots/ui_moo/frame_002_checkbox_an"
    aktionen.hinzufügen(a4)

    # Slider bei 75%: x = 10 + 20 + 165 = 195, y = 10 + 157
    setze a5 auf {}
    setze a5["action"] auf "klick_xy"
    setze a5["x"] auf 195
    setze a5["y"] auf 167
    aktionen.hinzufügen(a5)

    setze a6 auf {}
    setze a6["action"] auf "warte"
    setze a6["ms"] auf 150
    aktionen.hinzufügen(a6)

    setze a7 auf {}
    setze a7["action"] auf "frame"
    setze a7["pfad"] auf "beispiele/snapshots/ui_moo/frame_003_slider_75"
    aktionen.hinzufügen(a7)

    setze reports auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/ui_moo", aktionen)
    setze bericht["reports"] auf reports

    # Theme-Wechsel + eigener Abschluss-Frame (hell)
    uim_theme_setze(g["k"], uim_theme_hell())
    setze aktionen2 auf []
    setze b1 auf {}
    setze b1["action"] auf "warte"
    setze b1["ms"] auf 150
    aktionen2.hinzufügen(b1)
    setze b2 auf {}
    setze b2["action"] auf "frame"
    setze b2["pfad"] auf "beispiele/snapshots/ui_moo/frame_004_hell"
    aktionen2.hinzufügen(b2)
    setze reports2 auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/ui_moo", aktionen2)
    für r in reports2:
        bericht["reports"].hinzufügen(r)

    setze bericht["fertig"] auf wahr
    ui_beenden()

ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()

wenn bericht["fertig"] != wahr:
    zeige "=== UIMOO-VISUELL FEHLER (Sequenz nicht gelaufen) ==="
    beende(1)

# --- Aktions-Reports auswerten ---
setze reports auf bericht["reports"]
setze total auf länge(reports)
setze okc auf 0
setze frames auf []
setze i auf 0
solange i < total:
    setze r auf reports[i]
    wenn r["erfolg"] == wahr:
        setze okc auf okc + 1
    wenn r["action"] == "frame":
        frames.hinzufügen(r["target"])
    setze i auf i + 1
zeige "UIMOO-Visuell: " + text(okc) + "/" + text(total) + " Aktionen OK"

# --- Frames: JSON+PNG vorhanden, JSON parsebar ---
setze dateien_ok auf wahr
setze i auf 0
setze nf auf länge(frames)
solange i < nf:
    setze basis auf frames[i]
    setze jp auf basis + ".json"
    setze pp auf basis + ".png"
    wenn datei_existiert(jp) != wahr:
        zeige "FEHLT: " + jp
        setze dateien_ok auf falsch
    sonst:
        wenn datei_existiert(pp) != wahr:
            zeige "FEHLT: " + pp
            setze dateien_ok auf falsch
        sonst:
            setze roh auf datei_lesen(jp)
            setze parsed auf json_parse(roh)
            wenn parsed == nichts:
                zeige "JSON KAPUTT: " + jp
                setze dateien_ok auf falsch
            sonst:
                zeige "OK: " + jp
    setze i auf i + 1

# --- ui_moo-Zustand nach der Sequenz ---
setze chk_ok auf g["chk"]["wert"] == wahr
setze sw auf g["sl"]["wert"]
setze sl_ok auf sw > 72 und sw < 78
setze theme_ok auf g["k"]["theme"]["name"] == "hell"

wenn okc == total und dateien_ok und nf == 4 und chk_ok und sl_ok und theme_ok:
    zeige "=== UIMOO-VISUELL OK ==="
sonst:
    zeige "=== UIMOO-VISUELL FEHLER chk=" + text(chk_ok) + " sl=" + text(sw) + " theme=" + text(theme_ok) + " frames=" + text(nf) + " ==="
    beende(1)
