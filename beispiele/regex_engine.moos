# ============================================================
# moo Regex-Engine — eigene Implementierung von Grund auf
#
# Unterstuetzte Syntax:
#   literal       a, b, 5, ...
#   .             ein beliebiges Zeichen (ausser Zeilenende)
#   *             null oder mehr
#   +             ein oder mehr
#   ?             null oder eins
#   [abc]         Zeichenklasse
#   [a-z]         Bereich
#   [^abc]        negierte Klasse
#   ^             Anfang
#   $             Ende
#   (pattern)     Gruppe (nicht-erfassend)
#   |             Alternative
#   \.  \*  ...   Escape
#
# Architektur:
#   - AST-Knoten (Vererbung: Knoten -> Literal, Punkt, Klasse, Stern, ...)
#   - Parser (Pratt-aehnlich): Alt > Concat > Quant > Atom
#   - Matcher: jeder Knoten hat passe(text, pos) -> Liste moeglicher End-Positionen
#     (NFA-Simulation per Backtracking ueber alle moeglichen Zweige)
#   - API: match_full, find, find_all, ersetze
# ============================================================

# ------------------------------------------------------------
# AST-Knoten
# ------------------------------------------------------------

klasse Knoten:
    funktion erstelle():
        selbst.typ = "basis"

    funktion passe(text, pos):
        gib_zurück []

klasse Literal(Knoten):
    funktion erstelle(c):
        selbst.typ = "literal"
        selbst.c = c

    funktion passe(text, pos):
        wenn pos < länge(text) und text[pos] == selbst.c:
            gib_zurück [pos + 1]
        gib_zurück []

klasse Punkt(Knoten):
    funktion erstelle():
        selbst.typ = "punkt"

    funktion passe(text, pos):
        wenn pos < länge(text) und text[pos] != "\n":
            gib_zurück [pos + 1]
        gib_zurück []

klasse Klasse(Knoten):
    funktion erstelle(einzeln, ranges, negiert):
        selbst.typ = "klasse"
        selbst.einzeln = einzeln
        selbst.ranges = ranges
        selbst.negiert = negiert

    funktion passe(text, pos):
        wenn pos >= länge(text):
            gib_zurück []
        setze c auf text[pos]
        setze drin auf falsch
        setze i auf 0
        solange i < länge(selbst.einzeln):
            wenn selbst.einzeln[i] == c:
                setze drin auf wahr
            setze i auf i + 1
        setze i auf 0
        solange i < länge(selbst.ranges):
            setze r auf selbst.ranges[i]
            wenn c >= r[0] und c <= r[1]:
                setze drin auf wahr
            setze i auf i + 1
        wenn selbst.negiert:
            wenn drin == falsch:
                gib_zurück [pos + 1]
            gib_zurück []
        wenn drin:
            gib_zurück [pos + 1]
        gib_zurück []

klasse Anker(Knoten):
    funktion erstelle(welcher):
        selbst.typ = "anker"
        selbst.welcher = welcher

    funktion passe(text, pos):
        wenn selbst.welcher == "start":
            wenn pos == 0:
                gib_zurück [pos]
            gib_zurück []
        wenn selbst.welcher == "ende":
            wenn pos == länge(text):
                gib_zurück [pos]
            gib_zurück []
        gib_zurück []

klasse Stern(Knoten):
    funktion erstelle(kind):
        selbst.typ = "stern"
        selbst.kind = kind

    funktion passe(text, pos):
        # Sammle alle erreichbaren Positionen (0+ Wiederholungen)
        setze ergebnisse auf [pos]
        setze zu_pruefen auf [pos]
        solange länge(zu_pruefen) > 0:
            setze p auf zu_pruefen[0]
            setze rest auf []
            setze i auf 1
            solange i < länge(zu_pruefen):
                rest.hinzufügen(zu_pruefen[i])
                setze i auf i + 1
            setze zu_pruefen auf rest
            setze naechste auf selbst.kind.passe(text, p)
            setze j auf 0
            solange j < länge(naechste):
                setze np auf naechste[j]
                # nur hinzu wenn Fortschritt (vermeidet Endlosschleife bei leerem Match)
                wenn np > p:
                    setze bekannt auf falsch
                    setze k auf 0
                    solange k < länge(ergebnisse):
                        wenn ergebnisse[k] == np:
                            setze bekannt auf wahr
                        setze k auf k + 1
                    wenn bekannt == falsch:
                        ergebnisse.hinzufügen(np)
                        zu_pruefen.hinzufügen(np)
                setze j auf j + 1
        gib_zurück ergebnisse

