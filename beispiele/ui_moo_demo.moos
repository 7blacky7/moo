# ============================================================
# beispiele/ui_moo_demo.moo — Schaufenster für ui_moo (Schicht 3)
#
# Alle Widgets der ersten Generation in einem Fenster, mit
# Theme-Umschalter hell/dunkel. Komplett moo-gezeichnet — kein
# einziges natives Control (nur Fenster + Leinwand als Host).
#
# Kompilieren + Starten:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_moo_demo.moo && ./beispiele/ui_moo_demo
#
# Headless-Smoke: UIMOO_DEMO_AUTO=1 beendet nach 800ms selbst.
# ============================================================

importiere ui
importiere ui_moo

setze g auf {}
g["auto_timer"] = nichts

funktion status(text_neu):
    g["status"]["text"] = text_neu
    uim_neuzeichnen(g["k"])
    gib_zurück wahr

funktion auf_speichern(w):
    status("Gespeichert!")
    gib_zurück wahr

funktion auf_checkbox(w, wert):
    wenn wert:
        status("Benachrichtigungen: an")
    sonst:
        status("Benachrichtigungen: aus")
    gib_zurück wahr

funktion auf_slider(w, wert):
    g["fs"]["wert"] = wert / 100
    status("Lautstärke: " + text(wert))
    gib_zurück wahr

funktion auf_auswahl(w, idx):
    status("Gewählt: " + w["zeilen"][idx])
    gib_zurück wahr

funktion auf_theme(w):
    wenn g["k"]["theme"]["name"] == "dunkel":
        uim_theme_setze(g["k"], uim_theme_hell())
        g["theme_knopf"]["text"] = "Theme: hell"
    sonst:
        uim_theme_setze(g["k"], uim_theme_dunkel())
        g["theme_knopf"]["text"] = "Theme: dunkel"
    gib_zurück wahr

funktion auto_ende():
    wenn g["auto_timer"] != nichts:
        ui_timer_entfernen(g["auto_timer"])
        g["auto_timer"] = nichts
    zeige "UIMOO-DEMO-OK"
    ui_beenden()
    gib_zurück falsch

g["hFenster"] = ui_fenster("ui_moo — Schaufenster", 640, 500, 0, nichts)
g["k"] = uim_wurzel(g["hFenster"], 10, 10, 620, 440)

uim_hinzu(g["k"], uim_label("ui_moo: moo-eigene Widgets, komplett selbst gezeichnet", 20, 14, 400, 20))

# Linkes Panel: Formular-Widgets
g["pn"] = uim_hinzu(g["k"], uim_panel("Einstellungen", 20, 44, 280, 240))
uim_hinzu(g["pn"], uim_knopf("Speichern", 16, 32, 120, 32, auf_speichern))
g["theme_knopf"] = uim_hinzu(g["pn"], uim_knopf("Theme: dunkel", 148, 32, 116, 32, auf_theme))
uim_hinzu(g["pn"], uim_checkbox("Benachrichtigungen", 16, 84, 240, 24, wahr, auf_checkbox))
uim_hinzu(g["pn"], uim_label("Lautstärke", 16, 126, 100, 16))
uim_hinzu(g["pn"], uim_slider(16, 148, 248, 24, 0, 100, 60, auf_slider))
g["fs"] = uim_hinzu(g["pn"], uim_fortschritt(16, 190, 248, 14))
g["fs"]["wert"] = 0.6

# Rechts: Liste mit vielen Zeilen (Rad scrollt, Klick/Up/Down wählt)
setze zeilen auf []
setze i auf 1
solange i <= 40:
    zeilen.hinzufügen("Eintrag " + text(i))
    setze i auf i + 1
uim_hinzu(g["k"], uim_liste(320, 44, 280, 240, zeilen, auf_auswahl))

# Unten: Scrollbereich mit Inhalt über die sichtbare Höhe hinaus
g["sc"] = uim_hinzu(g["k"], uim_scroll(20, 300, 580, 100, 260))
uim_hinzu(g["sc"], uim_label("Scrollbereich: Rad drehen —", 12, 10, 300, 16))
uim_hinzu(g["sc"], uim_knopf("Oben", 12, 34, 100, 28, auf_speichern))
uim_hinzu(g["sc"], uim_label("… weiter unten …", 12, 120, 200, 16))
uim_hinzu(g["sc"], uim_knopf("Tief unten", 12, 200, 120, 28, auf_speichern))

g["status"] = uim_hinzu(g["k"], uim_label("Bereit.", 20, 416, 400, 18))

ui_zeige_nebenbei(g["hFenster"])
wenn umgebung("UIMOO_DEMO_AUTO") != nichts:
    g["auto_timer"] = ui_timer_hinzu(800, auto_ende)
ui_laufen()
