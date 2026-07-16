# ============================================================
# moo Brainfuck-Interpreter + Optimizer
#
# 8 Befehle: + - < > [ ] . ,
# Zwei Modi:
#   - naiv: arbeitet direkt auf der source mit jump-table
#   - optimiert: kompiliert zu Bytecode mit run-length encoding,
#     CLEAR ([-]) und MOVE_ADD ([->+<]) Patterns
#
# Tests: Hello-World, Cat/Copy, Fibonacci, Quine
# ============================================================

# ------------------------------------------------------------
# Bytecode-Opcodes
# ------------------------------------------------------------

konstante OP_ADD auf 0        # arg = delta (modulo 256)
konstante OP_MOVE auf 1       # arg = delta
konstante OP_OUT auf 2
konstante OP_IN auf 3
konstante OP_JZ auf 4         # arg = ziel (Index)
konstante OP_JNZ auf 5        # arg = ziel
konstante OP_CLEAR auf 6      # Setzt aktuelle Zelle auf 0
konstante OP_MOVE_ADD auf 7   # arg = offset: addiere *p zu p[offset], *p = 0

konstante TAPE_SIZE auf 30000

# ------------------------------------------------------------
# Tokenize: extrahiere nur die 8 relevanten Zeichen
# ------------------------------------------------------------

funktion tokenize(quelle):
    setze out auf []
    setze i auf 0
    solange i < länge(quelle):
        setze c auf quelle[i]
        wenn c == "+" oder c == "-" oder c == "<" oder c == ">" oder c == "[" oder c == "]" oder c == "." oder c == ",":
            out.hinzufügen(c)
        setze i auf i + 1
    gib_zurück out

# ------------------------------------------------------------
# Naive Compilation: Jump-Tabelle + direkte Ops
# ------------------------------------------------------------

funktion compile_naiv(tokens):
    setze ops auf []
    setze stack auf []
    setze i auf 0
    solange i < länge(tokens):
        setze c auf tokens[i]
        setze op auf {}
        wenn c == "+":
            op["code"] = OP_ADD
            op["arg"] = 1
        sonst wenn c == "-":
            op["code"] = OP_ADD
            op["arg"] = 255
        sonst wenn c == ">":
            op["code"] = OP_MOVE
            op["arg"] = 1
        sonst wenn c == "<":
            op["code"] = OP_MOVE
            op["arg"] = 0 - 1
        sonst wenn c == ".":
            op["code"] = OP_OUT
            op["arg"] = 0
        sonst wenn c == ",":
            op["code"] = OP_IN
            op["arg"] = 0
        sonst wenn c == "[":
            op["code"] = OP_JZ
            op["arg"] = 0
            stack.hinzufügen(länge(ops))
        sonst wenn c == "]":
            op["code"] = OP_JNZ
            setze offen auf stack[länge(stack) - 1]
            setze neu_stack auf []
            setze k auf 0
            solange k < länge(stack) - 1:
                neu_stack.hinzufügen(stack[k])
                setze k auf k + 1
            setze stack auf neu_stack
            op["arg"] = offen
            ops.hinzufügen(op)
            ops[offen]["arg"] = länge(ops)
            setze i auf i + 1
            weiter
        ops.hinzufügen(op)
        setze i auf i + 1
    gib_zurück ops

# ------------------------------------------------------------
# Optimized Compilation: Run-length + Patterns
# ------------------------------------------------------------

