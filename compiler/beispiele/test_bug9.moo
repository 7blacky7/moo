klasse Knoten:
    funktion erstelle(typ):
        selbst.knoten_typ = typ
        selbst.wert = 0
        selbst.name = ""
        selbst.op = ""
        selbst.links = nichts
        selbst.rechts = nichts
        selbst.kind = nichts

    funktion auswerten(umgebung):
        prüfe selbst.knoten_typ:
            fall "zahl":
                gib_zurück selbst.wert
            fall "var":
                gib_zurück umgebung[selbst.name]
            fall "binop":
                setze l auf selbst.links.auswerten(umgebung)
                setze r auf selbst.rechts.auswerten(umgebung)
                prüfe selbst.op:
                    fall "+":
                        gib_zurück l + r
                    standard:
                        wirf "unbekannt"
            standard:
                wirf "unbekannt"

setze k auf neu Knoten("zahl")
k.wert = 42
zeige k.auswerten({})
