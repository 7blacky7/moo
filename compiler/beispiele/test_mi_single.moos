daten klasse Token(typ, wert)

funktion ist_ziffer(c):
    gib_zurück c >= "0" und c <= "9"

funktion ist_buchstabe(c):
    gib_zurück (c >= "a" und c <= "z") oder (c >= "A" und c <= "Z") oder c == "_"

funktion tokenize(eingabe_text):
    setze tokens auf []
    setze i auf 0
    setze n auf länge(eingabe_text)
    solange i < n:
        setze c auf eingabe_text[i]
        wenn c == " " oder c == "\t":
            setze i auf i + 1
            weiter
        wenn ist_ziffer(c):
            setze start auf i
            solange i < n und ist_ziffer(eingabe_text[i]):
                setze i auf i + 1
            tokens.hinzufügen(neu Token("ZAHL", zahl(eingabe_text[start..i])))
            weiter
        wenn c == "+":
            tokens.hinzufügen(neu Token("PLUS", "+"))
        sonst wenn c == "*":
            tokens.hinzufügen(neu Token("MAL", "*"))
        setze i auf i + 1
    tokens.hinzufügen(neu Token("EOF", ""))
    gib_zurück tokens

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
                prüfe selbst.op:
                    fall "+":
                        gib_zurück l + r
                    fall "*":
                        gib_zurück l * r
            standard:
                wirf "unbekannt"

setze k1 auf neu Knoten("zahl")
k1.wert = 2
setze k2 auf neu Knoten("zahl")
k2.wert = 3
setze k3 auf neu Knoten("binop")
k3.op = "*"
k3.links = k1
k3.rechts = k2

zeige "2 * 3 = " + text(k3.auswerten())
