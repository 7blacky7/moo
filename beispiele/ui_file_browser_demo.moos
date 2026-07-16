# ============================================================
# ui_file_browser_demo.moo — Showcase fuer setup_file_browser
#
# Primitiver File-Open-Dialog. Zeigt eine hart codierte Datei-Liste
# (da moo's Datei-I/O-Stdlib variiert). In einer echten App wird
# die Liste per verzeichnis_liste(...) gefuellt.
# ============================================================

importiere ui
importiere ui_komponenten

setze g auf {}


funktion cb_oeffnen():
    setze idx auf ui_liste_auswahl(g["listFiles"])
    wenn idx < 0:
        ui_label_setze(g["lblStatus"], "Bitte eine Datei waehlen")
    sonst:
        setze zeile auf ui_liste_zeile(g["listFiles"], idx)
        ui_label_setze(g["lblStatus"], "Geoeffnet: " + zeile[0])

funktion cb_schliessen():
    ui_beenden()


ui_baue(g, "Datei oeffnen", 480, 420, [
    [setup_file_browser, ["Dateien:", "/home/user", 460, 360]],
    [setup_status_bar,   ["Bereit", "0 Eintraege"]],
])


# Callbacks umbinden: setup_file_browser hat die Buttons mit
# Defaults erzeugt — wir schaffen hier eigene Knoepfe im gleichen
# Fenster und ueberschreiben die Stelle NICHT (die Default-Buttons
# bleiben einfach bestehen — in echtem Code wuerde man das API
# erweitern um callbacks).

# Demo-Liste fuellen
setze beispiele auf [
    "readme.txt",
    "daten.json",
    "notizen.md",
    "foto.jpg",
    "skript.moo",
]
für datei in beispiele:
    ui_liste_zeile_hinzu(g["listFiles"], [datei])


# Wir legen zusaetzlich einen funktionalen Oeffnen-Knopf an
ui_knopf(g["hFenster"], "Datei oeffnen", 260, 370, 120, 30, cb_oeffnen)
ui_knopf(g["hFenster"], "Fenster zu",    385, 370, 90,  30, cb_schliessen)


ui_zeige(g["hFenster"])
ui_laufen()
