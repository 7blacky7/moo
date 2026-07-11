# ============================================================
# stdlib/ui_moo.moo — Schicht 3: moo-eigene gezeichnete Widgets
#
# "Von moo für UI": ein retained Widget-Tree, komplett in moo,
# gezeichnet über die Leinwand/Zeichner-API (Phase 5 + UIMOO-1).
# Design-Memo: Synapse-Memory `plan-uimoo-widget-toolkit`.
#
# Nutzung:
#   importiere ui
#   importiere ui_moo
#
#   setze k auf uim_wurzel(fenster, 10, 10, 580, 400)
#   uim_hinzu(k, uim_knopf("Klick mich", 20, 20, 140, 32, auf_klick))
#   uim_hinzu(k, uim_label("Hallo", 20, 70, 200, 20))
#
# Öffentliche API (Kern, UIMOO-2):
#   uim_wurzel(fenster, x, y, b, h)      -> kontext (legt Leinwand an,
#                                           bindet alle Events)
#   uim_hinzu(kontext, widget)           -> widget (an Wurzel anhängen)
#   uim_theme_setze(kontext, theme)      -> Theme wechseln + neu zeichnen
#   uim_theme_dunkel() / uim_theme_hell()-> Theme-Dicts
#   uim_neuzeichnen(kontext)             -> Repaint anfordern
#   uim_finde(kontext, id)               -> Widget per String-ID
#
# Referenz-Widgets (voller Satz folgt in UIMOO-3):
#   uim_knopf(text, x, y, b, h, on_klick)
#   uim_label(text, x, y, b, h)
#   uim_checkbox(text, x, y, b, h, initial, on_wechsel)   # on_wechsel(w, wert)
#   uim_slider(x, y, b, h, min, max, start, on_wechsel)   # Drag + Pfeiltasten
#   uim_fortschritt(x, y, b, h) + uim_fortschritt_setze(kontext, w, wert01)
#   uim_slider_setze(kontext, w, wert)
#
# Widget-Dict (Konvention):
#   typ, uid, id, x, y, b, h, sichtbar, aktiv, fokussierbar,
#   text, hover, druck, kinder, on_klick, on_wechsel, wert, min, max
#
# Zustands-Regel: moo-Funktionen können globale SKALARE nicht neu
# zuweisen — Dicts/Listen sind Referenz-geteilt. Darum lebt ALLER
# veränderliche Modul-Zustand im Dict _UIM (Kontexte, uid-Zähler).
# Widget-Identität wird über "uid" verglichen, nie über Dict-Vergleich.
# ============================================================

setze _UIM auf {}
_UIM["kontexte"] = {}
_UIM["naechste_uid"] = 1

# ------------------------------------------------------------
# Themes: JEDER Farb-/Maßwert der Zeichner kommt von hier.
# Farben als [r, g, b, a] (0..255).
# ------------------------------------------------------------

funktion uim_theme_dunkel():
    setze t auf {}
    t["name"] = "dunkel"
    t["hintergrund"]   = [30, 32, 38, 255]
    t["flaeche"]       = [58, 62, 74, 255]
    t["flaeche_hover"] = [72, 78, 94, 255]
    t["flaeche_druck"] = [44, 48, 58, 255]
    t["rand"]          = [96, 102, 120, 255]
    t["akzent"]        = [86, 156, 240, 255]
    t["text"]          = [230, 232, 238, 255]
    t["text_gedimmt"]  = [150, 154, 164, 255]
    t["radius"]        = 6
    t["schrift"]       = 13
    t["abstand"]       = 8
    gib_zurück t

funktion uim_theme_hell():
    setze t auf {}
    t["name"] = "hell"
    t["hintergrund"]   = [244, 245, 248, 255]
    t["flaeche"]       = [224, 227, 233, 255]
    t["flaeche_hover"] = [210, 214, 224, 255]
    t["flaeche_druck"] = [196, 200, 212, 255]
    t["rand"]          = [150, 156, 170, 255]
    t["akzent"]        = [40, 110, 210, 255]
    t["text"]          = [28, 30, 36, 255]
    t["text_gedimmt"]  = [110, 114, 126, 255]
    t["radius"]        = 6
    t["schrift"]       = 13
    t["abstand"]       = 8
    gib_zurück t

# ------------------------------------------------------------
# Interne Helfer
# ------------------------------------------------------------

funktion _uim_neue_uid():
    setze uid auf _UIM["naechste_uid"]
    _UIM["naechste_uid"] = uid + 1
    gib_zurück uid

