# ============================================================
# moo Mini-Datalog Engine
#
# Syntax:
#   parent(alice, bob).          # Fact
#   ancestor(X, Y) :- parent(X, Y).              # Rule
#   ancestor(X, Y) :- parent(X, Z), ancestor(Z, Y).
#   ?- ancestor(alice, Y).       # Query
#
# Bottom-up naive Evaluation mit Fixpoint-Iteration.
# ============================================================

# ------------------------------------------------------------
# Term-Konstruktoren: Atom, Var, Compound
# ------------------------------------------------------------

funktion make_atom(name):
    setze t auf {}
    t["typ"] = "atom"
    t["name"] = name
    gib_zurück t

funktion make_var(name):
    setze t auf {}
    t["typ"] = "var"
    t["name"] = name
    gib_zurück t

funktion make_compound(functor, args):
    setze t auf {}
    t["typ"] = "compound"
    t["functor"] = functor
    t["args"] = args
    gib_zurück t

funktion term_zu_text(t):
    wenn t["typ"] == "atom":
        gib_zurück t["name"]
    wenn t["typ"] == "var":
        gib_zurück t["name"]
    wenn t["typ"] == "compound":
        setze s auf t["functor"] + "("
        setze i auf 0
        solange i < länge(t["args"]):
            wenn i > 0:
                setze s auf s + ","
            setze s auf s + term_zu_text(t["args"][i])
            setze i auf i + 1
        setze s auf s + ")"
        gib_zurück s
    gib_zurück "?"

# ------------------------------------------------------------
# Substitution (dict var_name → term)
# ------------------------------------------------------------

funktion subst_kopie(s):
    setze k auf {}
    setze keys auf s.schlüssel()
    setze i auf 0
    solange i < länge(keys):
        k[keys[i]] = s[keys[i]]
        setze i auf i + 1
    gib_zurück k

# walk: folge var-Kette in subst
funktion walk(t, subst):
    setze cur auf t
    solange cur["typ"] == "var" und subst.hat(cur["name"]):
        setze cur auf subst[cur["name"]]
    gib_zurück cur

# Deep-Kopie eines terms (workaround fuer refcount-aliasing in verschachtelten dicts)
funktion term_kopie(t):
    wenn t["typ"] == "atom":
        gib_zurück make_atom(t["name"])
    wenn t["typ"] == "var":
        gib_zurück make_var(t["name"])
    wenn t["typ"] == "compound":
        setze neue_args auf []
        setze i auf 0
        solange i < länge(t["args"]):
            neue_args.hinzufügen(term_kopie(t["args"][i]))
            setze i auf i + 1
        gib_zurück make_compound(t["functor"], neue_args)
    gib_zurück t

funktion apply_subst(t, subst):
    setze t2 auf walk(t, subst)
    wenn t2["typ"] == "compound":
        setze neue_args auf []
        setze i auf 0
        solange i < länge(t2["args"]):
            neue_args.hinzufügen(apply_subst(t2["args"][i], subst))
            setze i auf i + 1
        gib_zurück make_compound(t2["functor"], neue_args)
    # Atome/Vars: frische Kopie, damit kein aliasing in die subst zurueckbleibt
    gib_zurück term_kopie(t2)

# ------------------------------------------------------------
# Unification
# ------------------------------------------------------------

# Liefert dict mit success + subst, oder success=falsch
funktion unify(t1, t2, subst):
    setze a auf walk(t1, subst)
    setze b auf walk(t2, subst)
    wenn a["typ"] == "var":
        wenn b["typ"] == "var" und a["name"] == b["name"]:
            setze r auf {}
            r["ok"] = wahr
            r["subst"] = subst
            gib_zurück r
        setze ns auf subst_kopie(subst)
        ns[a["name"]] = b
        setze r auf {}
        r["ok"] = wahr
        r["subst"] = ns
        gib_zurück r
    wenn b["typ"] == "var":
        setze ns auf subst_kopie(subst)
        ns[b["name"]] = a
        setze r auf {}
        r["ok"] = wahr
        r["subst"] = ns
        gib_zurück r
    wenn a["typ"] == "atom" und b["typ"] == "atom":
        setze r auf {}
        wenn a["name"] == b["name"]:
            r["ok"] = wahr
            r["subst"] = subst
        sonst:
            r["ok"] = falsch
        gib_zurück r
    wenn a["typ"] == "compound" und b["typ"] == "compound":
        wenn a["functor"] != b["functor"]:
            setze r auf {}
            r["ok"] = falsch
            gib_zurück r
        wenn länge(a["args"]) != länge(b["args"]):
            setze r auf {}
            r["ok"] = falsch
            gib_zurück r
        setze cur_s auf subst
        setze i auf 0
        solange i < länge(a["args"]):
            setze u auf unify(a["args"][i], b["args"][i], cur_s)
            wenn u["ok"] == falsch:
                setze r auf {}
                r["ok"] = falsch
                gib_zurück r
            setze cur_s auf u["subst"]
            setze i auf i + 1
        setze r auf {}
        r["ok"] = wahr
        r["subst"] = cur_s
        gib_zurück r
    setze r auf {}
    r["ok"] = falsch
    gib_zurück r

