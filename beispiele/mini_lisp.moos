# ============================================================
# moo Mini-Lisp/Scheme — Interpreter in pure moo
#
# Features:
#   - S-Expression Tokenizer + Parser
#   - Lisp-Datentypen via moo-Dicts: zahl, sym, nil, pair, closure, builtin
#   - Special Forms: quote, if, define, lambda, let, set!, begin
#   - Builtins: + - * / = < > eq? car cdr cons null? pair? print list
#   - Lexical Scoping mit Environment-Chains (echte Closures!)
#   - Rekursive Funktionen
# ============================================================

# ------------------------------------------------------------
# Werte-Konstruktoren
# ------------------------------------------------------------

funktion make_zahl(n):
    setze v auf {}
    v["typ"] = "zahl"
    v["wert"] = n
    gib_zurück v

funktion make_sym(s):
    setze v auf {}
    v["typ"] = "sym"
    v["wert"] = s
    gib_zurück v

funktion make_nil():
    setze v auf {}
    v["typ"] = "nil"
    gib_zurück v

funktion make_pair(a, d):
    setze v auf {}
    v["typ"] = "pair"
    v["car"] = a
    v["cdr"] = d
    gib_zurück v

funktion make_closure(params, body, env):
    setze v auf {}
    v["typ"] = "closure"
    v["params"] = params
    v["body"] = body
    v["env"] = env
    gib_zurück v

funktion make_builtin(name):
    setze v auf {}
    v["typ"] = "builtin"
    v["name"] = name
    gib_zurück v

funktion make_bool(b):
    setze v auf {}
    v["typ"] = "bool"
    v["wert"] = b
    gib_zurück v

funktion ist_nil(v):
    wenn v["typ"] == "nil":
        gib_zurück wahr
    gib_zurück falsch

funktion ist_pair(v):
    wenn v["typ"] == "pair":
        gib_zurück wahr
    gib_zurück falsch

# ------------------------------------------------------------
# Tokenizer
# ------------------------------------------------------------

funktion ist_ziffer(c):
    wenn länge(c) == 0:
        gib_zurück falsch
    wenn c >= "0" und c <= "9":
        gib_zurück wahr
    gib_zurück falsch

funktion ist_space(c):
    wenn c == " " oder c == "\t" oder c == "\n" oder c == "\r":
        gib_zurück wahr
    gib_zurück falsch

funktion tokenize(quelle):
    setze tokens auf []
    setze i auf 0
    solange i < länge(quelle):
        setze c auf quelle[i]
        wenn ist_space(c):
            setze i auf i + 1
        sonst wenn c == ";":
            # Kommentar bis Zeilenende
            solange i < länge(quelle) und quelle[i] != "\n":
                setze i auf i + 1
        sonst wenn c == "(" oder c == ")" oder c == "'":
            tokens.hinzufügen(c)
            setze i auf i + 1
        sonst:
            # Symbol oder Zahl: bis zum naechsten Trenner
            setze start auf i
            solange i < länge(quelle) und ist_space(quelle[i]) == falsch und quelle[i] != "(" und quelle[i] != ")" und quelle[i] != "'":
                setze i auf i + 1
            setze tok auf quelle.teilstring(start, i)
            tokens.hinzufügen(tok)
    gib_zurück tokens

# ------------------------------------------------------------
# Parser
# ------------------------------------------------------------

# Parser-State: dict mit tokens + pos
funktion mache_parser(tokens):
    setze p auf {}
    p["tokens"] = tokens
    p["pos"] = 0
    gib_zurück p

funktion parser_peek(p):
    wenn p["pos"] < länge(p["tokens"]):
        gib_zurück p["tokens"][p["pos"]]
    gib_zurück ""

funktion parser_lese(p):
    setze t auf parser_peek(p)
    p["pos"] = p["pos"] + 1
    gib_zurück t

funktion parse_atom(tok):
    # Zahl oder Symbol?
    wenn länge(tok) == 0:
        gib_zurück make_nil()
    setze erster auf tok[0]
    setze ist_zahl auf falsch
    wenn ist_ziffer(erster):
        setze ist_zahl auf wahr
    wenn erster == "-" und länge(tok) > 1 und ist_ziffer(tok[1]):
        setze ist_zahl auf wahr
    wenn ist_zahl:
        # Manuelles int-Parsen
        setze n auf 0
        setze start auf 0
        setze negativ auf falsch
        wenn erster == "-":
            setze negativ auf wahr
            setze start auf 1
        setze j auf start
        solange j < länge(tok):
            setze d auf bytes_zu_liste(tok[j])[0] - 48
            setze n auf n * 10 + d
            setze j auf j + 1
        wenn negativ:
            setze n auf 0 - n
        gib_zurück make_zahl(n)
    # Spezielle Symbole
    wenn tok == "#t":
        gib_zurück make_bool(wahr)
    wenn tok == "#f":
        gib_zurück make_bool(falsch)
    gib_zurück make_sym(tok)