funktion _uim_ctx(leinwand):
    setze schluessel auf "k" + text(leinwand)
    setze alle auf _UIM["kontexte"]
    wenn alle.enthält(schluessel):
        gib_zurück alle[schluessel]
    gib_zurück nichts

funktion _uim_farbe(z, f):
    ui_zeichne_farbe(z, f[0], f[1], f[2], f[3])
    gib_zurück wahr

# uid eines Widget-Slots im Kontext ("hover"/"druck"/"fokus"), 0 = leer.
funktion _uim_slot_uid(kontext, slot):
    setze w auf kontext[slot]
    wenn w == nichts:
        gib_zurück 0
    gib_zurück w["uid"]

# Basis aller Widgets: gemeinsame Felder einmal zentral.
funktion _uim_basis(typ, x, y, b, h):
    setze w auf {}
    w["typ"] = typ
    w["uid"] = _uim_neue_uid()
    w["id"] = ""
    w["x"] = x
    w["y"] = y
    w["b"] = b
    w["h"] = h
    w["sichtbar"] = wahr
    w["aktiv"] = wahr
    w["fokussierbar"] = falsch
    w["text"] = ""
    w["hover"] = falsch
    w["druck"] = falsch
    w["kinder"] = []
    w["on_klick"] = nichts
    w["on_wechsel"] = nichts
    w["wert"] = 0
    w["min"] = 0
    w["max"] = 0
    gib_zurück w

# Treffer-Ergebnis: Widget + Koordinaten LOKAL zur Eltern-Ebene des
# Widgets (lx liegt in w.x..w.x+w.b). Nötig für Slider/Liste in Panels.
funktion _uim_tref(w, lx, ly):
    setze t auf {}
    t["w"] = w
    t["lx"] = lx
    t["ly"] = ly
    gib_zurück t

# Rekursives Hit-Testing (z-Order: zuletzt hinzugefügt gewinnt).
# Panels/Scrollbereiche transformieren in ihr lokales Koordinatensystem;
# Scroll addiert scroll_y. Container decken ab: kein Kind getroffen →
# Container selbst ist der Treffer (blockt darunterliegende Widgets).
funktion _uim_treffer_rek(kinder, x, y):
    setze gefunden auf nichts
    setze i auf länge(kinder) - 1
    solange i >= 0:
        wenn gefunden == nichts:
            setze w auf kinder[i]
            wenn w["sichtbar"] und w["aktiv"]:
                wenn x >= w["x"] und x < w["x"] + w["b"] und y >= w["y"] und y < w["y"] + w["h"]:
                    wenn w["typ"] == "panel":
                        setze innen auf _uim_treffer_rek(w["kinder"], x - w["x"], y - w["y"])
                        wenn innen == nichts:
                            setze gefunden auf _uim_tref(w, x, y)
                        sonst:
                            setze gefunden auf innen
                    sonst wenn w["typ"] == "scroll":
                        setze innen auf _uim_treffer_rek(w["kinder"], x - w["x"], y - w["y"] + w["scroll_y"])
                        wenn innen == nichts:
                            setze gefunden auf _uim_tref(w, x, y)
                        sonst:
                            setze gefunden auf innen
                    sonst:
                        setze gefunden auf _uim_tref(w, x, y)
        setze i auf i - 1
    gib_zurück gefunden

funktion _uim_treffer(kontext, x, y):
    gib_zurück _uim_treffer_rek(kontext["wurzel"]["kinder"], x, y)

# Oberstes scrollbares Widget (scroll/liste) unter dem Punkt.
funktion _uim_scrollziel_rek(kinder, x, y):
    setze gefunden auf nichts
    setze i auf länge(kinder) - 1
    solange i >= 0:
        wenn gefunden == nichts:
            setze w auf kinder[i]
            wenn w["sichtbar"] und w["aktiv"]:
                wenn x >= w["x"] und x < w["x"] + w["b"] und y >= w["y"] und y < w["y"] + w["h"]:
                    wenn w["typ"] == "panel":
                        setze gefunden auf _uim_scrollziel_rek(w["kinder"], x - w["x"], y - w["y"])
                    sonst wenn w["typ"] == "scroll":
                        setze innen auf _uim_scrollziel_rek(w["kinder"], x - w["x"], y - w["y"] + w["scroll_y"])
                        wenn innen == nichts:
                            setze gefunden auf w
                        sonst:
                            setze gefunden auf innen
                    sonst wenn w["typ"] == "liste":
                        setze gefunden auf w
        setze i auf i - 1
    gib_zurück gefunden