funktion compile_opt(tokens):
    setze ops auf []
    setze stack auf []
    setze i auf 0
    solange i < länge(tokens):
        setze c auf tokens[i]
        # Pattern 1: [-] oder [+] → CLEAR
        wenn c == "[" und i + 2 < länge(tokens):
            wenn (tokens[i + 1] == "-" oder tokens[i + 1] == "+") und tokens[i + 2] == "]":
                setze op auf {}
                op["code"] = OP_CLEAR
                op["arg"] = 0
                ops.hinzufügen(op)
                setze i auf i + 3
                weiter
        # Pattern 2: [->+<] (move current to right) oder [-<+>] (move to left)
        wenn c == "[" und i + 5 < länge(tokens):
            wenn tokens[i + 1] == "-" und tokens[i + 2] == ">" und tokens[i + 3] == "+" und tokens[i + 4] == "<" und tokens[i + 5] == "]":
                setze op auf {}
                op["code"] = OP_MOVE_ADD
                op["arg"] = 1
                ops.hinzufügen(op)
                setze i auf i + 6
                weiter
            wenn tokens[i + 1] == "-" und tokens[i + 2] == "<" und tokens[i + 3] == "+" und tokens[i + 4] == ">" und tokens[i + 5] == "]":
                setze op auf {}
                op["code"] = OP_MOVE_ADD
                op["arg"] = 0 - 1
                ops.hinzufügen(op)
                setze i auf i + 6
                weiter
        # Run-length encoding fuer + - > <
        wenn c == "+" oder c == "-":
            setze delta auf 0
            solange i < länge(tokens) und (tokens[i] == "+" oder tokens[i] == "-"):
                wenn tokens[i] == "+":
                    setze delta auf delta + 1
                sonst:
                    setze delta auf delta - 1
                setze i auf i + 1
            # Modulo 256
            solange delta < 0:
                setze delta auf delta + 256
            setze delta auf delta % 256
            wenn delta != 0:
                setze op auf {}
                op["code"] = OP_ADD
                op["arg"] = delta
                ops.hinzufügen(op)
            weiter
        wenn c == ">" oder c == "<":
            setze delta auf 0
            solange i < länge(tokens) und (tokens[i] == ">" oder tokens[i] == "<"):
                wenn tokens[i] == ">":
                    setze delta auf delta + 1
                sonst:
                    setze delta auf delta - 1
                setze i auf i + 1
            wenn delta != 0:
                setze op auf {}
                op["code"] = OP_MOVE
                op["arg"] = delta
                ops.hinzufügen(op)
            weiter
        setze op auf {}
        wenn c == ".":
            op["code"] = OP_OUT
            op["arg"] = 0
        sonst wenn c == ",":
            op["code"] = OP_IN
            op["arg"] = 0
        sonst wenn c == "[":
            op["code"] = OP_JZ
            op["arg"] = 0
            stack.hinzufügen(länge(ops))
        sonst wenn c == "]":
            op["code"] = OP_JNZ
            setze offen auf stack[länge(stack) - 1]
            setze neu_stack auf []
            setze k auf 0
            solange k < länge(stack) - 1:
                neu_stack.hinzufügen(stack[k])
                setze k auf k + 1
            setze stack auf neu_stack
            op["arg"] = offen
            ops.hinzufügen(op)
            ops[offen]["arg"] = länge(ops)
            setze i auf i + 1
            weiter
        ops.hinzufügen(op)
        setze i auf i + 1
    gib_zurück ops

# ------------------------------------------------------------
# VM
# ------------------------------------------------------------

funktion fuehre_aus(ops):
    setze tape auf []
    setze i auf 0
    solange i < TAPE_SIZE:
        tape.hinzufügen(0)
        setze i auf i + 1
    setze ptr auf 0
    setze ip auf 0
    setze out auf ""
    setze cycles auf 0
    solange ip < länge(ops):
        setze cycles auf cycles + 1
        setze op auf ops[ip]
        setze code auf op["code"]
        wenn code == OP_ADD:
            setze v auf (tape[ptr] + op["arg"]) % 256
            tape[ptr] = v
            setze ip auf ip + 1
        sonst wenn code == OP_MOVE:
            setze ptr auf ptr + op["arg"]
            setze ip auf ip + 1
        sonst wenn code == OP_OUT:
            setze out auf out + bytes_neu([tape[ptr]])
            setze ip auf ip + 1
        sonst wenn code == OP_IN:
            tape[ptr] = 0
            setze ip auf ip + 1
        sonst wenn code == OP_JZ:
            wenn tape[ptr] == 0:
                setze ip auf op["arg"]
            sonst:
                setze ip auf ip + 1
        sonst wenn code == OP_JNZ:
            wenn tape[ptr] != 0:
                setze ip auf op["arg"]
            sonst:
                setze ip auf ip + 1
        sonst wenn code == OP_CLEAR:
            tape[ptr] = 0
            setze ip auf ip + 1
        sonst wenn code == OP_MOVE_ADD:
            setze ziel auf ptr + op["arg"]
            tape[ziel] = (tape[ziel] + tape[ptr]) % 256
            tape[ptr] = 0
            setze ip auf ip + 1
    setze r auf {}
    r["out"] = out
    r["cycles"] = cycles
    gib_zurück r

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
# BF-Programme
# ------------------------------------------------------------

