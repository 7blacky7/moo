# ============================================================
# test_api_smoke.moo — Smoke-Test fuer den einheitlichen test_*-Layer
# (Plan-008 A2). Uebt alle test_*-Builtins gegen ein 3D-Fenster UND
# ein Hybrid-Fenster aus. Headless lauffaehig via xvfb-run (gl33).
#
# START (headless):
#   xvfb-run -a -s "-screen 0 1280x800x24" \
#     env MOO_3D_BACKEND=gl33 moo-compiler run beispiele/test_api_smoke.moo
#
# Prueft:
#   - test_sim_taste / test_sim_maus_pos / test_sim_maus_taste /
#     test_sim_maus_rad / test_sim_maus_delta / test_sim_reset
#     laufen ohne Crash auf 3D- und Hybrid-Fenster (tag-dispatch).
#   - test_fenster_info liefert ein Dict mit backend + offen.
#   - test_screenshot schreibt eine BMP (gl33). Bei gl21 wuerde es
#     bewusst werfen (S5) — nicht Teil dieses gl33-Smokes.
#   - Aliases raum_sim_* funktionieren weiter (keine Breaking Changes).
# ============================================================

konstante WIN_B auf 640
konstante WIN_H auf 480

setze fehler auf 0

# ---- Hilfsfunktion: Sim-Sequenz gegen ein beliebiges Fenster ----
funktion sim_sequenz(win):
    test_sim_taste(win, "w", wahr)
    test_sim_taste(win, "w", falsch)
    test_sim_maus_pos(win, 100, 100)
    test_sim_maus_taste(win, "links", wahr)
    test_sim_maus_taste(win, "links", falsch)
    test_sim_maus_rad(win, 1.0)
    test_sim_maus_delta(win, 5.0, -3.0)
    test_sim_reset(win)

# ============================================================
# 1) 3D-Fenster (MOO_WINDOW3D)
# ============================================================
zeige "[smoke] erstelle 3D-Fenster ..."
setze raum auf raum_erstelle("test_api 3D", WIN_B, WIN_H)
raum_perspektive(raum, 70.0, 0.1, 300.0)

setze info3d auf test_fenster_info(raum)
zeige "[smoke] 3D backend=" + text(info3d["backend"]) + " offen=" + text(info3d["offen"])
wenn info3d["backend"] == "unbekannt":
    setze fehler auf fehler + 1
    zeige "[smoke] FEHLER: 3D backend unbekannt"

sim_sequenz(raum)
zeige "[smoke] 3D Sim-Sequenz OK"

# Ein paar Frames, dann Screenshot (gl33: echt; tag-dispatch via test_screenshot)
raum_löschen(raum, 0.2, 0.3, 0.4)
raum_aktualisieren(raum)
setze shot3d auf test_screenshot(raum, "/tmp/test_api_smoke_3d.bmp")
zeige "[smoke] 3D test_screenshot -> " + text(shot3d)
wenn shot3d != wahr:
    setze fehler auf fehler + 1
    zeige "[smoke] FEHLER: 3D Screenshot nicht erfolgreich"

# Alias-Gegenprobe (raum_sim_* muss weiter funktionieren)
raum_sim_taste(raum, "a", wahr)
raum_sim_taste(raum, "a", falsch)
raum_sim_reset(raum)
zeige "[smoke] 3D Alias raum_sim_* OK"

raum_schliessen(raum)

# ============================================================
# 2) Hybrid-Fenster (MOO_WINDOW_HYBRID)
# ============================================================
zeige "[smoke] erstelle Hybrid-Fenster ..."
setze hyb auf fenster_unified("test_api Hybrid", WIN_B, WIN_H)
raum_perspektive(hyb, 70.0, 0.1, 300.0)

setze infoh auf test_fenster_info(hyb)
zeige "[smoke] Hybrid backend=" + text(infoh["backend"]) + " offen=" + text(infoh["offen"])
wenn infoh["backend"] == "unbekannt":
    setze fehler auf fehler + 1
    zeige "[smoke] FEHLER: Hybrid backend unbekannt"

sim_sequenz(hyb)
zeige "[smoke] Hybrid Sim-Sequenz OK"

raum_löschen(hyb, 0.2, 0.3, 0.4)
hybrid_aktualisieren(hyb)
setze shoth auf test_screenshot(hyb, "/tmp/test_api_smoke_hybrid.bmp")
zeige "[smoke] Hybrid test_screenshot -> " + text(shoth)
wenn shoth != wahr:
    setze fehler auf fehler + 1
    zeige "[smoke] FEHLER: Hybrid Screenshot nicht erfolgreich"

hybrid_schliessen(hyb)

# ============================================================
# Ergebnis
# ============================================================
wenn fehler == 0:
    zeige "[smoke] ERGEBNIS: OK — alle test_* gegen 3D + Hybrid bestanden"
sonst:
    zeige "[smoke] ERGEBNIS: FEHLER (" + text(fehler) + " Probleme)"