# ------------------------------------------------------------
# Rule: head + body
# ------------------------------------------------------------

funktion make_rule(head, body):
    setze r auf {}
    r["head"] = head
    r["body"] = body
    gib_zurück r

# Variable-Renaming einer Rule (fresh copy)
funktion rename_term(t, praefix):
    wenn t["typ"] == "var":
        gib_zurück make_var(praefix + t["name"])
    wenn t["typ"] == "compound":
        setze neue_args auf []
        setze i auf 0
        solange i < länge(t["args"]):
            neue_args.hinzufügen(rename_term(t["args"][i], praefix))
            setze i auf i + 1
        gib_zurück make_compound(t["functor"], neue_args)
    gib_zurück t

funktion rename_rule(rule, praefix):
    setze head auf rename_term(rule["head"], praefix)
    setze body auf []
    setze i auf 0
    solange i < länge(rule["body"]):
        body.hinzufügen(rename_term(rule["body"][i], praefix))
        setze i auf i + 1
    gib_zurück make_rule(head, body)

# ------------------------------------------------------------
# Body-Matching: finde alle subst die body gegen facts erfuellen
# ------------------------------------------------------------

funktion match_body(body, idx, subst, facts, loesungen):
    wenn idx >= länge(body):
        loesungen.hinzufügen(subst_kopie(subst))
        gib_zurück nichts
    setze literal auf body[idx]
    setze i auf 0
    solange i < länge(facts):
        setze u auf unify(literal, facts[i], subst)
        wenn u["ok"]:
            match_body(body, idx + 1, u["subst"], facts, loesungen)
        setze i auf i + 1

# ------------------------------------------------------------
# Fact-Set mit Dedup via term_zu_text als key
# ------------------------------------------------------------

funktion fact_schluessel(f):
    gib_zurück term_zu_text(f)

funktion fact_enthalten(facts, f):
    setze k auf fact_schluessel(f)
    setze i auf 0
    solange i < länge(facts):
        wenn fact_schluessel(facts[i]) == k:
            gib_zurück wahr
        setze i auf i + 1
    gib_zurück falsch

# ------------------------------------------------------------
# Datalog-Engine: bottom-up Fixpoint
# ------------------------------------------------------------

klasse Datalog:
    funktion erstelle():
        setze selbst.facts auf []
        setze selbst.rules auf []

    funktion add_fact(f):
        wenn fact_enthalten(selbst.facts, f) == falsch:
            selbst.facts.hinzufügen(f)

    funktion add_rule(r):
        selbst.rules.hinzufügen(r)

    funktion evaluiere():
        setze geaendert auf wahr
        setze runde auf 0
        solange geaendert:
            setze geaendert auf falsch
            setze runde auf runde + 1
            setze ri auf 0
            solange ri < länge(selbst.rules):
                setze rule auf rename_rule(selbst.rules[ri], "r" + text(runde) + "_" + text(ri) + "_")
                setze loesungen auf []
                match_body(rule["body"], 0, {}, selbst.facts, loesungen)
                setze li auf 0
                solange li < länge(loesungen):
                    setze neues_fact auf apply_subst(rule["head"], loesungen[li])
                    wenn fact_enthalten(selbst.facts, neues_fact) == falsch:
                        selbst.facts.hinzufügen(neues_fact)
                        setze geaendert auf wahr
                    setze li auf li + 1
                setze ri auf ri + 1
            # Safety-Break falls zu viele Runden
            wenn runde > 100:
                setze geaendert auf falsch

    funktion query(q):
        setze loesungen auf []
        setze i auf 0
        solange i < länge(selbst.facts):
            setze u auf unify(q, selbst.facts[i], {})
            wenn u["ok"]:
                loesungen.hinzufügen(u["subst"])
            setze i auf i + 1
        gib_zurück loesungen

# ------------------------------------------------------------
# Parser
# ------------------------------------------------------------

funktion ist_uppercase(c):
    wenn c >= "A" und c <= "Z":
        gib_zurück wahr
    gib_zurück falsch

funktion ist_lowercase(c):
    wenn c >= "a" und c <= "z":
        gib_zurück wahr
    gib_zurück falsch

