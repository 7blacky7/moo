# P016-O5 UI1: deterministische sichtbare Glass-Vorschau ohne Desktop-UI.
# Die normative Pixelreferenz bleibt moo_compositor_effects_cpu.c; diese Moo-
# Vorschau ist der explizite portable Fallback fuer Gallery/Framebuffer.

importiere ui_moo_effects

setze GOLDEN_HASH auf "7a7922e92244858f"
setze qa auf {}
qa["fehler"] = 0

funktion pruef(name, ok):
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        qa["fehler"] = qa["fehler"] + 1
    gib_zurück ok

funktion rgba_ist(p, r, g, b, a):
    gib_zurück p != nichts und p["rot"] == r und p["gruen"] == g und p["blau"] == b und p["alpha"] == a

funktion rgba_gleich(a, b):
    gib_zurück a != nichts und b != nichts und a["rot"] == b["rot"] und a["gruen"] == b["gruen"] und a["blau"] == b["blau"] und a["alpha"] == b["alpha"]

funktion hintergrund(z):
    surface_clear(z, 18, 28, 48, 255)
    setze y auf 0
    solange y < 64:
        setze x auf 0
        solange x < 96:
            wenn (boden(x / 8) + boden(y / 8)) % 2 == 0:
                surface_rect(z, x, y, 8, 8, 36, 82, 132, 255)
            sonst:
                surface_rect(z, x, y, 8, 8, 16, 42, 78, 255)
            setze x auf x + 8
        setze y auf y + 8
    gib_zurück wahr

funktion glass_effekt():
    setze e auf uim_effekt_neu()
    uim_effekt_ecken_setze(e, 10, 10, 10, 10)
    uim_effekt_schatten_setze(e, 2, 4, 5, 1, 0, 0, 0, 112)
    uim_effekt_hintergrund_setze(e, 4, 320, 90, 160, 255, 255, 84, 20, 4242)
    uim_effekt_transform_setze(e, 67584, 0, 0, 67584, 65536, 0, 0, 0)
    uim_effekt_animation_aktiv_setze(e, wahr)
    e["fallback"] = 1
    gib_zurück e

setze z auf surface_new(96, 64)
pruef("surface", z != nichts)
hintergrund(z)
setze vor_aussen auf surface_read_pixel(z, 2, 2)
setze vor_ecke auf surface_read_pixel(z, 18, 14)
setze voll auf uim_effekt_aufloesen(glass_effekt(), 255)
setze zeichnung auf uim_effekt_vorschau_zeichnen(z, 16, 12, 62, 38, voll, falsch, 32768)
pruef("preview-ok", zeichnung["ok"])
pruef("preview-mode", zeichnung["modus"] == "portable-vorschau")
pruef("preview-all", zeichnung["effektiv"] == 255)
pruef("outside-unchanged", rgba_gleich(surface_read_pixel(z, 2, 2), vor_aussen))
setze nach_ecke auf surface_read_pixel(z, 18, 14)
setze mitte auf surface_read_pixel(z, 48, 32)
pruef("corner-clipped", nach_ecke["rot"] != mitte["rot"] oder nach_ecke["gruen"] != mitte["gruen"] oder nach_ecke["blau"] != mitte["blau"])
pruef("glass-center-changed", rgba_ist(surface_read_pixel(z, 48, 32), 36, 82, 132, 255) == falsch)
pruef("shadow-visible", rgba_ist(surface_read_pixel(z, 50, 55), 18, 28, 48, 255) == falsch)

setze hash1 auf surface_hash(z)
zeige "P016-UI1-GOLDEN-AKTUELL " + hash1
pruef("golden", hash1 == GOLDEN_HASH)

# Frischer identischer Frame muss bytegleich sein.
setze z2 auf surface_new(96, 64)
hintergrund(z2)
setze voll2 auf uim_effekt_aufloesen(glass_effekt(), 255)
setze zeichnung2 auf uim_effekt_vorschau_zeichnen(z2, 16, 12, 62, 38, voll2, falsch, 32768)
pruef("repeat-ok", zeichnung2["ok"])
pruef("repeat-hash", surface_hash(z2) == hash1)

# Fallbackstufe ohne Backdrop/Color/Transform/Animation bleibt sichtbar,
# meldet aber die exakte Degradation und erzeugt andere Pixel.
setze fallback_effekt auf glass_effekt()
setze klein auf uim_effekt_aufloesen(fallback_effekt, 3)
pruef("fallback-resolved", klein["ok"] und klein["degradiert"] == 252)
setze z3 auf surface_new(96, 64)
hintergrund(z3)
setze zeichnung3 auf uim_effekt_vorschau_zeichnen(z3, 16, 12, 62, 38, klein, wahr, 1)
pruef("fallback-preview", zeichnung3["ok"] und zeichnung3["effektiv"] == 3)
pruef("fallback-distinct", surface_hash(z3) != hash1)

# Fehlgeschlagene Aufloesung darf keinen Pixel veraendern.
setze strikt auf glass_effekt()
strikt["fallback"] = 0
setze nicht_ok auf uim_effekt_aufloesen(strikt, 3)
setze z4 auf surface_new(96, 64)
hintergrund(z4)
setze vorher_hash auf surface_hash(z4)
setze abgewiesen auf uim_effekt_vorschau_zeichnen(z4, 16, 12, 62, 38, nicht_ok, falsch, 0)
pruef("nogo-rejected", abgewiesen["ok"] == falsch)
pruef("nogo-no-pixels", surface_hash(z4) == vorher_hash)

wenn qa["fehler"] == 0:
    zeige "P016-UI1-EFFECTS-GOLDEN-OK"
sonst:
    zeige "P016-UI1-EFFECTS-GOLDEN-FEHLER " + text(qa["fehler"])
    wirf "P016 UI1 Effekt-Golden verletzt"
