# ============================================================
# stdlib/ui_moo_effects.moo — oeffentliche portable Effekt-API
#
# Moo-Dicts spiegeln die semantischen Werte des Compositor-Effects-Protokolls
# V2. Sie sind keine C-Struct-/Wire-Abbildung. Dadurch bleibt die API auf
# Desktop, Framebuffer und Moo-OS identisch und behauptet keine Hostfaehigkeit.
# requested/effective/degraded sowie Fallback sind immer beobachtbar.
# ============================================================

funktion _uime_ganzzahl_in(wert, lo, hi):
    gib_zurück wert >= lo und wert <= hi und wert % 1 == 0

funktion _uime_abs(wert):
    wenn wert < 0:
        gib_zurück 0 - wert
    gib_zurück wert

funktion _uime_bit_an(mask, bit):
    gib_zurück boden(mask / bit) % 2 == 1

funktion _uime_bit_setze(mask, bit, an):
    setze ist_an auf _uime_bit_an(mask, bit)
    wenn an und ist_an == falsch:
        gib_zurück mask + bit
    wenn an == falsch und ist_an:
        gib_zurück mask - bit
    gib_zurück mask

funktion _uime_farbe(r, g, b, a):
    setze f auf {}
    f["r"] = r
    f["g"] = g
    f["b"] = b
    f["a"] = a
    gib_zurück f

funktion _uime_farbe_gueltig(r, g, b, a):
    gib_zurück _uime_ganzzahl_in(r, 0, 255) und _uime_ganzzahl_in(g, 0, 255) und _uime_ganzzahl_in(b, 0, 255) und _uime_ganzzahl_in(a, 0, 255)

funktion uim_effekt_faehigkeiten():
    setze f auf {}
    f["ecken"] = 1
    f["schatten"] = 2
    f["hintergrund_unschaerfe"] = 4
    f["saettigung"] = 8
    f["toenung"] = 16
    f["rauschen"] = 32
    f["transform"] = 64
    f["animation"] = 128
    f["alle"] = 255
    gib_zurück f

funktion uim_effekt_neu():
    setze e auf {}
    e["version"] = 2
    e["aktiv"] = 0
    e["erforderlich"] = 0
    e["fallback"] = 0

    setze k auf {}
    k["oben_links"] = 0
    k["oben_rechts"] = 0
    k["unten_rechts"] = 0
    k["unten_links"] = 0
    e["ecken"] = k

    setze s auf {}
    s["x"] = 0
    s["y"] = 0
    s["unschaerfe"] = 0
    s["ausbreitung"] = 0
    s["farbe"] = _uime_farbe(0, 0, 0, 0)
    e["schatten"] = s

    setze h auf {}
    h["unschaerfe"] = 0
    h["saettigung"] = 256
    h["toenung"] = _uime_farbe(0, 0, 0, 0)
    h["toenung_mix"] = 0
    h["rauschen"] = 0
    h["rauschen_seed"] = 0
    e["hintergrund"] = h

    setze t auf {}
    t["m11"] = 65536
    t["m12"] = 0
    t["m21"] = 0
    t["m22"] = 65536
    t["tx"] = 0
    t["ty"] = 0
    t["ursprung_x"] = 0
    t["ursprung_y"] = 0
    e["transform"] = t
    gib_zurück e