funktion ist_alnum(c):
    wenn ist_lowercase(c) oder ist_uppercase(c):
        gib_zurück wahr
    wenn c >= "0" und c <= "9":
        gib_zurück wahr
    wenn c == "_":
        gib_zurück wahr
    gib_zurück falsch

klasse Parser:
    funktion erstelle(quelle):
        selbst.q = quelle
        selbst.pos = 0

    funktion skip_ws():
        solange selbst.pos < länge(selbst.q):
            setze c auf selbst.q[selbst.pos]
            wenn c == " " oder c == "\t" oder c == "\n" oder c == "\r":
                selbst.pos = selbst.pos + 1
            sonst wenn c == "%":
                solange selbst.pos < länge(selbst.q) und selbst.q[selbst.pos] != "\n":
                    selbst.pos = selbst.pos + 1
            sonst:
                gib_zurück nichts

    funktion peek():
        selbst.skip_ws()
        wenn selbst.pos < länge(selbst.q):
            gib_zurück selbst.q[selbst.pos]
        gib_zurück ""

    funktion erwarte_char(c):
        wenn selbst.peek() == c:
            selbst.pos = selbst.pos + 1
            gib_zurück wahr
        gib_zurück falsch

    funktion parse_ident():
        selbst.skip_ws()
        setze start auf selbst.pos
        solange selbst.pos < länge(selbst.q) und ist_alnum(selbst.q[selbst.pos]):
            selbst.pos = selbst.pos + 1
        gib_zurück selbst.q.teilstring(start, selbst.pos)

    funktion parse_term():
        setze name auf selbst.parse_ident()
        wenn länge(name) == 0:
            gib_zurück nichts
        # Variable wenn erster Buchstabe uppercase oder _
        wenn ist_uppercase(name[0]) oder name[0] == "_":
            gib_zurück make_var(name)
        # Compound mit args?
        wenn selbst.peek() == "(":
            selbst.pos = selbst.pos + 1
            setze args auf []
            setze erstes auf wahr
            solange selbst.peek() != ")":
                wenn erstes == falsch:
                    wenn selbst.peek() == ",":
                        selbst.pos = selbst.pos + 1
                setze erstes auf falsch
                setze arg auf selbst.parse_term()
                args.hinzufügen(arg)
            selbst.pos = selbst.pos + 1  # ')'
            gib_zurück make_compound(name, args)
        # Atom
        gib_zurück make_atom(name)

    funktion parse_statement():
        # Fact oder Rule: term ('.' | ':-' term_liste '.')
        setze head auf selbst.parse_term()
        wenn head == nichts:
            gib_zurück nichts
        selbst.skip_ws()
        wenn selbst.pos + 1 < länge(selbst.q) und selbst.q[selbst.pos] == ":" und selbst.q[selbst.pos + 1] == "-":
            selbst.pos = selbst.pos + 2
            setze body auf []
            setze erstes auf wahr
            solange selbst.peek() != ".":
                wenn erstes == falsch:
                    wenn selbst.peek() == ",":
                        selbst.pos = selbst.pos + 1
                setze erstes auf falsch
                setze t auf selbst.parse_term()
                body.hinzufügen(t)
            selbst.pos = selbst.pos + 1  # '.'
            setze r auf {}
            r["typ"] = "rule"
            r["rule"] = make_rule(head, body)
            gib_zurück r
        wenn selbst.peek() == ".":
            selbst.pos = selbst.pos + 1
        setze r auf {}
        r["typ"] = "fact"
        r["fact"] = head
        gib_zurück r

    funktion parse_alle():
        setze out auf []
        selbst.skip_ws()
        solange selbst.pos < länge(selbst.q):
            setze s auf selbst.parse_statement()
            wenn s != nichts:
                out.hinzufügen(s)
            selbst.skip_ws()
        gib_zurück out

funktion lade_programm(db, quelle):
    setze p auf neu Parser(quelle)
    setze statements auf p.parse_alle()
    setze i auf 0
    solange i < länge(statements):
        setze s auf statements[i]
        wenn s["typ"] == "fact":
            db.add_fact(s["fact"])
        sonst:
            db.add_rule(s["rule"])
        setze i auf i + 1

# ------------------------------------------------------------
# Query-Helfer
# ------------------------------------------------------------

funktion parse_query(quelle):
    setze p auf neu Parser(quelle)
    gib_zurück p.parse_term()

# Zeigt eine Liste von Loesungen aus einer Query
funktion zeige_loesungen(query, loesungen):
    wenn länge(loesungen) == 0:
        zeige "  (keine Loesung)"
        gib_zurück nichts
    setze i auf 0
    solange i < länge(loesungen):
        setze konkret auf apply_subst(query, loesungen[i])
        zeige "  " + term_zu_text(konkret)
        setze i auf i + 1

# ------------------------------------------------------------
# Test-Framework
# ------------------------------------------------------------