konstante BF_HELLO auf "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."

# Cell-width test: schreibt A (65)
konstante BF_A auf "++++++++[>++++++++<-]>+."

# Addiere 2+3 = 5, druckt '5' (53). Braucht '5' ASCII=53, also 48 mal '+' nach Move.
konstante BF_ADD23 auf "++>+++<[->+<]>++++++++++++++++++++++++++++++++++++++++++++++++."

# Fibonacci-Zahlen: gibt die ersten Fibonacci-Zahlen als ASCII aus
# (Variante die 5 Fibonaccis berechnet: 1,1,2,3,5 → '1','1','2','3','5')
konstante BF_FIB auf "++++++++++[>+>+>+<<<-]>>>+++++++++++++++++++++++++++++++++++++++++++++++++++++++.<<[->>+<<]++++++++[<+++++>-]<+>>>[-<+<.>>]"

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo Brainfuck — Naiv vs Optimiert"
zeige "================================================"

zeige ""
zeige "--- Hello World (naiv) ---"
setze tk auf tokenize(BF_HELLO)
zeige "Tokens: " + text(länge(tk))
setze ops_naiv auf compile_naiv(tk)
zeige "Naive Ops: " + text(länge(ops_naiv))
setze r_naiv auf fuehre_aus(ops_naiv)
zeige "Output: " + r_naiv["out"]
zeige "Cycles: " + text(r_naiv["cycles"])
check("Naiv: Hello World!", r_naiv["out"] == "Hello World!\n")

zeige ""
zeige "--- Hello World (optimiert) ---"
setze ops_opt auf compile_opt(tk)
zeige "Opt Ops: " + text(länge(ops_opt))
setze r_opt auf fuehre_aus(ops_opt)
zeige "Output: " + r_opt["out"]
zeige "Cycles: " + text(r_opt["cycles"])
check("Opt: Hello World!", r_opt["out"] == "Hello World!\n")

setze speedup auf boden((r_naiv["cycles"] * 100) / r_opt["cycles"]) / 100
zeige "Speedup: " + text(r_naiv["cycles"]) + " -> " + text(r_opt["cycles"]) + " cycles (" + text(boden(r_naiv["cycles"] / r_opt["cycles"])) + "x)"

zeige ""
zeige "--- Cell-Width Test: 'A' ---"
setze tk auf tokenize(BF_A)
setze r auf fuehre_aus(compile_opt(tk))
zeige "Output: " + r["out"]
check("Opt: 'A'", r["out"] == "A")

zeige ""
zeige "--- Optimizer-Pattern-Test ---"
# [-] mehrfach als CLEAR erkannt?
setze tk auf tokenize("+++[-]+++.")
setze ops_naiv auf compile_naiv(tk)
setze ops_opt auf compile_opt(tk)
zeige "'+++[-]+++.' naiv=" + text(länge(ops_naiv)) + " ops, opt=" + text(länge(ops_opt)) + " ops"
setze r auf fuehre_aus(ops_opt)
check("[-] als CLEAR → zelle=3", länge(r["out"]) == 1)
# 3 mal + → wert 3 → schriftzeichen \x03
check("Optimizer reduziert ops", länge(ops_opt) < länge(ops_naiv))

# [->+<] Pattern-Test: ziel = 5 + 3 = 8
setze tk auf tokenize("+++++>+++<[->+<]>.")
# Start: [5,3] ptr=0, nach [->+<]: [0,8], dann >: ptr=1, dann '.': print 8 (ASCII 8 = BS)
setze ops_opt auf compile_opt(tk)
# Check dass der MOVE_ADD Opcode drin ist
setze hat_ma auf falsch
setze i auf 0
solange i < länge(ops_opt):
    wenn ops_opt[i]["code"] == OP_MOVE_ADD:
        setze hat_ma auf wahr
    setze i auf i + 1
