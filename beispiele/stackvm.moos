# ============================================================
# moo Stack-VM — Bytecode Interpreter + Compiler
#
# Kompilieren: moo-compiler compile stackvm.moo -o stackvm
# Starten:     ./stackvm
#
# Sprache: Mini-Lisp S-Expressions
#   (fun name (args) body)
#   (if cond then else) / (while cond body) / (begin e1 e2 ...)
#   (set name value)    / (print expr)
#   Bin-Ops: + - * / < > =
#   (name args...) → Funktionsaufruf
#
# VM-Opcodes (Byte):
#   0x01 PUSH_INT (4 bytes BE signed)
#   0x02 POP
#   0x03 ADD 0x04 SUB 0x05 MUL 0x06 DIV
#   0x07 LT  0x08 GT  0x09 EQ
#   0x0A LOAD  i   (1 byte slot)
#   0x0B STORE i   (1 byte slot)
#   0x0C JMP  off16 (signed)
#   0x0D JMPZ off16
#   0x0E CALL addr32 + argc byte
#   0x0F RET
#   0x10 PRINT
#   0x11 HALT
# ============================================================

# ============================================================
# 1) LEXER
# ============================================================
funktion lex(src):
    setze tokens auf []
    setze i auf 0
    setze n auf länge(src)
    solange i < n:
        setze c auf src[i]
        wenn c == " " oder c == "\n" oder c == "\t":
            setze i auf i + 1
        sonst:
            wenn c == "(":
                tokens.hinzufügen("(")
                setze i auf i + 1
            sonst:
                wenn c == ")":
                    tokens.hinzufügen(")")
                    setze i auf i + 1
                sonst:
                    wenn c == ";":
                        # Kommentar bis Zeilenende
                        solange i < n und src[i] != "\n":
                            setze i auf i + 1
                    sonst:
                        # Atom (Zahl oder Symbol)
                        setze atom auf ""
                        solange i < n und src[i] != " " und src[i] != "(" und src[i] != ")" und src[i] != "\n" und src[i] != "\t":
                            setze atom auf atom + src[i]
                            setze i auf i + 1
                        tokens.hinzufügen(atom)
    gib_zurück tokens

# ============================================================
# 2) PARSER: Liste von Tokens → verschachtelter AST
# ============================================================
# Ein Node ist entweder
#   {"typ": "num", "v": N}
#   {"typ": "sym", "v": name}
#   {"typ": "list", "v": [child, child, ...]}

funktion ist_zahl(s):
    setze i auf 0
    setze n auf länge(s)
    wenn n == 0:
        gib_zurück falsch
    wenn s[0] == "-":
        wenn n == 1:
            gib_zurück falsch
        setze i auf 1
    solange i < n:
        wenn s[i] < "0" oder s[i] > "9":
            gib_zurück falsch
        setze i auf i + 1
    gib_zurück wahr

funktion str_zu_int(s):
    setze neg auf falsch
    setze i auf 0
    wenn s[0] == "-":
        setze neg auf wahr
        setze i auf 1
    setze r auf 0
    solange i < länge(s):
        setze r auf r * 10 + (bytes_zu_liste(s[i])[0] - 48)
        setze i auf i + 1
    wenn neg:
        gib_zurück 0 - r
    gib_zurück r

# Pos-Parser-State als Dict, damit wir mit dem Index arbeiten
funktion parse_one(tokens, state):
    setze pos auf state["pos"]
    wenn pos >= tokens.länge():
        wirf "Unerwartetes Ende"
    setze tk auf tokens[pos]
    state["pos"] = pos + 1
    wenn tk == "(":
        setze items auf []
        solange state["pos"] < tokens.länge() und tokens[state["pos"]] != ")":
            items.hinzufügen(parse_one(tokens, state))
        wenn state["pos"] >= tokens.länge():
            wirf "Fehlende )"
        state["pos"] = state["pos"] + 1
        setze n auf {}
        n["typ"] = "list"
        n["v"] = items
        gib_zurück n
    wenn ist_zahl(tk):
        setze n auf {}
        n["typ"] = "num"
        n["v"] = str_zu_int(tk)
        gib_zurück n
    setze n auf {}
    n["typ"] = "sym"
    n["v"] = tk
    gib_zurück n

