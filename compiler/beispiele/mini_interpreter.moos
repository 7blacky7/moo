# mini_interpreter — Arithmetischer Ausdrucks-Interpreter in moo
# Nutzt: Klassen, VERERBUNG, Methoden-Override, Match, Dicts, Listen, Rekursion, try/catch
#
# Grammatik:
#   statement  := ID "=" expression | expression
#   expression := term (("+"|"-") term)*
#   term       := factor (("*"|"/") factor)*
#   factor     := ZAHL | ID | "(" expression ")"

# ============================================================
# TOKEN
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
            tokens.hinzufügen(neu Token("ZAHL", zahl(eingabe_text[start..i])))
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
# AST-KNOTEN mit VERERBUNG
# ============================================================

klasse Knoten:
    funktion erstelle():
        selbst.typ = "Knoten"

    funktion auswerten(umgebung):
        wirf "auswerten() nicht implementiert in " + selbst.typ

klasse ZahlKnoten(Knoten):
    funktion erstelle(wert):
        selbst.typ = "Zahl"
        selbst.wert = wert

    funktion auswerten(umgebung):
        gib_zurück selbst.wert

klasse VariableKnoten(Knoten):
    funktion erstelle(name):
        selbst.typ = "Variable"
        selbst.name = name

    funktion auswerten(umgebung):
        wenn umgebung.hat(selbst.name) == falsch:
            wirf "Variable nicht definiert: " + selbst.name
        gib_zurück umgebung[selbst.name]

klasse BinOpKnoten(Knoten):
    funktion erstelle(links, op, rechts):
        selbst.typ = "BinOp"
        selbst.links = links
        selbst.op = op
        selbst.rechts = rechts

    funktion auswerten(umgebung):
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

klasse ZuweisungKnoten(Knoten):
    funktion erstelle(name, wert):
        selbst.typ = "Zuweisung"
        selbst.name = name
        selbst.wert = wert

    funktion auswerten(umgebung):
        setze w auf selbst.wert.auswerten(umgebung)
        umgebung[selbst.name] = w
        gib_zurück w

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
        gib_zurück neu ZahlKnoten(tok.wert)
    wenn tok.typ == "ID":
        parser_advance(p)
        gib_zurück neu VariableKnoten(tok.wert)
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
        setze links auf neu BinOpKnoten(links, op_tok.wert, rechts)
    gib_zurück links

funktion parse_expression(p):
    setze links auf parse_term(p)
    solange parser_peek(p).typ == "PLUS" oder parser_peek(p).typ == "MINUS":
        setze op_tok auf parser_advance(p)
        setze rechts auf parse_term(p)
        setze links auf neu BinOpKnoten(links, op_tok.wert, rechts)
    gib_zurück links

funktion parse_statement(p):
    wenn parser_peek(p).typ == "ID":
        setze erstes auf p["tokens"][p["pos"]]
        setze zweites auf p["tokens"][p["pos"] + 1]
        wenn zweites.typ == "GLEICH":
            parser_advance(p)
            parser_advance(p)
            setze wert auf parse_expression(p)
            gib_zurück neu ZuweisungKnoten(erstes.wert, wert)
    gib_zurück parse_expression(p)

funktion parse(tokens):
    setze p auf {"tokens": tokens, "pos": 0}
    gib_zurück parse_statement(p)

# ============================================================
# REPL-Helper
# ============================================================

funktion eval_zeile(mein_text, umgebung):
    versuche:
        setze tokens auf tokenize(mein_text)
        setze baum auf parse(tokens)
        gib_zurück baum.auswerten(umgebung)
    fange fehler:
        zeige "  Fehler: " + fehler
        gib_zurück nichts

# ============================================================
# TESTS
# ============================================================

zeige "=== mini_interpreter Tests ==="
setze umgebung auf {}

zeige "1 + 2 * 3 = " + text(eval_zeile("1 + 2 * 3", umgebung))
zeige "(1 + 2) * 3 = " + text(eval_zeile("(1 + 2) * 3", umgebung))
zeige "x = 5 -> " + text(eval_zeile("x = 5", umgebung))
zeige "x * 2 = " + text(eval_zeile("x * 2", umgebung))
zeige "(10 + 5) * 2 - 8 / 4 = " + text(eval_zeile("(10 + 5) * 2 - 8 / 4", umgebung))
zeige "y = 10 -> " + text(eval_zeile("y = 10", umgebung))
zeige "x + y * 2 = " + text(eval_zeile("x + y * 2", umgebung))

zeige ""
zeige "Test: undefinierte Variable 'z + 1':"
eval_zeile("z + 1", umgebung)

zeige ""
zeige "Test: Division durch Null '100 / 0':"
eval_zeile("100 / 0", umgebung)

zeige ""
zeige "Test: Fehler bei Syntax '1 + * 2':"
eval_zeile("1 + * 2", umgebung)

zeige ""
zeige "=== Tests fertig ==="