# Klick-Aktivierung, typ-bewusst: Knopf feuert on_klick, Checkbox
# toggelt + feuert on_wechsel. (Slider aktiviert nicht per Klick-Ende —
# sein Wert wird schon bei Press/Drag gesetzt.)
funktion _uim_aktiviere(kontext, w):
    wenn w["typ"] == "checkbox":
        wenn w["wert"]:
            w["wert"] = falsch
        sonst:
            w["wert"] = wahr
        setze cw auf w["on_wechsel"]
        wenn cw != nichts:
            cw(w, w["wert"])
        gib_zurück wahr
    setze cb auf w["on_klick"]
    wenn cb == nichts:
        gib_zurück falsch
    cb(w)
    gib_zurück wahr

# Slider-Wert aus Maus-X ableiten (klemmt auf min..max) und bei
# Änderung on_wechsel feuern. Liefert wahr wenn sich der Wert änderte.
funktion _uim_slider_setze_aus_x(w, x):
    setze spanne auf w["max"] - w["min"]
    wenn spanne <= 0:
        gib_zurück falsch
    setze anteil auf (x - w["x"]) / w["b"]
    wenn anteil < 0:
        setze anteil auf 0
    wenn anteil > 1:
        setze anteil auf 1
    setze wert auf w["min"] + anteil * spanne
    wenn wert == w["wert"]:
        gib_zurück falsch
    w["wert"] = wert
    setze cw auf w["on_wechsel"]
    wenn cw != nichts:
        cw(w, wert)
    gib_zurück wahr

# Fokus auf nächstes fokussierbares Widget (Tab-Reihenfolge =
# Einfüge-Reihenfolge, mit Umlauf). rueckwaerts: wahr bei Shift+Tab.
funktion _uim_fokus_weiter(kontext, rueckwaerts):
    setze kinder auf kontext["wurzel"]["kinder"]
    setze n auf länge(kinder)
    wenn n == 0:
        gib_zurück falsch
    setze start auf 0
    setze aktuell_uid auf _uim_slot_uid(kontext, "fokus")
    setze i auf 0
    solange i < n:
        setze w auf kinder[i]
        wenn w["uid"] == aktuell_uid:
            setze start auf i
        setze i auf i + 1
    setze schritt auf 1
    wenn rueckwaerts:
        setze schritt auf n - 1
    setze i auf 1
    setze ziel auf nichts
    solange i <= n:
        wenn ziel == nichts:
            setze idx auf (start + i * schritt) % n
            setze w auf kinder[idx]
            wenn w["fokussierbar"] und w["sichtbar"] und w["aktiv"]:
                setze ziel auf w
        setze i auf i + 1
    wenn ziel == nichts:
        gib_zurück falsch
    kontext["fokus"] = ziel
    gib_zurück wahr

# Listen-Zeilenhöhe: rein theme-getrieben.
funktion _uim_zeilenhoehe(kontext):
    setze t auf kontext["theme"]
    gib_zurück t["schrift"] + t["abstand"]

# Listen-Klick: Zeile aus lokaler Y-Koordinate (ohne floor-Builtin:
# beschränkte Zählschleife). Setzt auswahl + feuert on_auswahl.
funktion _uim_liste_klick(kontext, w, ly):
    setze zh auf _uim_zeilenhoehe(kontext)
    setze rel auf ly - w["y"] + w["scroll_y"]
    wenn rel < 0:
        gib_zurück falsch
    setze idx auf 0
    solange (idx + 1) * zh <= rel:
        setze idx auf idx + 1
    wenn idx >= länge(w["zeilen"]):
        gib_zurück falsch
    w["auswahl"] = idx
    setze cb auf w["on_auswahl"]
    wenn cb != nichts:
        cb(w, idx)
    gib_zurück wahr

# ------------------------------------------------------------
# Event-Dispatcher (an die Leinwand gebunden von uim_wurzel)
# ------------------------------------------------------------

funktion _uim_on_maus(lw, x, y, taste):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    wenn taste != 1:
        gib_zurück falsch
    setze tref auf _uim_treffer(kontext, x, y)
    wenn tref == nichts:
        kontext["druck"] = nichts
        kontext["fokus"] = nichts
    sonst:
        setze w auf tref["w"]
        kontext["druck"] = w
        # Offset Leinwand-global -> Widget-Eltern-lokal, für Drag ausserhalb.
        kontext["druck_ox"] = x - tref["lx"]
        kontext["druck_oy"] = y - tref["ly"]
        w["druck"] = wahr
        wenn w["fokussierbar"]:
            kontext["fokus"] = w
        wenn w["typ"] == "slider":
            _uim_slider_setze_aus_x(w, tref["lx"])
        wenn w["typ"] == "liste":
            _uim_liste_klick(kontext, w, tref["ly"])
    ui_leinwand_anfordern(lw)
    gib_zurück wahr

