klasse Knoten:
    funktion erstelle(typ):
        selbst.knoten_typ = typ
        selbst.wert = 0
        selbst.op = ""
        selbst.links = nichts
        selbst.rechts = nichts

    funktion auswerten():
        prüfe selbst.knoten_typ:
            fall "zahl":
                gib_zurück selbst.wert
            fall "binop":
                setze l auf selbst.links.auswerten()
                setze r auf selbst.rechts.auswerten()
                gib_zurück l + r
            standard:
                wirf "unbekannt"

setze a auf neu Knoten("zahl")
a.wert = 10
setze b auf neu Knoten("zahl")
b.wert = 20
setze c auf neu Knoten("binop")
c.links = a
c.rechts = b
c.op = "+"
zeige c.auswerten()