funktion _uime_kopie(e):
    setze z auf uim_effekt_neu()
    z["aktiv"] = e["aktiv"]
    z["erforderlich"] = e["erforderlich"]
    z["fallback"] = e["fallback"]

    z["ecken"]["oben_links"] = e["ecken"]["oben_links"]
    z["ecken"]["oben_rechts"] = e["ecken"]["oben_rechts"]
    z["ecken"]["unten_rechts"] = e["ecken"]["unten_rechts"]
    z["ecken"]["unten_links"] = e["ecken"]["unten_links"]

    z["schatten"]["x"] = e["schatten"]["x"]
    z["schatten"]["y"] = e["schatten"]["y"]
    z["schatten"]["unschaerfe"] = e["schatten"]["unschaerfe"]
    z["schatten"]["ausbreitung"] = e["schatten"]["ausbreitung"]
    setze sf auf e["schatten"]["farbe"]
    z["schatten"]["farbe"] = _uime_farbe(sf["r"], sf["g"], sf["b"], sf["a"])

    z["hintergrund"]["unschaerfe"] = e["hintergrund"]["unschaerfe"]
    z["hintergrund"]["saettigung"] = e["hintergrund"]["saettigung"]
    setze hf auf e["hintergrund"]["toenung"]
    z["hintergrund"]["toenung"] = _uime_farbe(hf["r"], hf["g"], hf["b"], hf["a"])
    z["hintergrund"]["toenung_mix"] = e["hintergrund"]["toenung_mix"]
    z["hintergrund"]["rauschen"] = e["hintergrund"]["rauschen"]
    z["hintergrund"]["rauschen_seed"] = e["hintergrund"]["rauschen_seed"]

    z["transform"]["m11"] = e["transform"]["m11"]
    z["transform"]["m12"] = e["transform"]["m12"]
    z["transform"]["m21"] = e["transform"]["m21"]
    z["transform"]["m22"] = e["transform"]["m22"]
    z["transform"]["tx"] = e["transform"]["tx"]
    z["transform"]["ty"] = e["transform"]["ty"]
    z["transform"]["ursprung_x"] = e["transform"]["ursprung_x"]
    z["transform"]["ursprung_y"] = e["transform"]["ursprung_y"]
    gib_zurück z

funktion uim_effekt_fallback_setze(e, fallback):
    wenn _uime_ganzzahl_in(fallback, 0, 2) == falsch:
        gib_zurück falsch
    e["fallback"] = fallback
    gib_zurück wahr

funktion uim_effekt_erforderlich_setze(e, mask):
    wenn _uime_ganzzahl_in(mask, 0, 255) == falsch:
        gib_zurück falsch
    setze bit auf 1
    solange bit <= 128:
        wenn _uime_bit_an(mask, bit) und _uime_bit_an(e["aktiv"], bit) == falsch:
            gib_zurück falsch
        setze bit auf bit * 2
    e["erforderlich"] = mask
    gib_zurück wahr

funktion uim_effekt_ecken_setze(e, oben_links, oben_rechts, unten_rechts, unten_links):
    wenn _uime_ganzzahl_in(oben_links, 0, 4096) == falsch oder _uime_ganzzahl_in(oben_rechts, 0, 4096) == falsch oder _uime_ganzzahl_in(unten_rechts, 0, 4096) == falsch oder _uime_ganzzahl_in(unten_links, 0, 4096) == falsch:
        gib_zurück falsch
    e["ecken"]["oben_links"] = oben_links
    e["ecken"]["oben_rechts"] = oben_rechts
    e["ecken"]["unten_rechts"] = unten_rechts
    e["ecken"]["unten_links"] = unten_links
    setze an auf oben_links + oben_rechts + unten_rechts + unten_links > 0
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 1, an)
    e["erforderlich"] = _uime_bit_setze(e["erforderlich"], 1, an und _uime_bit_an(e["erforderlich"], 1))
    gib_zurück wahr

funktion uim_effekt_schatten_setze(e, x, y, unschaerfe, ausbreitung, r, g, b, a):
    wenn _uime_ganzzahl_in(x, 0 - 4096, 4096) == falsch oder _uime_ganzzahl_in(y, 0 - 4096, 4096) == falsch oder _uime_ganzzahl_in(unschaerfe, 0, 64) == falsch oder _uime_ganzzahl_in(ausbreitung, 0, 64) == falsch oder _uime_farbe_gueltig(r, g, b, a) == falsch:
        gib_zurück falsch
    e["schatten"]["x"] = x
    e["schatten"]["y"] = y
    e["schatten"]["unschaerfe"] = unschaerfe
    e["schatten"]["ausbreitung"] = ausbreitung
    e["schatten"]["farbe"] = _uime_farbe(r, g, b, a)
    setze an auf x != 0 oder y != 0 oder unschaerfe != 0 oder ausbreitung != 0 oder r != 0 oder g != 0 oder b != 0 oder a != 0
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 2, an)
    e["erforderlich"] = _uime_bit_setze(e["erforderlich"], 2, an und _uime_bit_an(e["erforderlich"], 2))
    gib_zurück wahr

