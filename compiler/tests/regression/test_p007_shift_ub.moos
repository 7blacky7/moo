# P007-U3 Regression: Shift-UB (moo_ops.c #7).
# Frueher: `<<`/`>>` mit Count >= 64 oder < 0 = Undefined Behavior (Crash/Muell).
# Jetzt: validierter Count -> sauberer, fangbarer moo-Fehler.
# Legale Eingaben muessen UNVERAENDERT funktionieren (keine Semantik-Drift).
# Hinweis: Im Fehlerfall wird der Wurf in einer Zuweisung gefangen, damit kein
# Teilergebnis von `zeige` ausgegeben wird (Error-Flag wird erst nach dem
# Statement geprueft).

# 1. Legale Shifts (Semantik bleibt gleich, inkl. arithmetischer >>)
zeige 1 << 4
zeige 256 >> 2
zeige -8 >> 1

# 2. Shift-Count = 64 -> Fehlerpfad statt UB
versuche:
    setze a auf 1 << 64
    zeige "FEHLER: kein Wurf bei << 64"
fange fehler:
    zeige "ok: << 64 gefangen"

# 3. Negativer Shift-Count -> Fehlerpfad statt UB
versuche:
    setze b auf 1 >> -1
    zeige "FEHLER: kein Wurf bei >> -1"
fange fehler:
    zeige "ok: >> -1 gefangen"
