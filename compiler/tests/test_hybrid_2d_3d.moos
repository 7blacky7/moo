# Regressions-Test fuer M5.1 — Hybrid 2D+3D Renderer.
#
# Exerciert alle neuen Codegen-Pfade aus Phase P1-P4:
#  - moo_hybrid_create / is_open / clear / update / close
#  - moo_hybrid_rect_z / line_z / circle_z
#
# Kein Headful-Run noetig (CI-stabil): unreachable wenn-falsch-Branch
# zwingt den Compiler zu emittieren ohne tatsaechlich GL zu starten.

funktion exercise_hybrid_api():
    setze win auf fenster_unified("test", 200, 150)
    wenn hybrid_offen(win):
        hybrid_löschen(win, 0.1, 0.2, 0.3)
        # Hintergrund-Quad bei z=10 (weiter hinten)
        zeichne_rechteck_z(win, 10, 10, 10.0, 100, 80, "#FF0000")
        # Vorder-Quad bei z=2 (vorne) — verdeckt Teil des HG
        zeichne_rechteck_z(win, 50, 30, 2.0, 60, 40, "#00FF00")
        # Linie ueber alles bei z=0 (ganz vorn)
        zeichne_linie_z(win, 0, 0, 200, 150, 0.0, "#FFFFFF")
        zeichne_kreis_z(win, 100, 75, 1.0, 12, "#FFFF00")
        hybrid_aktualisieren(win)
    hybrid_schliessen(win)

# Run-Guard: Body wird nie ausgefuehrt — die Funktion existiert nur,
# damit alle neuen _z-Dispatch-Pfade emittiert werden. Fehlt eine
# Builtin-Bindung in codegen.rs, bricht die Kompilierung sofort.
wenn falsch:
    exercise_hybrid_api()

zeige "Hybrid 2D+3D OK"
