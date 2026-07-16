# ============================================================
# Moo Glass Gallery — P016-O5 UI1
#
# Das Bild wird vollstaendig toolkitfrei auf MOO_SURFACE erzeugt. Der normale
# Pfad zeigt es anschliessend in einem nativen Fenster; der Non-UI-Modus
# MOO_GLASS_GALLERY_NON_UI=1 rendert/prueft nur die Surface.
# ============================================================

importiere ui
importiere ui_moo_effects

setze G auf {}
G["breite"] = 1080
G["hoehe"] = 650
G["karten"] = []

funktion gallery_hintergrund(z):
    surface_clear(z, 10, 18, 34, 255)
    setze y auf 0
    solange y < G["hoehe"]:
        setze x auf 0
        solange x < G["breite"]:
            setze band auf (boden(x / 80) + boden(y / 60)) % 4
            wenn band == 0:
                surface_rect(z, x, y, 80, 60, 18, 56, 98, 255)
            sonst wenn band == 1:
                surface_rect(z, x, y, 80, 60, 38, 48, 96, 255)
            sonst wenn band == 2:
                surface_rect(z, x, y, 80, 60, 24, 82, 104, 255)
            sonst:
                surface_rect(z, x, y, 80, 60, 54, 38, 92, 255)
            setze x auf x + 80
        setze y auf y + 60
    # Helle Geometrie hinter den Karten macht Backdrop-Blur sichtbar.
    surface_circle(z, 180, 150, 92, 80, 178, 255, 210)
    surface_circle(z, 850, 470, 120, 255, 96, 170, 180)
    surface_roundrect(z, 390, 70, 310, 90, 28, 255, 196, 92, 190)
    surface_roundrect(z, 360, 430, 300, 110, 38, 72, 210, 166, 190)
    gib_zurück wahr

funktion gallery_effekt(variante):
    setze e auf uim_effekt_neu()
    uim_effekt_ecken_setze(e, 22, 22, 22, 22)
    uim_effekt_schatten_setze(e, 4, 10, 8, 2, 0, 0, 0, 132)
    uim_effekt_hintergrund_setze(e, 8, 336, 92, 156, 255, 255, 92, 18, 20260712 + variante)
    wenn variante == 4:
        uim_effekt_transform_setze(e, 68608, 2048, 0 - 2048, 68608, 131072, 65536, 0, 0)
    sonst:
        uim_effekt_transform_setze(e, 65536, 0, 0, 65536, 0, 0, 0, 0)
    uim_effekt_animation_aktiv_setze(e, wahr)
    e["fallback"] = 1
    gib_zurück e

funktion gallery_karte(z, titel, detail, x, y, faehigkeiten, fallback, reduziert, fortschritt, variante):
    setze e auf gallery_effekt(variante)
    e["fallback"] = fallback
    setze a auf uim_effekt_aufloesen(e, faehigkeiten)
    setze zeichnung auf nichts
    wenn a["ok"]:
        setze zeichnung auf uim_effekt_vorschau_zeichnen(z, x, y, 300, 160, a, reduziert, fortschritt)
    sonst:
        # Sichtbare atomare NOGO-Stufe: kein teilweise angewandter Effekt.
        surface_roundrect(z, x, y, 300, 160, 18, 96, 26, 40, 255)
        surface_roundrect(z, x + 5, y + 5, 290, 150, 14, 150, 42, 58, 255)
        setze zeichnung auf {}
        zeichnung["ok"] = falsch
        zeichnung["effektiv"] = 0
    setze k auf {}
    k["titel"] = titel
    k["detail"] = detail
    k["x"] = x
    k["y"] = y
    k["aufloesung"] = a
    k["zeichnung"] = zeichnung
    G["karten"].hinzufügen(k)
    gib_zurück k

