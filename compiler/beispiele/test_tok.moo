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
        wenn c == " ":
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
        setze i auf i + 1

    tokens.hinzufügen(neu Token("EOF", ""))
    gib_zurück tokens

setze t auf tokenize("1 + 2")
zeige länge(t)
für tok in t:
    zeige tok.typ + " = " + text(tok.wert)
