# ============================================================
# stdlib/ui.moo — oeffentliches UI-Modul fuer moo
#
# Cross-platform Desktop-UI. Inspiriert vom AutoIt GUIBuilder
# (Moritz Kolar). Schicht 1 (Primitive) liegt in C (moo_ui.h),
# Schicht 2 (Komposition) hier in moo-stdlib.
#
# Nutzung:
#   importiere ui
#
# Das bringt folgendes in den Scope:
#
#   === Schicht 1 — Primitive (als C-Builtins vom Runtime bereitgestellt) ===
#     ui_fenster(titel, b, h, resizable, maximiert, parent)   -> handle
#     ui_zeige(fenster)                                        -> blocking
#     ui_zeige_nebenbei(fenster)                               -> non-blocking
#     ui_laufen()                                              -> Event-Loop
#     ui_beenden()                                             -> Loop verlassen
#     ui_label(parent, text, x, y, b, h)
#     ui_label_setze(label, text)
#     ui_knopf(parent, text, x, y, b, h, callback)
#     ui_checkbox(parent, text, x, y, b, h, initial, callback)
#     ui_radio(parent, gruppe, text, x, y, b, h, callback)
#     ui_eingabe(parent, x, y, b, h, platzhalter, passwort)    -> handle
#     ui_eingabe_text(handle)                                  -> string
#     ui_eingabe_setze(handle, text)
#     ui_textbereich(parent, x, y, b, h)
#     ui_dropdown(parent, optionen, x, y, b, h, callback)
#     ui_liste(parent, spalten, x, y, b, h)
#     ui_slider(parent, min, max, start, x, y, b, h, callback)
#     ui_fortschritt(parent, x, y, b, h)
#     ui_fortschritt_setze(handle, anteil)
#     ui_bild(parent, pfad, x, y, b, h)
#     ui_leinwand(parent, x, y, b, h, on_zeichne)     -> handle (Custom-Draw)
#     ui_leinwand_anfordern(leinwand)                  -> Repaint-Request
#     ui_leinwand_erneuern(leinwand)                   -> Alias zu anfordern
#     # Zeichner-API (nur innerhalb on_zeichne(leinwand, zeichner) gueltig):
#     ui_zeichne_farbe(z, r, g, b, a)                  -> 0..255
#     ui_zeichne_linie(z, x1, y1, x2, y2, breite)
#     ui_zeichne_rechteck(z, x, y, b, h, gefuellt)
#     ui_zeichne_kreis(z, cx, cy, radius, gefuellt)
#     ui_zeichne_text(z, x, y, text, schriftgroesse)
#     ui_zeichne_bild(z, x, y, b, h, pfad)             -> PNG-Datei
#     ui_rahmen(parent, titel, x, y, b, h)
#     ui_trenner(parent, x, y, b, h)
#     ui_tabs(parent, x, y, b, h)
#     ui_tab_hinzu(tabs, titel)
#     ui_scroll(parent, x, y, b, h)
#     ui_sichtbar(widget, ja)
#     ui_aktiv(widget, ja)
#     ui_position_setze(widget, x, y)
#     ui_groesse_setze(widget, b, h)
#     ui_farbe_setze(widget, hex)
#     ui_schrift_setze(widget, groesse, fett)
#     ui_timer_hinzu(ms, callback)
#     ui_timer_entfernen(id)
#     ui_info(titel, text), ui_warnung(...), ui_fehler(...), ui_frage(...)
#     ui_eingabe_dialog(titel, prompt, default)
#     ui_datei_oeffnen(titel, filter)
#     ui_datei_speichern(titel, filter)
#     ui_ordner_waehlen(titel)
#     ui_menueleiste(fenster), ui_menue(leiste, titel),
#     ui_menue_eintrag(menue, text, callback), ui_menue_trenner(menue)
#
#   === Schicht 2 — Komposition (in moo, siehe ui_baue.moo + ui_komponenten.moo) ===
#     ui_baue(container, titel, b, h, setups, resizable, maximiert, parent)
#     ui_debug_baue(container, titel, b, h, setups, ...)
#     rufe_mit(funk, args)
#
#     setup_login_form(g, user_label, pass_label, btn_breite)
#     setup_user_form(g, name_label, email_label, cat_label, breite)
#     setup_toolbar(g, buttons_liste)
#     setup_status_bar(g, status_text, info_text)
#     setup_app_header(g, titel, version)
#     setup_action_buttons(g, buttons_liste)
#     setup_notes_area(g, label, breite, hoehe)
#     setup_about_info(g, titel, version, beschreibung)
#
# ============================================================
#
# Schnellstart-Beispiel:
#
#   importiere ui
#
#   setze g auf {}
#   ui_baue(g, "Login", 420, 240, [
#       [setup_login_form, ["Benutzer:", "Passwort:", 100]],
#       [setup_status_bar, ["Bereit", "v1.0"]],
#   ])
#   ui_zeige(g.hFenster)
#   ui_laufen()
#
# ============================================================


# Re-Export: importiert beide Komposition-Module. Danach sind
# alle Setup-Funktionen und ui_baue im aufrufenden Scope verfuegbar.
importiere ui_baue
importiere ui_komponenten
importiere ui_aktionen
importiere ui_layout
importiere ui_bind
