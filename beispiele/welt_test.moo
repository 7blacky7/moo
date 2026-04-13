importiere welt

setze w auf welt_erstelle("moo Welt Test", 1024, 768)
welt_seed(w, 42)

solange welt_offen(w):
    welt_aktualisieren(w)

welt_beenden(w)
