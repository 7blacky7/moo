# P016-D1 B1: toolkitfreier oeffentlicher Theme-/Backendvertrag.
# Kein Fenster, kein Desktop-Toolkit, keine Host-Ereignisschleife.

importiere ui_moo_kern

setze s auf {}
s["fehler"] = 0

funktion pruef(name, ok):
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        s["fehler"] = s["fehler"] + 1
    gib_zurück ok

funktion theme_schema_v1(theme):
    wenn theme == nichts:
        gib_zurück falsch
    setze felder auf ["version", "name", "hintergrund", "flaeche", "flaeche_hover", "flaeche_druck", "rand", "akzent", "text", "text_gedimmt", "radius", "schrift", "abstand"]
    für feld in felder:
        wenn theme.enthält(feld) == falsch:
            gib_zurück falsch
    wenn theme["version"] != 1:
        gib_zurück falsch
    wenn länge(theme["hintergrund"]) != 4 oder länge(theme["flaeche"]) != 4:
        gib_zurück falsch
    wenn länge(theme["flaeche_hover"]) != 4 oder länge(theme["flaeche_druck"]) != 4:
        gib_zurück falsch
    wenn länge(theme["rand"]) != 4 oder länge(theme["akzent"]) != 4:
        gib_zurück falsch
    wenn länge(theme["text"]) != 4 oder länge(theme["text_gedimmt"]) != 4:
        gib_zurück falsch
    gib_zurück wahr

funktion widget_schema_v1(widget):
    wenn typ_von(widget) != "Woerterbuch":
        gib_zurück falsch
    setze felder auf ["typ", "uid", "id", "x", "y", "b", "h", "sichtbar", "aktiv", "fokussierbar", "text", "hover", "druck", "kinder", "on_klick", "on_wechsel", "ausrichtung", "wert", "min", "max"]
    für feld in felder:
        wenn widget.enthält(feld) == falsch:
            gib_zurück falsch
    wenn widget.enthält("version") == falsch:
        gib_zurück falsch
    gib_zurück widget["version"] == 1

funktion d1_custom_zeichne(kontext, widget, absolut_x, absolut_y):
    s["custom_aufrufe"] = s["custom_aufrufe"] + 1
    s["custom_kontext_ok"] = kontext["d1_custom_kennung"] == 73
    s["custom_uid"] = widget["uid"]
    s["custom_x"] = absolut_x
    s["custom_y"] = absolut_y
    setze op auf kontext["backend_vertrag"]["operationen"]["rechteck"]
    gib_zurück op(kontext, [nichts, absolut_x, absolut_y, widget["b"], widget["h"], wahr])

funktion d1_custom_befehl_ok(befehle):
    wenn länge(befehle) == 0:
        gib_zurück falsch
    setze befehl auf befehle[länge(befehle) - 1]
    wenn befehl["op"] != "rechteck":
        gib_zurück falsch
    setze args auf befehl["args"]
    gib_zurück länge(args) == 5 und args[0] == 10 und args[1] == 16 und args[2] == 9 und args[3] == 7 und args[4] == wahr

setze dunkel auf uim_theme_dunkel()
setze hell auf uim_theme_hell()
pruef("canonical-dark-theme-schema-v1", theme_schema_v1(dunkel))
pruef("canonical-light-theme-schema-v1", theme_schema_v1(hell))

# Der existierende Backendvertrag v1 bleibt kompatibel.
funktion backend_v1_akzeptiert():
    setze backend auf uim_backend_mock()
    wenn backend == nichts:
        gib_zurück falsch
    wenn backend.enthält("version") == falsch:
        gib_zurück falsch
    wenn backend["version"] != 1:
        gib_zurück falsch
    gib_zurück uim_backend_wurzel(backend, 64, 48) != nichts

pruef("backend-v1-accepted", backend_v1_akzeptiert())

# Eine explizite Registry verwaltet versionierte Backends ohne globalen Zustand.
# Der Backend-Name ist der kanonische Registrierungsschluessel.
funktion backend_register_schema_v1(register):
    wenn typ_von(register) != "Woerterbuch":
        gib_zurück falsch
    setze felder auf ["version", "backends", "auswahl"]
    für feld in felder:
        wenn register.enthält(feld) == falsch:
            gib_zurück falsch
    wenn register["version"] != 1:
        gib_zurück falsch
    wenn typ_von(register["backends"]) != "Woerterbuch":
        gib_zurück falsch
    gib_zurück register["auswahl"] == ""

setze backend_register auf uim_backend_register_neu()
pruef("backend-registry-schema-v1", backend_register_schema_v1(backend_register))

setze registry_mock auf uim_backend_mock()
registry_mock["d1_registry_id"] = 91
setze registry_registriert auf uim_backend_registriere(backend_register, registry_mock)
setze registry_gefunden auf uim_backend_finde(backend_register, "mock-framebuffer", 1)
setze registry_ausgewaehlt auf uim_backend_waehle(backend_register, "mock-framebuffer", 1)