funktion parse_expr(p):
    setze t auf parser_lese(p)
    wenn t == "(":
        # Liste parsen bis ')'
        setze items auf []
        solange parser_peek(p) != ")" und parser_peek(p) != "":
            items.hinzufügen(parse_expr(p))
        parser_lese(p)  # verbrauche ')'
        # Cons-Kette bauen
        setze erg auf make_nil()
        setze i auf länge(items) - 1
        solange i >= 0:
            setze erg auf make_pair(items[i], erg)
            setze i auf i - 1
        gib_zurück erg
    sonst wenn t == "'":
        # Quote: '(a b) → (quote (a b))
        setze innen auf parse_expr(p)
        gib_zurück make_pair(make_sym("quote"), make_pair(innen, make_nil()))
    gib_zurück parse_atom(t)

funktion parse_alle(quelle):
    setze p auf mache_parser(tokenize(quelle))
    setze liste auf []
    solange p["pos"] < länge(p["tokens"]):
        liste.hinzufügen(parse_expr(p))
    gib_zurück liste

# ------------------------------------------------------------
# Environment
# ------------------------------------------------------------

funktion mache_env(parent):
    setze e auf {}
    e["vars"] = {}
    e["parent"] = parent
    gib_zurück e

funktion env_define(env, name, wert):
    env["vars"][name] = wert

funktion env_get(env, name):
    setze cur auf env
    solange cur != nichts:
        wenn cur["vars"].hat(name):
            gib_zurück cur["vars"][name]
        setze cur auf cur["parent"]
    zeige "ERROR: unbekannte Variable: " + name
    gib_zurück make_nil()

funktion env_set(env, name, wert):
    setze cur auf env
    solange cur != nichts:
        wenn cur["vars"].hat(name):
            cur["vars"][name] = wert
            gib_zurück nichts
        setze cur auf cur["parent"]
    zeige "ERROR: set! auf unbekannte Variable: " + name

# ------------------------------------------------------------
# Pair-Hilfen
# ------------------------------------------------------------

funktion pair_zu_liste(p):
    setze liste auf []
    setze cur auf p
    solange cur["typ"] == "pair":
        liste.hinzufügen(cur["car"])
        setze cur auf cur["cdr"]
    gib_zurück liste

funktion laenge_paar(p):
    setze n auf 0
    setze cur auf p
    solange cur["typ"] == "pair":
        setze n auf n + 1
        setze cur auf cur["cdr"]
    gib_zurück n

# ------------------------------------------------------------
# Eval
# ------------------------------------------------------------

