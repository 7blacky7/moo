# === DEUTSCH Test ===

# Variablen & Konstanten
setze name auf "Welt"
setze alter auf 25

# String + Zahl Ausgabe
zeige "Hallo " + name
zeige alter

# Rechnen
setze ergebnis auf (10 + 5) * 2
setze rest auf 17 % 5
setze potenz auf 2 ** 10
zeige ergebnis
zeige rest
zeige potenz

# Bedingungen
wenn alter >= 18:
    zeige "Erwachsen"
sonst:
    zeige "Kind"

# Schleife
setze i auf 0
solange i < 5:
    zeige i
    i += 1

# Listen
setze farben auf ["rot", "grün", "blau"]
für farbe in farben:
    zeige farbe

zeige farben[0]
zeige farben

# Dictionary
setze person auf {"name": "Anna", "alter": 30}
zeige person["name"]

# Funktionen
funktion quadrat(n):
    gib_zurück n * n

zeige quadrat(7)

funktion begrüße(wer):
    gib_zurück "Hallo " + wer + "!"

zeige begrüße("Max")

# Match
setze tag auf "Montag"
prüfe tag:
    fall "Montag":
        zeige "Wochenstart!"
    fall "Freitag":
        zeige "Wochenende!"
    standard:
        zeige "Normaler Tag"
