# ============================================================
# ui_login_demo.moo — Minimaler Showcase fuer stdlib/ui
#
# Zeigt: ui_baue + setup_login_form + setup_status_bar.
# Nach Klick auf "Anmelden" wird der eingetragene Name in der
# Status-Leiste angezeigt.
#
# Kompilieren:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_login_demo.moo
# Starten:
#   ./beispiele/ui_login_demo
# ============================================================

importiere ui

setze g auf {}

ui_baue(g, "Login", 340, 260, [
    [setup_login_form, ["Benutzername:", "Passwort:", 100]],
    [setup_status_bar, ["Bereit", "v1.0"]],
])

# Nachtraeglich Login-Button umbinden
ui_knopf_callback_setze(g.btnLogin, () => {
    setze name auf ui_eingabe_text(g.inpUser)
    ui_label_setze(g.lblStatus, "Willkommen, " + name + "!")
})

ui_zeige(g.hFenster)
ui_laufen()