klasse Plus(Knoten):
    funktion erstelle(kind):
        selbst.typ = "plus"
        selbst.kind = kind

    funktion passe(text, pos):
        # Mindestens einmal matchen, dann Star-Semantik
        setze erste auf selbst.kind.passe(text, pos)
        setze ergebnisse auf []
        setze i auf 0
        solange i < länge(erste):
            ergebnisse.hinzufügen(erste[i])
            setze i auf i + 1
        setze zu_pruefen auf []
        setze i auf 0
        solange i < länge(erste):
            zu_pruefen.hinzufügen(erste[i])
            setze i auf i + 1
        solange länge(zu_pruefen) > 0:
            setze p auf zu_pruefen[0]
            setze rest auf []
            setze i auf 1
            solange i < länge(zu_pruefen):
                rest.hinzufügen(zu_pruefen[i])
                setze i auf i + 1
            setze zu_pruefen auf rest
            setze naechste auf selbst.kind.passe(text, p)
            setze j auf 0
            solange j < länge(naechste):
                setze np auf naechste[j]
                wenn np > p:
                    setze bekannt auf falsch
                    setze k auf 0
                    solange k < länge(ergebnisse):
                        wenn ergebnisse[k] == np:
                            setze bekannt auf wahr
                        setze k auf k + 1
                    wenn bekannt == falsch:
                        ergebnisse.hinzufügen(np)
                        zu_pruefen.hinzufügen(np)
                setze j auf j + 1
        gib_zurück ergebnisse

klasse Frage(Knoten):
    funktion erstelle(kind):
        selbst.typ = "frage"
        selbst.kind = kind

    funktion passe(text, pos):
        setze ergebnisse auf [pos]
        setze k_erg auf selbst.kind.passe(text, pos)
        setze i auf 0
        solange i < länge(k_erg):
            setze np auf k_erg[i]
            setze bekannt auf falsch
            setze j auf 0
            solange j < länge(ergebnisse):
                wenn ergebnisse[j] == np:
                    setze bekannt auf wahr
                setze j auf j + 1
            wenn bekannt == falsch:
                ergebnisse.hinzufügen(np)
            setze i auf i + 1
        gib_zurück ergebnisse

klasse Konkat(Knoten):
    funktion erstelle(kinder):
        selbst.typ = "konkat"
        selbst.kinder = kinder

    funktion passe(text, pos):
        setze aktuelle auf [pos]
        setze i auf 0
        solange i < länge(selbst.kinder):
            setze naechste auf []
            setze j auf 0
            solange j < länge(aktuelle):
                setze p auf aktuelle[j]
                setze erg auf selbst.kinder[i].passe(text, p)
                setze k auf 0
                solange k < länge(erg):
                    setze np auf erg[k]
                    setze bekannt auf falsch
                    setze m auf 0
                    solange m < länge(naechste):
                        wenn naechste[m] == np:
                            setze bekannt auf wahr
                        setze m auf m + 1
                    wenn bekannt == falsch:
                        naechste.hinzufügen(np)
                    setze k auf k + 1
                setze j auf j + 1
            setze aktuelle auf naechste
            wenn länge(aktuelle) == 0:
                gib_zurück []
            setze i auf i + 1
        gib_zurück aktuelle

