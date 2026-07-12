# P016-O1: echter ui_moo-Widgetbaum auf toolkitfreier RGBA-Surface.
# Golden- und State-Hash sind bewusst fest; keine automatische Aktualisierung.

importiere ui_moo_surface

setze GOLDEN_HASH auf "71c13f0a4596278d"
setze STATE_HASH auf "b29855ed9cc393b9"

setze status auf {}
status["fehler"] = 0
status["klicks"] = 0
status["slider_cb"] = 0

funktion pruef(name, ok):
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        status["fehler"] = status["fehler"] + 1
    gib_zurück ok

funktion rgba_ist(pixel, r, g, b, a):
    wenn pixel == nichts:
        gib_zurück falsch
    gib_zurück pixel["rot"] == r und pixel["gruen"] == g und pixel["blau"] == b und pixel["alpha"] == a

funktion auf_ok(w):
    status["klicks"] = status["klicks"] + 1
    gib_zurück wahr

funktion auf_slider(w, wert):
    status["slider_cb"] = status["slider_cb"] + 1
    gib_zurück wahr

setze k auf uim_surface_wurzel(160, 120)
pruef("surface-wurzel", k != nichts)
uim_hinzu(k, uim_label("A", 4, 4, 20, 12))
setze knopf auf uim_hinzu(k, uim_knopf("OK", 20, 20, 64, 24, auf_ok))
setze regler auf uim_hinzu(k, uim_slider(20, 52, 100, 20, 0, 100, 25, auf_slider))
setze rolle auf uim_hinzu(k, uim_scroll(20, 80, 120, 30, 80))
uim_hinzu(rolle, uim_label("CLIP", 4, 4, 60, 12))
uim_hinzu(rolle, uim_label("AUSSEN", 4, 40, 80, 12))

pruef("erstes-render", uim_surface_zeichne(k))
setze golden_ist auf uim_surface_hash(k)
zeige "P016-O1-GOLDEN-AKTUELL " + golden_ist

# Schlüsselpixel verhindern, dass ein versehentlich neu gebless-ter Hash
# einen leeren oder falsch orientierten Frame maskiert.
pruef("root-rgba", rgba_ist(uim_surface_pixel(k, 159, 119), 30, 32, 38, 255))
pruef("font-A-pixel", rgba_ist(uim_surface_pixel(k, 6, 4), 230, 232, 238, 255))
pruef("button-flaeche", rgba_ist(uim_surface_pixel(k, 30, 30), 58, 62, 74, 255))
pruef("scroll-kind-sichtbar", rgba_ist(uim_surface_pixel(k, 26, 84), 230, 232, 238, 255))
pruef("scroll-kind-geclippt", rgba_ist(uim_surface_pixel(k, 26, 114), 30, 32, 38, 255))
pruef("golden-hash", golden_ist == GOLDEN_HASH)

# Volles Repaint auf derselben Surface muss wegen opakem Hintergrund bytegleich sein.
pruef("zweites-render", uim_surface_zeichne(k))
pruef("repaint-deterministisch", uim_surface_hash(k) == golden_ist)

# Normalisierter Input ohne Hosteventloop: Callback, Sliderzustand und Fokus.
uim_surface_maus(k, 40, 32, wahr)
uim_surface_maus(k, 40, 32, falsch)
pruef("button-callback", status["klicks"] == 1)
uim_surface_maus(k, 95, 62, wahr)
uim_surface_maus(k, 95, 62, falsch)
pruef("slider-callback", status["slider_cb"] > 0)
pruef("slider-wert", regler["wert"] > 70 und regler["wert"] < 80)
pruef("state-render", uim_surface_zeichne(k))
setze state_ist auf uim_surface_hash(k)
zeige "P016-O1-STATE-AKTUELL " + state_ist
pruef("state-hash", state_ist == STATE_HASH)
pruef("state-unterscheidet-golden", state_ist != golden_ist)

# Langlauf: kein Commandbuffer, keine Hostqueue, stabile Pixelzustände.
setze runde auf 0
setze langlauf_ok auf wahr
solange runde < 500:
    wenn uim_surface_zeichne(k) == falsch:
        setze langlauf_ok auf falsch
    wenn uim_surface_hash(k) != state_ist:
        setze langlauf_ok auf falsch
    setze runde auf runde + 1
pruef("langlauf-500-stabil", langlauf_ok)

setze moment auf uim_surface_snapshot(k)
pruef("snapshot-frame", moment != nichts)

# Clipstack-Ueberlauf muss fail-closed bleiben: kein Kind/Pop beim
# fehlgeschlagenen Push, alle erfolgreichen Vorfahren poppen exakt einmal.
setze clip_k auf uim_surface_wurzel(64, 64)
setze aussen auf uim_hinzu(clip_k, uim_scroll(8, 8, 24, 24, 240))
setze tief auf aussen
setze tiefe auf 0
solange tiefe < 40:
    setze naechster auf uim_hinzu(tief, uim_scroll(0, 0, 24, 24, 240))
    setze tief auf naechster
    setze tiefe auf tiefe + 1
# Dieses spaet gezeichnete Geschwister liegt ausserhalb des aeusseren Clips.
uim_hinzu(aussen, uim_label("A", 30, 0, 12, 12))
setze clip_render_ok auf uim_surface_zeichne(clip_k)
pruef("clipoverflow-render-falsch", clip_render_ok == falsch)
pruef("clipoverflow-ungueltig", clip_k["backend_zustand"]["ungueltig"] == wahr)
pruef("clipoverflow-kein-overdraw", rgba_ist(uim_surface_pixel(clip_k, 40, 8), 30, 32, 38, 255))
pruef("clipoverflow-stack-ausgeglichen", surface_clip_pop(uim_surface_handle(clip_k)) == falsch)

wenn status["fehler"] == 0:
    zeige "P016-O1-SURFACE-GOLDEN-OK"
sonst:
    zeige "P016-O1-SURFACE-GOLDEN-FEHLER " + text(status["fehler"])
    wirf "P016-O1 Widget-Golden verletzt"
