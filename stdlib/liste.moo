# moo Standard-Bibliothek: Listen-Hilfsfunktionen
# importiere liste

# Summe aller Elemente
funktion summe(l):
    setze s auf 0
    für x in l:
        setze s auf s + x
    gib_zurück s

# Durchschnitt
funktion durchschnitt(l):
    gib_zurück summe(l) / länge(l)

# Minimum einer Liste
funktion minimum(l):
    setze m auf l[0]
    für x in l:
        wenn x < m:
            setze m auf x
    gib_zurück m

# Maximum einer Liste
funktion maximum(l):
    setze m auf l[0]
    für x in l:
        wenn x > m:
            setze m auf x
    gib_zurück m

# Flach machen (1 Ebene): [[1,2],[3,4]] → [1,2,3,4]
funktion flach_machen(l):
    setze ergebnis auf []
    für teil in l:
        für x in teil:
            ergebnis.hinzufügen(x)
    gib_zurück ergebnis

# Nur eindeutige Elemente (Reihenfolge beibehalten)
funktion eindeutig(l):
    setze ergebnis auf []
    setze gesehen auf []
    für x in l:
        wenn nicht gesehen.enthält(x):
            ergebnis.hinzufügen(x)
            gesehen.hinzufügen(x)
    gib_zurück ergebnis

# Zip: Zwei Listen zusammenfuehren
funktion zip(a, b):
    setze ergebnis auf []
    setze laenge auf min(länge(a), länge(b))
    für i in 0..laenge:
        ergebnis.hinzufügen([a[i], b[i]])
    gib_zurück ergebnis

# Bereich als Liste: bereich(1, 5) → [1, 2, 3, 4]
funktion bereich(start, ende):
    setze ergebnis auf []
    für i in start..ende:
        ergebnis.hinzufügen(i)
    gib_zurück ergebnis

# Alle Elemente erfuellen Bedingung? (mit Lambda)
funktion alle(l, bedingung):
    für x in l:
        wenn nicht bedingung(x):
            gib_zurück falsch
    gib_zurück wahr

# Mindestens ein Element erfuellt Bedingung?
funktion irgendein(l, bedingung):
    für x in l:
        wenn bedingung(x):
            gib_zurück wahr
    gib_zurück falsch

# Zaehle Elemente die Bedingung erfuellen
funktion zaehle_wenn(l, bedingung):
    setze n auf 0
    für x in l:
        wenn bedingung(x):
            setze n auf n + 1
    gib_zurück n

# Produkt aller Elemente
funktion produkt(l):
    setze p auf 1
    für x in l:
        setze p auf p * x
    gib_zurück p
