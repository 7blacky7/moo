funktion ev():
    versuche:
        wirf "immer!"
        zeige "nie"
        gib_zurück 42
    fange fehler:
        zeige "Catch: " + fehler
        gib_zurück nichts

zeige ev()
