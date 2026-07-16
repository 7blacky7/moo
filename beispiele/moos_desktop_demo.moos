# ============================================================
# beispiele/moos_desktop_demo.moos — MOOS-Desktop-Demo
#
# Ein interaktiver Vorgeschmack auf MOOS OS - vollstaendig mit
# Moo Code gebaut. Bootscreen, Desktop, Fensterverwaltung,
# Startmenue, Taskleiste, Dateimanager, Terminal, Einstellungen,
# Systemmonitor, Moo-Code-Runner (Simulation), AI Garden,
# Benachrichtigungen und Energieoptionen — sichtbar verbunden,
# alles bedienbar. Simulierte Teile sind als DEMO gekennzeichnet.
#
# Bedienung Boot: ESC ueberspringen, F2 Firmware, F12 Bootmenue,
# Klick aufs Logo: Version. Desktop: Rechtsklick = Kontextmenue,
# Icon 1x Klick = waehlen, Klick auf gewaehltes = oeffnen.
# Fenster: Titelleiste ziehen, - minimiert, x schliesst,
# Griff unten rechts = Groesse aendern. Terminal: HILFE tippen.
# ============================================================

importiere ui
importiere ui_moo_effects

setze G auf {}
G["b"] = 1000
G["h"] = 620
G["leiste_h"] = 44
G["frame"] = nichts
G["modus"] = "boot"
G["boot_tick"] = 0
G["memtest_tick"] = 0
G["tick"] = 0
G["uhr"] = ""
G["drag"] = nichts
G["resize"] = nichts
G["off_x"] = 0
G["off_y"] = 0
G["ref_x"] = 0
G["ref_y"] = 0
G["start_offen"] = falsch
G["noti_offen"] = falsch
G["kontext"] = nichts
G["hover_icon"] = 0 - 1
G["gewaehlt_icon"] = 0 - 1
G["wp_aktiv"] = 1
G["grow_anim"] = 0
G["safe_growth"] = falsch
G["naechste_id"] = 1
G["diag_nach_boot"] = falsch

# ---------------- Benachrichtigungen ----------------

funktion noti_push(text):
    setze n auf {}
    n["text"] = text
    n["rest"] = 12
    G["noti"].hinzufügen(n)
    wenn länge(G["noti"]) > 24:
        setze neue auf []
        setze i auf länge(G["noti"]) - 24
        solange i < länge(G["noti"]):
            neue.hinzufügen(G["noti"][i])
            setze i auf i + 1
        G["noti"] = neue
    gib_zurück wahr

# ---------------- Fenster/App-Verwaltung ----------------

funktion app_neu(typ, titel, x, y, b, h, tr, tg, tb, seed):
    setze f auf {}
    f["id"] = G["naechste_id"]
    G["naechste_id"] = G["naechste_id"] + 1
    f["typ"] = typ
    f["titel"] = titel
    f["x"] = x
    f["y"] = y
    f["b"] = b
    f["h"] = h
    f["sichtbar"] = wahr
    setze e auf uim_effekt_neu()
    uim_effekt_ecken_setze(e, 12, 12, 12, 12)
    uim_effekt_schatten_setze(e, 3, 8, 8, 2, 0, 0, 0, 120)
    uim_effekt_hintergrund_setze(e, 8, 336, tr, tg, tb, 255, 110, 8, seed)
    f["aufl"] = uim_effekt_aufloesen(e, 255)
    gib_zurück f

funktion app_finde(typ):
    für f in G["fenster"]:
        wenn f["typ"] == typ:
            gib_zurück f
    gib_zurück nichts

funktion desk_nach_vorn(f):
    setze neue auf []
    für g in G["fenster"]:
        wenn g["id"] != f["id"]:
            neue.hinzufügen(g)
    neue.hinzufügen(f)
    G["fenster"] = neue

funktion desk_vorderstes():
    setze i auf länge(G["fenster"]) - 1
    solange i >= 0:
        setze f auf G["fenster"][i]
        wenn f["sichtbar"]:
            gib_zurück f
        setze i auf i - 1
    gib_zurück nichts

funktion app_oeffne(typ):
    setze f auf app_finde(typ)
    wenn f != nichts:
        f["sichtbar"] = wahr
        desk_nach_vorn(f)
        gib_zurück f
    setze nf auf nichts
    wenn typ == "monitor":
        setze nf auf app_neu(typ, "GROWBOX MONITOR", 80, 60, 380, 300, 84, 180, 150, 101)
    wenn typ == "greenhouse":
        setze nf auf app_neu(typ, "GREENHOUSE", 300, 190, 330, 220, 120, 230, 160, 102)
    wenn typ == "moosik":
        setze nf auf app_neu(typ, "MOOSIK-PLAYER", 560, 100, 344, 240, 255, 120, 190, 103)
    wenn typ == "rootsystem":
        setze nf auf app_neu(typ, "ROOTSYSTEM", 180, 120, 380, 280, 110, 170, 235, 104)
    wenn typ == "terminal":
        setze nf auf app_neu(typ, "TERMINAL", 240, 150, 430, 260, 30, 44, 38, 105)
    wenn typ == "einstellungen":
        setze nf auf app_neu(typ, "DARSTELLUNG", 340, 90, 360, 300, 230, 200, 120, 106)
    wenn typ == "moocode":
        setze nf auf app_neu(typ, "MOO CODE", 140, 90, 430, 330, 160, 140, 235, 107)
    wenn typ == "aigarden":
        setze nf auf app_neu(typ, "AI GARDEN", 420, 140, 400, 300, 130, 220, 110, 108)
    wenn nf == nichts:
        gib_zurück nichts
    G["fenster"].hinzufügen(nf)
    noti_push(nf["titel"] + " GESPROSST")
    gib_zurück nf

# ---------------- Wallpaper + Icons ----------------

funktion desk_wallpaper(z):
    setze wp auf G["wallpaper"]
    wenn G["wp_aktiv"] == 2 und G["wallpaper2"] != nichts:
        setze wp auf G["wallpaper2"]
    wenn wp != nichts und G["safe_growth"] == falsch:
        surface_blit_frame(z, 0, 0, wp)
    sonst:
        setze y auf 0
        solange y < G["h"]:
            setze t auf y * 100 / G["h"]
            setze wr auf boden(24 + 10 * t / 100)
            setze wg auf boden(34 + 30 * t / 100)
            setze wb auf boden(30 + 16 * t / 100)
            surface_rect(z, 0, y, G["b"], 4, wr, wg, wb, 255)
            setze y auf y + 4
        wenn G["safe_growth"]:
            surface_text(z, 20, G["h"] - 70, 1, "SAFE GROWTH MODUS", 200, 210, 200, 255)
    desk_icons(z)
    gib_zurück wahr

funktion desk_icons(z):
    setze alpha_faktor auf 255
    wenn G["grow_anim"] > 0:
        setze alpha_faktor auf 255 - G["grow_anim"] * 42
        wenn alpha_faktor < 0:
            setze alpha_faktor auf 0
    setze i auf 0
    solange i < länge(G["icons"]):
        setze ic auf G["icons"][i]
        setze a auf boden(46 * alpha_faktor / 255)
        setze ta auf alpha_faktor
        wenn i == G["hover_icon"]:
            setze a auf boden(86 * alpha_faktor / 255)
        wenn i == G["hover_icon"]:
            surface_roundrect(z, ic["x"] - 2, ic["y"] - 2, 60, 60, 12, 255, 255, 255, a)
        wenn i == G["gewaehlt_icon"]:
            surface_roundrect(z, ic["x"] - 3, ic["y"] - 3, 62, 62, 12, 160, 230, 180, boden(120 * alpha_faktor / 255))
        setze icf auf G["icon_frames"][ic["typ"]]
        wenn icf != nichts und alpha_faktor > 200:
            surface_blit_frame(z, ic["x"], ic["y"], icf)
        sonst:
            surface_roundrect(z, ic["x"] + 6, ic["y"] + 6, 44, 44, 10, 255, 255, 255, boden(60 * alpha_faktor / 255))
        surface_text(z, ic["x"] - 8, ic["y"] + 60, 1, ic["name"], 34, 52, 40, ta)
        setze i auf i + 1
    gib_zurück wahr

