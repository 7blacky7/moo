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
# Referenz-Widgets (Rest folgt in UIMOO-3):
#   uim_knopf(text, x, y, b, h, on_klick)
#   uim_label(text, x, y, b, h)
#
# Widget-Dict (Konvention):
#   typ, uid, id, x, y, b, h, sichtbar, aktiv, fokussierbar,
#   text, hover, druck, kinder, on_klick
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
    gib_zurück w

# Oberstes getroffenes Widget (z-Order: zuletzt hinzugefügt gewinnt).
# Rückwärts-Suche ohne break: erster Treffer bleibt stehen.
funktion _uim_treffer(kontext, x, y):
    setze kinder auf kontext["wurzel"]["kinder"]
    setze gefunden auf nichts
    setze i auf länge(kinder) - 1
    solange i >= 0:
        wenn gefunden == nichts:
            setze w auf kinder[i]
            wenn w["sichtbar"] und w["aktiv"]:
                wenn x >= w["x"] und x < w["x"] + w["b"] und y >= w["y"] und y < w["y"] + w["h"]:
                    setze gefunden auf w
        setze i auf i - 1
    gib_zurück gefunden

# Klick-Aktivierung (Knopf-Callback; weitere Typen folgen in UIMOO-3).
funktion _uim_aktiviere(kontext, w):
    setze cb auf w["on_klick"]
    wenn cb == nichts:
        gib_zurück falsch
    cb(w)
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

# ------------------------------------------------------------
# Event-Dispatcher (an die Leinwand gebunden von uim_wurzel)
# ------------------------------------------------------------

funktion _uim_on_maus(lw, x, y, taste):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    wenn taste != 1:
        gib_zurück falsch
    setze w auf _uim_treffer(kontext, x, y)
    kontext["druck"] = w
    wenn w == nichts:
        kontext["fokus"] = nichts
    sonst:
        w["druck"] = wahr
        wenn w["fokussierbar"]:
            kontext["fokus"] = w
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
    setze w auf _uim_treffer(kontext, x, y)
    wenn w != nichts:
        wenn w["uid"] == p["uid"]:
            _uim_aktiviere(kontext, w)
    ui_leinwand_anfordern(lw)
    gib_zurück wahr

funktion _uim_on_bewegung(lw, x, y):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    setze w auf _uim_treffer(kontext, x, y)
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
    gib_zurück falsch

# ------------------------------------------------------------
# Zeichnen: pro Frame Hintergrund + alle Kinder in Einfüge-Reihenfolge.
# Widget-Zeichner sind rein: Zustand rein, Pixel raus, KEINE Werte
# außerhalb des Themes.
# ------------------------------------------------------------

funktion _uim_z_knopf(kontext, z, w):
    setze t auf kontext["theme"]
    setze flaeche auf t["flaeche"]
    wenn w["aktiv"] == falsch:
        setze flaeche auf t["flaeche_druck"]
    sonst wenn w["druck"]:
        setze flaeche auf t["flaeche_druck"]
    sonst wenn w["hover"]:
        setze flaeche auf t["flaeche_hover"]
    _uim_farbe(z, flaeche)
    ui_zeichne_rechteck_rund(z, w["x"], w["y"], w["b"], w["h"], t["radius"], wahr)
    _uim_farbe(z, t["rand"])
    ui_zeichne_rechteck_rund(z, w["x"], w["y"], w["b"], w["h"], t["radius"], falsch)
    wenn _uim_slot_uid(kontext, "fokus") == w["uid"]:
        _uim_farbe(z, t["akzent"])
        ui_zeichne_rechteck_rund(z, w["x"] - 2, w["y"] - 2, w["b"] + 4, w["h"] + 4, t["radius"] + 2, falsch)
    setze schrift auf t["schrift"]
    setze tb auf ui_zeichne_text_breite(z, w["text"], schrift)
    setze tx auf w["x"] + (w["b"] - tb) / 2
    setze ty auf w["y"] + (w["h"] - schrift) / 2 - 1
    setze farbe auf t["text"]
    wenn w["aktiv"] == falsch:
        setze farbe auf t["text_gedimmt"]
    _uim_farbe(z, farbe)
    ui_zeichne_text(z, tx, ty, w["text"], schrift)
    gib_zurück wahr

funktion _uim_z_label(kontext, z, w):
    setze t auf kontext["theme"]
    _uim_farbe(z, t["text"])
    ui_zeichne_text(z, w["x"], w["y"], w["text"], t["schrift"])
    gib_zurück wahr

funktion _uim_on_zeichne(lw, z):
    setze kontext auf _uim_ctx(lw)
    wenn kontext == nichts:
        gib_zurück falsch
    setze t auf kontext["theme"]
    setze wurzel auf kontext["wurzel"]
    _uim_farbe(z, t["hintergrund"])
    ui_zeichne_rechteck(z, 0, 0, wurzel["b"], wurzel["h"], wahr)
    setze kinder auf wurzel["kinder"]
    für w in kinder:
        wenn w["sichtbar"]:
            wenn w["typ"] == "knopf":
                _uim_z_knopf(kontext, z, w)
            sonst wenn w["typ"] == "label":
                _uim_z_label(kontext, z, w)
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
    kontext["fokus"] = nichts
    setze wurzel auf _uim_basis("wurzel", 0, 0, b, h)
    kontext["wurzel"] = wurzel
    setze alle auf _UIM["kontexte"]
    alle["k" + text(lw)] = kontext
    ui_leinwand_on_maus(lw, _uim_on_maus)
    ui_leinwand_on_maus_los(lw, _uim_on_maus_los)
    ui_leinwand_on_bewegung(lw, _uim_on_bewegung)
    ui_leinwand_on_taste(lw, _uim_on_taste)
    gib_zurück kontext

funktion uim_hinzu(kontext, widget):
    kontext["wurzel"]["kinder"].hinzufügen(widget)
    ui_leinwand_anfordern(kontext["leinwand"])
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