funktion parse(src):
    setze tokens auf lex(src)
    setze state auf {}
    state["pos"] = 0
    setze roots auf []
    solange state["pos"] < tokens.länge():
        roots.hinzufügen(parse_one(tokens, state))
    gib_zurück roots

# ============================================================
# 3) CODEGEN: AST → Bytecode
# ============================================================
# Wir haben eine flache Instruction-Liste. Fuer Jumps patchen
# wir Offsets nach dem Emit ein.
#
# Symbol-Table: {"globals": {name → slot}, "funcs": {name → {addr, params, locals}}}
# Pro Funktion eigene local-Map.

funktion new_ctx():
    setze c auf {}
    c["code"] = []
    c["funcs"] = {}
    c["func_fixups"] = []   # Liste {pos, name} — CALL-Addressen zu patchen
    gib_zurück c

funktion emit_byte(ctx, b):
    ctx["code"].hinzufügen(b)

funktion emit_u32be(ctx, v):
    setze x auf v
    wenn x < 0:
        setze x auf x + 4294967296
    emit_byte(ctx, boden(x / 16777216) % 256)
    emit_byte(ctx, boden(x / 65536) % 256)
    emit_byte(ctx, boden(x / 256) % 256)
    emit_byte(ctx, x % 256)

funktion emit_s16be(ctx, v):
    setze x auf v
    wenn x < 0:
        setze x auf x + 65536
    emit_byte(ctx, boden(x / 256) % 256)
    emit_byte(ctx, x % 256)

funktion patch_s16_at(ctx, pos, v):
    setze x auf v
    wenn x < 0:
        setze x auf x + 65536
    ctx["code"][pos] = boden(x / 256) % 256
    ctx["code"][pos + 1] = x % 256

funktion patch_u32_at(ctx, pos, v):
    setze x auf v
    wenn x < 0:
        setze x auf x + 4294967296
    ctx["code"][pos] = boden(x / 16777216) % 256
    ctx["code"][pos + 1] = boden(x / 65536) % 256
    ctx["code"][pos + 2] = boden(x / 256) % 256
    ctx["code"][pos + 3] = x % 256

# Lookup oder neuer local-slot
funktion slot_fuer(locals, name):
    wenn locals.enthält(name):
        gib_zurück locals[name]
    setze idx auf locals.länge()
    locals[name] = idx
    gib_zurück idx