pruef("backend-registry-named-register-lookup", registry_registriert und registry_gefunden != nichts und registry_gefunden["d1_registry_id"] == 91)
setze registry_kontext auf uim_backend_wurzel(registry_ausgewaehlt, 32, 24)
pruef("backend-registry-selection-mock-frame-compatible", registry_ausgewaehlt != nichts und backend_register["auswahl"] == "mock-framebuffer" und registry_kontext != nichts und registry_kontext["backend"] == "mock-framebuffer")

setze registry_vorher auf backend_register["auswahl"]
setze registry_unbekannt_gefunden auf uim_backend_finde(backend_register, "nicht-registriert", 1)
setze registry_unbekannt_ausgewaehlt auf uim_backend_waehle(backend_register, "nicht-registriert", 1)
pruef("backend-registry-unknown-fail-closed", registry_unbekannt_gefunden == nichts und registry_unbekannt_ausgewaehlt == nichts und backend_register["auswahl"] == registry_vorher)

setze registry_anzahl_vorher auf länge(backend_register["backends"])
setze registry_mangelhaft auf {}
registry_mangelhaft["name"] = "fake-v1"
registry_mangelhaft["version"] = 1
setze registry_mangelhaft_registriert auf uim_backend_registriere(backend_register, registry_mangelhaft)

setze registry_v2 auf uim_backend_mock()
registry_v2["name"] = "mock-v2"
registry_v2["version"] = 2
setze registry_v2_registriert auf uim_backend_registriere(backend_register, registry_v2)
setze registry_v2_gefunden auf uim_backend_finde(backend_register, "mock-framebuffer", 2)
setze registry_v2_ausgewaehlt auf uim_backend_waehle(backend_register, "mock-framebuffer", 2)
setze registry_gueltig_nachher auf uim_backend_finde(backend_register, "mock-framebuffer", 1)
pruef("backend-registry-version-mismatch-fail-closed", registry_mangelhaft_registriert == falsch und backend_register["backends"].enthält("fake-v1") == falsch und registry_v2_registriert == falsch und backend_register["backends"].enthält("mock-v2") == falsch und länge(backend_register["backends"]) == registry_anzahl_vorher und registry_gueltig_nachher != nichts und registry_gueltig_nachher["d1_registry_id"] == 91 und registry_v2_gefunden == nichts und registry_v2_ausgewaehlt == nichts und backend_register["auswahl"] == registry_vorher)

# Ein partielles Theme muss fail-closed abgelehnt werden. Weder die
# Theme-Referenz noch der Invalidation-State duerfen sich dabei aendern.
setze k auf uim_mock_wurzel(80, 60)
setze vorher auf k["theme"]
vorher["d1_identitaet"] = "unveraendert"
k["backend_zustand"]["ungueltig"] = falsch

setze partiell auf {}
partiell["version"] = 1
partiell["name"] = "partiell"
setze angenommen auf uim_theme_setze(k, partiell)
pruef("partial-theme-rejected", angenommen == falsch)

funktion theme_state_unveraendert(kontext):
    wenn kontext["backend_zustand"]["ungueltig"] != falsch:
        gib_zurück falsch
    setze theme auf kontext["theme"]
    wenn theme.enthält("d1_identitaet") == falsch:
        gib_zurück falsch
    wenn theme["d1_identitaet"] != "unveraendert":
        gib_zurück falsch
    wenn theme.enthält("name") == falsch:
        gib_zurück falsch
    gib_zurück theme["name"] == "dunkel"

pruef("partial-theme-state-byte-identical", theme_state_unveraendert(k))

# Jeder oeffentliche Konstruktor liefert denselben versionierten Basisvertrag.
pruef("button-widget-schema-v1", widget_schema_v1(uim_knopf("D1", 0, 0, 20, 10, nichts)))
pruef("panel-widget-schema-v1", widget_schema_v1(uim_panel("D1", 0, 0, 20, 10)))
setze schema_kontext auf uim_mock_wurzel(20, 10)
pruef("root-widget-schema-v1", widget_schema_v1(schema_kontext["wurzel"]))