funktion lisp_eval(expr, env):
    setze t auf expr["typ"]
    wenn t == "zahl" oder t == "bool" oder t == "nil" oder t == "closure" oder t == "builtin":
        gib_zurück expr
    wenn t == "sym":
        gib_zurück env_get(env, expr["wert"])
    wenn t == "pair":
        setze head auf expr["car"]
        # Special Forms
        wenn head["typ"] == "sym":
            setze name auf head["wert"]
            wenn name == "quote":
                gib_zurück expr["cdr"]["car"]
            wenn name == "if":
                setze args auf expr["cdr"]
                setze bed auf lisp_eval(args["car"], env)
                wenn bed["typ"] == "bool" und bed["wert"] == falsch:
                    # false branch
                    setze rest auf args["cdr"]["cdr"]
                    wenn rest["typ"] == "pair":
                        gib_zurück lisp_eval(rest["car"], env)
                    gib_zurück make_nil()
                wenn bed["typ"] == "nil":
                    setze rest auf args["cdr"]["cdr"]
                    wenn rest["typ"] == "pair":
                        gib_zurück lisp_eval(rest["car"], env)
                    gib_zurück make_nil()
                gib_zurück lisp_eval(args["cdr"]["car"], env)
            wenn name == "define":
                setze args auf expr["cdr"]
                setze ziel auf args["car"]
                wenn ziel["typ"] == "sym":
                    # (define name expr)
                    setze wert auf lisp_eval(args["cdr"]["car"], env)
                    env_define(env, ziel["wert"], wert)
                    gib_zurück make_nil()
                wenn ziel["typ"] == "pair":
                    # (define (name params...) body...) -> syntactic sugar
                    setze fn_name auf ziel["car"]["wert"]
                    setze params_liste auf pair_zu_liste(ziel["cdr"])
                    setze body_liste auf pair_zu_liste(args["cdr"])
                    # body wird als begin gewickelt
                    setze body_expr auf make_body(body_liste)
                    setze params_namen auf []
                    setze i auf 0
                    solange i < länge(params_liste):
                        params_namen.hinzufügen(params_liste[i]["wert"])
                        setze i auf i + 1
                    setze cl auf make_closure(params_namen, body_expr, env)
                    env_define(env, fn_name, cl)
                    gib_zurück make_nil()
            wenn name == "lambda":
                setze args auf expr["cdr"]
                setze params_liste auf pair_zu_liste(args["car"])
                setze body_liste auf pair_zu_liste(args["cdr"])
                setze body_expr auf make_body(body_liste)
                setze params_namen auf []
                setze i auf 0
                solange i < länge(params_liste):
                    params_namen.hinzufügen(params_liste[i]["wert"])
                    setze i auf i + 1
                gib_zurück make_closure(params_namen, body_expr, env)
            wenn name == "let":
                # (let ((a 1) (b 2)) body...)
                setze args auf expr["cdr"]
                setze bindings auf pair_zu_liste(args["car"])
                setze body_liste auf pair_zu_liste(args["cdr"])
                setze neu_env auf mache_env(env)
                setze i auf 0
                solange i < länge(bindings):
                    setze b auf bindings[i]
                    setze b_name auf b["car"]["wert"]
                    setze b_wert auf lisp_eval(b["cdr"]["car"], env)
                    env_define(neu_env, b_name, b_wert)
                    setze i auf i + 1
                setze ergebnis auf make_nil()
                setze i auf 0
                solange i < länge(body_liste):
                    setze ergebnis auf lisp_eval(body_liste[i], neu_env)
                    setze i auf i + 1
                gib_zurück ergebnis
            wenn name == "set!":
                setze args auf expr["cdr"]
                setze n auf args["car"]["wert"]
                setze w auf lisp_eval(args["cdr"]["car"], env)
                env_set(env, n, w)
                gib_zurück make_nil()
            wenn name == "begin":
                setze body_liste auf pair_zu_liste(expr["cdr"])
                setze ergebnis auf make_nil()
                setze i auf 0
                solange i < länge(body_liste):
                    setze ergebnis auf lisp_eval(body_liste[i], env)
                    setze i auf i + 1
                gib_zurück ergebnis
        # Normaler Funktionsaufruf
        setze fnv auf lisp_eval(head, env)
        setze args_paar auf expr["cdr"]
        setze args auf []
        solange args_paar["typ"] == "pair":
            args.hinzufügen(lisp_eval(args_paar["car"], env))
            setze args_paar auf args_paar["cdr"]
        gib_zurück lisp_apply(fnv, args)
    gib_zurück make_nil()

funktion make_body(body_liste):
    # Wickelt eine Liste von Expressions in (begin ...)
    wenn länge(body_liste) == 1:
        gib_zurück body_liste[0]
    setze erg auf make_nil()
    setze i auf länge(body_liste) - 1
    solange i >= 0:
        setze erg auf make_pair(body_liste[i], erg)
        setze i auf i - 1
    gib_zurück make_pair(make_sym("begin"), erg)

funktion lisp_apply(fnv, args):
    wenn fnv["typ"] == "closure":
        setze neu_env auf mache_env(fnv["env"])
        setze i auf 0
        solange i < länge(fnv["params"]):
            env_define(neu_env, fnv["params"][i], args[i])
            setze i auf i + 1
        gib_zurück lisp_eval(fnv["body"], neu_env)
    wenn fnv["typ"] == "builtin":
        gib_zurück builtin_call(fnv["name"], args)
    zeige "ERROR: nicht aufrufbar, typ=" + fnv["typ"]
    gib_zurück make_nil()

# ------------------------------------------------------------
# Builtins
# ------------------------------------------------------------

