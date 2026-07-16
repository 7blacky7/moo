# ============================================================
# ui_login_demo.moo — Minimaler Showcase fuer stdlib/ui
#
# Zeigt: ui_baue + setup_login_form + setup_status_bar.
# Klick auf "Anmelden" -> Benutzername in Status-Leiste.
#
# ⚠ Dict-Zugriff via `g["key"]` (Bracket-Syntax). Dot-Access auf
# Dicts mit UI-Handles segfaultet aktuell im Runtime.
#
# Kompilieren:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_login_demo.moo
# Starten:
#   ./beispiele/ui_login_demo
# ============================================================

importiere ui

setze g auf {}

funktion on_login():
    setze name auf ui_eingabe_text(g["inpUser"])
    ui_label_setze(g["lblStatus"], "Willkommen, " + name + "!")

funktion on_cancel():
    ui_beenden()


ui_baue(g, "Login", 340, 260, [
    [setup_login_form, ["Benutzername:", "Passwort:", 100, on_login, on_cancel]],
    [setup_status_bar, ["Bereit", "v1.0"]],
])

ui_zeige(g["hFenster"])
ui_laufen()
