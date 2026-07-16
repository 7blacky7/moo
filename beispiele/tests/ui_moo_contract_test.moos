# P016-F1: deterministischer Widget-/Container-/Eingabevertrag.
# Kein Fenster, kein GTK/SDL, keine Host-Ereignisschleife.

importiere ui
importiere ui_moo

setze s auf {}
s["fehler"] = 0
s["nested"] = 0
s["unten"] = 0
s["oben"] = 0
s["clip_kind"] = 0
s["blur_klick"] = 0
s["liste_cb"] = 0
s["slider_cb"] = 0

funktion pruef(name, ok):
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        s["fehler"] = s["fehler"] + 1
    gib_zurück ok

funktion klick_nested(w):
    s["nested"] = s["nested"] + 1
    gib_zurück wahr

funktion klick_unten(w):
    s["unten"] = s["unten"] + 1
    gib_zurück wahr

funktion klick_oben(w):
    s["oben"] = s["oben"] + 1
    gib_zurück wahr

funktion klick_clip(w):
    s["clip_kind"] = s["clip_kind"] + 1
    gib_zurück wahr

funktion klick_blur(w):
    s["blur_klick"] = s["blur_klick"] + 1
    gib_zurück wahr

funktion liste_gewaehlt(w, idx):
    s["liste_cb"] = s["liste_cb"] + 1
    gib_zurück wahr

funktion slider_geaendert(w, wert):
    s["slider_cb"] = s["slider_cb"] + 1
    gib_zurück wahr

setze k auf uim_mock_wurzel(400, 300)

# Verschachtelte Offsets und rekursiver Fokus/Suche.
setze panel auf uim_hinzu(k, uim_panel("P", 20, 20, 150, 60))
setze nested auf uim_hinzu(panel, uim_knopf("N", 10, 10, 80, 24, klick_nested))
nested["id"] = "nested"
uim_mock_maus(k, 35, 35, wahr)
uim_mock_maus(k, 35, 35, falsch)
pruef("nested-offset-klick", s["nested"] == 1)
setze nested_gefunden auf uim_finde(k, "nested")
setze nested_finde_ok auf falsch
wenn nested_gefunden != nichts:
    setze nested_finde_ok auf nested_gefunden["uid"] == nested["uid"]
pruef("nested-finde", nested_finde_ok)

# Z-Order: sichtbar-deaktiviert blockt die tiefere Lage.
setze unten auf uim_hinzu(k, uim_knopf("U", 200, 20, 100, 30, klick_unten))
setze oben auf uim_hinzu(k, uim_knopf("O", 200, 20, 100, 30, klick_oben))
oben["aktiv"] = falsch
uim_mock_maus(k, 220, 35, wahr)
uim_mock_maus(k, 220, 35, falsch)
pruef("disabled-blockt-z", s["unten"] == 0 und s["oben"] == 0)
oben["aktiv"] = wahr
uim_mock_maus(k, 220, 35, wahr)
uim_mock_maus(k, 220, 35, falsch)
pruef("z-order-oben", s["unten"] == 0 und s["oben"] == 1)
oben["aktiv"] = falsch

# Scroll-Clip und Rad: unsichtbares Kind ist nicht treffbar, nach Scroll schon.
setze sc auf uim_hinzu(k, uim_scroll(20, 100, 120, 60, 200))
setze clip_kind auf uim_hinzu(sc, uim_knopf("C", 10, 80, 70, 24, klick_clip))
uim_mock_maus(k, 35, 185, wahr)
uim_mock_maus(k, 35, 185, falsch)
pruef("clip-hit-test", s["clip_kind"] == 0)
uim_mock_rad(k, 30, 130, 1)
pruef("rad-scrollt", sc["scroll_y"] > 0)
uim_mock_maus(k, 35, 145, wahr)
uim_mock_maus(k, 35, 145, falsch)
pruef("scroll-transform-klick", s["clip_kind"] == 1)

# Scrollbar-Capture: rechte Hitzone, Drag bis ans Ende, Release raeumt auf.
uim_mock_maus(k, 136, 112, wahr)
uim_mock_bewegung(k, 136, 158)
uim_mock_maus(k, 136, 158, falsch)
pruef("scrollbar-draggable", sc["scroll_y"] > 125)
pruef("scrollbar-release", k["druck"] == nichts und sc["druck"] == falsch)

# Slider-Capture verlaesst Widget; Deaktivierung waehrend Capture bricht ab.
setze panel2 auf uim_hinzu(k, uim_panel("S", 180, 80, 170, 70))
setze slider auf uim_hinzu(panel2, uim_slider(10, 10, 100, 24, 0, 100, 0, slider_geaendert))
uim_mock_maus(k, 210, 102, wahr)
uim_mock_bewegung(k, 390, 102)
uim_mock_maus(k, 390, 102, falsch)
pruef("slider-drag-aussen", slider["wert"] == 100)
pruef("slider-release", k["druck"] == nichts und slider["druck"] == falsch)

uim_slider_setze(k, slider, 0)
setze cb_vor_disable auf s["slider_cb"]
uim_mock_maus(k, 215, 102, wahr)
setze wert_vor_disable auf slider["wert"]
slider["aktiv"] = falsch
uim_mock_bewegung(k, 390, 102)
uim_mock_maus(k, 390, 102, falsch)
pruef("slider-disable-cancel", slider["wert"] == wert_vor_disable und k["druck"] == nichts)
pruef("slider-disable-kein-callback", s["slider_cb"] == cb_vor_disable + 1)
slider["aktiv"] = wahr

