# P016-A1: Reiner Moo-Test des Backendvertrags.
# Oeffnet kein Fenster und ruft weder GTK noch SDL auf.

importiere ui
importiere ui_moo

setze s auf {}
s["klicks"] = 0

funktion auf_klick(w):
    s["klicks"] = s["klicks"] + 1
    gib_zurück wahr

setze k auf uim_mock_wurzel(320, 200)
setze knopf auf uim_hinzu(k, uim_knopf("TEST", 20, 20, 100, 32, auf_klick))
uim_hinzu(k, uim_label("Mock-Framebuffer", 20, 70, 180, 20))
uim_hinzu(k, uim_checkbox("Aktiv", 20, 105, 120, 24, falsch, nichts))

setze zeichnen_ok auf uim_mock_zeichne(k)
setze befehle auf uim_mock_befehle(k)
setze hat_befehle auf länge(befehle) > 3

uim_mock_maus(k, 40, 35, wahr)
uim_mock_maus(k, 40, 35, falsch)
setze klick_ok auf s["klicks"] == 1
setze fokus_ok auf k["fokus"] != nichts
uim_backend_bewegung(k, 40, 35)
setze hover_vor_blur auf knopf["hover"]

uim_mock_fokus(k, falsch)
setze fokus_event_ok auf k["hat_fokus"] == falsch und k["druck"] == nichts
setze druck_geloest auf knopf["druck"] == falsch
setze hover_geloest auf hover_vor_blur und knopf["hover"] == falsch und k["hover"] == nichts
setze klicks_vor_blur auf s["klicks"]
uim_mock_maus(k, 40, 35, wahr)
uim_mock_maus(k, 40, 35, falsch)
setze blur_blockiert auf s["klicks"] == klicks_vor_blur

setze backend auf k["backend_vertrag"]
setze version_ok auf backend["version"] == 1
setze faehigkeiten auf backend["faehigkeiten"]
setze vertrag_ok auf faehigkeiten["alpha"] und faehigkeiten["clip"] und faehigkeiten["rechteck_rund"]

setze ops_unvollstaendig auf {}
setze op_namen_ohne_clip_loesche auf ["anfordern", "farbe", "rechteck", "rechteck_rund", "kreis", "linie", "text", "text_breite", "clip_setze"]
für op_name in op_namen_ohne_clip_loesche:
    ops_unvollstaendig[op_name] = backend["operationen"][op_name]
setze fehlende_op_abgelehnt auf uim_backend_neu("ungueltig-op", faehigkeiten, ops_unvollstaendig) == nichts
setze fehlende_caps_abgelehnt auf uim_backend_neu("ungueltig-caps", {}, backend["operationen"]) == nichts
setze caps_falscher_typ auf {}
caps_falscher_typ["alpha"] = "ja"
caps_falscher_typ["clip"] = wahr
caps_falscher_typ["rechteck_rund"] = wahr
caps_falscher_typ["kreis_rand"] = wahr
caps_falscher_typ["text_metrik"] = "test"
setze falscher_cap_typ_abgelehnt auf uim_backend_neu("ungueltig-cap-typ", caps_falscher_typ, backend["operationen"]) == nichts
caps_falscher_typ["alpha"] = wahr
caps_falscher_typ["text_metrik"] = falsch
setze text_metrik_bool_abgelehnt auf uim_backend_neu("ungueltig-text-metrik-bool", caps_falscher_typ, backend["operationen"]) == nichts
caps_falscher_typ["text_metrik"] = 8
setze text_metrik_zahl_abgelehnt auf uim_backend_neu("ungueltig-text-metrik-zahl", caps_falscher_typ, backend["operationen"]) == nichts
caps_falscher_typ["text_metrik"] = ""
setze text_metrik_leer_abgelehnt auf uim_backend_neu("ungueltig-text-metrik-leer", caps_falscher_typ, backend["operationen"]) == nichts

wenn zeichnen_ok und hat_befehle und klick_ok und fokus_ok und fokus_event_ok und druck_geloest und hover_geloest und blur_blockiert und version_ok und vertrag_ok und fehlende_op_abgelehnt und fehlende_caps_abgelehnt und falscher_cap_typ_abgelehnt und text_metrik_bool_abgelehnt und text_metrik_zahl_abgelehnt und text_metrik_leer_abgelehnt:
    zeige "UIMOO-MOCK-BACKEND-OK"
sonst:
    zeige "UIMOO-MOCK-BACKEND-FEHLER befehle=" + text(länge(befehle)) + " klicks=" + text(s["klicks"])
    wirf "P016-A1 Mock-Backend-Invariante verletzt"