klasse Alt(Knoten):
    funktion erstelle(links, rechts):
        selbst.typ = "alt"
        selbst.links = links
        selbst.rechts = rechts

    funktion passe(text, pos):
        setze ergebnisse auf selbst.links.passe(text, pos)
        setze r auf selbst.rechts.passe(text, pos)
        setze i auf 0
        solange i < länge(r):
            setze np auf r[i]
            setze bekannt auf falsch
            setze j auf 0
            solange j < länge(ergebnisse):
                wenn ergebnisse[j] == np:
                    setze bekannt auf wahr
                setze j auf j + 1
            wenn bekannt == falsch:
                ergebnisse.hinzufügen(np)
            setze i auf i + 1
        gib_zurück ergebnisse

# ------------------------------------------------------------
# Parser: Regex-String -> AST
# ------------------------------------------------------------

klasse Parser:
    funktion erstelle(muster):
        selbst.muster = muster
        selbst.pos = 0

    funktion peek():
        wenn selbst.pos < länge(selbst.muster):
            gib_zurück selbst.muster[selbst.pos]
        gib_zurück ""

    funktion konsumiere():
        setze c auf selbst.peek()
        selbst.pos = selbst.pos + 1
        gib_zurück c

    # alt = concat ('|' concat)*
    funktion parse_alt():
        setze links auf selbst.parse_concat()
        solange selbst.peek() == "|":
            selbst.konsumiere()
            setze rechts auf selbst.parse_concat()
            setze links auf neu Alt(links, rechts)
        gib_zurück links

    # concat = quant+
    funktion parse_concat():
        setze kinder auf []
        solange selbst.pos < länge(selbst.muster) und selbst.peek() != "|" und selbst.peek() != ")":
            setze q auf selbst.parse_quant()
            kinder.hinzufügen(q)
        wenn länge(kinder) == 1:
            gib_zurück kinder[0]
        gib_zurück neu Konkat(kinder)

    # quant = atom ('*' | '+' | '?')?
    funktion parse_quant():
        setze a auf selbst.parse_atom()
        setze nxt auf selbst.peek()
        wenn nxt == "*":
            selbst.konsumiere()
            gib_zurück neu Stern(a)
        wenn nxt == "+":
            selbst.konsumiere()
            gib_zurück neu Plus(a)
        wenn nxt == "?":
            selbst.konsumiere()
            gib_zurück neu Frage(a)
        gib_zurück a

    # atom = literal | '.' | '^' | '$' | '(' alt ')' | '[' class ']' | '\' char
    funktion parse_atom():
        setze c auf selbst.peek()
        wenn c == ".":
            selbst.konsumiere()
            gib_zurück neu Punkt()
        wenn c == "^":
            selbst.konsumiere()
            gib_zurück neu Anker("start")
        wenn c == "$":
            selbst.konsumiere()
            gib_zurück neu Anker("ende")
        wenn c == "(":
            selbst.konsumiere()
            setze innen auf selbst.parse_alt()
            wenn selbst.peek() == ")":
                selbst.konsumiere()
            gib_zurück innen
        wenn c == "[":
            selbst.konsumiere()
            gib_zurück selbst.parse_klasse()
        wenn c == "\\":
            selbst.konsumiere()
            setze esc auf selbst.konsumiere()
            gib_zurück neu Literal(esc)
        selbst.konsumiere()
        gib_zurück neu Literal(c)

    # Zeichenklasse: alles bis ']'
    funktion parse_klasse():
        setze negiert auf falsch
        wenn selbst.peek() == "^":
            selbst.konsumiere()
            setze negiert auf wahr
        setze einzeln auf ""
        setze ranges auf []
        solange selbst.pos < länge(selbst.muster) und selbst.peek() != "]":
            setze a auf selbst.konsumiere()
            wenn a == "\\":
                setze b auf selbst.konsumiere()
                setze einzeln auf einzeln + b
            sonst wenn selbst.peek() == "-" und (selbst.pos + 1) < länge(selbst.muster) und selbst.muster[selbst.pos + 1] != "]":
                selbst.konsumiere()
                setze ende_c auf selbst.konsumiere()
                ranges.hinzufügen([a, ende_c])
            sonst:
                setze einzeln auf einzeln + a
        wenn selbst.peek() == "]":
            selbst.konsumiere()
        gib_zurück neu Klasse(einzeln, ranges, negiert)

