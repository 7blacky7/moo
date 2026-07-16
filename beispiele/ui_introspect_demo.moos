# ============================================================
# beispiele/ui_introspect_demo.moo — Diagnose-Demo (Plan-004 P1)
#
# Zeigt die Introspection-API:
#   - Fenster mit ein paar Widgets bauen
#   - IDs per ui_widget_id_setze vergeben
#   - Baum via ui_widget_baum abrufen und als JSON drucken
#     (parsebar, fuer spaetere KI-Sidecars)
#   - ui_widget_suche auf gesetzte IDs
#   - ui_widget_dump fuer menschliche Diagnose
#   - ui_widget_anzahl / ui_widget_typen_zaehlen
#
# ALIVE-Test:
#   ./compiler/target/release/moo-compiler run \
#       beispiele/ui_introspect_demo.moo
# ============================================================

importiere ui
importiere ui_introspect


# Fenster + Widgets aufbauen. Keine ui_zeige + ui_laufen — diese
# Demo dient nur der Introspection und soll headless schnell
# durchlaufen (stdout pruefbar per Test-Runner).
setze g auf {}

setze g["hFenster"] auf ui_fenster("Introspect Demo", 400, 260, 0, nichts)
setze g["lblTitel"] auf ui_label(g["hFenster"], "Diagnose-Demo", 20, 20, 360, 24)
setze g["inpName"] auf ui_eingabe(g["hFenster"], 20, 60, 360, 28, "", falsch)
setze g["btnOk"]   auf ui_knopf(g["hFenster"], "OK",       20, 110, 170, 32, nichts)
setze g["btnAbbr"] auf ui_knopf(g["hFenster"], "Abbrechen", 210, 110, 170, 32, nichts)
setze g["chkAktiv"] auf ui_checkbox(g["hFenster"], "Aktiv", 20, 160, 200, 24, wahr, nichts)


# IDs vergeben — wichtig fuer ui_widget_suche.
ui_widget_id_setze(g["hFenster"],  "root")
ui_widget_id_setze(g["lblTitel"],  "lbl_titel")
ui_widget_id_setze(g["inpName"],   "inp_name")
ui_widget_id_setze(g["btnOk"],     "btn_ok")
ui_widget_id_setze(g["btnAbbr"],   "btn_abbrechen")
ui_widget_id_setze(g["chkAktiv"],  "chk_aktiv")


# Kopf-Block fuer den Test-Runner.
zeige "=== ui_introspect_demo ==="
zeige "anzahl=" + ui_widget_anzahl(g["hFenster"])


# JSON-Baum (valid parsebar, Sidecar-tauglich).
setze baum_json auf ui_widget_baum_json(g["hFenster"])
zeige "--- JSON-ANFANG ---"
zeige baum_json
zeige "--- JSON-ENDE ---"


# Typ-Zaehler.
setze typen auf ui_widget_typen_zaehlen(g["hFenster"])
zeige "typen=" + typen


# Such-Test: wir pruefen bewusst einen existierenden und einen
# nicht existierenden Key.
setze treffer auf ui_widget_suche(g["hFenster"], "btn_ok")
wenn treffer == nichts:
    zeige "suche btn_ok: NICHT GEFUNDEN"
sonst:
    zeige "suche btn_ok: OK"
    zeige ui_widget_dump(treffer)

setze fehl auf ui_widget_suche(g["hFenster"], "gibts_nicht")
wenn fehl == nichts:
    zeige "suche gibts_nicht: korrekt nichts"
sonst:
    zeige "suche gibts_nicht: UNERWARTET ein Widget zurueck"


# Alle Top-Ebenen-Widgets dumpen (Debug-Text).
zeige "--- DUMP ---"
zeige ui_widget_dump(g["hFenster"])
zeige ui_widget_dump(g["lblTitel"])
zeige ui_widget_dump(g["inpName"])
zeige ui_widget_dump(g["btnOk"])
zeige ui_widget_dump(g["btnAbbr"])
zeige ui_widget_dump(g["chkAktiv"])

zeige "=== ENDE ==="
