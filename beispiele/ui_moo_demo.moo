# ============================================================
# beispiele/ui_moo_demo.moo — Schaufenster für ui_moo (Schicht 3)
#
# Alle Widgets der ersten Generation in einem Fenster, mit
# Theme-Umschalter (dunkel <-> hell). Interaktiv:
#
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_moo_demo.moo
#   ./beispiele/ui_moo_demo
#
# Bedienung: alles klickbar; Tab wechselt den Fokus (Top-Level),
# space/Return aktiviert, Pfeiltasten bewegen Slider/Liste,
# Mausrad scrollt Liste + Scrollbereich.
# ============================================================

importiere ui
importiere ui_moo

setze g auf {}
g["klicks"] = 0

funktion status(text_neu):
    g["status"]["text"] = text_neu
    uim_neuzeichnen(g["k"])
    gib_zurück wahr

funktion auf_knopf(w):
    g["klicks"] = g["klicks"] + 1
    status("Knopf geklickt: " + text(g["klicks"]) + "x")
    gib_zurück wahr

funktion auf_checkbox(w, wert):
    status("Checkbox: " + text(wert))
    gib_zurück wahr

funktion auf_slider(w, wert):
    uim_fortschritt_setze(g["k"], g["fs"], wert / 100)
    status("Slider: " + text(wert))
    gib_zurück wahr

funktion auf_liste(w, idx):
    status("Auswahl: " + w["zeilen"][idx])
    gib_zurück wahr

funktion auf_theme(w):
    wenn g["k"]["theme"]["name"] == "dunkel":
        uim_theme_setze(g["k"], uim_theme_hell())
        w["text"] = "Theme: hell"
    sonst:
        uim_theme_setze(g["k"], uim_theme_dunkel())
        w["text"] = "Theme: dunkel"
    gib_zurück wahr

g["hFenster"] = ui_fenster("ui_moo — von moo für UI", 640, 480, 0, nichts)
g["k"] = uim_wurzel(g["hFenster"], 10, 10, 620, 460)

setze titel auf uim_hinzu(g["k"], uim_label("ui_moo Schaufenster", 0, 14, 620, 20))
uim_label_ausrichtung(titel, "mitte")

# Linkes Panel: Eingabe-Widgets
g["pn"] = uim_hinzu(g["k"], uim_panel("Steuerung", 20, 50, 280, 240))
uim_hinzu(g["pn"], uim_knopf("Klick mich", 20, 30, 140, 32, auf_knopf))
uim_hinzu(g["pn"], uim_checkbox("Benachrichtigungen", 20, 80, 230, 24, wahr, auf_checkbox))
uim_hinzu(g["pn"], uim_slider(20, 125, 230, 24, 0, 100, 30, auf_slider))
g["fs"] = uim_hinzu(g["pn"], uim_fortschritt(20, 165, 230, 16))
uim_fortschritt_setze(g["k"], g["fs"], 0.3)
uim_hinzu(g["pn"], uim_knopf("Theme: dunkel", 20, 195, 140, 30, auf_theme))

# Rechts: Liste mit vielen Zeilen (Rad/Up/Down)
setze zeilen auf []
setze i auf 1
solange i <= 40:
    zeilen.hinzufügen("Eintrag " + text(i))
    setze i auf i + 1
uim_hinzu(g["k"], uim_liste(330, 50, 270, 240, zeilen, auf_liste))

# Unten: Scrollbereich mit Inhalt über die Kante hinaus
g["sc"] = uim_hinzu(g["k"], uim_scroll(20, 310, 580, 100, 260))
uim_hinzu(g["sc"], uim_label("Scrollbarer Inhalt (Mausrad)", 10, 10, 300, 16))
uim_hinzu(g["sc"], uim_knopf("Oben", 10, 34, 100, 28, auf_knopf))
uim_hinzu(g["sc"], uim_knopf("Mitte", 10, 120, 100, 28, auf_knopf))
uim_hinzu(g["sc"], uim_knopf("Unten", 10, 210, 100, 28, auf_knopf))

g["status"] = uim_hinzu(g["k"], uim_label("Bereit.", 20, 430, 580, 18))

ui_zeige_nebenbei(g["hFenster"])
ui_laufen()
