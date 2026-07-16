# P007-U3 Regression: String-Groessen-Overflow (moo_string.c #1 repeat, #4 replace).
# Frueher: text * grosseZahl bzw. Replace mit Expansion = signed-int32-Overflow
# (UB) + zu kleiner malloc -> Heap-Overflow/Crash.
# Jetzt: checked arithmetic -> sauberer, fangbarer moo-Fehler; korrekte Groessen.

# 1. Legale Wiederholung (Semantik unveraendert)
zeige "ab" * 3

# 2. text_wiederhole mit absurdem n -> sauberer Fehler statt Crash
versuche:
    setze x auf "abc" * 2000000000
    zeige "FEHLER: kein Wurf bei riesigem Repeat"
fange fehler:
    zeige "ok: riesiger Repeat gefangen"

# 3. Replace-Unterschaetzungs-Bug: 8x "a" -> "XYZ" ergibt 24 Zeichen.
#    Der alte Puffer length*2 (=16) war zu klein -> Heap-Overflow.
#    Jetzt exakt berechnet -> korrektes, vollstaendiges Ergebnis.
zeige "aaaaaaaa".ersetzen("a", "XYZ")

# 4. Replace ohne Treffer / leeres Muster bleibt korrekt
zeige "hallo welt".ersetzen("xx", "YY")