funktion uim_effekt_hintergrund_setze(e, unschaerfe, saettigung, r, g, b, a, mix, rauschen, seed):
    wenn _uime_ganzzahl_in(unschaerfe, 0, 32) == falsch oder _uime_ganzzahl_in(saettigung, 256, 1024) == falsch oder _uime_farbe_gueltig(r, g, b, a) == falsch oder _uime_ganzzahl_in(mix, 0, 255) == falsch oder _uime_ganzzahl_in(rauschen, 0, 255) == falsch oder _uime_ganzzahl_in(seed, 0, 4294967295) == falsch:
        gib_zurück falsch
    e["hintergrund"]["unschaerfe"] = unschaerfe
    e["hintergrund"]["saettigung"] = saettigung
    e["hintergrund"]["toenung"] = _uime_farbe(r, g, b, a)
    e["hintergrund"]["toenung_mix"] = mix
    e["hintergrund"]["rauschen"] = rauschen
    e["hintergrund"]["rauschen_seed"] = seed
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 4, unschaerfe > 0)
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 8, saettigung != 256)
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 16, mix > 0)
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 32, rauschen > 0)
    setze bit auf 4
    solange bit <= 32:
        wenn _uime_bit_an(e["aktiv"], bit) == falsch:
            e["erforderlich"] = _uime_bit_setze(e["erforderlich"], bit, falsch)
        setze bit auf bit * 2
    gib_zurück wahr

funktion uim_effekt_transform_setze(e, m11, m12, m21, m22, tx, ty, ursprung_x, ursprung_y):
    wenn _uime_ganzzahl_in(m11, 0 - 1048576, 1048576) == falsch oder _uime_ganzzahl_in(m12, 0 - 1048576, 1048576) == falsch oder _uime_ganzzahl_in(m21, 0 - 1048576, 1048576) == falsch oder _uime_ganzzahl_in(m22, 0 - 1048576, 1048576) == falsch oder _uime_ganzzahl_in(tx, 0 - 2147418112, 2147418112) == falsch oder _uime_ganzzahl_in(ty, 0 - 2147418112, 2147418112) == falsch oder _uime_ganzzahl_in(ursprung_x, 0 - 2147418112, 2147418112) == falsch oder _uime_ganzzahl_in(ursprung_y, 0 - 2147418112, 2147418112) == falsch:
        gib_zurück falsch
    e["transform"]["m11"] = m11
    e["transform"]["m12"] = m12
    e["transform"]["m21"] = m21
    e["transform"]["m22"] = m22
    e["transform"]["tx"] = tx
    e["transform"]["ty"] = ty
    e["transform"]["ursprung_x"] = ursprung_x
    e["transform"]["ursprung_y"] = ursprung_y
    setze an auf m11 != 65536 oder m12 != 0 oder m21 != 0 oder m22 != 65536 oder tx != 0 oder ty != 0 oder ursprung_x != 0 oder ursprung_y != 0
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 64, an)
    wenn an == falsch:
        e["erforderlich"] = _uime_bit_setze(e["erforderlich"], 64, falsch)
    gib_zurück wahr

funktion uim_effekt_animation_aktiv_setze(e, an):
    e["aktiv"] = _uime_bit_setze(e["aktiv"], 128, an)
    wenn an == falsch:
        e["erforderlich"] = _uime_bit_setze(e["erforderlich"], 128, falsch)
    gib_zurück wahr

funktion _uime_deaktivieren(e, bit):
    e["aktiv"] = _uime_bit_setze(e["aktiv"], bit, falsch)
    e["erforderlich"] = _uime_bit_setze(e["erforderlich"], bit, falsch)
    wenn bit == 1:
        e["ecken"]["oben_links"] = 0
        e["ecken"]["oben_rechts"] = 0
        e["ecken"]["unten_rechts"] = 0
        e["ecken"]["unten_links"] = 0
    sonst wenn bit == 2:
        e["schatten"]["x"] = 0
        e["schatten"]["y"] = 0
        e["schatten"]["unschaerfe"] = 0
        e["schatten"]["ausbreitung"] = 0
        e["schatten"]["farbe"] = _uime_farbe(0, 0, 0, 0)
    sonst wenn bit == 4:
        e["hintergrund"]["unschaerfe"] = 0
    sonst wenn bit == 8:
        e["hintergrund"]["saettigung"] = 256
    sonst wenn bit == 16:
        e["hintergrund"]["toenung"] = _uime_farbe(0, 0, 0, 0)
        e["hintergrund"]["toenung_mix"] = 0
    sonst wenn bit == 32:
        e["hintergrund"]["rauschen"] = 0
        e["hintergrund"]["rauschen_seed"] = 0
    sonst wenn bit == 64:
        e["transform"]["m11"] = 65536
        e["transform"]["m12"] = 0
        e["transform"]["m21"] = 0
        e["transform"]["m22"] = 65536
        e["transform"]["tx"] = 0
        e["transform"]["ty"] = 0
        e["transform"]["ursprung_x"] = 0
        e["transform"]["ursprung_y"] = 0
    gib_zurück wahr

