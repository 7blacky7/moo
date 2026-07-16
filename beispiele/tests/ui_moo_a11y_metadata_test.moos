# P016-O4: toolkitfreier öffentlicher Accessibility-Metadatenvertrag.
# Kein Fenster, kein Hostadapter, keine native Eingabe.
importiere ui_moo_kern

setze s auf {}
s["fehler"] = 0
s["checks"] = 0

funktion pruef(name, ok):
    s["checks"] = s["checks"] + 1
    wenn ok:
        zeige "PASS " + name
    sonst:
        s["fehler"] = s["fehler"] + 1
        zeige "FAIL " + name
    gib_zurück ok

funktion nichts_klick(w):
    gib_zurück wahr

funktion nichts_wechsel(w, wert):
    gib_zurück wahr

setze k auf uim_mock_wurzel(240, 160)

# Exakt ein noch fehlender öffentlicher API-Aufruf: dadurch ist RED eindeutig.
funktion meta(w):
    gib_zurück uim_a11y(k, w)

pruef("nichts-fail-closed", meta(nichts) == nichts)
pruef("invalid-fail-closed", meta({}) == nichts)

setze knopf auf uim_hinzu(k, uim_knopf("Speichern", 10, 10, 90, 24, nichts_klick))
knopf["a11y_name"] = "Dokument speichern"
knopf["a11y_beschreibung"] = "Speichert das aktuelle Dokument"
setze knopf_felder auf länge(knopf)

setze a auf meta(knopf)
pruef("schema-v1-exakt", typ_von(a) == "Woerterbuch" und länge(a) == 10 und a.enthält("version") und a.enthält("uid") und a.enthält("role") und a.enthält("states") und a.enthält("actions") und a.enthält("bounds") und a.enthält("bounds_space") und a.enthält("name") und a.enthält("value") und a.enthält("description"))
pruef("button-role", a["version"] == 1 und a["role"] == 4)
pruef("button-state-initial", a["states"] == 0)
pruef("button-actions", a["actions"] == 3)
pruef("button-local-bounds", länge(a["bounds"]) == 4 und a["bounds"]["x"] == 10 und a["bounds"]["y"] == 10 und a["bounds"]["b"] == 90 und a["bounds"]["h"] == 24 und a["bounds_space"] == "parent-local")
pruef("button-name-description", a["name"] == "Dokument speichern" und a["description"] == "Speichert das aktuelle Dokument" und a["value"] == "")

# Jede Abfrage liefert eine frische Kopie und mutiert das Widget nicht.
a["role"] = 99
a["bounds"]["x"] = 999
setze a2 auf meta(knopf)
pruef("fresh-copy-no-widget-mutation", a2["role"] == 4 und a2["bounds"]["x"] == 10 und länge(knopf) == knopf_felder)

# Deterministischer Mock-Press setzt Fokus, ohne Host-Eingabe.
uim_mock_maus(k, 20, 20, wahr)
setze af auf meta(knopf)
pruef("focused-state", af["states"] == 2)

setze checkbox auf uim_checkbox("Aktiv", 10, 45, 90, 24, wahr, nichts_wechsel)
checkbox["aktiv"] = falsch
checkbox["sichtbar"] = falsch
setze ac auf meta(checkbox)
pruef("checkbox-role-state-actions", ac["role"] == 5 und ac["states"] == 69 und ac["actions"] == 3 und ac["value"] == "wahr")

setze slider auf uim_slider(10, 80, 100, 24, 0, 100, 25, nichts_wechsel)
setze slider_meta auf meta(slider)
pruef("slider-role-actions-value", slider_meta["role"] == 6 und slider_meta["actions"] == 29 und slider_meta["value"] == "25")

setze label auf uim_label("Status", 10, 115, 80, 20)
setze al auf meta(label)
pruef("label-default-name", al["role"] == 3 und al["actions"] == 0 und al["name"] == "Status" und al["description"] == "")

setze panel auf uim_panel("Gruppe", 120, 10, 100, 100)
setze ap auf meta(panel)
pruef("panel-group-role", ap["role"] == 2 und ap["actions"] == 0)

wenn s["fehler"] == 0 und s["checks"] == 14:
    zeige "P016-O4-MOO-A11Y-OK checks=14 no_ui=1"
sonst:
    zeige "P016-O4-MOO-A11Y-FEHLER checks=" + text(s["checks"]) + " fehler=" + text(s["fehler"])
    wirf "P016-O4 ui_moo Accessibility-Vertrag verletzt"
