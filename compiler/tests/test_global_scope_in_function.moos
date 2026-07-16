# Regression-Tests für PRIO 2a: Globale Variablen in Funktions-Bodies sichtbar.
# Historisch war das ein harter Bug (stress-tetris 2026-04-11, chat-server,
# noise-dev, blog_engine). Heute ist es gefixt — dieser Test verhindert
# Regressionen.

# (1) Globale Liste lesen
setze xs auf [10, 20, 30]
funktion hole(i):
    gib_zurück xs[i]
zeige hole(1)

# (2) Konstante Liste lesen
konstante YS auf [100, 200, 300]
funktion hole_y(i):
    gib_zurück YS[i]
zeige hole_y(2)

# (3) Globalen Counter in Funktion mutieren (setze auf +1)
setze counter auf 0
funktion inc():
    setze counter auf counter + 1
inc()
inc()
inc()
zeige counter

# (4) Globale Liste über Index in Funktion mutieren (noise-dev Muster)
setze _prng_state auf [0]
funktion seed_setzen(s):
    _prng_state[0] = s
funktion seed_lesen():
    gib_zurück _prng_state[0]
seed_setzen(42)
zeige seed_lesen()

# (5) Globales Dict in Funktion lesen (chat-server Muster)
setze db auf ["conn"]
funktion speichern(x):
    db.hinzufügen(x)
speichern(7)
zeige db