funktion _uime_aufloesung_fehler(e, faehigkeiten, grund):
    setze r auf {}
    r["ok"] = falsch
    r["grund"] = grund
    r["faehigkeiten"] = faehigkeiten
    r["angefordert"] = _uime_kopie(e)
    r["effektiv"] = nichts
    r["degradiert"] = 0
    gib_zurück r

funktion uim_effekt_aufloesen(e, faehigkeiten):
    wenn _uime_ganzzahl_in(faehigkeiten, 0, 255) == falsch:
        gib_zurück _uime_aufloesung_fehler(e, faehigkeiten, "ungueltige-faehigkeiten")
    wenn _uime_ganzzahl_in(e["aktiv"], 0, 255) == falsch oder _uime_ganzzahl_in(e["erforderlich"], 0, 255) == falsch oder _uime_ganzzahl_in(e["fallback"], 0, 2) == falsch:
        gib_zurück _uime_aufloesung_fehler(e, faehigkeiten, "ungueltiger-descriptor")

    # Preflight ohne Mutation: required/REQUIRE und nicht approximierbare Bits.
    setze bit auf 1
    solange bit <= 128:
        setze fehlt auf _uime_bit_an(e["aktiv"], bit) und _uime_bit_an(faehigkeiten, bit) == falsch
        wenn fehlt:
            wenn _uime_bit_an(e["erforderlich"], bit) oder e["fallback"] == 0:
                gib_zurück _uime_aufloesung_fehler(e, faehigkeiten, "erforderlich-fehlt")
            wenn e["fallback"] == 2 und bit != 4 und bit != 8 und bit != 32:
                gib_zurück _uime_aufloesung_fehler(e, faehigkeiten, "nicht-annaeherbar")
        setze bit auf bit * 2

    setze effektiv auf _uime_kopie(e)
    setze degradiert auf 0
    setze bit auf 1
    solange bit <= 128:
        wenn _uime_bit_an(e["aktiv"], bit) und _uime_bit_an(faehigkeiten, bit) == falsch:
            _uime_deaktivieren(effektiv, bit)
            setze degradiert auf degradiert + bit
        setze bit auf bit * 2

    setze r auf {}
    r["ok"] = wahr
    r["grund"] = "ok"
    r["faehigkeiten"] = faehigkeiten
    r["angefordert"] = _uime_kopie(e)
    r["effektiv"] = effektiv
    r["degradiert"] = degradiert
    gib_zurück r

funktion uim_effekt_animation(eigenschaft, start_wert, ziel_wert, dauer_ms, easing, wiederholungen):
    setze prop_ok auf eigenschaft == "deckkraft" oder eigenschaft == "ecken" oder eigenschaft == "schatten" oder eigenschaft == "hintergrund" oder eigenschaft == "transform"
    setze easing_ok auf easing == "linear" oder easing == "ein" oder easing == "aus" oder easing == "ein_aus"
    wenn prop_ok == falsch oder easing_ok == falsch oder _uime_ganzzahl_in(dauer_ms, 1, 600000) == falsch oder _uime_ganzzahl_in(wiederholungen, 1, 1000000) == falsch:
        gib_zurück nichts
    setze a auf {}
    a["eigenschaft"] = eigenschaft
    a["von"] = start_wert
    a["zu"] = ziel_wert
    a["dauer_ms"] = dauer_ms
    a["easing"] = easing
    a["wiederholungen"] = wiederholungen
    gib_zurück a

funktion uim_effekt_animation_wert(a, vergangen_ms, reduzierte_bewegung):
    wenn reduzierte_bewegung:
        gib_zurück a["zu"]
    wenn vergangen_ms <= 0:
        gib_zurück a["von"]
    setze gesamt auf a["dauer_ms"] * a["wiederholungen"]
    wenn vergangen_ms >= gesamt:
        gib_zurück a["zu"]
    # Exakte interne Grenze startet gemaess V2 den naechsten Play bei from.
    setze lokal auf vergangen_ms % a["dauer_ms"]
    setze t auf lokal / a["dauer_ms"]
    wenn a["easing"] == "ein":
        setze t auf t * t
    sonst wenn a["easing"] == "aus":
        setze u auf 1 - t
        setze t auf 1 - u * u
    sonst wenn a["easing"] == "ein_aus":
        wenn t < 0.5:
            setze t auf 2 * t * t
        sonst:
            setze u auf 1 - t
            setze t auf 1 - 2 * u * u
    gib_zurück runde(a["von"] + (a["zu"] - a["von"]) * t)