funktion gallery_bauen(pfad):
    setze z auf surface_new(G["breite"], G["hoehe"])
    wenn z == nichts:
        gib_zurück nichts
    gallery_hintergrund(z)

    # Voller portabler Pfad: alle Effekte und animierte Halbzeit.
    gallery_karte(z, "FULL GLASS", "caps=255 req=255 eff=255", 35, 40, 255, 1, falsch, 32768, 0)
    # Harte reduzierte Faehigkeit: nur Corner+Shadow, exakte Degradation.
    gallery_karte(z, "SAFE FALLBACK", "caps=3 degraded=252", 390, 40, 3, 1, falsch, 65536, 1)
    # Approximation ist protokolltreu nur fuer Blur/Saturation/Noise.
    gallery_karte(z, "APPROX BLUR", "caps=251 missing=blur", 745, 40, 251, 2, falsch, 65536, 2)
    # Nicht approximierbare Toenung: atomare rote NOGO-Karte.
    gallery_karte(z, "STRICT NOGO", "caps=239 missing=tint", 35, 355, 239, 2, falsch, 65536, 3)
    # Transformierte Karte zeigt Q16.16 Scale/Shear/Translation.
    gallery_karte(z, "TRANSFORM", "Q16 scale+shear+translate", 390, 355, 255, 1, falsch, 65536, 4)
    # Reduced Motion springt trotz progress=1 sofort auf den Endzustand.
    gallery_karte(z, "REDUCED MOTION", "progress=1 -> endpoint", 745, 355, 255, 1, wahr, 1, 5)

    setze frame auf surface_snapshot_to_frame(z)
    wenn frame == nichts:
        gib_zurück nichts
    wenn test_frame_save_bmp(frame, pfad) == falsch:
        gib_zurück nichts
    setze ergebnis auf {}
    ergebnis["surface"] = z
    ergebnis["frame"] = frame
    ergebnis["pfad"] = pfad
    ergebnis["hash"] = surface_hash(z)
    ergebnis["karten"] = G["karten"]
    gib_zurück ergebnis

funktion gallery_label(fenster, karte, bild_x, bild_y):
    setze a auf karte["aufloesung"]
    setze farbe_text auf karte["titel"]
    wenn a["ok"] == falsch:
        setze farbe_text auf farbe_text + " — NOGO"
    ui_label(fenster, farbe_text, bild_x + karte["x"] + 12, bild_y + karte["y"] + 10, 270, 24)
    setze status auf karte["detail"] + " | requested=" + text(a["angefordert"]["aktiv"]) + " effective="
    wenn a["effektiv"] == nichts:
        setze status auf status + "0"
    sonst:
        setze status auf status + text(a["effektiv"]["aktiv"])
    setze status auf status + " degraded=" + text(a["degradiert"])
    ui_label(fenster, status, bild_x + karte["x"] + 12, bild_y + karte["y"] + 120, 276, 36)
    gib_zurück wahr

setze bmp_pfad auf umgebung("MOO_GLASS_GALLERY_BMP")
wenn bmp_pfad == nichts:
    setze bmp_pfad auf "moo_glass_gallery.bmp"
setze ergebnis auf gallery_bauen(bmp_pfad)
wenn ergebnis == nichts:
    wirf "Glass Gallery konnte nicht gerendert werden"

zeige "P016-UI1-GALLERY-HASH " + ergebnis["hash"]
zeige "P016-UI1-GALLERY-BMP " + bmp_pfad
zeige "P016-UI1-GALLERY-CARDS " + text(länge(ergebnis["karten"]))

setze non_ui auf umgebung("MOO_GLASS_GALLERY_NON_UI")
wenn non_ui == "1":
    wenn ergebnis["hash"] != "10996657a7b5aeef" oder länge(ergebnis["karten"]) != 6:
        wirf "Glass Gallery Non-UI-Golden verletzt"
    zeige "P016-UI1-GLASS-GALLERY-NONUI-OK"
sonst:
    setze nonce auf umgebung("MOO_GLASS_GALLERY_NONCE")
    wenn nonce == nichts:
        setze nonce auf "LOCAL"
    setze titel auf "Moo Glass Gallery — " + nonce
    setze fenster auf ui_fenster(titel, 1120, 760, 0, nichts)
    ui_label(fenster, "MOO GLASS / AERO EFFECTS — Backend + Capabilities + Parameter", 20, 12, 1060, 28)
    ui_bild(fenster, bmp_pfad, 20, 50, G["breite"], G["hoehe"])
    für karte in G["karten"]:
        gallery_label(fenster, karte, 20, 50)
    ui_label(fenster, "backend=portable-vorschau | native Hostadapter separat | nonce=" + nonce, 20, 708, 1060, 24)
    zeige "P016-UI1-GALLERY-WINDOW " + titel
    ui_zeige_nebenbei(fenster)
    ui_laufen()