# Compile eines Ausdrucks (erzeugt einen Wert auf dem Stack)
funktion compile_expr(ctx, node, locals):
    wenn node["typ"] == "num":
        emit_byte(ctx, 0x01)
        emit_u32be(ctx, node["v"])
        gib_zurück nichts
    wenn node["typ"] == "sym":
        setze nm auf node["v"]
        setze s auf slot_fuer(locals, nm)
        emit_byte(ctx, 0x0A)
        emit_byte(ctx, s)
        gib_zurück nichts
    # list
    setze kinder auf node["v"]
    wenn kinder.länge() == 0:
        emit_byte(ctx, 0x01)
        emit_u32be(ctx, 0)
        gib_zurück nichts
    setze head auf kinder[0]
    wenn head["typ"] != "sym":
        wirf "Funktionsaufruf-Head muss Symbol sein"
    setze op auf head["v"]

    wenn op == "+" oder op == "-" oder op == "*" oder op == "/" oder op == "<" oder op == ">" oder op == "=":
        compile_expr(ctx, kinder[1], locals)
        compile_expr(ctx, kinder[2], locals)
        wenn op == "+":
            emit_byte(ctx, 0x03)
        wenn op == "-":
            emit_byte(ctx, 0x04)
        wenn op == "*":
            emit_byte(ctx, 0x05)
        wenn op == "/":
            emit_byte(ctx, 0x06)
        wenn op == "<":
            emit_byte(ctx, 0x07)
        wenn op == ">":
            emit_byte(ctx, 0x08)
        wenn op == "=":
            emit_byte(ctx, 0x09)
        gib_zurück nichts

    wenn op == "set":
        setze name auf kinder[1]["v"]
        setze s auf slot_fuer(locals, name)
        compile_expr(ctx, kinder[2], locals)
        emit_byte(ctx, 0x0B)
        emit_byte(ctx, s)
        # set produziert keinen Stackwert → pushe 0 zurueck
        emit_byte(ctx, 0x01)
        emit_u32be(ctx, 0)
        gib_zurück nichts

    wenn op == "print":
        compile_expr(ctx, kinder[1], locals)
        emit_byte(ctx, 0x10)
        emit_byte(ctx, 0x01)
        emit_u32be(ctx, 0)
        gib_zurück nichts

    wenn op == "if":
        compile_expr(ctx, kinder[1], locals)
        emit_byte(ctx, 0x0D)
        setze jmpz_pos auf ctx["code"].länge()
        emit_s16be(ctx, 0)
        compile_expr(ctx, kinder[2], locals)
        emit_byte(ctx, 0x0C)
        setze jmp_pos auf ctx["code"].länge()
        emit_s16be(ctx, 0)
        setze else_start auf ctx["code"].länge()
        patch_s16_at(ctx, jmpz_pos, else_start - jmpz_pos - 2)
        compile_expr(ctx, kinder[3], locals)
        setze nach_else auf ctx["code"].länge()
        patch_s16_at(ctx, jmp_pos, nach_else - jmp_pos - 2)
        gib_zurück nichts

    wenn op == "while":
        setze loop_start auf ctx["code"].länge()
        compile_expr(ctx, kinder[1], locals)
        emit_byte(ctx, 0x0D)
        setze jmpz_pos auf ctx["code"].länge()
        emit_s16be(ctx, 0)
        compile_expr(ctx, kinder[2], locals)
        emit_byte(ctx, 0x02)  # POP result
        emit_byte(ctx, 0x0C)
        setze jmp_pos auf ctx["code"].länge()
        emit_s16be(ctx, 0)
        setze nach_loop auf ctx["code"].länge()
        patch_s16_at(ctx, jmpz_pos, nach_loop - jmpz_pos - 2)
        patch_s16_at(ctx, jmp_pos, loop_start - jmp_pos - 2)
        # while produziert 0
        emit_byte(ctx, 0x01)
        emit_u32be(ctx, 0)
        gib_zurück nichts

    wenn op == "begin":
        setze i auf 1
        setze m auf kinder.länge()
        solange i < m:
            compile_expr(ctx, kinder[i], locals)
            wenn i + 1 < m:
                emit_byte(ctx, 0x02)  # pop alle ausser letztem
            setze i auf i + 1
        gib_zurück nichts

    # Funktionsaufruf
    # Push args in Reihenfolge, dann CALL
    setze i auf 1
    solange i < kinder.länge():
        compile_expr(ctx, kinder[i], locals)
        setze i auf i + 1
    setze argc auf kinder.länge() - 1
    emit_byte(ctx, 0x0E)
    setze call_pos auf ctx["code"].länge()
    emit_u32be(ctx, 0)            # platzhalter fuer addr
    emit_byte(ctx, argc)
    # Funktions-Name merken, patchen nach dem Gesamt-Compile
    setze fx auf {}
    fx["pos"] = call_pos
    fx["name"] = op
    ctx["func_fixups"].hinzufügen(fx)

# Compile eine Funktions-Definition
funktion compile_fun(ctx, node):
    setze name auf node["v"][1]["v"]
    setze params auf node["v"][2]["v"]
    setze body auf node["v"][3]

    setze locals auf {}
    # Parameter landen in den ersten Slots. VM schreibt die
    # Argumente vom Stack in Reihenfolge in locals 0..argc-1
    setze i auf 0
    solange i < params.länge():
        slot_fuer(locals, params[i]["v"])
        setze i auf i + 1

    setze start auf ctx["code"].länge()
    setze f auf {}
    f["addr"] = start
    f["params"] = params.länge()
    ctx["funcs"][name] = f

    compile_expr(ctx, body, locals)
    emit_byte(ctx, 0x0F)  # RET