funktion uim_effekt_status(backend, aufloesung):
    setze d auf {}
    d["backend"] = backend
    d["faehigkeiten"] = aufloesung["faehigkeiten"]
    d["ok"] = aufloesung["ok"]
    d["grund"] = aufloesung["grund"]
    d["degradiert"] = aufloesung["degradiert"]
    d["angefordert"] = aufloesung["angefordert"]["aktiv"]
    wenn aufloesung["effektiv"] == nichts:
        d["effektiv"] = 0
    sonst:
        d["effektiv"] = aufloesung["effektiv"]["aktiv"]
    gib_zurück d


# Sichtbare portable Vorschau. Die normative Pixelreferenz bleibt der
# freestanding C-Compositor; dieser explizite Fallback nutzt nur MOO_SURFACE.
funktion _uime_klemme_kanal(wert):
    wenn wert < 0:
        gib_zurück 0
    wenn wert > 255:
        gib_zurück 255
    gib_zurück runde(wert)

funktion _uime_max4(a, b, c, d):
    setze m auf a
    wenn b > m:
        setze m auf b
    wenn c > m:
        setze m auf c
    wenn d > m:
        setze m auf d
    gib_zurück m

funktion _uime_im_rundclip(e, lx, ly, b, h):
    setze radius auf 0
    setze cx auf 0
    setze cy auf 0
    wenn lx < b / 2 und ly < h / 2:
        setze radius auf e["ecken"]["oben_links"]
        setze cx auf radius
        setze cy auf radius
    sonst wenn lx >= b / 2 und ly < h / 2:
        setze radius auf e["ecken"]["oben_rechts"]
        setze cx auf b - radius
        setze cy auf radius
    sonst wenn lx >= b / 2 und ly >= h / 2:
        setze radius auf e["ecken"]["unten_rechts"]
        setze cx auf b - radius
        setze cy auf h - radius
    sonst:
        setze radius auf e["ecken"]["unten_links"]
        setze cx auf radius
        setze cy auf h - radius
    wenn radius <= 0:
        gib_zurück wahr
    setze in_ecke auf (lx < radius oder lx >= b - radius) und (ly < radius oder ly >= h - radius)
    wenn in_ecke == falsch:
        gib_zurück wahr
    setze dx auf lx + 0.5 - cx
    setze dy auf ly + 0.5 - cy
    gib_zurück dx * dx + dy * dy <= radius * radius

funktion _uime_preview_noise(seed, x, y, staerke):
    # Gescrambelter Hash statt linearem Muster — lineare Formeln wie
    # (x*17 + y*31) % 251 erzeugen sichtbare Diagonalstreifen im Glas.
    setze hx auf (x * 73856093) % 65521
    setze hy auf (y * 19349663) % 65521
    setze n auf (hx * 31 + hy * 17 + seed % 65521) % 251
    gib_zurück (n - 125) * staerke / 1020

funktion _uime_preview_fehler(grund):
    setze r auf {}
    r["ok"] = falsch
    r["grund"] = grund
    r["modus"] = "portable-vorschau"
    r["effektiv"] = 0
    r["degradiert"] = 0
    gib_zurück r

