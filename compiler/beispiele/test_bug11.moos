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
                wenn umgebung.hat(selbst.name) == falsch:
                    wirf "undef: " + selbst.name
                gib_zurück umgebung[selbst.name]
            fall "binop":
                setze l auf selbst.links.auswerten(umgebung)
                setze r auf selbst.rechts.auswerten(umgebung)
                prüfe selbst.op:
                    fall "+":
                        gib_zurück l + r
                    fall "-":
                        gib_zurück l - r
                    fall "*":
                        gib_zurück l * r
                    fall "/":
                        wenn r == 0:
                            wirf "div0"
                        gib_zurück l / r
                    standard:
                        wirf "unbekannt op"
            fall "zuweisung":
                setze w auf selbst.kind.auswerten(umgebung)
                umgebung[selbst.name] = w
                gib_zurück w
            standard:
                wirf "unbekannt typ"

funktion knoten_zahl(w):
    setze k auf neu Knoten("zahl")
    k.wert = w
    gib_zurück k

setze z auf knoten_zahl(42)
zeige z.auswerten({})
