# ===========================
# moo Showcase — Alle Features
# ===========================

# --- Variablen & Konstanten ---
setze name auf "Welt"
setze alter auf 25
setze pi auf 3.14159
konstante MAX auf 100

# --- Ausgabe ---
zeige "Hallo " + name

# --- Rechnen ---
setze ergebnis auf (10 + 5) * 2
setze rest auf 17 % 5
setze potenz auf 2 ** 10
zeige ergebnis
zeige rest
zeige potenz

# --- Bedingungen ---
wenn alter >= 18:
    zeige "Erwachsen"
sonst wenn alter >= 13:
    zeige "Teenager"
sonst:
    zeige "Kind"

# --- Schleifen ---
setze i auf 0
solange i < 5:
    wenn i == 3:
        i += 1
        weiter
    zeige i
    i += 1

# --- Listen ---
setze farben auf ["rot", "grün", "blau"]
für farbe in farben:
    zeige farbe

zeige farben[0]
farben[1] = "gelb"
zeige farben

# --- Dictionaries ---
setze person auf {"name": "Anna", "alter": 30}
zeige person["name"]

# --- Funktionen ---
funktion begrüße(wer, gruß = "Hallo"):
    gib_zurück gruß + ", " + wer + "!"

zeige begrüße("Max")
zeige begrüße("Lisa", "Servus")

# --- Lambdas ---
setze verdopple auf (x) => x * 2
zeige verdopple(21)

# --- Klassen ---
klasse Tier:
    funktion erstelle(name, laut):
        selbst.name = name
        selbst.laut = laut

    funktion sprechen():
        gib_zurück selbst.name + " macht " + selbst.laut

setze hund auf neu Tier("Rex", "Wuff")
zeige hund.sprechen()
zeige hund.name

# --- Vererbung ---
klasse Hund(Tier):
    funktion erstelle(name):
        selbst.name = name
        selbst.laut = "Wuff"

    funktion apportieren():
        gib_zurück selbst.name + " apportiert!"

setze bello auf neu Hund("Bello")
zeige bello.sprechen()
zeige bello.apportieren()

# --- Fehlerbehandlung ---
versuche:
    setze x auf 10 / 0
fange fehler:
    zeige "Fehler aufgefangen!"

# --- Match/Switch ---
setze tag auf "Montag"
prüfe tag:
    fall "Montag":
        zeige "Wochenstart!"
    fall "Freitag":
        zeige "Fast Wochenende!"
    standard:
        zeige "Ein normaler Tag"

# --- Nichts (null/None) ---
setze leer auf nichts
wenn leer == nichts:
    zeige "Variable ist leer"