funktion builtin_call(name, args):
    wenn name == "+":
        setze s auf 0
        setze i auf 0
        solange i < länge(args):
            setze s auf s + args[i]["wert"]
            setze i auf i + 1
        gib_zurück make_zahl(s)
    wenn name == "-":
        wenn länge(args) == 1:
            gib_zurück make_zahl(0 - args[0]["wert"])
        setze s auf args[0]["wert"]
        setze i auf 1
        solange i < länge(args):
            setze s auf s - args[i]["wert"]
            setze i auf i + 1
        gib_zurück make_zahl(s)
    wenn name == "*":
        setze s auf 1
        setze i auf 0
        solange i < länge(args):
            setze s auf s * args[i]["wert"]
            setze i auf i + 1
        gib_zurück make_zahl(s)
    wenn name == "/":
        setze s auf args[0]["wert"]
        setze i auf 1
        solange i < länge(args):
            setze s auf boden(s / args[i]["wert"])
            setze i auf i + 1
        gib_zurück make_zahl(s)
    wenn name == "=":
        gib_zurück make_bool(args[0]["wert"] == args[1]["wert"])
    wenn name == "<":
        gib_zurück make_bool(args[0]["wert"] < args[1]["wert"])
    wenn name == ">":
        gib_zurück make_bool(args[0]["wert"] > args[1]["wert"])
    wenn name == "eq?":
        setze a auf args[0]
        setze b auf args[1]
        wenn a["typ"] != b["typ"]:
            gib_zurück make_bool(falsch)
        wenn a["typ"] == "zahl":
            gib_zurück make_bool(a["wert"] == b["wert"])
        wenn a["typ"] == "sym":
            gib_zurück make_bool(a["wert"] == b["wert"])
        wenn a["typ"] == "nil":
            gib_zurück make_bool(wahr)
        gib_zurück make_bool(falsch)
    wenn name == "car":
        gib_zurück args[0]["car"]
    wenn name == "cdr":
        gib_zurück args[0]["cdr"]
    wenn name == "cons":
        gib_zurück make_pair(args[0], args[1])
    wenn name == "null?":
        gib_zurück make_bool(args[0]["typ"] == "nil")
    wenn name == "pair?":
        gib_zurück make_bool(args[0]["typ"] == "pair")
    wenn name == "list":
        setze erg auf make_nil()
        setze i auf länge(args) - 1
        solange i >= 0:
            setze erg auf make_pair(args[i], erg)
            setze i auf i - 1
        gib_zurück erg
    wenn name == "print":
        zeige wert_zu_text(args[0])
        gib_zurück make_nil()
    zeige "ERROR: unbekanntes builtin: " + name
    gib_zurück make_nil()

# ------------------------------------------------------------
# Pretty-Print
# ------------------------------------------------------------

funktion wert_zu_text(v):
    setze t auf v["typ"]
    wenn t == "zahl":
        gib_zurück text(v["wert"])
    wenn t == "sym":
        gib_zurück v["wert"]
    wenn t == "nil":
        gib_zurück "()"
    wenn t == "bool":
        wenn v["wert"]:
            gib_zurück "#t"
        gib_zurück "#f"
    wenn t == "pair":
        setze s auf "("
        setze cur auf v
        setze erste auf wahr
        solange cur["typ"] == "pair":
            wenn erste == falsch:
                setze s auf s + " "
            setze erste auf falsch
            setze s auf s + wert_zu_text(cur["car"])
            setze cur auf cur["cdr"]
        wenn cur["typ"] != "nil":
            setze s auf s + " . " + wert_zu_text(cur)
        setze s auf s + ")"
        gib_zurück s
    wenn t == "closure":
        gib_zurück "#<closure>"
    wenn t == "builtin":
        gib_zurück "#<builtin:" + v["name"] + ">"
    gib_zurück "#<?>"

# ------------------------------------------------------------
# Standard-Environment
# ------------------------------------------------------------

funktion standard_env():
    setze e auf mache_env(nichts)
    setze bs auf ["+", "-", "*", "/", "=", "<", ">", "eq?", "car", "cdr", "cons", "null?", "pair?", "list", "print"]
    setze i auf 0
    solange i < länge(bs):
        env_define(e, bs[i], make_builtin(bs[i]))
        setze i auf i + 1
    gib_zurück e

funktion eval_quelle(quelle, env):
    setze exprs auf parse_alle(quelle)
    setze letzter auf make_nil()
    setze i auf 0
    solange i < länge(exprs):
        setze letzter auf lisp_eval(exprs[i], env)
        setze i auf i + 1
    gib_zurück letzter

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

funktion pruefe_lisp(name, quelle, erwartet_text):
    setze env auf standard_env()
    setze ergebnis auf eval_quelle(quelle, env)
    setze erg_text auf wert_zu_text(ergebnis)
    zaehler["gesamt"] = zaehler["gesamt"] + 1
    wenn erg_text == erwartet_text:
        zaehler["ok"] = zaehler["ok"] + 1
        zeige "  OK    " + name + " = " + erg_text
    sonst:
        zeige "  FAIL  " + name + " erwartet " + erwartet_text + " bekam " + erg_text

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo Mini-Lisp — Test-Suite"
zeige "================================================"
zeige ""