# Moo-Referenz des Farb-Passes (Blur-Resampling, Saettigung, Toenung,
# Rauschen, Rund-Clip). Der C-Fastpath surface_glass_farbpass portiert diese
# Semantik exakt; die Effekt-Goldens gelten fuer beide Pfade.
funktion _uime_farbpass_langsam(z, px, py, pb, ph, e):
    setze scratch auf []
    setze blur auf e["hintergrund"]["unschaerfe"]
    wenn blur > 24:
        setze blur auf 24
    setze ly auf 0
    solange ly < ph:
        setze lx auf 0
        solange lx < pb:
            wenn _uime_im_rundclip(e, lx, ly, pb, ph):
                setze sum_r auf 0
                setze sum_g auf 0
                setze sum_b auf 0
                setze sum_a auf 0
                setze anzahl auf 0
                setze sy auf 0 - blur
                solange sy <= blur:
                    setze sx auf 0 - blur
                    solange sx <= blur:
                        setze q auf surface_read_pixel(z, px + lx + sx, py + ly + sy)
                        wenn q != nichts:
                            setze sum_r auf sum_r + q["rot"]
                            setze sum_g auf sum_g + q["gruen"]
                            setze sum_b auf sum_b + q["blau"]
                            setze sum_a auf sum_a + q["alpha"]
                            setze anzahl auf anzahl + 1
                        setze sx auf sx + 1
                    setze sy auf sy + 1
                wenn anzahl > 0:
                    setze rr auf sum_r / anzahl
                    setze gg auf sum_g / anzahl
                    setze bb auf sum_b / anzahl
                    setze aa auf sum_a / anzahl
                    wenn _uime_bit_an(e["aktiv"], 8):
                        setze lum auf (rr + gg + bb) / 3
                        setze sat auf e["hintergrund"]["saettigung"] / 256
                        setze rr auf lum + (rr - lum) * sat
                        setze gg auf lum + (gg - lum) * sat
                        setze bb auf lum + (bb - lum) * sat
                    wenn _uime_bit_an(e["aktiv"], 16):
                        setze tf auf e["hintergrund"]["toenung"]
                        setze mix auf e["hintergrund"]["toenung_mix"] / 255
                        setze rr auf rr * (1 - mix) + tf["r"] * mix
                        setze gg auf gg * (1 - mix) + tf["g"] * mix
                        setze bb auf bb * (1 - mix) + tf["b"] * mix
                    wenn _uime_bit_an(e["aktiv"], 32):
                        setze n auf _uime_preview_noise(e["hintergrund"]["rauschen_seed"], px + lx, py + ly, e["hintergrund"]["rauschen"])
                        setze rr auf rr + n
                        setze gg auf gg + n
                        setze bb auf bb + n
                    scratch.hinzufügen([px + lx, py + ly, _uime_klemme_kanal(rr), _uime_klemme_kanal(gg), _uime_klemme_kanal(bb), _uime_klemme_kanal(aa)])
            setze lx auf lx + 1
        setze ly auf ly + 1
    für punkt in scratch:
        surface_rect(z, punkt[0], punkt[1], 1, 1, punkt[2], punkt[3], punkt[4], punkt[5])
    gib_zurück wahr