# Oeffentliche Custom-Widgets zeichnen ueber einen Moo-Callback. Der Callback
# erhaelt Kontext, exaktes Widget und bereits transformierte Absolutkoordinaten.
s["custom_aufrufe"] = 0
s["custom_kontext_ok"] = falsch
s["custom_uid"] = 0
s["custom_x"] = 0
s["custom_y"] = 0
setze custom_kontext auf uim_mock_wurzel(80, 60)
custom_kontext["hintergrund_zeichnen"] = falsch
custom_kontext["d1_custom_kennung"] = 73
setze custom_panel auf uim_panel("Custom", 7, 11, 30, 25)
setze custom_widget auf uim_widget_eigen(3, 5, 9, 7, d1_custom_zeichne)
uim_hinzu(custom_panel, custom_widget)
uim_hinzu(custom_kontext, custom_panel)
setze custom_gezeichnet auf uim_mock_zeichne(custom_kontext)
setze custom_befehle auf uim_mock_befehle(custom_kontext)
pruef("custom-widget-constructor-schema-v1", widget_schema_v1(custom_widget) und custom_widget["typ"] == "eigen" und custom_widget.enthält("on_zeichne"))
pruef("custom-widget-draw-callback-once", custom_gezeichnet und s["custom_aufrufe"] == 1)
pruef("custom-widget-context-widget-absolute-offset", s["custom_kontext_ok"] und s["custom_uid"] == custom_widget["uid"] und s["custom_x"] == 10 und s["custom_y"] == 16)
pruef("custom-widget-deterministic-mock-command", d1_custom_befehl_ok(custom_befehle))

# Unbekannte Typen und gefaelschte Custom-Widgets ohne Callback bleiben
# fail-closed und duerfen keine Backendbefehle erzeugen.
setze unbekannt_kontext auf uim_mock_wurzel(20, 10)
unbekannt_kontext["hintergrund_zeichnen"] = falsch
setze unbekannt auf uim_panel("?", 0, 0, 10, 5)
unbekannt["typ"] = "nicht-registriert"
uim_hinzu(unbekannt_kontext, unbekannt)
setze unbekannt_ergebnis auf uim_mock_zeichne(unbekannt_kontext)
pruef("unknown-widget-type-fail-closed", unbekannt_ergebnis == falsch und länge(uim_mock_befehle(unbekannt_kontext)) == 0)

setze ohne_callback_kontext auf uim_mock_wurzel(20, 10)
ohne_callback_kontext["hintergrund_zeichnen"] = falsch
setze ohne_callback auf uim_panel("?", 0, 0, 10, 5)
ohne_callback["typ"] = "eigen"
uim_hinzu(ohne_callback_kontext, ohne_callback)
setze ohne_callback_ergebnis auf uim_mock_zeichne(ohne_callback_kontext)
pruef("unregistered-custom-widget-fail-closed", ohne_callback_ergebnis == falsch und länge(uim_mock_befehle(ohne_callback_kontext)) == 0)

# Der oeffentliche Capability-Manifestvertrag berichtet ausschliesslich
# bereits bewiesene, plattformneutrale D1-Faehigkeiten. Jeder Aufruf muss
# ein frisches Woerterbuch liefern; Aufrufermutation darf nicht leaken.
setze faehigkeiten_a auf uim_faehigkeiten()
pruef("capability-manifest-schema-v1", typ_von(faehigkeiten_a) == "Woerterbuch" und länge(faehigkeiten_a) == 6 und faehigkeiten_a.enthält("version") und faehigkeiten_a.enthält("theme_schema") und faehigkeiten_a.enthält("backend_registry") und faehigkeiten_a.enthält("custom_widget") und faehigkeiten_a.enthält("hybrid_services") und faehigkeiten_a.enthält("hybrid_host_adapter_owned"))
pruef("capability-manifest-version-v1", faehigkeiten_a["version"] == 1)
pruef("capability-manifest-theme-schema-v1", faehigkeiten_a["theme_schema"] == 1)
pruef("capability-manifest-backend-registry-v1", faehigkeiten_a["backend_registry"] == 1)
pruef("capability-manifest-custom-widget-v1", faehigkeiten_a["custom_widget"] == 1)
pruef("capability-manifest-hybrid-services-v1", faehigkeiten_a["hybrid_services"] == 1)
pruef("capability-manifest-hybrid-host-owned", faehigkeiten_a["hybrid_host_adapter_owned"] == wahr)
faehigkeiten_a["version"] = 99
faehigkeiten_a["theme_schema"] = 99
faehigkeiten_a["unbekannte_faehigkeit"] = wahr
setze faehigkeiten_b auf uim_faehigkeiten()
pruef("capability-manifest-fresh-copy-exact-keyset", faehigkeiten_b["version"] == 1 und faehigkeiten_b["theme_schema"] == 1 und länge(faehigkeiten_b) == 6 und faehigkeiten_b.enthält("unbekannte_faehigkeit") == falsch und faehigkeiten_b.enthält("full_bidi") == falsch und faehigkeiten_b.enthält("native_widgets") == falsch und faehigkeiten_b.enthält("gpu") == falsch)

wenn s["fehler"] == 0:
    zeige "P016-D1-PUBLIC-CONTRACT-OK"
sonst:
    zeige "P016-D1-PUBLIC-CONTRACT-FEHLER " + text(s["fehler"])
    wirf "P016-D1 oeffentlicher Vertrag verletzt"
