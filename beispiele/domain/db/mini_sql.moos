# Mini-SQL-Engine in moo — Stresstest
# Features:
#   CREATE TABLE t (col1, col2, ...)
#   INSERT INTO t VALUES (v1, v2, ...)
#   SELECT * FROM t [WHERE col op val] [ORDER BY col [ASC|DESC]] [LIMIT n]
#   UPDATE t SET col = val [WHERE col op val]
#   DELETE FROM t [WHERE col op val]
#
# Architektur:
#   Lexer      — rohe SQL-Zeichen -> Tokens
#   Parser     — Tokens -> Statement-Dict (AST)
#   Engine     — Tables + execute(stmt) -> Resultat
#
# Nutzt neue Features: Klassen, Globale Vars in Funktionen,
# Multi-line Literale, try/catch mit return.


# ============================================================
# LEXER
# ============================================================

klasse Lexer:
    funktion erstelle(quelle):
        selbst.quelle = quelle
        selbst.pos = 0
        selbst.n = länge(quelle)

    funktion ist_space(c):
        gib_zurück c == " " oder c == "\t" oder c == "\n" oder c == "\r"

    funktion ist_digit(c):
        gib_zurück c >= "0" und c <= "9"

    funktion ist_alpha(c):
        wenn c >= "a" und c <= "z":
            gib_zurück wahr
        wenn c >= "A" und c <= "Z":
            gib_zurück wahr
        gib_zurück c == "_"

    funktion ist_alnum(c):
        wenn selbst.ist_alpha(c):
            gib_zurück wahr
        gib_zurück selbst.ist_digit(c)

    funktion peek():
        wenn selbst.pos >= selbst.n:
            gib_zurück ""
        gib_zurück selbst.quelle[selbst.pos]

    funktion peek_at(i):
        wenn i >= selbst.n:
            gib_zurück ""
        gib_zurück selbst.quelle[i]

    funktion ueberspringe_space():
        solange selbst.pos < selbst.n:
            setze c auf selbst.quelle[selbst.pos]
            wenn selbst.ist_space(c):
                selbst.pos = selbst.pos + 1
            sonst:
                gib_zurück nichts

    funktion lies_ident():
        setze start auf selbst.pos
        solange selbst.pos < selbst.n:
            setze c auf selbst.quelle[selbst.pos]
            wenn selbst.ist_alnum(c):
                selbst.pos = selbst.pos + 1
            sonst:
                gib_zurück selbst.quelle.slice(start, selbst.pos)
        gib_zurück selbst.quelle.slice(start, selbst.pos)

    funktion lies_zahl():
        setze start auf selbst.pos
        solange selbst.pos < selbst.n:
            setze c auf selbst.quelle[selbst.pos]
            wenn selbst.ist_digit(c) oder c == ".":
                selbst.pos = selbst.pos + 1
            sonst:
                gib_zurück selbst.quelle.slice(start, selbst.pos)
        gib_zurück selbst.quelle.slice(start, selbst.pos)

    funktion lies_string():
        selbst.pos = selbst.pos + 1
        setze start auf selbst.pos
        solange selbst.pos < selbst.n:
            wenn selbst.quelle[selbst.pos] == "'":
                setze wert auf selbst.quelle.slice(start, selbst.pos)
                selbst.pos = selbst.pos + 1
                gib_zurück wert
            selbst.pos = selbst.pos + 1
        gib_zurück selbst.quelle.slice(start, selbst.pos)

    funktion tokenisiere():
        setze tokens auf []
        solange selbst.pos < selbst.n:
            selbst.ueberspringe_space()
            wenn selbst.pos >= selbst.n:
                gib_zurück tokens
            setze c auf selbst.quelle[selbst.pos]

            wenn selbst.ist_alpha(c):
                setze w auf selbst.lies_ident()
                setze up auf w.gross()
                setze schluessel auf ["SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "CREATE", "TABLE", "UPDATE", "SET", "DELETE", "ORDER", "BY", "LIMIT", "ASC", "DESC", "AND"]
                setze ist_kw auf falsch
                für k in schluessel:
                    wenn k == up:
                        setze ist_kw auf wahr
                wenn ist_kw:
                    tokens.hinzufügen({"typ": "kw", "wert": up})
                sonst:
                    tokens.hinzufügen({"typ": "ident", "wert": w})
            sonst wenn selbst.ist_digit(c):
                setze z auf selbst.lies_zahl()
                tokens.hinzufügen({"typ": "num", "wert": zahl(z)})
            sonst wenn c == "'":
                setze s auf selbst.lies_string()
                tokens.hinzufügen({"typ": "str", "wert": s})
            sonst wenn c == "*":
                tokens.hinzufügen({"typ": "star", "wert": "*"})
                selbst.pos = selbst.pos + 1
            sonst wenn c == ",":
                tokens.hinzufügen({"typ": "komma", "wert": ","})
                selbst.pos = selbst.pos + 1
            sonst wenn c == "(":
                tokens.hinzufügen({"typ": "lparen", "wert": "("})
                selbst.pos = selbst.pos + 1
            sonst wenn c == ")":
                tokens.hinzufügen({"typ": "rparen", "wert": ")"})
                selbst.pos = selbst.pos + 1
            sonst wenn c == ";":
                selbst.pos = selbst.pos + 1
            sonst wenn c == "=":
                tokens.hinzufügen({"typ": "op", "wert": "="})
                selbst.pos = selbst.pos + 1
            sonst wenn c == "<":
                wenn selbst.peek_at(selbst.pos + 1) == "=":
                    tokens.hinzufügen({"typ": "op", "wert": "<="})
                    selbst.pos = selbst.pos + 2
                sonst wenn selbst.peek_at(selbst.pos + 1) == ">":
                    tokens.hinzufügen({"typ": "op", "wert": "!="})
                    selbst.pos = selbst.pos + 2
                sonst:
                    tokens.hinzufügen({"typ": "op", "wert": "<"})
                    selbst.pos = selbst.pos + 1
            sonst wenn c == ">":
                wenn selbst.peek_at(selbst.pos + 1) == "=":
                    tokens.hinzufügen({"typ": "op", "wert": ">="})
                    selbst.pos = selbst.pos + 2
                sonst:
                    tokens.hinzufügen({"typ": "op", "wert": ">"})
                    selbst.pos = selbst.pos + 1
            sonst wenn c == "!":
                wenn selbst.peek_at(selbst.pos + 1) == "=":
                    tokens.hinzufügen({"typ": "op", "wert": "!="})
                    selbst.pos = selbst.pos + 2
                sonst:
                    selbst.pos = selbst.pos + 1
            sonst:
                selbst.pos = selbst.pos + 1
        gib_zurück tokens


# ============================================================
# PARSER
# ============================================================

klasse Parser:
    funktion erstelle(tokens):
        selbst.tokens = tokens
        selbst.i = 0

    funktion peek():
        wenn selbst.i >= länge(selbst.tokens):
            gib_zurück {"typ": "eof", "wert": ""}
        gib_zurück selbst.tokens[selbst.i]

    funktion consume():
        setze t auf selbst.peek()
        selbst.i = selbst.i + 1
        gib_zurück t

    funktion erwarte_kw(k):
        setze t auf selbst.consume()
        wenn t["typ"] != "kw" oder t["wert"] != k:
            wirf "Erwartet Keyword " + k + ", bekam " + t["wert"]
        gib_zurück t

    funktion erwarte_typ(typ):
        setze t auf selbst.consume()
        wenn t["typ"] != typ:
            wirf "Erwartet " + typ + ", bekam " + t["typ"] + " " + t["wert"]
        gib_zurück t

    funktion parse():
        setze t auf selbst.peek()
        wenn t["typ"] != "kw":
            wirf "Erwartet SQL-Keyword"
        wenn t["wert"] == "CREATE":
            gib_zurück selbst.parse_create()
        wenn t["wert"] == "INSERT":
            gib_zurück selbst.parse_insert()
        wenn t["wert"] == "SELECT":
            gib_zurück selbst.parse_select()
        wenn t["wert"] == "UPDATE":
            gib_zurück selbst.parse_update()
        wenn t["wert"] == "DELETE":
            gib_zurück selbst.parse_delete()
        wirf "Unbekanntes Statement: " + t["wert"]

    funktion parse_create():
        selbst.erwarte_kw("CREATE")
        selbst.erwarte_kw("TABLE")
        setze name auf selbst.erwarte_typ("ident")["wert"]
        selbst.erwarte_typ("lparen")
        setze spalten auf []
        solange wahr:
            setze c auf selbst.erwarte_typ("ident")["wert"]
            spalten.hinzufügen(c)
            setze nx auf selbst.peek()
            wenn nx["typ"] == "komma":
                selbst.consume()
            sonst:
                stopp
        selbst.erwarte_typ("rparen")
        gib_zurück {"op": "create", "tabelle": name, "spalten": spalten}

    funktion parse_insert():
        selbst.erwarte_kw("INSERT")
        selbst.erwarte_kw("INTO")
        setze name auf selbst.erwarte_typ("ident")["wert"]
        selbst.erwarte_kw("VALUES")
        selbst.erwarte_typ("lparen")
        setze werte auf []
        solange wahr:
            setze t auf selbst.consume()
            wenn t["typ"] == "num" oder t["typ"] == "str":
                werte.hinzufügen(t["wert"])
            sonst wenn t["typ"] == "ident":
                werte.hinzufügen(t["wert"])
            sonst:
                wirf "Wert erwartet in VALUES"
            setze nx auf selbst.peek()
            wenn nx["typ"] == "komma":
                selbst.consume()
            sonst:
                stopp
        selbst.erwarte_typ("rparen")
        gib_zurück {"op": "insert", "tabelle": name, "werte": werte}

    funktion parse_where():
        setze t auf selbst.peek()
        wenn t["typ"] == "kw" und t["wert"] == "WHERE":
            selbst.consume()
            setze col auf selbst.erwarte_typ("ident")["wert"]
            setze op auf selbst.erwarte_typ("op")["wert"]
            setze v auf selbst.consume()
            gib_zurück {"spalte": col, "op": op, "wert": v["wert"]}
        gib_zurück nichts

    funktion parse_select():
        selbst.erwarte_kw("SELECT")
        setze spalten auf []
        setze t auf selbst.peek()
        wenn t["typ"] == "star":
            selbst.consume()
            spalten.hinzufügen("*")
        sonst:
            solange wahr:
                setze c auf selbst.erwarte_typ("ident")["wert"]
                spalten.hinzufügen(c)
                setze nx auf selbst.peek()
                wenn nx["typ"] == "komma":
                    selbst.consume()
                sonst:
                    stopp
        selbst.erwarte_kw("FROM")
        setze tab auf selbst.erwarte_typ("ident")["wert"]
        setze bedingung auf selbst.parse_where()
        setze sortierung auf nichts
        setze limit auf -1
        setze nx auf selbst.peek()
        wenn nx["typ"] == "kw" und nx["wert"] == "ORDER":
            selbst.consume()
            selbst.erwarte_kw("BY")
            setze ocol auf selbst.erwarte_typ("ident")["wert"]
            setze richtung auf "ASC"
            setze nx2 auf selbst.peek()
            wenn nx2["typ"] == "kw":
                wenn nx2["wert"] == "ASC" oder nx2["wert"] == "DESC":
                    setze richtung auf nx2["wert"]
                    selbst.consume()
            setze sortierung auf {"spalte": ocol, "richtung": richtung}
        setze nx auf selbst.peek()
        wenn nx["typ"] == "kw" und nx["wert"] == "LIMIT":
            selbst.consume()
            setze l auf selbst.erwarte_typ("num")["wert"]
            setze limit auf l
        gib_zurück {"op": "select", "spalten": spalten, "tabelle": tab, "where": bedingung, "order": sortierung, "limit": limit}

    funktion parse_update():
        selbst.erwarte_kw("UPDATE")
        setze tab auf selbst.erwarte_typ("ident")["wert"]
        selbst.erwarte_kw("SET")
        setze col auf selbst.erwarte_typ("ident")["wert"]
        selbst.erwarte_typ("op")
        setze v auf selbst.consume()
        setze bedingung auf selbst.parse_where()
        gib_zurück {"op": "update", "tabelle": tab, "spalte": col, "wert": v["wert"], "where": bedingung}

    funktion parse_delete():
        selbst.erwarte_kw("DELETE")
        selbst.erwarte_kw("FROM")
        setze tab auf selbst.erwarte_typ("ident")["wert"]
        setze bedingung auf selbst.parse_where()
        gib_zurück {"op": "delete", "tabelle": tab, "where": bedingung}


# ============================================================
# ENGINE
# ============================================================

klasse Engine:
    funktion erstelle():
        selbst.tabellen = {}

    funktion execute(sql):
        setze lex auf neu Lexer(sql)
        setze toks auf lex.tokenisiere()
        setze p auf neu Parser(toks)
        setze stmt auf p.parse()
        gib_zurück selbst.run(stmt)

    funktion run(stmt):
        setze op auf stmt["op"]
        wenn op == "create":
            gib_zurück selbst.do_create(stmt)
        wenn op == "insert":
            gib_zurück selbst.do_insert(stmt)
        wenn op == "select":
            gib_zurück selbst.do_select(stmt)
        wenn op == "update":
            gib_zurück selbst.do_update(stmt)
        wenn op == "delete":
            gib_zurück selbst.do_delete(stmt)
        wirf "Unbekannte Operation"

    funktion do_create(stmt):
        selbst.tabellen[stmt["tabelle"]] = {"spalten": stmt["spalten"], "zeilen": []}
        gib_zurück {"status": "ok", "typ": "create"}

    funktion do_insert(stmt):
        setze tab auf selbst.tabellen[stmt["tabelle"]]
        setze zeile auf {}
        setze spalten auf tab["spalten"]
        setze idx auf 0
        solange idx < länge(spalten):
            zeile[spalten[idx]] = stmt["werte"][idx]
            setze idx auf idx + 1
        tab["zeilen"].hinzufügen(zeile)
        gib_zurück {"status": "ok", "typ": "insert"}

    funktion passt_where(zeile, wh):
        wenn wh == nichts:
            gib_zurück wahr
        setze cv auf zeile[wh["spalte"]]
        setze v auf wh["wert"]
        setze op auf wh["op"]
        wenn op == "=":
            gib_zurück cv == v
        wenn op == "!=":
            gib_zurück cv != v
        wenn op == "<":
            gib_zurück cv < v
        wenn op == ">":
            gib_zurück cv > v
        wenn op == "<=":
            gib_zurück cv <= v
        wenn op == ">=":
            gib_zurück cv >= v
        gib_zurück falsch

    funktion sortiere(zeilen, sortierung):
        # einfacher Insertion-Sort (O(n^2) ist fuer Tests ok)
        setze n auf länge(zeilen)
        setze spalte auf sortierung["spalte"]
        setze absteigend auf sortierung["richtung"] == "DESC"
        setze idx auf 1
        solange idx < n:
            setze jdx auf idx
            solange jdx > 0:
                setze a auf zeilen[jdx - 1][spalte]
                setze b auf zeilen[jdx][spalte]
                setze tausche auf falsch
                wenn absteigend:
                    wenn a < b:
                        setze tausche auf wahr
                sonst:
                    wenn a > b:
                        setze tausche auf wahr
                wenn tausche:
                    setze tmp auf zeilen[jdx - 1]
                    zeilen[jdx - 1] = zeilen[jdx]
                    zeilen[jdx] = tmp
                    setze jdx auf jdx - 1
                sonst:
                    stopp
            setze idx auf idx + 1
        gib_zurück zeilen

    funktion do_select(stmt):
        setze tab auf selbst.tabellen[stmt["tabelle"]]
        setze ergebnis auf []
        für zeile in tab["zeilen"]:
            wenn selbst.passt_where(zeile, stmt["where"]):
                ergebnis.hinzufügen(zeile)
        wenn stmt["order"] != nichts:
            setze ergebnis auf selbst.sortiere(ergebnis, stmt["order"])
        wenn stmt["limit"] >= 0:
            setze begrenzt auf []
            setze idx auf 0
            solange idx < länge(ergebnis) und idx < stmt["limit"]:
                begrenzt.hinzufügen(ergebnis[idx])
                setze idx auf idx + 1
            setze ergebnis auf begrenzt
        # Projektion
        wenn länge(stmt["spalten"]) == 1 und stmt["spalten"][0] == "*":
            gib_zurück ergebnis
        setze proj auf []
        für zeile in ergebnis:
            setze neu_zeile auf {}
            für sp in stmt["spalten"]:
                neu_zeile[sp] = zeile[sp]
            proj.hinzufügen(neu_zeile)
        gib_zurück proj

    funktion do_update(stmt):
        setze tab auf selbst.tabellen[stmt["tabelle"]]
        setze anzahl auf 0
        für zeile in tab["zeilen"]:
            wenn selbst.passt_where(zeile, stmt["where"]):
                zeile[stmt["spalte"]] = stmt["wert"]
                setze anzahl auf anzahl + 1
        gib_zurück {"status": "ok", "typ": "update", "anzahl": anzahl}

    funktion do_delete(stmt):
        setze tab auf selbst.tabellen[stmt["tabelle"]]
        setze behalten auf []
        setze geloescht auf 0
        für zeile in tab["zeilen"]:
            wenn selbst.passt_where(zeile, stmt["where"]):
                setze geloescht auf geloescht + 1
            sonst:
                behalten.hinzufügen(zeile)
        tab["zeilen"] = behalten
        gib_zurück {"status": "ok", "typ": "delete", "anzahl": geloescht}


# ============================================================
# TESTS
# ============================================================

setze TESTS_OK auf 0
setze TESTS_FAIL auf 0

funktion check(name, bedingung):
    wenn bedingung:
        zeige "  OK  " + name
        setze TESTS_OK auf TESTS_OK + 1
    sonst:
        zeige "  FAIL " + name
        setze TESTS_FAIL auf TESTS_FAIL + 1


setze db auf neu Engine()

zeige "=== Mini-SQL-Engine Tests ==="
zeige ""

# 1. CREATE
zeige "Test 1: CREATE TABLE"
db.execute("CREATE TABLE users (id, name, alter)")
check("Tabelle users existiert", db.tabellen.hat("users"))
check("Spalten korrekt", länge(db.tabellen["users"]["spalten"]) == 3)

# 2. INSERT einzeln
zeige ""
zeige "Test 2: INSERT einzeln"
db.execute("INSERT INTO users VALUES (1, 'Anna', 30)")
db.execute("INSERT INTO users VALUES (2, 'Bob', 25)")
db.execute("INSERT INTO users VALUES (3, 'Eve', 45)")
check("3 Zeilen eingefuegt", länge(db.tabellen["users"]["zeilen"]) == 3)
check("Anna hat age 30", db.tabellen["users"]["zeilen"][0]["alter"] == 30)

# 3. SELECT * FROM users
zeige ""
zeige "Test 3: SELECT *"
setze r auf db.execute("SELECT * FROM users")
check("3 Zeilen zurueck", länge(r) == 3)

# 4. SELECT WHERE
zeige ""
zeige "Test 4: SELECT mit WHERE"
setze r auf db.execute("SELECT * FROM users WHERE alter > 28")
check("2 Treffer (alter > 28)", länge(r) == 2)

setze r auf db.execute("SELECT * FROM users WHERE name = 'Bob'")
check("1 Treffer (name = Bob)", länge(r) == 1)
check("Bob gefunden", r[0]["name"] == "Bob")

# 5. SELECT mit Projektion
zeige ""
zeige "Test 5: SELECT Projektion"
setze r auf db.execute("SELECT name, alter FROM users")
check("Nur 2 Spalten", länge(r[0].schlüssel()) == 2)
check("Nur name+alter", r[0].hat("name") und r[0].hat("alter"))
check("Kein id", r[0].hat("id") == falsch)

# 6. SELECT ORDER BY ASC
zeige ""
zeige "Test 6: ORDER BY ASC"
setze r auf db.execute("SELECT * FROM users ORDER BY alter ASC")
check("Bob zuerst (25)", r[0]["name"] == "Bob")
check("Eve letzte (45)", r[2]["name"] == "Eve")

# 7. SELECT ORDER BY DESC + LIMIT
zeige ""
zeige "Test 7: ORDER BY DESC + LIMIT"
setze r auf db.execute("SELECT * FROM users ORDER BY alter DESC LIMIT 2")
check("2 Zeilen", länge(r) == 2)
check("Eve zuerst", r[0]["name"] == "Eve")
check("Anna zweite", r[1]["name"] == "Anna")

# 8. UPDATE
zeige ""
zeige "Test 8: UPDATE"
setze r auf db.execute("UPDATE users SET alter = 26 WHERE name = 'Bob'")
check("1 Update", r["anzahl"] == 1)
setze r2 auf db.execute("SELECT * FROM users WHERE name = 'Bob'")
check("Bob ist jetzt 26", r2[0]["alter"] == 26)

# 9. DELETE
zeige ""
zeige "Test 9: DELETE"
setze r auf db.execute("DELETE FROM users WHERE name = 'Eve'")
check("1 Delete", r["anzahl"] == 1)
check("Nur 2 Zeilen uebrig", länge(db.tabellen["users"]["zeilen"]) == 2)

# 10. Stresstest: 100 Zeilen INSERT + SELECT + ORDER + LIMIT
zeige ""
zeige "Test 10: Stresstest 100 Zeilen"
db.execute("CREATE TABLE produkte (id, name, preis)")
setze i auf 0
solange i < 100:
    setze name auf "Artikel" + text(i)
    setze preis auf (100 - i) * 1
    setze sql auf "INSERT INTO produkte VALUES (" + text(i) + ", '" + name + "', " + text(preis) + ")"
    db.execute(sql)
    setze i auf i + 1
check("100 Zeilen eingefuegt", länge(db.tabellen["produkte"]["zeilen"]) == 100)

setze r auf db.execute("SELECT * FROM produkte WHERE preis >= 50")
check("51 Zeilen mit preis >= 50", länge(r) == 51)

setze r auf db.execute("SELECT name, preis FROM produkte ORDER BY preis ASC LIMIT 5")
check("5 billigste", länge(r) == 5)
check("billigster ist preis 1", r[0]["preis"] == 1)
check("fuenfter ist preis 5", r[4]["preis"] == 5)

setze r auf db.execute("SELECT * FROM produkte ORDER BY preis DESC LIMIT 3")
check("3 teuerste", länge(r) == 3)
check("teuerster ist 100", r[0]["preis"] == 100)

# 11. DELETE mit Bereich
zeige ""
zeige "Test 11: DELETE mit Bereich"
setze r auf db.execute("DELETE FROM produkte WHERE preis < 20")
check("19 geloescht", r["anzahl"] == 19)
check("81 uebrig", länge(db.tabellen["produkte"]["zeilen"]) == 81)

# 12. UPDATE mit Bereich
zeige ""
zeige "Test 12: UPDATE mit Bereich"
setze r auf db.execute("UPDATE produkte SET preis = 999 WHERE preis >= 90")
check("11 geupdated", r["anzahl"] == 11)
setze r2 auf db.execute("SELECT * FROM produkte WHERE preis = 999")
check("11 mit preis 999", länge(r2) == 11)

zeige ""
zeige "=========================================="
zeige "Ergebnis: " + text(TESTS_OK) + " OK, " + text(TESTS_FAIL) + " FAIL"
zeige "=========================================="