uim_slider_setze(k, slider, 0)
uim_mock_maus(k, 215, 102, wahr)
setze wert_vor_parent_disable auf slider["wert"]
panel2["aktiv"] = falsch
uim_mock_bewegung(k, 390, 102)
uim_mock_maus(k, 390, 102, falsch)
pruef("slider-parent-disable-cancel", slider["wert"] == wert_vor_parent_disable und k["druck"] == nichts)
panel2["aktiv"] = wahr

# Tab-Reihenfolge beginnt beim ersten Ziel und traversiert Container.
k["fokus"] = nichts
uim_mock_taste(k, "Tab", wahr, 0)
setze fokus_1 auf k["fokus"]
pruef("tab-erster-nested", fokus_1 != nichts und fokus_1["uid"] == nested["uid"])
uim_mock_taste(k, "Tab", wahr, 0)
setze fokus_2 auf k["fokus"]
pruef("tab-naechster-top", fokus_2 != nichts und fokus_2["uid"] == unten["uid"])
uim_mock_taste(k, "Tab", wahr, 1)
setze fokus_3 auf k["fokus"]
pruef("shift-tab-zurueck", fokus_3 != nichts und fokus_3["uid"] == nested["uid"])

# Liste: Tastaturauswahl bleibt im Viewport sichtbar.
setze zeilen auf []
setze zi auf 0
solange zi < 12:
    zeilen.hinzufügen("Z" + text(zi))
    setze zi auf zi + 1
setze liste auf uim_hinzu(k, uim_liste(180, 170, 120, 42, zeilen, liste_gewaehlt))
k["fokus"] = liste
setze ti auf 0
solange ti < 7:
    uim_mock_taste(k, "Down", wahr, 0)
    setze ti auf ti + 1
pruef("liste-key-auswahl", liste["auswahl"] == 6)
pruef("liste-key-sichtbar", liste["scroll_y"] > 0)

# Staler Fokus in deaktiviertem Vorfahren darf nicht mutieren.
k["fokus"] = slider
panel2["aktiv"] = falsch
setze wert_vor_taste auf slider["wert"]
uim_mock_taste(k, "Right", wahr, 0)
pruef("disabled-parent-fokus-blockiert", slider["wert"] == wert_vor_taste und k["fokus"] == nichts)
panel2["aktiv"] = wahr

# Blur waehrend gehaltenem Polling-Button: bis zum echten Release blockieren.
setze blur_btn auf uim_hinzu(k, uim_knopf("B", 310, 230, 70, 24, klick_blur))
uim_mock_maus(k, 330, 240, wahr)
uim_mock_fokus(k, falsch)
uim_mock_fokus(k, wahr)
uim_backend_maus_taste(k, 330, 240, 2, falsch)
uim_mock_maus(k, 330, 240, wahr)
uim_mock_maus(k, 330, 240, falsch)
pruef("blur-kein-synth-press", s["blur_klick"] == 0 und k["druck"] == nichts)
uim_mock_maus(k, 330, 240, wahr)
uim_mock_maus(k, 330, 240, falsch)
pruef("blur-release-reaktiviert", s["blur_klick"] == 1)

# Render-/Resize-Gates laufen auf frischem Kontext, damit Eingabe-Lifetimes
# und Commandbuffer-Vertrag unabhaengig diagnostizierbar bleiben.
setze rk auf uim_mock_wurzel(200, 140)
setze rp auf uim_hinzu(rk, uim_panel("R", 5, 5, 180, 120))
setze rs auf uim_hinzu(rp, uim_scroll(5, 20, 150, 80, 200))
uim_hinzu(rs, uim_label("clip", 10, 100, 60, 20))
uim_neuzeichnen(rk)
setze rz auf rk["backend_zustand"]
pruef("nested-hinzu-explizit-invalidiert", rz["ungueltig"])

uim_mock_groesse_setze(rk, 640, 480)
setze rw auf rk["wurzel"]
pruef("resize-root", rw["b"] == 640 und rw["h"] == 480)
uim_mock_groesse_setze(rk, 0 - 5, 0 - 7)
setze rw2 auf rk["wurzel"]
pruef("resize-klemmt", rw2["b"] == 0 und rw2["h"] == 0)
uim_mock_groesse_setze(rk, 200, 140)

uim_mock_zeichne(rk)
setze dunkel auf uim_mock_befehle(rk)
setze hat_clip_setze auf falsch
setze hat_clip_loesche auf falsch
für befehl in dunkel:
    wenn befehl["op"] == "clip_setze":
        setze hat_clip_setze auf wahr
    wenn befehl["op"] == "clip_loesche":
        setze hat_clip_loesche auf wahr
setze erster_dunkel auf dunkel[0]
setze args_dunkel auf erster_dunkel["args"]
setze rgba_dunkel auf args_dunkel[0]
uim_theme_setze(rk, uim_theme_hell())
uim_mock_zeichne(rk)
setze hell auf uim_mock_befehle(rk)
setze erster_hell auf hell[0]
setze args_hell auf erster_hell["args"]
setze rgba_hell auf args_hell[0]
pruef("clip-kommandos", hat_clip_setze und hat_clip_loesche)
pruef("theme-wechsel", rgba_dunkel[0] != rgba_hell[0])

wenn s["fehler"] == 0:
    zeige "P016-F1-CONTRACT-OK"
sonst:
    zeige "P016-F1-CONTRACT-FEHLER " + text(s["fehler"])
    wirf "P016-F1 Widgetvertrag verletzt"
