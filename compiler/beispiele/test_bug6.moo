funktion tokenize(t):
    setze tokens auf []
    setze i auf 0
    setze n auf länge(t)
    solange i < n:
        setze c auf t[i]
        wenn c == " ":
            setze i auf i + 1
            weiter
        wenn c == "+":
            tokens.hinzufügen("plus")
        sonst wenn c == "-":
            tokens.hinzufügen("minus")
        sonst wenn c == "*":
            tokens.hinzufügen("mal")
        sonst wenn c == "/":
            tokens.hinzufügen("div")
        sonst wenn c == "(":
            tokens.hinzufügen("lp")
        sonst wenn c == ")":
            tokens.hinzufügen("rp")
        sonst wenn c == "=":
            tokens.hinzufügen("eq")
        sonst:
            wirf "unbekannt: " + c
        setze i auf i + 1
    gib_zurück tokens

zeige tokenize("1+2")