setze zaehler auf {}
zaehler["gesamt"] = 0
zaehler["ok"] = 0

funktion check(name, bedingung):
    zaehler["gesamt"] = zaehler["gesamt"] + 1
    wenn bedingung:
        zaehler["ok"] = zaehler["ok"] + 1
        zeige "  OK    " + name
    sonst:
        zeige "  FAIL  " + name

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo Mini-Datalog"
zeige "================================================"
zeige ""

zeige "--- Test 1: Unification ---"
setze u auf unify(make_atom("foo"), make_atom("foo"), {})
check("atom=atom", u["ok"])
setze u auf unify(make_atom("foo"), make_atom("bar"), {})
check("atom!=atom", u["ok"] == falsch)
setze u auf unify(make_var("X"), make_atom("alice"), {})
check("var unif", u["ok"] und u["subst"]["X"]["name"] == "alice")
setze u auf unify(make_compound("p", [make_var("X"), make_atom("b")]), make_compound("p", [make_atom("a"), make_var("Y")]), {})
check("compound unif", u["ok"] und u["subst"]["X"]["name"] == "a" und u["subst"]["Y"]["name"] == "b")

zeige ""
zeige "--- Test 2: Familie (facts + ancestor) ---"
setze db auf neu Datalog()
setze prog auf "parent(alice, bob). parent(bob, charlie). parent(charlie, dave). parent(dave, eve). ancestor(X, Y) :- parent(X, Y). ancestor(X, Y) :- parent(X, Z), ancestor(Z, Y)."
lade_programm(db, prog)
zeige "Facts vor Evaluation: " + text(länge(db.facts))
zeige "Rules: " + text(länge(db.rules))
check("4 parent-facts geladen", länge(db.facts) == 4)
check("2 rules geladen", länge(db.rules) == 2)
db.evaluiere()
zeige "Facts nach Evaluation: " + text(länge(db.facts))
# 4 parents + ancestor: 4 direct + transitive = 10 total ancestor facts
# Aber insgesamt: parent(*) + ancestor(*)
# 4 parent + 10 ancestor = 14
check("Facts >= 14 nach Fixpoint", länge(db.facts) >= 14)

zeige ""
zeige "--- Test 3: Query ancestor(alice, Y) ---"
setze q auf parse_query("ancestor(alice, Y)")
setze loesungen auf db.query(q)
zeige "Loesungen:"
zeige_loesungen(q, loesungen)
check("alice hat 4 Nachfahren", länge(loesungen) == 4)

zeige ""
zeige "--- Test 4: Query ancestor(alice, dave) (yes/no) ---"
setze q auf parse_query("ancestor(alice, dave)")
setze loesungen auf db.query(q)
check("alice -> dave wahr", länge(loesungen) >= 1)

setze q auf parse_query("ancestor(dave, alice)")
setze loesungen auf db.query(q)
check("dave -> alice falsch", länge(loesungen) == 0)

zeige ""
zeige "--- Test 5: Graph-Reachability (zyklisch!) ---"
setze db2 auf neu Datalog()
setze prog auf "edge(a, b). edge(b, c). edge(c, d). edge(d, b). edge(a, e). path(X, Y) :- edge(X, Y). path(X, Y) :- edge(X, Z), path(Z, Y)."
lade_programm(db2, prog)
db2.evaluiere()
zeige "Facts nach Eval: " + text(länge(db2.facts))
setze q auf parse_query("path(a, X)")
setze loesungen auf db2.query(q)
zeige "Erreichbar von a:"
zeige_loesungen(q, loesungen)
# a erreicht: b, c, d, e (b->c->d->b-Zyklus aber keine neuen Knoten)
check("terminierung (nicht in Endlos)", wahr)
check("a erreicht 4 Knoten", länge(loesungen) == 4)

setze q auf parse_query("path(b, b)")
setze loesungen auf db2.query(q)
check("b erreicht sich selbst (Zyklus)", länge(loesungen) >= 1)

zeige ""
zeige "--- Test 6: Mehrere Literale im Body ---"
setze db3 auf neu Datalog()
setze prog auf "likes(alice, pizza). likes(bob, pizza). likes(alice, sushi). likes(carol, pizza). same_food(X, Y) :- likes(X, F), likes(Y, F)."
lade_programm(db3, prog)
db3.evaluiere()
setze q auf parse_query("same_food(alice, Y)")
setze loesungen auf db3.query(q)
zeige "alice hat gleichen Geschmack wie:"
zeige_loesungen(q, loesungen)
# alice mag pizza und sushi. Gleicher Geschmack: alice (self, via beide), bob (via pizza), carol (via pizza)
check("same_food(alice, _) hat 3 Loesungen", länge(loesungen) == 3)

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"