funktion uim_effekt_vorschau_zeichnen(z, x, y, b, h, aufloesung, reduzierte_bewegung, fortschritt_q16):
    wenn aufloesung["ok"] == falsch:
        gib_zurück _uime_preview_fehler("aufloesung-nogo")
    wenn _uime_ganzzahl_in(x, 0 - 2147418112, 2147418112) == falsch oder _uime_ganzzahl_in(y, 0 - 2147418112, 2147418112) == falsch oder _uime_ganzzahl_in(b, 1, 4096) == falsch oder _uime_ganzzahl_in(h, 1, 4096) == falsch:
        gib_zurück _uime_preview_fehler("ungueltige-geometrie")
    wenn _uime_ganzzahl_in(fortschritt_q16, 0, 65536) == falsch:
        gib_zurück _uime_preview_fehler("ungueltiger-fortschritt")
    setze e auf aufloesung["effektiv"]
    setze px auf x
    setze py auf y
    setze pb auf b
    setze ph auf h
    wenn _uime_bit_an(e["aktiv"], 64):
        setze px auf px + runde(e["transform"]["tx"] / 65536)
        setze py auf py + runde(e["transform"]["ty"] / 65536)
        setze pb auf runde(pb * e["transform"]["m11"] / 65536)
        setze ph auf runde(ph * e["transform"]["m22"] / 65536)
        wenn pb < 1 oder ph < 1:
            gib_zurück _uime_preview_fehler("leere-transform")
    setze radius auf _uime_max4(e["ecken"]["oben_links"], e["ecken"]["oben_rechts"], e["ecken"]["unten_rechts"], e["ecken"]["unten_links"])
    setze max_radius auf boden(pb / 2)
    wenn boden(ph / 2) < max_radius:
        setze max_radius auf boden(ph / 2)
    wenn radius > max_radius:
        setze radius auf max_radius

    wenn _uime_bit_an(e["aktiv"], 2):
        setze s auf e["schatten"]
        setze sf auf s["farbe"]
        setze lage auf s["unschaerfe"]
        wenn lage > 8:
            setze lage auf 8
        setze i auf lage
        solange i >= 0:
            setze alpha auf sf["a"] / (lage + 2)
            wenn i == 0:
                setze alpha auf sf["a"] / 2
            surface_roundrect(z, px + s["x"] - s["ausbreitung"] - i, py + s["y"] - s["ausbreitung"] - i, pb + 2 * (s["ausbreitung"] + i), ph + 2 * (s["ausbreitung"] + i), radius + s["ausbreitung"] + i, sf["r"], sf["g"], sf["b"], _uime_klemme_kanal(alpha))
            setze i auf i - 1

    setze farb_pass auf _uime_bit_an(e["aktiv"], 4) oder _uime_bit_an(e["aktiv"], 8) oder _uime_bit_an(e["aktiv"], 16) oder _uime_bit_an(e["aktiv"], 32)
    wenn farb_pass:
        # Fastpath: identische Semantik in C (surface_glass_farbpass in
        # moo_surface.c); bei falsch (alter Runtime/ungueltige Args) laeuft
        # die Moo-Referenz.
        wenn surface_glass_farbpass(z, px, py, pb, ph, e) == falsch:
            _uime_farbpass_langsam(z, px, py, pb, ph, e)

    setze deckkraft auf fortschritt_q16
    wenn reduzierte_bewegung oder _uime_bit_an(e["aktiv"], 128) == falsch:
        setze deckkraft auf 65536
    setze glas_alpha auf _uime_klemme_kanal(42 * deckkraft / 65536)
    surface_roundrect(z, px, py, pb, ph, radius, 224, 240, 255, glas_alpha)
    # Aero-Politur (P016-O5-AERO): Sheen-Verlauf + Glasrand, deterministisch.
    wenn ph > 7 und pb > 7:
        setze sheen_h auf boden(ph * 2 / 5)
        wenn sheen_h > 3:
            # Kontinuierlicher Verlauf 64 -> 0 ohne Kappen-Sprung; Zeilen im
            # Eckbereich folgen dem Radius per Kreisbogen-Inset.
            setze zeile auf 0
            solange zeile < sheen_h:
                setze sheen_a auf 64 * (sheen_h - zeile) / sheen_h
                setze inset auf 2
                wenn zeile < radius:
                    setze bogen_dy auf radius - zeile - 0.5
                    setze bogen auf radius * radius - bogen_dy * bogen_dy
                    wenn bogen < 0:
                        setze bogen auf 0
                    setze inset auf 2 + runde(radius - wurzel(bogen))
                wenn pb - 2 * inset >= 1:
                    surface_rect(z, px + inset, py + 2 + zeile, pb - 2 * inset, 1, 255, 255, 255, _uime_klemme_kanal(sheen_a * deckkraft / 65536))
                setze zeile auf zeile + 1
        setze rand_b auf pb - 2 * radius
        setze rand_h auf ph - 2 * radius
        wenn rand_b >= 1 und rand_h >= 1:
            setze rand_a auf _uime_klemme_kanal(120 * deckkraft / 65536)
            setze kanten_a auf _uime_klemme_kanal(80 * deckkraft / 65536)
            surface_rect(z, px + radius, py + 1, rand_b, 1, 255, 255, 255, rand_a)
            surface_rect(z, px + radius, py + ph - 2, rand_b, 1, 255, 255, 255, _uime_klemme_kanal(60 * deckkraft / 65536))
            surface_rect(z, px + 1, py + radius, 1, rand_h, 255, 255, 255, rand_a)
            surface_rect(z, px + pb - 2, py + radius, 1, rand_h, 255, 255, 255, rand_a)
            surface_rect(z, px + radius, py, rand_b, 1, 20, 30, 40, kanten_a)
            surface_rect(z, px + radius, py + ph - 1, rand_b, 1, 20, 30, 40, kanten_a)
            surface_rect(z, px, py + radius, 1, rand_h, 20, 30, 40, kanten_a)
            surface_rect(z, px + pb - 1, py + radius, 1, rand_h, 20, 30, 40, kanten_a)

    setze r auf {}
    r["ok"] = wahr
    r["grund"] = "ok"
    r["modus"] = "portable-vorschau"
    r["effektiv"] = e["aktiv"]
    r["degradiert"] = aufloesung["degradiert"]
    r["x"] = px
    r["y"] = py
    r["b"] = pb
    r["h"] = ph
    gib_zurück r