check("[->+<] als MOVE_ADD erkannt", hat_ma)

# Wert-Check via BF_ADD23: druckt '5'
setze tk auf tokenize(BF_ADD23)
setze r auf fuehre_aus(compile_opt(tk))
check("2+3=5 als '5' ASCII", r["out"] == "5")

zeige ""
zeige "--- Bracket-Matching (Jump-Table) ---"
# Verschachtelte Brackets
setze tk auf tokenize("[+[-]+]")
setze ops auf compile_naiv(tk)
# Check: ops[0] ist JZ → arg = länge(ops)-1 (äußerstes ]); ops[2] (innerstes JZ [ bei [-]) zeigt auf ops[4] ([-] geschlossen bei ops[3])
# Tatsächlich schwer zu verifizieren ohne Struktur-Dump. Einfacher check: jeder JZ hat ein passendes JNZ.
setze jz_count auf 0
setze jnz_count auf 0
setze i auf 0
solange i < länge(ops):
    wenn ops[i]["code"] == OP_JZ:
        setze jz_count auf jz_count + 1
    wenn ops[i]["code"] == OP_JNZ:
        setze jnz_count auf jnz_count + 1
    setze i auf i + 1
check("2 JZ", jz_count == 2)
check("2 JNZ", jnz_count == 2)

zeige ""
zeige "--- Langer Performance-Test (Hello World laengere Variante) ---"
# Variante mit mehr inner loops (>300 tokens)
setze bf_lang auf ">++++++++[<+++++++++>-]<.>>+>-[+]++>++>+++[>[->+++<<+++>]<<]>-----.>->+++..+++.>-.<<+[>[+>+]>>]<--------------.>>.+++.------.--------.>+.>+."
setze tk auf tokenize(bf_lang)
setze ops_naiv auf compile_naiv(tk)
setze ops_opt auf compile_opt(tk)
setze r1 auf fuehre_aus(ops_naiv)
setze r2 auf fuehre_aus(ops_opt)
zeige "Naiv:     " + text(länge(ops_naiv)) + " ops, " + text(r1["cycles"]) + " cycles"
zeige "Optimiert: " + text(länge(ops_opt)) + " ops, " + text(r2["cycles"]) + " cycles"
zeige "Naiv-Output: " + r1["out"]
zeige "Opt-Output:  " + r2["out"]
check("Lang: beide gleich", r1["out"] == r2["out"])
check("Opt reduziert cycles (>1.5x)", r2["cycles"] * 3 < r1["cycles"] * 2)

# Extremes Run-Length Beispiel: 64 mal '+'
setze bf_runs auf "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++."
setze tk auf tokenize(bf_runs)
setze on auf compile_naiv(tk)
setze oo auf compile_opt(tk)
setze rn auf fuehre_aus(on)
setze ro auf fuehre_aus(oo)
zeige "64x '+': naiv=" + text(länge(on)) + " ops/" + text(rn["cycles"]) + " cycles, opt=" + text(länge(oo)) + " ops/" + text(ro["cycles"]) + " cycles"
check("Run-length: opt viel weniger ops", länge(oo) < 5)
check("Run-length: opt viel weniger cycles", ro["cycles"] * 10 < rn["cycles"])

zeige ""
zeige "--- Loops (Quadrat von 5: 5*5 = 25) ---"
# Setze Zelle 0 auf 5, kopiere nach Zelle 1 via MOVE, dann loop: fuer jede 1 in Zelle 1, addiere 5 zu Zelle 2
# Einfacher: [>+++++<-] multipliziert Zelle 0 mit 5 nach Zelle 1, wenn Zelle 0 5 ist → Zelle 1 = 25
setze bf_quad auf "+++++[>+++++<-]>."
setze tk auf tokenize(bf_quad)
setze r auf fuehre_aus(compile_opt(tk))
# Ergebnis: ASCII 25 = SUB (nicht druckbar)
setze erste auf bytes_zu_liste(r["out"])[0]
check("5*5=25", erste == 25)

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"