funktion _uim_on_maus_los(lw, x, y, taste):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    wenn taste != 1:
        gib_zurück falsch
    setze p auf kontext["druck"]
    wenn p == nichts:
        gib_zurück falsch
    p["druck"] = falsch
    kontext["druck"] = nichts
    wenn p["typ"] == "slider":
        # Drag-Ende: Wert wurde bereits bei Press/Drag gesetzt.
        ui_leinwand_anfordern(lw)
        gib_zurück wahr
    setze tref auf _uim_treffer(kontext, x, y)
    wenn tref != nichts:
        wenn tref["w"]["uid"] == p["uid"]:
            _uim_aktiviere(kontext, p)
    ui_leinwand_anfordern(lw)
    gib_zurück wahr

funktion _uim_on_bewegung(lw, x, y):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    # Aktiver Slider-Drag hat Vorrang vor Hover. Lokales X über den beim
    # Press gemerkten Offset (Zeiger darf den Slider verlassen).
    setze p auf kontext["druck"]
    wenn p != nichts:
        wenn p["typ"] == "slider":
            wenn _uim_slider_setze_aus_x(p, x - kontext["druck_ox"]):
                ui_leinwand_anfordern(lw)
            gib_zurück wahr
    setze tref auf _uim_treffer(kontext, x, y)
    setze w auf nichts
    wenn tref != nichts:
        setze w auf tref["w"]
    setze uid_treffer auf 0
    wenn w != nichts:
        setze uid_treffer auf w["uid"]
    setze alt_uid auf _uim_slot_uid(kontext, "hover")
    wenn uid_treffer == alt_uid:
        gib_zurück wahr
    setze alt auf kontext["hover"]
    wenn alt != nichts:
        alt["hover"] = falsch
    wenn w != nichts:
        w["hover"] = wahr
    kontext["hover"] = w
    ui_leinwand_anfordern(lw)
    gib_zurück wahr

funktion _uim_on_taste(lw, taste, gedrueckt, mod):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    wenn gedrueckt == falsch:
        gib_zurück falsch
    wenn taste == "Tab":
        setze rueck auf falsch
        wenn mod % 2 == 1:
            setze rueck auf wahr
        _uim_fokus_weiter(kontext, rueck)
        ui_leinwand_anfordern(lw)
        gib_zurück wahr
    setze f auf kontext["fokus"]
    wenn f == nichts:
        gib_zurück falsch
    wenn taste == "Return" oder taste == "space":
        _uim_aktiviere(kontext, f)
        ui_leinwand_anfordern(lw)
        gib_zurück wahr
    wenn f["typ"] == "slider":
        wenn taste == "Left" oder taste == "Right":
            setze schritt auf (f["max"] - f["min"]) / 20
            wenn schritt <= 0:
                setze schritt auf 1
            wenn taste == "Left":
                setze schritt auf 0 - schritt
            setze wert auf f["wert"] + schritt
            wenn wert < f["min"]:
                setze wert auf f["min"]
            wenn wert > f["max"]:
                setze wert auf f["max"]
            wenn wert != f["wert"]:
                f["wert"] = wert
                setze cw auf f["on_wechsel"]
                wenn cw != nichts:
                    cw(f, wert)
            ui_leinwand_anfordern(lw)
            gib_zurück wahr
    wenn f["typ"] == "liste":
        wenn taste == "Up" oder taste == "Down":
            setze n auf länge(f["zeilen"])
            wenn n == 0:
                gib_zurück wahr
            setze idx auf f["auswahl"]
            wenn taste == "Up":
                setze idx auf idx - 1
            sonst:
                setze idx auf idx + 1
            wenn idx < 0:
                setze idx auf 0
            wenn idx > n - 1:
                setze idx auf n - 1
            wenn idx != f["auswahl"]:
                f["auswahl"] = idx
                setze cb auf f["on_auswahl"]
                wenn cb != nichts:
                    cb(f, idx)
            ui_leinwand_anfordern(lw)
            gib_zurück wahr
    gib_zurück falsch

# Scrollrad: oberstes scrollbares Widget unter dem Zeiger scrollen.
funktion _uim_on_rad(lw, x, y, delta):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    setze w auf _uim_scrollziel_rek(kontext["wurzel"]["kinder"], x, y)
    wenn w == nichts:
        gib_zurück falsch
    setze zh auf _uim_zeilenhoehe(kontext)
    setze max_scroll auf 0
    wenn w["typ"] == "liste":
        setze max_scroll auf länge(w["zeilen"]) * zh - w["h"]
    sonst:
        setze max_scroll auf w["inhalt_hoehe"] - w["h"]
    wenn max_scroll < 0:
        setze max_scroll auf 0
    setze s auf w["scroll_y"] + delta * zh * 2
    wenn s < 0:
        setze s auf 0
    wenn s > max_scroll:
        setze s auf max_scroll
    wenn s != w["scroll_y"]:
        w["scroll_y"] = s
        ui_leinwand_anfordern(lw)
    gib_zurück wahr