zeige "--- Arithmetik ---"
pruefe_lisp("(+ 1 2)", "(+ 1 2)", "3")
pruefe_lisp("(+ 1 2 3 4 5)", "(+ 1 2 3 4 5)", "15")
pruefe_lisp("(- 10 3)", "(- 10 3)", "7")
pruefe_lisp("(* 6 7)", "(* 6 7)", "42")
pruefe_lisp("(/ 100 5 2)", "(/ 100 5 2)", "10")
pruefe_lisp("Verschachtelt", "(+ (* 3 4) (- 10 5))", "17")

zeige ""
zeige "--- Special Forms ---"
pruefe_lisp("if wahr", "(if (= 1 1) 42 99)", "42")
pruefe_lisp("if falsch", "(if (= 1 2) 42 99)", "99")
pruefe_lisp("quote", "(quote (a b c))", "(a b c)")
pruefe_lisp("'quote abkuerzung", "'(1 2 3)", "(1 2 3)")

zeige ""
zeige "--- Define + Lambda ---"
pruefe_lisp("square", "(define square (lambda (x) (* x x))) (square 5)", "25")
pruefe_lisp("define mit sugar", "(define (double x) (* x 2)) (double 21)", "42")

zeige ""
zeige "--- Rekursion: factorial(10) ---"
setze src auf "(define (factorial n) (if (= n 0) 1 (* n (factorial (- n 1))))) (factorial 10)"
pruefe_lisp("factorial 10", src, "3628800")

zeige ""
zeige "--- Let ---"
pruefe_lisp("let", "(let ((a 1) (b 2)) (+ a b))", "3")
pruefe_lisp("let mit body", "(let ((x 5) (y 10)) (* x y))", "50")

zeige ""
zeige "--- Cons / Listen ---"
pruefe_lisp("cons", "(cons 1 (cons 2 (cons 3 '())))", "(1 2 3)")
pruefe_lisp("car", "(car '(1 2 3))", "1")
pruefe_lisp("cdr", "(cdr '(1 2 3))", "(2 3)")
pruefe_lisp("list builtin", "(list 1 2 3)", "(1 2 3)")
pruefe_lisp("null? leer", "(null? '())", "#t")
pruefe_lisp("null? nicht leer", "(null? '(1))", "#f")
pruefe_lisp("pair? paar", "(pair? '(1 2))", "#t")

zeige ""
zeige "--- Closures ---"
setze src auf "(define adder (lambda (x) (lambda (y) (+ x y)))) (define add3 (adder 3)) (add3 10)"
pruefe_lisp("closure capture", src, "13")

setze src auf "(define adder (lambda (x) (lambda (y) (+ x y)))) ((adder 5) 7)"
pruefe_lisp("direkter closure call", src, "12")

zeige ""
zeige "--- set! + Counter (Closure-State) ---"
setze src auf "(define counter ((lambda () (let ((n 0)) (lambda () (set! n (+ n 1)) n))))) (counter) (counter) (counter)"
pruefe_lisp("counter 3 mal", src, "3")

zeige ""
zeige "--- Hoehere Funktionen ---"
setze src auf "(define (lisp_apply-twice f x) (f (f x))) (lisp_apply-twice (lambda (n) (* n n)) 3)"
pruefe_lisp("lisp_apply-twice square 3", src, "81")

zeige ""
zeige "--- Map ueber Liste ---"
setze src auf "(define (map f xs) (if (null? xs) '() (cons (f (car xs)) (map f (cdr xs))))) (map (lambda (x) (* x x)) '(1 2 3 4 5))"
pruefe_lisp("map square", src, "(1 4 9 16 25)")

zeige ""
zeige "--- Fibonacci (n=10) ---"
setze src auf "(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (fib 10)"
pruefe_lisp("fib 10", src, "55")

zeige ""
zeige "--- eq? ---"
pruefe_lisp("eq? zahlen", "(eq? 1 1)", "#t")
pruefe_lisp("eq? sym", "(eq? 'foo 'foo)", "#t")
pruefe_lisp("eq? different", "(eq? 'foo 'bar)", "#f")

zeige ""
zeige "--- begin ---"
pruefe_lisp("begin", "(begin (+ 1 1) (+ 2 2) (+ 3 3))", "6")

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"
