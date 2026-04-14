# mini_interpreter.moo — Arithmetischer Ausdrucks-Interpreter in moo
# Stresstest fuer OOP, Klassen, Methoden, Listen, Dicts, Rekursion
#
# Grammatik:
#   statement := ID "=" expression | expression
#   expression := term (("+"|"-") term)*
#   term := factor (("*"|"/") factor)*
#   factor := ZAHL | ID | "(" expression ")"

# ============================================================
# TOKEN-KLASSE
# ============================================================

daten klasse Token(typ, wert)

# ============================================================
# LEXER
# ============================================================

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
            setze num_str auf eingabe_text[start..i]
            tokens.hinzufügen(neu Token("ZAHL", zahl(num_str)))
            weiter

        wenn ist_buchstabe(c):
            setze start auf i
            solange i < n und (ist_buchstabe(eingabe_text[i]) oder ist_ziffer(eingabe_text[i])):
                setze i auf i + 1
            tokens.hinzufügen(neu Token("ID", eingabe_text[start..i]))
            weiter

        wenn c == "+":
            tokens.hinzufügen(neu Token("PLUS", "+"))
        sonst wenn c == "-":
            tokens.hinzufügen(neu Token("MINUS", "-"))
        sonst wenn c == "*":
            tokens.hinzufügen(neu Token("MAL", "*"))
        sonst wenn c == "/":
            tokens.hinzufügen(neu Token("DIV", "/"))
        sonst wenn c == "(":
            tokens.hinzufügen(neu Token("LKLAMMER", "("))
        sonst wenn c == ")":
            tokens.hinzufügen(neu Token("RKLAMMER", ")"))
        sonst wenn c == "=":
            tokens.hinzufügen(neu Token("GLEICH", "="))
        sonst:
            wirf "Unbekanntes Zeichen: " + c

        setze i auf i + 1

    tokens.hinzufügen(neu Token("EOF", ""))
    gib_zurück tokens

# ============================================================
# AST-KNOTEN (alles in einer Klasse, wegen Vererbungs-Bug)
# typ: "zahl" | "binop" | "var" | "zuweisung"
# ============================================================

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
                    wirf "Variable nicht definiert: " + selbst.name
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
                            wirf "Division durch Null"
                        gib_zurück l / r
                    standard:
                        wirf "Unbekannter Operator: " + selbst.op
            fall "zuweisung":
                setze w auf selbst.kind.auswerten(umgebung)
                umgebung[selbst.name] = w
                gib_zurück w
            standard:
                wirf "Unbekannter Knoten-Typ: " + selbst.knoten_typ

# Factory-Funktionen
funktion knoten_zahl(w):
    setze k auf neu Knoten("zahl")
    k.wert = w
    gib_zurück k

funktion knoten_var(name):
    setze k auf neu Knoten("var")
    k.name = name
    gib_zurück k

funktion knoten_binop(links, op, rechts):
    setze k auf neu Knoten("binop")
    k.links = links
    k.op = op
    k.rechts = rechts
    gib_zurück k

funktion knoten_zuweisung(name, wert):
    setze k auf neu Knoten("zuweisung")
    k.name = name
    k.kind = wert
    gib_zurück k

# ============================================================
# PARSER (Recursive Descent)
# ============================================================

funktion parser_peek(p):
    gib_zurück p["tokens"][p["pos"]]

funktion parser_advance(p):
    setze tok auf p["tokens"][p["pos"]]
    p["pos"] = p["pos"] + 1
    gib_zurück tok

funktion parse_factor(p):
    setze tok auf parser_peek(p)

    wenn tok.typ == "ZAHL":
        parser_advance(p)
        gib_zurück knoten_zahl(tok.wert)

    wenn tok.typ == "ID":
        parser_advance(p)
        gib_zurück knoten_var(tok.wert)

    wenn tok.typ == "LKLAMMER":
        parser_advance(p)
        setze ausdruck auf parse_expression(p)
        setze naechstes auf parser_advance(p)
        wenn naechstes.typ != "RKLAMMER":
            wirf "Erwartet ')', bekommen " + naechstes.typ
        gib_zurück ausdruck

    wirf "Unerwartetes Token: " + tok.typ

funktion parse_term(p):
    setze links auf parse_factor(p)
    solange parser_peek(p).typ == "MAL" oder parser_peek(p).typ == "DIV":
        setze op_tok auf parser_advance(p)
        setze rechts auf parse_factor(p)
        setze links auf knoten_binop(links, op_tok.wert, rechts)
    gib_zurück links

funktion parse_expression(p):
    setze links auf parse_term(p)
    solange parser_peek(p).typ == "PLUS" oder parser_peek(p).typ == "MINUS":
        setze op_tok auf parser_advance(p)
        setze rechts auf parse_term(p)
        setze links auf knoten_binop(links, op_tok.wert, rechts)
    gib_zurück links

funktion parse_statement(p):
    wenn parser_peek(p).typ == "ID":
        setze erstes auf p["tokens"][p["pos"]]
        setze zweites auf p["tokens"][p["pos"] + 1]
        wenn zweites.typ == "GLEICH":
            parser_advance(p)
            parser_advance(p)
            setze wert auf parse_expression(p)
            gib_zurück knoten_zuweisung(erstes.wert, wert)
    gib_zurück parse_expression(p)

funktion parse(tokens):
    setze p auf {"tokens": tokens, "pos": 0}
    gib_zurück parse_statement(p)

# ============================================================
# EVAL + TESTS
# ============================================================

funktion eval_zeile(mein_text, umgebung):
    versuche:
        setze tokens auf tokenize(mein_text)
        setze baum auf parse(tokens)
        gib_zurück baum.auswerten(umgebung)
    fange fehler:
        zeige "Fehler: " + fehler
