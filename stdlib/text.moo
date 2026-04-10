# moo Standard-Bibliothek: Text-Hilfsfunktionen
# importiere text

# Wiederhole einen Text n-mal
funktion wiederhole(t, n):
    setze ergebnis auf ""
    für i in 0..n:
        setze ergebnis auf ergebnis + t
    gib_zurück ergebnis

# Kehre einen Text um
funktion umkehren(t):
    setze ergebnis auf ""
    setze i auf länge(t) - 1
    solange i >= 0:
        setze ergebnis auf ergebnis + t[i]
        setze i auf i - 1
    gib_zurück ergebnis

# Zaehle Vorkommen eines Zeichens
funktion zaehle(t, zeichen):
    setze n auf 0
    für i in 0..länge(t):
        wenn t[i] == zeichen:
            setze n auf n + 1
    gib_zurück n

# Beginnt mit?
funktion beginnt_mit(t, prefix):
    wenn länge(prefix) > länge(t):
        gib_zurück falsch
    für i in 0..länge(prefix):
        wenn t[i] != prefix[i]:
            gib_zurück falsch
    gib_zurück wahr

# Endet mit?
funktion endet_mit(t, suffix):
    setze tl auf länge(t)
    setze sl auf länge(suffix)
    wenn sl > tl:
        gib_zurück falsch
    setze start auf tl - sl
    für i in 0..sl:
        wenn t[start + i] != suffix[i]:
            gib_zurück falsch
    gib_zurück wahr

# Ist leer?
funktion ist_leer(t):
    gib_zurück länge(t) == 0

# Links auffuellen (Padding)
funktion links_auffuellen(t, laenge, zeichen):
    setze ergebnis auf t
    solange länge(ergebnis) < laenge:
        setze ergebnis auf zeichen + ergebnis
    gib_zurück ergebnis

# Rechts auffuellen
funktion rechts_auffuellen(t, laenge, zeichen):
    setze ergebnis auf t
    solange länge(ergebnis) < laenge:
        setze ergebnis auf ergebnis + zeichen
    gib_zurück ergebnis

# Grossbuchstabe am Anfang
funktion kapitalisiere(t):
    wenn länge(t) == 0:
        gib_zurück t
    gib_zurück t[0].upper() + t.slice(1, länge(t))