funktion compile_programm(src):
    setze ctx auf new_ctx()
    setze trees auf parse(src)

    # Zuerst alle Funktions-Defs sammeln (muessen vor main liegen).
    # Main-Code (alles ausser fun-Defs) wird ans Ende geschrieben
    # und vor dem Haupt-Code via JMP-Fix eingesprungen.
    # Wir schreiben: JMP main | func1 | func2 | ... | HALT_marker | main_code | HALT
    emit_byte(ctx, 0x0C)
    setze main_jmp_pos auf ctx["code"].länge()
    emit_s16be(ctx, 0)  # wird gepatcht

    setze main_trees auf []
    setze i auf 0
    solange i < trees.länge():
        setze t auf trees[i]
        wenn t["typ"] == "list" und t["v"].länge() > 0 und t["v"][0]["typ"] == "sym" und t["v"][0]["v"] == "fun":
            compile_fun(ctx, t)
        sonst:
            main_trees.hinzufügen(t)
        setze i auf i + 1

    setze main_start auf ctx["code"].länge()
    patch_s16_at(ctx, main_jmp_pos, main_start - main_jmp_pos - 2)

    setze main_locals auf {}
    setze i auf 0
    solange i < main_trees.länge():
        compile_expr(ctx, main_trees[i], main_locals)
        emit_byte(ctx, 0x02)  # pop jedes top-level result
        setze i auf i + 1

    emit_byte(ctx, 0x11)  # HALT

    # Fixups fuer CALLs
    setze i auf 0
    solange i < ctx["func_fixups"].länge():
        setze fx auf ctx["func_fixups"][i]
        setze name auf fx["name"]
        wenn nicht ctx["funcs"].enthält(name):
            wirf "Unbekannte Funktion: " + name
        setze addr auf ctx["funcs"][name]["addr"]
        patch_u32_at(ctx, fx["pos"], addr)
        setze i auf i + 1

    gib_zurück ctx["code"]

# ============================================================
# 4) VM
# ============================================================

funktion s32(b0, b1, b2, b3):
    setze v auf b0 * 16777216 + b1 * 65536 + b2 * 256 + b3
    wenn v >= 2147483648:
        setze v auf v - 4294967296
    gib_zurück v

funktion s16(b0, b1):
    setze v auf b0 * 256 + b1
    wenn v >= 32768:
        setze v auf v - 65536
    gib_zurück v