# ------------------------------------------------------------
# API
# ------------------------------------------------------------

funktion kompiliere(muster):
    setze p auf neu Parser(muster)
    gib_zurück p.parse_alt()

funktion passt_vollstaendig(muster, text):
    setze baum auf kompiliere(muster)
    setze erg auf baum.passe(text, 0)
    setze i auf 0
    solange i < länge(erg):
        wenn erg[i] == länge(text):
            gib_zurück wahr
        setze i auf i + 1
    gib_zurück falsch

# finde — gibt [start, ende] zurueck, oder nichts
funktion suche(muster, text):
    setze baum auf kompiliere(muster)
    setze start auf 0
    solange start <= länge(text):
        setze erg auf baum.passe(text, start)
        wenn länge(erg) > 0:
            # Laengsten match waehlen
            setze max_end auf erg[0]
            setze i auf 1
            solange i < länge(erg):
                wenn erg[i] > max_end:
                    setze max_end auf erg[i]
                setze i auf i + 1
            gib_zurück [start, max_end]
        setze start auf start + 1
    gib_zurück nichts

funktion suche_alle(muster, text):
    setze baum auf kompiliere(muster)
    setze ergebnisse auf []
    setze start auf 0
    solange start <= länge(text):
        setze erg auf baum.passe(text, start)
        wenn länge(erg) > 0:
            setze max_end auf erg[0]
            setze i auf 1
            solange i < länge(erg):
                wenn erg[i] > max_end:
                    setze max_end auf erg[i]
                setze i auf i + 1
            ergebnisse.hinzufügen([start, max_end])
            wenn max_end == start:
                setze start auf start + 1
            sonst:
                setze start auf max_end
        sonst:
            setze start auf start + 1
    gib_zurück ergebnisse

funktion tausche(muster, eingabe, ersatz):
    setze treffer auf suche_alle(muster, eingabe)
    wenn länge(treffer) == 0:
        gib_zurück eingabe
    setze erg auf ""
    setze pos auf 0
    setze i auf 0
    solange i < länge(treffer):
        setze t auf treffer[i]
        setze s auf t[0]
        setze e auf t[1]
        setze j auf pos
        solange j < s:
            setze erg auf erg + eingabe[j]
            setze j auf j + 1
        setze erg auf erg + ersatz
        setze pos auf e
        setze i auf i + 1
    setze j auf pos
    solange j < länge(eingabe):
        setze erg auf erg + eingabe[j]
        setze j auf j + 1
    gib_zurück erg

# ------------------------------------------------------------
# Test-Framework (einfach)
# ------------------------------------------------------------

setze zaehler auf {}
zaehler["gesamt"] = 0
zaehler["ok"] = 0

funktion teste_match(muster, eingabe_text, erwartung):
    zaehler["gesamt"] = zaehler["gesamt"] + 1
    setze erg auf passt_vollstaendig(muster, eingabe_text)
    wenn erg == erwartung:
        zaehler["ok"] = zaehler["ok"] + 1
        zeige "  OK    /" + muster + "/ auf '" + eingabe_text + "' = " + text(erg)
    sonst:
        zeige "  FAIL  /" + muster + "/ auf '" + eingabe_text + "' erwartet " + text(erwartung) + " bekam " + text(erg)

funktion teste_finde(muster, eingabe_text, erwartet_start, erwartet_ende):
    zaehler["gesamt"] = zaehler["gesamt"] + 1
    setze erg auf suche(muster, eingabe_text)
    wenn erg == nichts:
        wenn erwartet_start == -1:
            zaehler["ok"] = zaehler["ok"] + 1
            zeige "  OK    finde /" + muster + "/ in '" + eingabe_text + "' = kein Match"
        sonst:
            zeige "  FAIL  finde /" + muster + "/ in '" + eingabe_text + "' erwartet [" + text(erwartet_start) + "," + text(erwartet_ende) + "] bekam nichts"
    sonst:
        wenn erg[0] == erwartet_start und erg[1] == erwartet_ende:
            zaehler["ok"] = zaehler["ok"] + 1
            zeige "  OK    finde /" + muster + "/ in '" + eingabe_text + "' = [" + text(erg[0]) + "," + text(erg[1]) + "]"
        sonst:
            zeige "  FAIL  finde /" + muster + "/ in '" + eingabe_text + "' erwartet [" + text(erwartet_start) + "," + text(erwartet_ende) + "] bekam [" + text(erg[0]) + "," + text(erg[1]) + "]"