funktion _uim_z_knopf(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    setze ax auf ox + w["x"]
    setze ay auf oy + w["y"]
    setze flaeche auf t["flaeche"]
    wenn w["aktiv"] == falsch:
        setze flaeche auf t["flaeche_druck"]
    sonst wenn w["druck"]:
        setze flaeche auf t["flaeche_druck"]
    sonst wenn w["hover"]:
        setze flaeche auf t["flaeche_hover"]
    _uim_farbe(z, flaeche)
    ui_zeichne_rechteck_rund(z, ax, ay, w["b"], w["h"], t["radius"], wahr)
    _uim_farbe(z, t["rand"])
    ui_zeichne_rechteck_rund(z, ax, ay, w["b"], w["h"], t["radius"], falsch)
    wenn _uim_slot_uid(kontext, "fokus") == w["uid"]:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_rechteck_rund(z, ax - 2, ay - 2, w["b"] + 4, w["h"] + 4, t["radius"] + 2, falsch)
    setze schrift auf t["schrift"]
    setze tb auf ui_zeichne_text_breite(z, w["text"], schrift)
    setze tx auf ax + (w["b"] - tb) / 2
    setze ty auf ay + (w["h"] - schrift) / 2 - 1
    setze farbe auf t["text"]
    wenn w["aktiv"] == falsch:
        setze farbe auf t["text_gedimmt"]
    _uim_farbe(z, farbe)
    ui_zeichne_text(z, tx, ty, w["text"], schrift)
    gib_zurück wahr

funktion _uim_z_label(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    _uim_farbe(z, t["text"])
    ui_zeichne_text(z, ox + w["x"], oy + w["y"], w["text"], t["schrift"])
    gib_zurück wahr

funktion _uim_z_checkbox(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    setze ax auf ox + w["x"]
    setze ay auf oy + w["y"]
    setze kh auf w["h"]
    wenn kh > 18:
        setze kh auf 18
    setze ky auf ay + (w["h"] - kh) / 2
    setze flaeche auf t["flaeche"]
    wenn w["hover"]:
        setze flaeche auf t["flaeche_hover"]
    wenn w["druck"]:
        setze flaeche auf t["flaeche_druck"]
    _uim_farbe(z, flaeche)
    ui_zeichne_rechteck_rund(z, ax, ky, kh, kh, 3, wahr)
    _uim_farbe(z, t["rand"])
    ui_zeichne_rechteck_rund(z, ax, ky, kh, kh, 3, falsch)
    wenn w["wert"]:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_linie(z, ax + kh * 0.22, ky + kh * 0.52, ax + kh * 0.42, ky + kh * 0.74, 2)
        ui_zeichne_linie(z, ax + kh * 0.42, ky + kh * 0.74, ax + kh * 0.80, ky + kh * 0.28, 2)
    wenn _uim_slot_uid(kontext, "fokus") == w["uid"]:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_rechteck_rund(z, ax - 2, ky - 2, kh + 4, kh + 4, 5, falsch)
    setze farbe auf t["text"]
    wenn w["aktiv"] == falsch:
        setze farbe auf t["text_gedimmt"]
    _uim_farbe(z, farbe)
    setze ty auf ay + (w["h"] - t["schrift"]) / 2 - 1
    ui_zeichne_text(z, ax + kh + t["abstand"], ty, w["text"], t["schrift"])
    gib_zurück wahr

funktion _uim_z_slider(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    setze ax auf ox + w["x"]
    setze ay auf oy + w["y"]
    setze spanne auf w["max"] - w["min"]
    setze anteil auf 0
    wenn spanne > 0:
        setze anteil auf (w["wert"] - w["min"]) / spanne
    setze mitte_y auf ay + w["h"] / 2
    # Track (voll) + gefüllter Teil in Akzent
    _uim_farbe(z, t["flaeche"])
    ui_zeichne_rechteck_rund(z, ax, mitte_y - 3, w["b"], 6, 3, wahr)
    wenn anteil > 0:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_rechteck_rund(z, ax, mitte_y - 3, w["b"] * anteil, 6, 3, wahr)
    # Knob
    setze kx auf ax + w["b"] * anteil
    setze radius auf 8
    wenn w["druck"] oder w["hover"]:
        setze radius auf 9
    setze knopf_farbe auf t["text"]
    wenn w["aktiv"] == falsch:
        setze knopf_farbe auf t["text_gedimmt"]
    _uim_farbe(z, knopf_farbe)
    ui_zeichne_kreis(z, kx, mitte_y, radius, wahr)
    _uim_farbe(z, t["rand"])
    ui_zeichne_kreis(z, kx, mitte_y, radius, falsch)
    wenn _uim_slot_uid(kontext, "fokus") == w["uid"]:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_kreis(z, kx, mitte_y, radius + 3, falsch)
    gib_zurück wahr

funktion _uim_z_fortschritt(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    setze ax auf ox + w["x"]
    setze ay auf oy + w["y"]
    setze anteil auf w["wert"]
    wenn anteil < 0:
        setze anteil auf 0
    wenn anteil > 1:
        setze anteil auf 1
    _uim_farbe(z, t["flaeche"])
    ui_zeichne_rechteck_rund(z, ax, ay, w["b"], w["h"], t["radius"], wahr)
    wenn anteil > 0:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_rechteck_rund(z, ax, ay, w["b"] * anteil, w["h"], t["radius"], wahr)
    _uim_farbe(z, t["rand"])
    ui_zeichne_rechteck_rund(z, ax, ay, w["b"], w["h"], t["radius"], falsch)
    gib_zurück wahr

funktion _uim_z_panel(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    setze ax auf ox + w["x"]
    setze ay auf oy + w["y"]
    _uim_farbe(z, t["flaeche"])
    ui_zeichne_rechteck_rund(z, ax, ay, w["b"], w["h"], t["radius"], wahr)
    _uim_farbe(z, t["rand"])
    ui_zeichne_rechteck_rund(z, ax, ay, w["b"], w["h"], t["radius"], falsch)
    wenn w["text"] != "":
        _uim_farbe(z, t["text_gedimmt"])
        ui_zeichne_text(z, ax + t["abstand"], ay + t["abstand"] / 2, w["text"], t["schrift"] - 1)
    gib_zurück wahr

funktion _uim_z_liste(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    setze ax auf ox + w["x"]
    setze ay auf oy + w["y"]
    setze zh auf _uim_zeilenhoehe(kontext)
    setze zeilen auf w["zeilen"]
    setze n auf länge(zeilen)
    _uim_farbe(z, t["hintergrund"])
    ui_zeichne_rechteck(z, ax, ay, w["b"], w["h"], wahr)
    ui_zeichne_clip_setze(z, ax, ay, w["b"], w["h"])
    # Virtuelles Rendering: erste sichtbare Zeile ohne floor-Builtin.
    setze start auf 0
    solange (start + 1) * zh <= w["scroll_y"]:
        setze start auf start + 1
    setze i auf start
    setze zy auf ay + start * zh - w["scroll_y"]
    solange i < n und zy < ay + w["h"]:
        wenn i == w["auswahl"]:
            _uim_farbe(z, t["akzent"])
            ui_zeichne_rechteck(z, ax, zy, w["b"], zh, wahr)
            _uim_farbe(z, t["hintergrund"])
        sonst:
            _uim_farbe(z, t["text"])
        ui_zeichne_text(z, ax + t["abstand"], zy + (zh - t["schrift"]) / 2, zeilen[i], t["schrift"])
        setze i auf i + 1
        setze zy auf zy + zh
    ui_zeichne_clip_loesche(z)
    _uim_farbe(z, t["rand"])
    ui_zeichne_rechteck(z, ax, ay, w["b"], w["h"], falsch)
    wenn _uim_slot_uid(kontext, "fokus") == w["uid"]:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_rechteck(z, ax - 2, ay - 2, w["b"] + 4, w["h"] + 4, falsch)
    gib_zurück wahr

# Scrollbereich: Rahmen + Clip öffnen (Kinder zeichnet _uim_zeichne_widget)
funktion _uim_z_scroll_rahmen(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    ui_zeichne_clip_setze(z, ox + w["x"], oy + w["y"], w["b"], w["h"])
    gib_zurück wahr

# Scrollbereich: Clip schliessen + Scrollbar rechts zeichnen
funktion _uim_z_scroll_leiste(kontext, z, w, ox, oy):
    setze t auf kontext["theme"]
    ui_zeichne_clip_loesche(z)
    setze ax auf ox + w["x"]
    setze ay auf oy + w["y"]
    _uim_farbe(z, t["rand"])
    ui_zeichne_rechteck(z, ax, ay, w["b"], w["h"], falsch)
    setze inhalt auf w["inhalt_hoehe"]
    wenn inhalt <= w["h"]:
        gib_zurück wahr
    # Thumb: Groesse und Position proportional.
    setze leiste_x auf ax + w["b"] - 6
    _uim_farbe(z, t["flaeche"])
    ui_zeichne_rechteck_rund(z, leiste_x, ay, 5, w["h"], 2, wahr)
    setze thumb_h auf w["h"] * w["h"] / inhalt
    wenn thumb_h < 16:
        setze thumb_h auf 16
    setze max_scroll auf inhalt - w["h"]
    setze thumb_y auf ay
    wenn max_scroll > 0:
        setze thumb_y auf ay + (w["h"] - thumb_h) * (w["scroll_y"] / max_scroll)
    _uim_farbe(z, t["text_gedimmt"])
    ui_zeichne_rechteck_rund(z, leiste_x, thumb_y, 5, thumb_h, 2, wahr)
    gib_zurück wahr

# Zentraler rekursiver Zeichner: Container transformieren ihre Kinder.
funktion _uim_zeichne_widget(kontext, z, w, ox, oy):
    wenn w["sichtbar"] == falsch:
        gib_zurück falsch
    wenn w["typ"] == "panel":
        _uim_z_panel(kontext, z, w, ox, oy)
        für k in w["kinder"]:
            _uim_zeichne_widget(kontext, z, k, ox + w["x"], oy + w["y"])
        gib_zurück wahr
    wenn w["typ"] == "scroll":
        _uim_z_scroll_rahmen(kontext, z, w, ox, oy)
        für k in w["kinder"]:
            _uim_zeichne_widget(kontext, z, k, ox + w["x"], oy + w["y"] - w["scroll_y"])
        _uim_z_scroll_leiste(kontext, z, w, ox, oy)
        gib_zurück wahr
    wenn w["typ"] == "knopf":
        gib_zurück _uim_z_knopf(kontext, z, w, ox, oy)
    wenn w["typ"] == "label":
        gib_zurück _uim_z_label(kontext, z, w, ox, oy)
    wenn w["typ"] == "checkbox":
        gib_zurück _uim_z_checkbox(kontext, z, w, ox, oy)
    wenn w["typ"] == "slider":
        gib_zurück _uim_z_slider(kontext, z, w, ox, oy)
    wenn w["typ"] == "fortschritt":
        gib_zurück _uim_z_fortschritt(kontext, z, w, ox, oy)
    wenn w["typ"] == "liste":
        gib_zurück _uim_z_liste(kontext, z, w, ox, oy)
    gib_zurück falsch

funktion _uim_on_zeichne(lw, z):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    setze t auf kontext["theme"]
    setze wurzel auf kontext["wurzel"]
    _uim_farbe(z, t["hintergrund"])
    ui_zeichne_rechteck(z, 0, 0, wurzel["b"], wurzel["h"], wahr)
    für w in wurzel["kinder"]:
        _uim_zeichne_widget(kontext, z, w, 0, 0)
    gib_zurück wahr

# ------------------------------------------------------------
# Öffentliche API
# ------------------------------------------------------------

funktion uim_wurzel(fenster, x, y, b, h):
    setze lw auf ui_leinwand(fenster, x, y, b, h, _uim_on_zeichne)
    setze kontext auf {}
    kontext["leinwand"] = lw
    kontext["fenster"] = fenster
    kontext["theme"] = uim_theme_dunkel()
    kontext["hover"] = nichts
    kontext["druck"] = nichts
    kontext["druck_ox"] = 0
    kontext["druck_oy"] = 0
    kontext["fokus"] = nichts
    setze wurzel auf _uim_basis("wurzel", 0, 0, b, h)
    kontext["wurzel"] = wurzel
    setze alle auf _UIM["kontexte"]
    alle["k" + text(lw)] = kontext
    ui_leinwand_on_maus(lw, _uim_on_maus)
    ui_leinwand_on_maus_los(lw, _uim_on_maus_los)
    ui_leinwand_on_bewegung(lw, _uim_on_bewegung)
    ui_leinwand_on_taste(lw, _uim_on_taste)
    ui_leinwand_on_rad(lw, _uim_on_rad)
    gib_zurück kontext

funktion uim_hinzu(ziel, widget):
    wenn ziel.enthält("wurzel"):
        ziel["wurzel"]["kinder"].hinzufügen(widget)
        ui_leinwand_anfordern(ziel["leinwand"])
    sonst:
        ziel["kinder"].hinzufügen(widget)
    gib_zurück widget

funktion uim_theme_setze(kontext, theme):
    kontext["theme"] = theme
    ui_leinwand_anfordern(kontext["leinwand"])
    gib_zurück wahr

funktion uim_neuzeichnen(kontext):
    ui_leinwand_anfordern(kontext["leinwand"])
    gib_zurück wahr

funktion uim_finde(kontext, id):
    setze kinder auf kontext["wurzel"]["kinder"]
    setze gefunden auf nichts
    für w in kinder:
        wenn gefunden == nichts:
            wenn w["id"] == id:
                setze gefunden auf w
    gib_zurück gefunden

# ------------------------------------------------------------
# Referenz-Widgets (voller Satz folgt in UIMOO-3)
# ------------------------------------------------------------

funktion uim_knopf(beschriftung, x, y, b, h, on_klick):
    setze w auf _uim_basis("knopf", x, y, b, h)
    w["text"] = beschriftung
    w["fokussierbar"] = wahr
    w["on_klick"] = on_klick
    gib_zurück w

funktion uim_label(beschriftung, x, y, b, h):
    setze w auf _uim_basis("label", x, y, b, h)
    w["text"] = beschriftung
    gib_zurück w

# Checkbox: Klick oder space/Return toggelt. on_wechsel(w, wert).
funktion uim_checkbox(beschriftung, x, y, b, h, initial, on_wechsel):
    setze w auf _uim_basis("checkbox", x, y, b, h)
    w["text"] = beschriftung
    w["fokussierbar"] = wahr
    w["wert"] = initial
    w["on_wechsel"] = on_wechsel
    gib_zurück w

# Slider: Press setzt Wert, Drag zieht, Pfeiltasten in 1/20-Schritten.
# on_wechsel(w, wert) bei jeder Wertänderung.
funktion uim_slider(x, y, b, h, min, max, start, on_wechsel):
    setze w auf _uim_basis("slider", x, y, b, h)
    w["fokussierbar"] = wahr
    w["min"] = min
    w["max"] = max
    setze anfang auf start
    wenn anfang < min:
        setze anfang auf min
    wenn anfang > max:
        setze anfang auf max
    w["wert"] = anfang
    w["on_wechsel"] = on_wechsel
    gib_zurück w

# Fortschrittsbalken: passiv, wert 0..1 über uim_fortschritt_setze.
funktion uim_fortschritt(x, y, b, h):
    setze w auf _uim_basis("fortschritt", x, y, b, h)
    w["wert"] = 0
    gib_zurück w

funktion uim_fortschritt_setze(kontext, w, wert):
    wenn wert < 0:
        setze wert auf 0
    wenn wert > 1:
        setze wert auf 1
    w["wert"] = wert
    ui_leinwand_anfordern(kontext["leinwand"])
    gib_zurück wahr

funktion uim_slider_setze(kontext, w, wert):
    wenn wert < w["min"]:
        setze wert auf w["min"]
    wenn wert > w["max"]:
        setze wert auf w["max"]
    w["wert"] = wert
    ui_leinwand_anfordern(kontext["leinwand"])
    gib_zurück wahr

# ------------------------------------------------------------
# Container (UIMOO-5). Kind-Koordinaten sind RELATIV zum Container.
# Tab-Fokus läuft in V1 nur über Top-Level-Widgets (dokumentierte
# Einschränkung); Klick-Fokus funktioniert auch in Containern.
# ------------------------------------------------------------

# Panel: Fläche + optionaler Titel + Kinder. uim_hinzu(panel, w).
funktion uim_panel(titel, x, y, b, h):
    setze w auf _uim_basis("panel", x, y, b, h)
    w["text"] = titel
    gib_zurück w

# Scrollbereich: Kinder werden am Rechteck geclippt (UIMOO-1-Scissor),
# Rad scrollt, Scrollbar rechts. inhalt_hoehe = virtuelle Gesamthöhe.
funktion uim_scroll(x, y, b, h, inhalt_hoehe):
    setze w auf _uim_basis("scroll", x, y, b, h)
    w["inhalt_hoehe"] = inhalt_hoehe
    w["scroll_y"] = 0
    gib_zurück w

# Liste: virtuelles Rendering (nur sichtbare Zeilen), Klick/Up/Down
# wählt aus, Rad scrollt. zeilen = Liste von Strings.
# on_auswahl(w, index).
funktion uim_liste(x, y, b, h, zeilen, on_auswahl):
    setze w auf _uim_basis("liste", x, y, b, h)
    w["zeilen"] = zeilen
    w["auswahl"] = 0 - 1
    w["scroll_y"] = 0
    w["on_auswahl"] = on_auswahl
    w["fokussierbar"] = wahr
    gib_zurück w