funktion vm_run(code):
    setze stack auf []
    setze call_stack auf []  # Liste von {ret_ip, locals, locals_count_alt}
    setze locals auf []
    setze ip auf 0
    setze steps auf 0

    solange wahr:
        setze steps auf steps + 1
        setze op auf code[ip]
        setze ip auf ip + 1

        wenn op == 0x01:  # PUSH_INT
            setze v auf s32(code[ip], code[ip + 1], code[ip + 2], code[ip + 3])
            setze ip auf ip + 4
            stack.hinzufügen(v)
        wenn op == 0x02:  # POP
            stack.pop()
        wenn op == 0x03:  # ADD
            setze b auf stack.pop()
            setze a auf stack.pop()
            stack.hinzufügen(a + b)
        wenn op == 0x04:  # SUB
            setze b auf stack.pop()
            setze a auf stack.pop()
            stack.hinzufügen(a - b)
        wenn op == 0x05:  # MUL
            setze b auf stack.pop()
            setze a auf stack.pop()
            stack.hinzufügen(a * b)
        wenn op == 0x06:  # DIV
            setze b auf stack.pop()
            setze a auf stack.pop()
            stack.hinzufügen(boden(a / b))
        wenn op == 0x07:  # LT
            setze b auf stack.pop()
            setze a auf stack.pop()
            wenn a < b:
                stack.hinzufügen(1)
            sonst:
                stack.hinzufügen(0)
        wenn op == 0x08:  # GT
            setze b auf stack.pop()
            setze a auf stack.pop()
            wenn a > b:
                stack.hinzufügen(1)
            sonst:
                stack.hinzufügen(0)
        wenn op == 0x09:  # EQ
            setze b auf stack.pop()
            setze a auf stack.pop()
            wenn a == b:
                stack.hinzufügen(1)
            sonst:
                stack.hinzufügen(0)
        wenn op == 0x0A:  # LOAD
            setze slot auf code[ip]
            setze ip auf ip + 1
            solange locals.länge() <= slot:
                locals.hinzufügen(0)
            stack.hinzufügen(locals[slot])
        wenn op == 0x0B:  # STORE
            setze slot auf code[ip]
            setze ip auf ip + 1
            solange locals.länge() <= slot:
                locals.hinzufügen(0)
            locals[slot] = stack.pop()
        wenn op == 0x0C:  # JMP
            setze off auf s16(code[ip], code[ip + 1])
            setze ip auf ip + 2 + off
        wenn op == 0x0D:  # JMPZ
            setze off auf s16(code[ip], code[ip + 1])
            setze ip auf ip + 2
            setze v auf stack.pop()
            wenn v == 0:
                setze ip auf ip + off
        wenn op == 0x0E:  # CALL
            setze addr auf s32(code[ip], code[ip + 1], code[ip + 2], code[ip + 3])
            setze argc auf code[ip + 4]
            setze ip auf ip + 5
            # Frame bauen: neue locals mit argc Slots
            setze neue_locals auf []
            setze k auf 0
            solange k < argc:
                neue_locals.hinzufügen(0)
                setze k auf k + 1
            setze k auf argc - 1
            solange k >= 0:
                neue_locals[k] = stack.pop()
                setze k auf k - 1
            setze frame auf {}
            frame["ret_ip"] = ip
            frame["prev_locals"] = locals
            call_stack.hinzufügen(frame)
            setze locals auf neue_locals
            setze ip auf addr
        wenn op == 0x0F:  # RET
            # Rueckgabewert bleibt oben auf dem Stack
            wenn call_stack.länge() == 0:
                gib_zurück stack
            setze frame auf call_stack.pop()
            setze ip auf frame["ret_ip"]
            setze locals auf frame["prev_locals"]
        wenn op == 0x10:  # PRINT
            setze v auf stack.pop()
            zeige "  [vm] " + text(v)
            # Print liefert nichts zurueck → Stack unveraendert (der 0 push)
        wenn op == 0x11:  # HALT
            gib_zurück stack

# ============================================================
# 5) Tests
# ============================================================
funktion run_programm(quelle, label):
    zeige ""
    zeige "--- " + label + " ---"
    setze code auf compile_programm(quelle)
    zeige "  Bytecode: " + text(code.länge()) + " bytes"
    setze stack auf vm_run(code)
    wenn stack.länge() > 0:
        zeige "  Stack-Top: " + text(stack[stack.länge() - 1])

zeige "=== moo Stack-VM ==="

run_programm("(print (+ 1 (+ 2 3)))", "Test 1: 1 + 2 + 3")

setze fact_src auf "(fun fact (n) (if (< n 2) 1 (* n (fact (- n 1))))) (print (fact 10))"
run_programm(fact_src, "Test 2: fact(10)")

setze fib_src auf "(fun fib (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (print (fib 20))"
run_programm(fib_src, "Test 3: fib(20)")

setze loop_src auf "(set i 0) (while (< i 5) (set i (+ i 1))) (print i)"
run_programm(loop_src, "Test 4a: simple while")

setze loop_src_b auf "(set i 0) (set s 0) (while (< i 11) (begin (print i) (set s (+ s i)) (set i (+ i 1)))) (print s)"
run_programm(loop_src_b, "Test 4b trace: sum 0..10")

setze deep_src auf "(fun depth (n) (if (= n 0) 42 (depth (- n 1)))) (print (depth 200))"
run_programm(deep_src, "Test 5: recursion depth 200")

zeige ""
zeige "=== Fertig ==="