# ---------------- App-Inhalte ----------------

funktion inhalt_monitor(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    surface_text(z, fx + 16, fy + 44, 1, "GROWBOX LOCKED. INTEGRITY OK.", 235, 250, 242, 255)
    surface_text(z, fx + 16, fy + 64, 1, "SEED OK - ROOTSYSTEM MONTIERT", 220, 240, 230, 255)
    # CPU-Verlauf (Demo-Werte, tickt live)
    surface_text(z, fx + 16, fy + 92, 1, "CPU VERLAUF", 210, 235, 220, 255)
    setze gx auf fx + 16
    setze i auf 0
    solange i < 24:
        setze hval auf 8 + (i * 37 + G["tick"] * 13) % 40
        surface_rect(z, gx + i * 9, fy + 150 - hval, 6, hval, 120, 220, 160, 190)
        setze i auf i + 1
    # Sprout-Liste = echte offene Fenster
    surface_text(z, fx + 16, fy + 168, 1, "SPROUTS AKTIV " + text(länge(G["fenster"])), 235, 250, 242, 255)
    setze reihe auf 0
    für g in G["fenster"]:
        wenn reihe < 5:
            setze status auf "AKTIV"
            wenn g["sichtbar"] == falsch:
                setze status auf "RUHT"
            surface_text(z, fx + 24, fy + 190 + reihe * 16, 1, "PID " + text(g["id"]) + " " + g["titel"] + " - " + status, 220, 240, 230, 255)
            setze reihe auf reihe + 1
    gib_zurück wahr

funktion inhalt_greenhouse(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    surface_text(z, fx + 16, fy + 44, 1, "x MOOS-LEISTE BAUEN", 240, 250, 242, 255)
    surface_text(z, fx + 16, fy + 66, 1, "x SAT-BLUR BUTTERWEICH", 240, 250, 242, 255)
    surface_text(z, fx + 16, fy + 88, 1, "x DESKTOP-ERFAHRUNG", 240, 250, 242, 255)
    surface_text(z, fx + 16, fy + 110, 1, "- MYCELIUM-AGENTENBUS", 240, 250, 242, 255)
    surface_text(z, fx + 16, fy + 132, 1, "- NUTRIENTS-PAKETMANAGER", 240, 250, 242, 255)
    surface_text(z, fx + 16, fy + 168, 1, "GREENHOUSE - GESCHUETZTER BEREICH ZUM WACHSEN", 220, 240, 230, 255)
    gib_zurück wahr

funktion inhalt_moosik(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    surface_text(z, fx + 16, fy + 44, 1, "NOW PLAYING - MOSS GARDEN LOFI", 255, 244, 250, 255)
    setze takt auf G["tick"]
    wenn G["eq_an"] == falsch:
        setze takt auf 0
        surface_text(z, fx + 16, fy + 66, 1, "EQUALIZER PAUSIERT - SIEHE DARSTELLUNG", 255, 230, 240, 255)
    setze i auf 0
    solange i < 8:
        setze hoehe auf 24 + (i * 53 + takt * 29) % 96
        setze bx auf fx + 24 + i * 38
        surface_roundrect(z, bx, fy + f["h"] - 24 - hoehe, 24, hoehe, 6, 120 + (i * 33) % 135, 200 - (i * 21) % 110, 255 - (i * 45) % 160, 215)
        setze i auf i + 1
    gib_zurück wahr

funktion inhalt_rootsystem(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    setze pfad auf f["pfad"]
    surface_roundrect(z, fx + 12, fy + 38, 70, 22, 6, 255, 255, 255, 60)
    surface_text(z, fx + 22, fy + 44, 1, "HOCH", 255, 255, 255, 255)
    surface_text(z, fx + 92, fy + 44, 2, pfad, 220, 235, 245, 255)
    setze eintraege auf G["fs"][pfad]
    setze reihe auf 0
    für e in eintraege:
        wenn reihe < 8:
            setze ey auf fy + 74 + reihe * 24
            wenn e["typ"] == "ordner":
                surface_rect(z, fx + 18, ey + 1, 14, 10, 240, 200, 120, 255)
                surface_text(z, fx + 42, ey, 2, e["name"], 255, 255, 255, 255)
            sonst:
                surface_rect(z, fx + 20, ey, 10, 12, 210, 225, 245, 255)
                surface_text(z, fx + 42, ey, 2, e["name"] + "  " + e["groesse"], 225, 235, 245, 255)
            setze reihe auf reihe + 1
    surface_text(z, fx + 16, fy + f["h"] - 22, 1, "KLICK OEFFNET ORDNER - HOCH GEHT ZURUECK", 210, 225, 240, 255)
    gib_zurück wahr

funktion inhalt_terminal(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    surface_rect(z, fx + 8, fy + 36, f["b"] - 16, f["h"] - 46, 12, 20, 16, 235)
    setze reihe auf 0
    setze start auf 0
    wenn länge(G["term_zeilen"]) > 9:
        setze start auf länge(G["term_zeilen"]) - 9
    setze i auf start
    solange i < länge(G["term_zeilen"]):
        surface_text(z, fx + 16, fy + 44 + reihe * 16, 1, G["term_zeilen"][i], 150, 240, 170, 255)
        setze reihe auf reihe + 1
        setze i auf i + 1
    setze cursor auf ""
    wenn G["tick"] % 2 == 0:
        setze cursor auf "-"
    surface_text(z, fx + 16, fy + 44 + reihe * 16, 1, "MOOS. " + G["term_eingabe"] + cursor, 190, 255, 200, 255)
    surface_text(z, fx + 16, fy + f["h"] - 20, 1, "TIPPE HILFE - FENSTER MUSS VORNE SEIN", 120, 180, 140, 255)
    gib_zurück wahr

funktion inhalt_einstellungen(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    surface_text(z, fx + 16, fy + 44, 1, "WALLPAPER", 50, 44, 30, 255)
    surface_roundrect(z, fx + 16, fy + 62, 90, 26, 6, 130, 200, 140, 220)
    surface_text(z, fx + 32, fy + 70, 1, "MOOS", 30, 50, 36, 255)
    surface_roundrect(z, fx + 116, fy + 62, 90, 26, 6, 60, 80, 100, 220)
    surface_text(z, fx + 128, fy + 70, 1, "STATION", 220, 230, 245, 255)
    wenn G["wp_aktiv"] == 1:
        surface_roundrect(z, fx + 14, fy + 60, 94, 30, 8, 255, 255, 255, 90)
    sonst:
        surface_roundrect(z, fx + 114, fy + 60, 94, 30, 8, 255, 255, 255, 90)
    surface_text(z, fx + 16, fy + 108, 1, "EQUALIZER", 50, 44, 30, 255)
    wenn G["eq_an"]:
        surface_roundrect(z, fx + 150, fy + 104, 46, 22, 11, 90, 190, 110, 235)
        surface_circle(z, fx + 185, fy + 115, 8, 255, 255, 255, 255)
    sonst:
        surface_roundrect(z, fx + 150, fy + 104, 46, 22, 11, 120, 120, 120, 200)
        surface_circle(z, fx + 161, fy + 115, 8, 235, 235, 235, 255)
    surface_text(z, fx + 16, fy + 148, 1, "REDUZIERTE BEWEGUNG", 50, 44, 30, 255)
    wenn G["red_bewegung"]:
        surface_roundrect(z, fx + 236, fy + 144, 46, 22, 11, 90, 190, 110, 235)
        surface_circle(z, fx + 271, fy + 155, 8, 255, 255, 255, 255)
    sonst:
        surface_roundrect(z, fx + 236, fy + 144, 46, 22, 11, 120, 120, 120, 200)
        surface_circle(z, fx + 247, fy + 155, 8, 235, 235, 235, 255)
    surface_text(z, fx + 16, fy + 188, 1, "LEISTEN-FARBE", 50, 44, 30, 255)
    surface_roundrect(z, fx + 16, fy + 208, 40, 24, 6, 18, 34, 24, 255)
    surface_roundrect(z, fx + 64, fy + 208, 40, 24, 6, 16, 26, 44, 255)
    surface_roundrect(z, fx + 112, fy + 208, 40, 24, 6, 44, 22, 26, 255)
    setze markx auf fx + 16 + G["leiste_idx"] * 48
    surface_roundrect(z, markx - 2, fy + 206, 44, 28, 8, 255, 255, 255, 120)
    surface_text(z, fx + 16, fy + 250, 1, "AENDERUNGEN WIRKEN SOFORT", 60, 54, 40, 255)
    gib_zurück wahr

funktion inhalt_moocode(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    surface_rect(z, fx + 8, fy + 36, f["b"] - 16, 130, 20, 24, 40, 235)
    setze reihe auf 0
    für zeile in G["code_zeilen"]:
        surface_text(z, fx + 16, fy + 44 + reihe * 16, 1, zeile, 200, 220, 255, 255)
        setze reihe auf reihe + 1
    surface_roundrect(z, fx + 12, fy + 176, 110, 26, 6, 90, 190, 110, 235)
    surface_text(z, fx + 26, fy + 184, 1, "AUSFUEHREN", 20, 40, 26, 255)
    surface_text(z, fx + 136, fy + 184, 1, "DEMO-RUNNER - SIMULIERT", 90, 84, 140, 255)
    surface_rect(z, fx + 8, fy + 212, f["b"] - 16, f["h"] - 222, 14, 16, 26, 235)
    setze reihe2 auf 0
    setze start auf 0
    wenn länge(G["code_out"]) > 6:
        setze start auf länge(G["code_out"]) - 6
    setze i auf start
    solange i < länge(G["code_out"]):
        surface_text(z, fx + 16, fy + 220 + reihe2 * 16, 1, G["code_out"][i], 170, 240, 190, 255)
        setze reihe2 auf reihe2 + 1
        setze i auf i + 1
    gib_zurück wahr

funktion inhalt_aigarden(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    surface_text(z, fx + 16, fy + 44, 1, "MYCELIUM AKTIV - KLICK PFLANZT", 235, 255, 235, 255)
    surface_text(z, fx + 16, fy + 62, 1, "WACHSTUM SIMULIERT - TENSOR BACKEND BEREIT", 210, 240, 215, 255)
    setze boden_y auf fy + f["h"] - 26
    surface_rect(z, fx + 10, boden_y, f["b"] - 20, 12, 70, 52, 36, 235)
    für p in G["garten"]:
        setze px auf fx + p["x"]
        setze hoehe auf p["hoehe"]
        surface_rect(z, px, boden_y - hoehe, 3, hoehe, 60, 140, 70, 255)
        surface_circle(z, px + 1, boden_y - hoehe, 5 + p["hoehe"] / 12, 110, 210, 100, 230)
    gib_zurück wahr

# ---------------- Fenster-Rahmen ----------------

funktion desk_fenster_zeichnen(z, f):
    setze fx auf f["x"]
    setze fy auf f["y"]
    wenn f["aufl"]["ok"]:
        uim_effekt_vorschau_zeichnen(z, fx, fy, f["b"], f["h"], f["aufl"], G["red_bewegung"], 65536)
    surface_roundrect(z, fx + 2, fy + 2, f["b"] - 4, 26, 10, 255, 255, 255, 24)
    surface_rect(z, fx + 10, fy + 30, f["b"] - 20, 1, 255, 255, 255, 60)
    # Buttons: Minimieren + Schliessen
    surface_roundrect(z, fx + f["b"] - 50, fy + 6, 20, 20, 6, 120, 160, 200, 150)
    surface_text(z, fx + f["b"] - 44, fy + 12, 1, "-", 255, 255, 255, 235)
    surface_roundrect(z, fx + f["b"] - 26, fy + 6, 20, 20, 6, 235, 90, 90, 150)
    surface_text(z, fx + f["b"] - 20, fy + 11, 1, "x", 255, 255, 255, 235)
    setze vorn auf desk_vorderstes()
    wenn vorn != nichts und vorn["id"] == f["id"]:
        surface_text(z, fx + 14, fy + 11, 2, f["titel"], 255, 255, 255, 255)
    sonst:
        surface_text(z, fx + 14, fy + 11, 2, f["titel"], 208, 214, 224, 255)
    wenn f["typ"] == "monitor":
        inhalt_monitor(z, f)
    wenn f["typ"] == "greenhouse":
        inhalt_greenhouse(z, f)
    wenn f["typ"] == "moosik":
        inhalt_moosik(z, f)
    wenn f["typ"] == "rootsystem":
        inhalt_rootsystem(z, f)
    wenn f["typ"] == "terminal":
        inhalt_terminal(z, f)
    wenn f["typ"] == "einstellungen":
        inhalt_einstellungen(z, f)
    wenn f["typ"] == "moocode":
        inhalt_moocode(z, f)
    wenn f["typ"] == "aigarden":
        inhalt_aigarden(z, f)
    # Resize-Griff
    surface_rect(z, fx + f["b"] - 14, fy + f["h"] - 4, 12, 2, 255, 255, 255, 140)
    surface_rect(z, fx + f["b"] - 4, fy + f["h"] - 14, 2, 12, 255, 255, 255, 140)
    gib_zurück wahr

# ---------------- Leiste / Startmenue / Panels ----------------

funktion desk_leiste_zeichnen(z):
    setze ly auf G["h"] - G["leiste_h"]
    setze aufl auf G["leiste_aufl"][G["leiste_idx"]]
    wenn aufl["ok"]:
        uim_effekt_vorschau_zeichnen(z, 0, ly, G["b"], G["leiste_h"], aufl, falsch, 65536)
    surface_rect(z, 0, ly, G["b"], 1, 255, 255, 255, 90)
    surface_roundrect(z, 6, ly + 6, 82, 32, 10, 46, 104, 62, 235)
    wenn G["startknopf"] != nichts:
        surface_blit_frame(z, 10, ly + 7, G["startknopf"])
    surface_text(z, 46, ly + 14, 1, "MOOS", 235, 255, 240, 255)
    setze vorn auf desk_vorderstes()
    setze i auf 0
    solange i < länge(G["fenster"]):
        setze f auf G["fenster"][i]
        setze bx auf 100 + i * 148
        wenn bx + 140 < G["b"] - 150:
            setze alpha auf 36
            wenn f["sichtbar"] und vorn != nichts und vorn["id"] == f["id"]:
                setze alpha auf 96
            surface_roundrect(z, bx, ly + 6, 140, 32, 8, 255, 255, 255, alpha)
            wenn f["sichtbar"]:
                surface_text(z, bx + 10, ly + 17, 1, f["titel"], 255, 255, 255, 255)
            sonst:
                surface_text(z, bx + 10, ly + 17, 1, f["titel"], 190, 196, 206, 255)
        setze i auf i + 1
    # Glocke + Uhr
    setze glocke_a auf 120
    wenn länge(G["noti"]) > 0:
        setze glocke_a auf 235
    surface_circle(z, G["b"] - 104, ly + 20, 8, 255, 240, 160, glocke_a)
    surface_text(z, G["b"] - 108, ly + 15, 1, "!", 60, 50, 20, glocke_a)
    surface_text(z, G["b"] - 84, ly + 17, 2, G["uhr"], 255, 255, 255, 255)
    gib_zurück wahr

funktion desk_startmenue_zeichnen(z):
    setze mx auf 6
    setze mh auf 420
    setze my auf G["h"] - G["leiste_h"] - mh - 6
    wenn G["menue"]["ok"]:
        uim_effekt_vorschau_zeichnen(z, mx, my, 250, mh, G["menue"], falsch, 65536)
    setze eintraege auf G["start_eintraege"]
    setze i auf 0
    solange i < länge(eintraege):
        setze alpha auf 22
        wenn i >= länge(eintraege) - 3:
            setze alpha auf 34
        surface_roundrect(z, mx + 10, my + 14 + i * 44, 230, 36, 8, 255, 255, 255, alpha)
        surface_text(z, mx + 24, my + 26 + i * 44, 2, eintraege[i]["label"], 255, 255, 255, 255)
        setze i auf i + 1
    surface_rect(z, mx + 14, my + 14 + (länge(eintraege) - 3) * 44 - 5, 222, 1, 255, 255, 255, 80)
    gib_zurück wahr

funktion desk_noti_panel_zeichnen(z):
    setze pb auf 300
    setze ph auf 200
    setze px auf G["b"] - pb - 8
    setze py auf G["h"] - G["leiste_h"] - ph - 8
    wenn G["menue"]["ok"]:
        uim_effekt_vorschau_zeichnen(z, px, py, pb, ph, G["menue"], falsch, 65536)
    surface_text(z, px + 14, py + 14, 1, "MELDUNGEN", 255, 255, 255, 255)
    wenn länge(G["noti"]) == 0:
        surface_text(z, px + 14, py + 44, 1, "ALLES RUHIG IM BEET.", 230, 240, 235, 255)
    setze start auf 0
    wenn länge(G["noti"]) > 8:
        setze start auf länge(G["noti"]) - 8
    setze reihe auf 0
    setze i auf start
    solange i < länge(G["noti"]):
        surface_text(z, px + 14, py + 44 + reihe * 17, 1, G["noti"][i]["text"], 230, 240, 235, 255)
        setze reihe auf reihe + 1
        setze i auf i + 1
    gib_zurück wahr

funktion desk_toast_zeichnen(z):
    setze letzt auf nichts
    für n in G["noti"]:
        wenn n["rest"] > 0:
            setze letzt auf n
    wenn letzt == nichts:
        gib_zurück falsch
    setze pb auf 320
    setze px auf G["b"] - pb - 10
    setze py auf G["h"] - G["leiste_h"] - 52
    surface_roundrect(z, px, py, pb, 40, 10, 24, 40, 30, 225)
    surface_roundrect(z, px, py, pb, 40, 10, 255, 255, 255, 20)
    surface_text(z, px + 14, py + 14, 1, letzt["text"], 220, 250, 225, 255)
    gib_zurück wahr

funktion desk_kontext_zeichnen(z):
    setze k auf G["kontext"]
    wenn k == nichts:
        gib_zurück falsch
    setze kb auf 230
    setze kh auf länge(k["eintraege"]) * 30 + 16
    surface_roundrect(z, k["x"], k["y"], kb, kh, 8, 20, 34, 26, 235)
    surface_roundrect(z, k["x"], k["y"], kb, kh, 8, 255, 255, 255, 18)
    setze i auf 0
    solange i < länge(k["eintraege"]):
        surface_text(z, k["x"] + 14, k["y"] + 12 + i * 30, 1, k["eintraege"][i]["label"], 235, 250, 240, 255)
        setze i auf i + 1
    gib_zurück wahr

# ---------------- Boot / Menue / Sonderscreens ----------------

funktion boot_zeichnen(z):
    surface_clear(z, 244, 247, 240, 255)
    wenn G["boot_logo"] != nichts:
        surface_blit_frame(z, G["b"] / 2 - 210, 90, G["boot_logo"])
    sonst:
        surface_text(z, G["b"] / 2 - 90, 170, 4, "MOOS", 90, 150, 80, 255)
    surface_text(z, G["b"] / 2 - 92, 262, 1, "Growing up M O O S ...", 70, 100, 74, 255)
    setze breite auf 360
    setze bx auf G["b"] / 2 - 180
    surface_roundrect(z, bx, 292, breite, 14, 7, 60, 90, 60, 50)
    setze fortschritt auf G["boot_tick"] * breite / 20
    wenn fortschritt > breite:
        setze fortschritt auf breite
    surface_roundrect(z, bx, 292, fortschritt, 14, 7, 110, 190, 100, 235)
    setze zeilen auf ["[OK] Seed initialized", "[OK] Growbox online", "[OK] RootSystem mounted", "[OK] Mycelium connected", "[OK] Greenhouse prepared", "[OK] Desktop sprouts started"]
    setze sichtbar auf boden((G["boot_tick"] - 2) / 3)
    setze i auf 0
    solange i < länge(zeilen) und i < sichtbar:
        surface_text(z, bx, 330 + i * 20, 1, zeilen[i], 80, 120, 84, 255)
        setze i auf i + 1
    surface_text(z, bx, 490, 1, "ESC ueberspringen - F2 Firmware - F12 Bootmenue", 120, 140, 120, 255)
    surface_text(z, bx, 510, 1, "Klick auf das Logo zeigt die Version", 140, 160, 140, 255)
    gib_zurück wahr

funktion bootmenu_zeichnen(z):
    surface_clear(z, 12, 18, 16, 255)
    surface_text(z, G["b"] / 2 - 130, 120, 2, "BOOTMENUE", 150, 220, 130, 255)
    setze eintraege auf ["MOOS DESKTOP", "MOOS SAFE GROWTH", "DIAGNOSTICS", "MEMORY TEST"]
    setze i auf 0
    solange i < 4:
        setze alpha auf 30
        wenn i == G["boot_wahl"]:
            setze alpha auf 90
        surface_roundrect(z, G["b"] / 2 - 160, 200 + i * 54, 320, 42, 8, 255, 255, 255, alpha)
        surface_text(z, G["b"] / 2 - 140, 214 + i * 54, 2, eintraege[i], 230, 245, 230, 255)
        setze i auf i + 1
    surface_text(z, G["b"] / 2 - 160, 440, 1, "KLICK WAEHLT DEN EINTRAG", 140, 180, 150, 255)
    gib_zurück wahr

funktion firmware_zeichnen(z):
    surface_clear(z, 16, 20, 26, 255)
    surface_text(z, 60, 60, 2, "GROWBOX FIRMWARE", 160, 210, 240, 255)
    surface_text(z, 60, 130, 1, "SEED VERSION 0.1 DEMO", 220, 230, 240, 255)
    surface_text(z, 60, 158, 1, "GROWBOX KERNEL - LOCKED", 220, 230, 240, 255)
    surface_text(z, 60, 186, 1, "SUNLIGHT GPU - BEREIT", 220, 230, 240, 255)
    surface_text(z, 60, 214, 1, "NUTRIENTS QUELLE - LOKAL", 220, 230, 240, 255)
    surface_text(z, 60, 242, 1, "SIMULIERTE ANSICHT - KEINE ECHTE FIRMWARE", 180, 190, 210, 255)
    surface_roundrect(z, 60, 300, 140, 34, 8, 90, 190, 110, 235)
    surface_text(z, 82, 311, 1, "ZURUECK", 20, 40, 26, 255)
    gib_zurück wahr

funktion memtest_zeichnen(z):
    surface_clear(z, 10, 14, 12, 255)
    surface_text(z, 60, 60, 2, "MEMORY TEST", 150, 220, 130, 255)
    setze breite auf 500
    setze fortschritt auf G["memtest_tick"] * breite / 30
    wenn fortschritt > breite:
        setze fortschritt auf breite
    surface_roundrect(z, 60, 140, breite, 16, 8, 255, 255, 255, 40)
    surface_roundrect(z, 60, 140, fortschritt, 16, 8, 130, 210, 120, 235)
    setze mb auf G["memtest_tick"] * 1024 / 30
    wenn mb > 1024:
        setze mb auf 1024
    surface_text(z, 60, 180, 2, text(boden(mb)) + " MB GEPRUEFT - KEINE FEHLER", 200, 235, 205, 255)
    wenn G["memtest_tick"] >= 30:
        surface_text(z, 60, 220, 1, "TEST ABGESCHLOSSEN - KLICK FUER BOOTMENUE", 235, 250, 240, 255)
    gib_zurück wahr

funktion schlaf_zeichnen(z):
    surface_clear(z, 4, 8, 6, 255)
    surface_text(z, G["b"] / 2 - 200, G["h"] / 2 - 20, 1, "MOOS SCHLAEFT - KLICK ZUM AUFWECKEN", 90, 140, 100, 255)
    gib_zurück wahr

# ---------------- Render ----------------

funktion render_frame():
    setze z auf G["z"]
    wenn G["modus"] == "boot":
        boot_zeichnen(z)
    sonst wenn G["modus"] == "bootmenu":
        bootmenu_zeichnen(z)
    sonst wenn G["modus"] == "firmware":
        firmware_zeichnen(z)
    sonst wenn G["modus"] == "memtest":
        memtest_zeichnen(z)
    sonst wenn G["modus"] == "schlaf":
        schlaf_zeichnen(z)
    sonst:
        desk_wallpaper(z)
        für f in G["fenster"]:
            wenn f["sichtbar"]:
                desk_fenster_zeichnen(z, f)
        desk_leiste_zeichnen(z)
        wenn G["start_offen"]:
            desk_startmenue_zeichnen(z)
        wenn G["noti_offen"]:
            desk_noti_panel_zeichnen(z)
        desk_toast_zeichnen(z)
        desk_kontext_zeichnen(z)
    setze frame auf surface_snapshot_to_frame(z)
    wenn frame == nichts:
        gib_zurück falsch
    G["frame"] = frame
    gib_zurück wahr

funktion draw_cb(w, zeichner):
    wenn G["frame"] != nichts:
        ui_zeichne_frame(zeichner, 0, 0, G["b"], G["h"], G["frame"])
    gib_zurück wahr

funktion neu_zeichnen():
    render_frame()
    ui_leinwand_anfordern(G["leinwand"])
    gib_zurück wahr

# ---------------- Terminal-Logik ----------------

funktion term_aus(text):
    G["term_zeilen"].hinzufügen(text)
    gib_zurück wahr

funktion term_befehl(roh):
    term_aus("MOOS. " + roh)
    setze cmd auf roh
    wenn cmd == "hilfe" oder cmd == "HILFE":
        term_aus("BEFEHLE- HILFE MOO SPROUTS GROWBOX")
        term_aus("WACHSE ERNTE COMPOST KLAR NEUSTART")
    sonst wenn cmd == "moo" oder cmd == "MOO":
        term_aus("MOOS 0.1 - GROWS INSIDE THE GROWBOX")
    sonst wenn cmd == "sprouts" oder cmd == "SPROUTS":
        für f in G["fenster"]:
            setze status auf "AKTIV"
            wenn f["sichtbar"] == falsch:
                setze status auf "RUHT"
            term_aus("PID " + text(f["id"]) + " " + f["titel"] + " " + status)
    sonst wenn cmd == "growbox" oder cmd == "GROWBOX":
        term_aus("GROWBOX LOCKED. INTEGRITY PROTECTED.")
    sonst wenn cmd == "wachse" oder cmd == "WACHSE":
        term_aus("ES GRUENT... NEUER SPROSS IM AI GARDEN!")
        setze p auf {}
        p["x"] = 30 + (G["tick"] * 53) % 330
        p["hoehe"] = 10
        p["max"] = 60 + (G["tick"] * 31) % 120
        G["garten"].hinzufügen(p)
    sonst wenn cmd == "ernte" oder cmd == "ERNTE":
        G["garten"] = []
        term_aus("ERNTE EINGEFAHREN - GARTEN IST LEER.")
    sonst wenn cmd == "compost" oder cmd == "COMPOST":
        term_aus("COMPOST GELEERT. PRUNING ERLEDIGT.")
        noti_push("COMPOST GELEERT")
    sonst wenn cmd == "klar" oder cmd == "KLAR" oder cmd == "clear":
        G["term_zeilen"] = []
    sonst wenn cmd == "neustart" oder cmd == "NEUSTART":
        term_aus("RESTARTING THE GROWBOX ...")
        G["modus"] = "boot"
        G["boot_tick"] = 0
    sonst wenn länge(cmd) > 0:
        term_aus("UNBEKANNT- " + cmd + " - TIPPE HILFE")
    gib_zurück wahr

# ---------------- Aktionen ----------------

funktion aktion_ausfuehren(name):
    wenn name == "icon_oeffnen" oder name == "icon_info":
        icon_aktion(name)
        gib_zurück wahr
    wenn name == "wallpaper":
        wenn G["wp_aktiv"] == 1:
            G["wp_aktiv"] = 2
        sonst:
            G["wp_aktiv"] = 1
        noti_push("WALLPAPER GEWECHSELT")
    wenn name == "sortieren":
        G["gewaehlt_icon"] = 0 - 1
        noti_push("ICONS SORTIERT")
    wenn name == "projekt":
        app_oeffne("moocode")
    wenn name == "darstellung":
        app_oeffne("einstellungen")
    wenn name == "wachsen":
        G["grow_anim"] = 6
        noti_push("DESKTOP WAECHST NEU")
    wenn name == "ruhe":
        G["modus"] = "schlaf"
        G["start_offen"] = falsch
    wenn name == "neustart":
        G["modus"] = "boot"
        G["boot_tick"] = 0
        G["start_offen"] = falsch
        noti_push("GROWBOX NEU GESTARTET")
    wenn name == "beenden":
        ui_beenden()
    gib_zurück wahr

funktion code_ausfuehren():
    G["code_out"] = []
    G["code_out"].hinzufügen("KOMPILIERE MOOS-QUELLE ... OK")
    G["code_lauf"] = 5
    gib_zurück wahr

# ---------------- Eingabe: Maus ----------------

funktion klemme(wert, lo, hi):
    wenn wert < lo:
        gib_zurück lo
    wenn wert > hi:
        gib_zurück hi
    gib_zurück wert

funktion desk_klick(x, y):
    setze ly auf G["h"] - G["leiste_h"]
    # Kontextmenue offen? Eintrag oder wegklicken
    wenn G["kontext"] != nichts:
        setze k auf G["kontext"]
        setze kh auf länge(k["eintraege"]) * 30 + 16
        wenn x >= k["x"] und x < k["x"] + 230 und y >= k["y"] und y < k["y"] + kh:
            setze idx auf boden((y - k["y"] - 6) / 30)
            wenn idx >= 0 und idx < länge(k["eintraege"]):
                aktion_ausfuehren(k["eintraege"][idx]["aktion"])
        G["kontext"] = nichts
        neu_zeichnen()
        gib_zurück wahr
    # Startmenue
    wenn G["start_offen"]:
        setze mx auf 6
        setze mh auf 420
        setze my auf ly - mh - 6
        wenn x >= mx und x < mx + 250 und y >= my und y < my + mh:
            setze idx auf boden((y - my - 14) / 44)
            wenn idx >= 0 und idx < länge(G["start_eintraege"]):
                setze e auf G["start_eintraege"][idx]
                wenn e["typ"] == "app":
                    app_oeffne(e["aktion"])
                sonst:
                    aktion_ausfuehren(e["aktion"])
        G["start_offen"] = falsch
        neu_zeichnen()
        gib_zurück wahr
    # Noti-Panel schliessen bei Klick ausserhalb
    wenn G["noti_offen"]:
        G["noti_offen"] = falsch
        neu_zeichnen()
        gib_zurück wahr
    # Leiste
    wenn y >= ly:
        wenn x >= 6 und x < 88:
            G["start_offen"] = wahr
            neu_zeichnen()
            gib_zurück wahr
        wenn x >= G["b"] - 116 und x < G["b"] - 92:
            G["noti_offen"] = wahr
            neu_zeichnen()
            gib_zurück wahr
        setze i auf 0
        solange i < länge(G["fenster"]):
            setze f auf G["fenster"][i]
            setze bx auf 100 + i * 148
            wenn x >= bx und x < bx + 140:
                setze vorn auf desk_vorderstes()
                wenn f["sichtbar"] und vorn != nichts und vorn["id"] == f["id"]:
                    f["sichtbar"] = falsch
                sonst:
                    f["sichtbar"] = wahr
                    desk_nach_vorn(f)
                neu_zeichnen()
                gib_zurück wahr
            setze i auf i + 1
        gib_zurück wahr
    # Fenster von oben nach unten
    setze i auf länge(G["fenster"]) - 1
    solange i >= 0:
        setze f auf G["fenster"][i]
        wenn f["sichtbar"] und x >= f["x"] und x < f["x"] + f["b"] und y >= f["y"] und y < f["y"] + f["h"]:
            desk_nach_vorn(f)
            # Schliessen
            wenn x >= f["x"] + f["b"] - 26 und x < f["x"] + f["b"] - 6 und y >= f["y"] + 6 und y < f["y"] + 26:
                f["sichtbar"] = falsch
                neu_zeichnen()
                gib_zurück wahr
            # Minimieren
            wenn x >= f["x"] + f["b"] - 50 und x < f["x"] + f["b"] - 30 und y >= f["y"] + 6 und y < f["y"] + 26:
                f["sichtbar"] = falsch
                noti_push(f["titel"] + " RUHT IM BEET")
                neu_zeichnen()
                gib_zurück wahr
            # Resize-Griff
            wenn x >= f["x"] + f["b"] - 18 und y >= f["y"] + f["h"] - 18:
                G["resize"] = f
                G["off_x"] = f["b"] - (x - f["x"])
                G["off_y"] = f["h"] - (y - f["y"])
                neu_zeichnen()
                gib_zurück wahr
            # Titelleiste: Drag
            wenn y < f["y"] + 30:
                G["drag"] = f
                G["off_x"] = x - f["x"]
                G["off_y"] = y - f["y"]
                G["ref_x"] = f["x"]
                G["ref_y"] = f["y"]
                neu_zeichnen()
                gib_zurück wahr
            # App-spezifische Klicks (fensterlokal)
            app_klick(f, x - f["x"], y - f["y"])
            neu_zeichnen()
            gib_zurück wahr
        setze i auf i - 1
    # Desktop: Icons
    setze idx auf icon_bei(x, y)
    wenn idx >= 0:
        wenn G["gewaehlt_icon"] == idx:
            app_oeffne(G["icons"][idx]["typ"])
            G["gewaehlt_icon"] = 0 - 1
        sonst:
            G["gewaehlt_icon"] = idx
        neu_zeichnen()
        gib_zurück wahr
    wenn G["gewaehlt_icon"] >= 0:
        G["gewaehlt_icon"] = 0 - 1
        neu_zeichnen()
    gib_zurück wahr

funktion icon_bei(x, y):
    setze i auf 0
    solange i < länge(G["icons"]):
        setze ic auf G["icons"][i]
        wenn x >= ic["x"] - 4 und x < ic["x"] + 60 und y >= ic["y"] - 4 und y < ic["y"] + 72:
            gib_zurück i
        setze i auf i + 1
    gib_zurück 0 - 1

funktion app_klick(f, lx, ly):
    wenn f["typ"] == "rootsystem":
        wenn lx >= 12 und lx < 82 und ly >= 38 und ly < 60:
            wenn f["pfad"] != "WURZEL":
                f["pfad"] = "WURZEL"
            gib_zurück wahr
        setze idx auf boden((ly - 74) / 24)
        setze eintraege auf G["fs"][f["pfad"]]
        wenn idx >= 0 und idx < länge(eintraege):
            setze e auf eintraege[idx]
            wenn e["typ"] == "ordner":
                f["pfad"] = e["name"]
            sonst:
                noti_push(e["name"] + " GEOEFFNET - NUR DEMO")
    wenn f["typ"] == "einstellungen":
        wenn ly >= 60 und ly < 92:
            wenn lx >= 14 und lx < 108:
                G["wp_aktiv"] = 1
            wenn lx >= 114 und lx < 208:
                G["wp_aktiv"] = 2
        wenn ly >= 100 und ly < 130 und lx >= 150 und lx < 196:
            G["eq_an"] = G["eq_an"] == falsch
        wenn ly >= 140 und ly < 170 und lx >= 236 und lx < 282:
            G["red_bewegung"] = G["red_bewegung"] == falsch
        wenn ly >= 204 und ly < 236:
            wenn lx >= 16 und lx < 56:
                G["leiste_idx"] = 0
            wenn lx >= 64 und lx < 104:
                G["leiste_idx"] = 1
            wenn lx >= 112 und lx < 152:
                G["leiste_idx"] = 2
    wenn f["typ"] == "moocode":
        wenn lx >= 12 und lx < 122 und ly >= 176 und ly < 202:
            code_ausfuehren()
    wenn f["typ"] == "aigarden":
        wenn ly > 70 und ly < f["h"] - 26:
            setze p auf {}
            p["x"] = klemme(lx, 14, f["b"] - 14)
            p["hoehe"] = 8
            p["max"] = 50 + (lx * 7) % 130
            G["garten"].hinzufügen(p)
    gib_zurück wahr

funktion on_maus(w, x, y, taste):
    wenn G["modus"] == "boot":
        wenn taste == 1:
            # Logo-Klick = Version, sonst nichts
            wenn x >= G["b"] / 2 - 210 und x < G["b"] / 2 + 210 und y >= 90 und y < 245:
                noti_push("MOOS 0.1 - GROWBOX KERNEL DEMO")
                G["modus"] = "desktop"
                neu_zeichnen()
        gib_zurück wahr
    wenn G["modus"] == "bootmenu":
        wenn taste == 1:
            setze idx auf boden((y - 200) / 54)
            wenn idx >= 0 und idx < 4 und x >= G["b"] / 2 - 160 und x < G["b"] / 2 + 160:
                G["boot_wahl"] = idx
                wenn idx == 0:
                    G["safe_growth"] = falsch
                    G["modus"] = "boot"
                    G["boot_tick"] = 0
                wenn idx == 1:
                    G["safe_growth"] = wahr
                    G["modus"] = "boot"
                    G["boot_tick"] = 0
                    noti_push("SAFE GROWTH AKTIV")
                wenn idx == 2:
                    G["diag_nach_boot"] = wahr
                    G["modus"] = "boot"
                    G["boot_tick"] = 0
                wenn idx == 3:
                    G["modus"] = "memtest"
                    G["memtest_tick"] = 0
                neu_zeichnen()
        gib_zurück wahr
    wenn G["modus"] == "firmware":
        wenn taste == 1 und x >= 60 und x < 200 und y >= 300 und y < 334:
            G["modus"] = "boot"
            neu_zeichnen()
        gib_zurück wahr
    wenn G["modus"] == "memtest":
        wenn taste == 1 und G["memtest_tick"] >= 30:
            G["modus"] = "bootmenu"
            neu_zeichnen()
        gib_zurück wahr
    wenn G["modus"] == "schlaf":
        wenn taste == 1:
            G["modus"] = "desktop"
            noti_push("GUTEN MORGEN! MOOS IST WACH.")
            neu_zeichnen()
        gib_zurück wahr
    # Desktop
    wenn taste == 1:
        desk_klick(x, y)
        gib_zurück wahr
    wenn taste == 3:
        # Rechtsklick: Icon-Menue oder Desktop-Menue
        setze idx auf icon_bei(x, y)
        setze k auf {}
        k["x"] = klemme(x, 0, G["b"] - 240)
        k["y"] = klemme(y, 0, G["h"] - G["leiste_h"] - 140)
        wenn idx >= 0:
            G["gewaehlt_icon"] = idx
            setze ee auf []
            setze e1 auf {}
            e1["label"] = "OEFFNEN"
            e1["aktion"] = "icon_oeffnen"
            ee.hinzufügen(e1)
            setze e2 auf {}
            e2["label"] = "INFO"
            e2["aktion"] = "icon_info"
            ee.hinzufügen(e2)
            k["eintraege"] = ee
            G["icon_menue_idx"] = idx
        sonst:
            setze ee auf []
            setze m1 auf {}
            m1["label"] = "WALLPAPER WECHSELN"
            m1["aktion"] = "wallpaper"
            ee.hinzufügen(m1)
            setze m2 auf {}
            m2["label"] = "ICONS SORTIEREN"
            m2["aktion"] = "sortieren"
            ee.hinzufügen(m2)
            setze m3 auf {}
            m3["label"] = "NEUES MOO-PROJEKT"
            m3["aktion"] = "projekt"
            ee.hinzufügen(m3)
            setze m4 auf {}
            m4["label"] = "DARSTELLUNG OEFFNEN"
            m4["aktion"] = "darstellung"
            ee.hinzufügen(m4)
            setze m5 auf {}
            m5["label"] = "DESKTOP NEU WACHSEN LASSEN"
            m5["aktion"] = "wachsen"
            ee.hinzufügen(m5)
            k["eintraege"] = ee
        G["kontext"] = k
        neu_zeichnen()
    gib_zurück wahr

funktion on_bewegung(w, x, y):
    wenn G["drag"] != nichts:
        setze f auf G["drag"]
        setze nx auf klemme(x - G["off_x"], 0 - f["b"] + 60, G["b"] - 60)
        setze ny auf klemme(y - G["off_y"], 0, G["h"] - G["leiste_h"] - 24)
        f["x"] = nx
        f["y"] = ny
        setze dx auf nx - G["ref_x"]
        wenn dx < 0:
            setze dx auf 0 - dx
        setze dy auf ny - G["ref_y"]
        wenn dy < 0:
            setze dy auf 0 - dy
        wenn dx + dy >= 2:
            G["ref_x"] = nx
            G["ref_y"] = ny
            neu_zeichnen()
        gib_zurück wahr
    wenn G["resize"] != nichts:
        setze f auf G["resize"]
        f["b"] = klemme(x - f["x"] + G["off_x"], 220, G["b"] - 20)
        f["h"] = klemme(y - f["y"] + G["off_y"], 150, G["h"] - G["leiste_h"] - 20)
        neu_zeichnen()
        gib_zurück wahr
    wenn G["modus"] == "desktop":
        setze idx auf icon_bei(x, y)
        wenn idx != G["hover_icon"]:
            G["hover_icon"] = idx
            neu_zeichnen()
    gib_zurück wahr

funktion on_maus_los(w, x, y, taste):
    wenn taste == 1:
        wenn G["drag"] != nichts oder G["resize"] != nichts:
            G["drag"] = nichts
            G["resize"] = nichts
            neu_zeichnen()
    gib_zurück wahr

# Icon-Kontextmenue-Sonderaktionen laufen ueber aktion_ausfuehren
funktion icon_aktion(name):
    setze idx auf G["icon_menue_idx"]
    wenn idx >= 0 und idx < länge(G["icons"]):
        wenn name == "icon_oeffnen":
            app_oeffne(G["icons"][idx]["typ"])
        wenn name == "icon_info":
            noti_push(G["icons"][idx]["name"] + " - TEIL VON MOOS 0.1")
    gib_zurück wahr

# ---------------- Eingabe: Tastatur ----------------

funktion on_taste(w, taste, gedrueckt, mod):
    wenn gedrueckt == falsch:
        gib_zurück falsch
    wenn G["modus"] == "boot":
        wenn taste == "Escape" oder taste == "escape":
            G["modus"] = "desktop"
            neu_zeichnen()
            gib_zurück wahr
        wenn taste == "F2" oder taste == "f2":
            G["modus"] = "firmware"
            neu_zeichnen()
            gib_zurück wahr
        wenn taste == "F12" oder taste == "f12":
            G["modus"] = "bootmenu"
            neu_zeichnen()
            gib_zurück wahr
        gib_zurück falsch
    wenn G["modus"] != "desktop":
        gib_zurück falsch
    # Terminal-Eingabe wenn Terminal vorn
    setze vorn auf desk_vorderstes()
    wenn vorn == nichts:
        gib_zurück falsch
    wenn vorn["typ"] != "terminal":
        gib_zurück falsch
    wenn taste == "Return" oder taste == "Enter" oder taste == "KP_Enter":
        term_befehl(G["term_eingabe"])
        G["term_eingabe"] = ""
        neu_zeichnen()
        gib_zurück wahr
    wenn taste == "BackSpace" oder taste == "Back":
        setze n auf länge(G["term_eingabe"])
        wenn n > 0:
            G["term_eingabe"] = G["term_eingabe"].teilstring(0, n - 1)
        neu_zeichnen()
        gib_zurück wahr
    wenn taste == "space":
        G["term_eingabe"] = G["term_eingabe"] + " "
        neu_zeichnen()
        gib_zurück wahr
    wenn länge(taste) == 1:
        wenn länge(G["term_eingabe"]) < 34:
            G["term_eingabe"] = G["term_eingabe"] + taste
        neu_zeichnen()
        gib_zurück wahr
    gib_zurück falsch

# ---------------- Tick (250ms) ----------------

funktion on_tick():
    G["tick"] = G["tick"] + 1
    setze dirty auf falsch
    wenn G["tick"] % 4 == 0:
        G["uhr"] = ui_zeit_format(ui_zeit_jetzt(), "%H:%M:%S")
        setze dirty auf wahr
    wenn G["modus"] == "boot":
        G["boot_tick"] = G["boot_tick"] + 1
        wenn G["boot_tick"] >= 22:
            G["modus"] = "desktop"
            noti_push("WILLKOMMEN! MOOS IST GEWACHSEN.")
            wenn G["diag_nach_boot"]:
                app_oeffne("monitor")
                G["diag_nach_boot"] = falsch
        setze dirty auf wahr
    wenn G["modus"] == "memtest":
        wenn G["memtest_tick"] < 30:
            G["memtest_tick"] = G["memtest_tick"] + 1
            setze dirty auf wahr
    wenn G["modus"] == "desktop":
        # Toast-Ablauf
        für n in G["noti"]:
            wenn n["rest"] > 0:
                n["rest"] = n["rest"] - 1
                setze dirty auf wahr
        # Equalizer/Monitor-Animation
        wenn G["eq_an"] und G["red_bewegung"] == falsch:
            setze mf auf app_finde("moosik")
            setze gf auf app_finde("monitor")
            wenn (mf != nichts und mf["sichtbar"]) oder (gf != nichts und gf["sichtbar"]):
                wenn G["tick"] % 2 == 0:
                    setze dirty auf wahr
        # Garten waechst
        für p in G["garten"]:
            wenn p["hoehe"] < p["max"]:
                p["hoehe"] = p["hoehe"] + 2
                setze dirty auf wahr
        # Grow-Animation
        wenn G["grow_anim"] > 0:
            G["grow_anim"] = G["grow_anim"] - 1
            setze dirty auf wahr
        # Code-Runner-Simulation
        wenn G["code_lauf"] > 0:
            G["code_lauf"] = G["code_lauf"] - 1
            setze schritt auf 5 - G["code_lauf"]
            wenn schritt == 1:
                G["code_out"].hinzufügen("SPROSS 1 WAECHST")
            wenn schritt == 2:
                G["code_out"].hinzufügen("SPROSS 2 WAECHST")
            wenn schritt == 3:
                G["code_out"].hinzufügen("SPROSS 3 WAECHST")
            wenn schritt == 4:
                G["code_out"].hinzufügen("MOO! 3 SPROSSEN GEZOGEN.")
            wenn schritt == 5:
                G["code_out"].hinzufügen("FERTIG - EXIT 0")
            setze dirty auf wahr
    wenn dirty:
        neu_zeichnen()
    gib_zurück wahr

# ---------------- Setup ----------------

setze z auf surface_new(G["b"], G["h"])
wenn z == nichts:
    wirf "Surface konnte nicht erzeugt werden"
G["z"] = z

G["fenster"] = []
G["noti"] = []
G["garten"] = []
G["term_zeilen"] = []
G["term_eingabe"] = ""
G["eq_an"] = wahr
G["red_bewegung"] = falsch
G["leiste_idx"] = 0
G["boot_wahl"] = 0
G["icon_menue_idx"] = 0 - 1
G["code_lauf"] = 0
G["code_out"] = []
G["code_zeilen"] = ["FUNKTION HAUPT-", "    FUER I IN 1 BIS 3-", "        ZEIGE SPROSS I WAECHST", "    ZEIGE MOO!", "HAUPT()"]

# Dateisystem (Demo-Inhalt)
setze fs auf {}
setze wurzel auf []
setze o1 auf {}
o1["name"] = "GREENHOUSE"
o1["typ"] = "ordner"
wurzel.hinzufügen(o1)
setze o2 auf {}
o2["name"] = "HOME"
o2["typ"] = "ordner"
wurzel.hinzufügen(o2)
setze o3 auf {}
o3["name"] = "COMPOST"
o3["typ"] = "ordner"
wurzel.hinzufügen(o3)
setze d1 auf {}
d1["name"] = "SEED.CFG"
d1["typ"] = "datei"
d1["groesse"] = "1 KB"
wurzel.hinzufügen(d1)
fs["WURZEL"] = wurzel
setze gh auf []
setze g1 auf {}
g1["name"] = "TOMATEN.MOOS"
g1["typ"] = "datei"
g1["groesse"] = "4 KB"
gh.hinzufügen(g1)
setze g2 auf {}
g2["name"] = "BASILIKUM.MOOS"
g2["typ"] = "datei"
g2["groesse"] = "2 KB"
gh.hinzufügen(g2)
fs["GREENHOUSE"] = gh
setze hm auf []
setze h1 auf {}
h1["name"] = "GAERTNER-NOTIZEN.TXT"
h1["typ"] = "datei"
h1["groesse"] = "3 KB"
hm.hinzufügen(h1)
setze h2 auf {}
h2["name"] = "MOOS-DESKTOP-DEMO.MOOS"
h2["typ"] = "datei"
h2["groesse"] = "38 KB"
hm.hinzufügen(h2)
fs["HOME"] = hm
setze cp auf []
setze c1 auf {}
c1["name"] = "ALTE-BLAETTER.TMP"
c1["typ"] = "datei"
c1["groesse"] = "12 KB"
cp.hinzufügen(c1)
fs["COMPOST"] = cp
G["fs"] = fs

# Icons
G["icons"] = []
setze inamen auf [["ROOTSYSTEM", "rootsystem"], ["COMPOST", "compost"], ["MOO CODE", "moocode"], ["AI GARDEN", "aigarden"], ["GREENHOUSE", "greenhouse"], ["MONITOR", "monitor"]]
setze ii auf 0
solange ii < länge(inamen):
    setze ic auf {}
    ic["name"] = inamen[ii][0]
    ic["typ"] = inamen[ii][1]
    ic["x"] = 26
    ic["y"] = 26 + ii * 92
    wenn ii >= 4:
        ic["x"] = 110
        ic["y"] = 26 + (ii - 4) * 92
    G["icons"].hinzufügen(ic)
    setze ii auf ii + 1

# Startmenue-Eintraege
G["start_eintraege"] = []
setze smenu auf [["GROWBOX MONITOR", "app", "monitor"], ["ROOTSYSTEM", "app", "rootsystem"], ["TERMINAL", "app", "terminal"], ["MOO CODE", "app", "moocode"], ["AI GARDEN", "app", "aigarden"], ["DARSTELLUNG", "app", "einstellungen"], ["RUHEZUSTAND", "aktion", "ruhe"], ["NEUSTART", "aktion", "neustart"], ["GROWBOX SCHLIESSEN", "aktion", "beenden"]]
setze sidx auf 0
solange sidx < länge(smenu):
    setze seintrag auf {}
    seintrag["label"] = smenu[sidx][0]
    seintrag["typ"] = smenu[sidx][1]
    seintrag["aktion"] = smenu[sidx][2]
    G["start_eintraege"].hinzufügen(seintrag)
    setze sidx auf sidx + 1

# Effekt-Aufloesungen fuer Leiste (3 Tints) + Menue
G["leiste_aufl"] = []
setze tints auf [[18, 22, 34], [16, 26, 44], [44, 22, 26]]
setze ti auf 0
solange ti < 3:
    setze le auf uim_effekt_neu()
    uim_effekt_ecken_setze(le, 0, 0, 0, 0)
    uim_effekt_hintergrund_setze(le, 5, 300, tints[ti][0], tints[ti][1], tints[ti][2], 255, 150, 6, 300 + ti)
    G["leiste_aufl"].hinzufügen(uim_effekt_aufloesen(le, 255))
    setze ti auf ti + 1
setze menue auf uim_effekt_neu()
uim_effekt_ecken_setze(menue, 10, 10, 10, 10)
uim_effekt_schatten_setze(menue, 2, 6, 8, 2, 0, 0, 0, 130)
uim_effekt_hintergrund_setze(menue, 8, 320, 44, 66, 56, 255, 132, 8, 400)
G["menue"] = uim_effekt_aufloesen(menue, 255)

# Wallpaper + Start-Apps
G["wallpaper"] = frame_lade_bmp("beispiele/assets/moos/wallpaper.bmp")
G["wallpaper2"] = frame_lade_bmp("beispiele/assets/moos/wallpaper2.bmp")
G["boot_logo"] = frame_lade_bmp("beispiele/assets/moos/boot_logo.bmp")
G["startknopf"] = frame_lade_bmp("beispiele/assets/moos/startbutton_30.bmp")
G["icon_frames"] = {}
setze icon_dateien auf [["rootsystem", "icon_rootsystem"], ["compost", "icon_compost"], ["moocode", "icon_moo_code"], ["aigarden", "icon_ai_garden"], ["greenhouse", "icon_greenhouse"], ["monitor", "icon_growbox"], ["terminal", "icon_terminal"], ["einstellungen", "icon_settings"], ["moosik", "icon_moosik_player"]]
setze fi auf 0
solange fi < länge(icon_dateien):
    G["icon_frames"][icon_dateien[fi][0]] = frame_lade_bmp("beispiele/assets/moos/icons/" + icon_dateien[fi][1] + ".bmp")
    setze fi auf fi + 1
G["uhr"] = ui_zeit_format(ui_zeit_jetzt(), "%H:%M:%S")

setze f1 auf app_oeffne("monitor")
setze f2 auf app_oeffne("greenhouse")
setze f3 auf app_oeffne("moosik")
setze rs auf app_finde("rootsystem")
G["noti"] = []

# RootSystem-Fenster braucht einen Startpfad, sobald es geoeffnet wird —
# app_neu kennt kein pfad-Feld, also hier zentral nachruesten:
für f in G["fenster"]:
    f["pfad"] = "WURZEL"

wenn render_frame() == falsch:
    wirf "Initialer Frame konnte nicht gerendert werden"

setze fenster auf ui_fenster("MOOS-Desktop-Demo — grows inside the Growbox", 1040, 700, 0, nichts)
ui_label(fenster, "MOOS-Desktop-Demo — ein interaktiver Vorgeschmack auf MOOS OS, vollstaendig mit Moo Code gebaut.", 20, 10, 990, 22)
G["leinwand"] = ui_leinwand(fenster, 12, 36, G["b"], G["h"], draw_cb)
ui_leinwand_on_maus(G["leinwand"], on_maus)
ui_leinwand_on_bewegung(G["leinwand"], on_bewegung)
ui_leinwand_on_maus_los(G["leinwand"], on_maus_los)
ui_leinwand_on_taste(G["leinwand"], on_taste)
ui_leinwand_fokus_setze(G["leinwand"])
ui_timer_hinzu(250, on_tick)
zeige "P016-UI1-MOOS-DESKTOP-START"
ui_zeige_nebenbei(fenster)
ui_laufen()