funktion teste_ersetze(muster, eingabe_text, ersatz_str, erwartet):
    zaehler["gesamt"] = zaehler["gesamt"] + 1
    setze erg auf tausche(muster, eingabe_text, ersatz_str)
    wenn erg == erwartet:
        zaehler["ok"] = zaehler["ok"] + 1
        zeige "  OK    ersetze /" + muster + "/ -> '" + ersatz_str + "' in '" + eingabe_text + "' = '" + erg + "'"
    sonst:
        zeige "  FAIL  ersetze /" + muster + "/ -> '" + ersatz_str + "' in '" + eingabe_text + "' erwartet '" + erwartet + "' bekam '" + erg + "'"

# ------------------------------------------------------------
# Test-Suite (30+ Tests)
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo Regex-Engine — Test-Suite"
zeige "================================================"
zeige ""
zeige "--- passt_vollstaendig ---"

# Basis-Literale
teste_match("abc", "abc", wahr)
teste_match("abc", "abd", falsch)
teste_match("", "", wahr)
teste_match("a", "", falsch)

# Punkt
teste_match("a.c", "abc", wahr)
teste_match("a.c", "axc", wahr)
teste_match("a.c", "ac", falsch)
teste_match("...", "xyz", wahr)

# Stern
teste_match("a*", "", wahr)
teste_match("a*", "aaaa", wahr)
teste_match("a*b", "b", wahr)
teste_match("a*b", "aaab", wahr)
teste_match("a*b", "aaac", falsch)

# Plus
teste_match("a+", "", falsch)
teste_match("a+", "a", wahr)
teste_match("a+", "aaaa", wahr)

# Fragezeichen
teste_match("ab?c", "abc", wahr)
teste_match("ab?c", "ac", wahr)
teste_match("ab?c", "abbc", falsch)

# Zeichenklassen
teste_match("[abc]", "a", wahr)
teste_match("[abc]", "d", falsch)
teste_match("[a-z]+", "hallo", wahr)
teste_match("[a-z]+", "Hallo", falsch)
teste_match("[^0-9]+", "abc", wahr)
teste_match("[^0-9]+", "ab1", falsch)
teste_match("[0-9]+", "12345", wahr)

# Gruppen + Alternativen
teste_match("(cat|dog)", "cat", wahr)
teste_match("(cat|dog)", "dog", wahr)
teste_match("(cat|dog)", "fish", falsch)
teste_match("(ab)+", "ababab", wahr)
teste_match("(ab)+", "aba", falsch)

# Komplexe Kombinationen
teste_match("[a-z]+@[a-z]+\\.[a-z]+", "hallo@welt.de", wahr)
teste_match("[a-z]+@[a-z]+\\.[a-z]+", "ohne-at.de", falsch)

# Anker
teste_match("^abc$", "abc", wahr)
teste_match("^abc", "abc", wahr)

# Escape
teste_match("a\\.b", "a.b", wahr)
teste_match("a\\.b", "axb", falsch)

zeige ""
zeige "--- finde ---"
teste_finde("bc", "abcdef", 1, 3)
teste_finde("xyz", "abcdef", -1, -1)
teste_finde("[0-9]+", "abc123def456", 3, 6)
teste_finde("a+", "bbbaabbb", 3, 5)

zeige ""
zeige "--- ersetze ---"
teste_ersetze("a", "banana", "o", "bonono")
teste_ersetze("[0-9]+", "abc123def456", "X", "abcXdefX")
teste_ersetze("cat", "fat cat sat", "dog", "fat dog sat")

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"
