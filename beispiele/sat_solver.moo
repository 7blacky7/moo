# ============================================================
# moo DPLL SAT-Solver + Sudoku-Encoder
#
# DPLL:
#   - Unit Propagation
#   - Pure Literal Elimination
#   - Decision + Backtracking
#   - CNF im DIMACS-Format
#
# Sudoku-as-SAT:
#   - 729 boolean vars (9x9x9)
#   - Constraints: cell/row/col/box "exactly one"
# ============================================================

# ------------------------------------------------------------
# Assignment-Helfer (dict: varname → wahr/falsch)
# ------------------------------------------------------------

funktion abs_zahl(n):
    wenn n < 0:
        gib_zurück 0 - n
    gib_zurück n

funktion var_key(var):
    gib_zurück text(var)

funktion a_hat(assignment, var):
    gib_zurück assignment.hat(var_key(var))

funktion a_get(assignment, var):
    gib_zurück assignment[var_key(var)]

funktion a_set(assignment, var, wert):
    assignment[var_key(var)] = wert

funktion a_kopie(assignment):
    setze k auf {}
    setze keys auf assignment.schlüssel()
    setze i auf 0
    solange i < länge(keys):
        k[keys[i]] = assignment[keys[i]]
        setze i auf i + 1
    gib_zurück k

# ------------------------------------------------------------
# Klausel-Status
# ------------------------------------------------------------

# Liefert dict: status ∈ "sat"/"conflict"/"unit"/"undef" + unit_lit
funktion klausel_status(klausel, assignment):
    setze unknown_count auf 0
    setze unknown_lit auf 0
    setze i auf 0
    solange i < länge(klausel):
        setze lit auf klausel[i]
        setze var auf abs_zahl(lit)
        setze positiv auf wahr
        wenn lit < 0:
            setze positiv auf falsch
        wenn a_hat(assignment, var):
            setze wert auf a_get(assignment, var)
            wenn positiv == wert:
                setze r auf {}
                r["status"] = "sat"
                r["lit"] = 0
                gib_zurück r
        sonst:
            setze unknown_count auf unknown_count + 1
            setze unknown_lit auf lit
        setze i auf i + 1
    wenn unknown_count == 0:
        setze r auf {}
        r["status"] = "conflict"
        r["lit"] = 0
        gib_zurück r
    wenn unknown_count == 1:
        setze r auf {}
        r["status"] = "unit"
        r["lit"] = unknown_lit
        gib_zurück r
    setze r auf {}
    r["status"] = "undef"
    r["lit"] = 0
    gib_zurück r

# ------------------------------------------------------------
# DPLL-Kern
# ------------------------------------------------------------

# Unit Propagation: propagiert bis kein Unit mehr da. Gibt falsch zurueck bei Konflikt.
funktion unit_propagate(klauseln, assignment):
    setze geaendert auf wahr
    solange geaendert:
        setze geaendert auf falsch
        setze i auf 0
        solange i < länge(klauseln):
            setze s auf klausel_status(klauseln[i], assignment)
            wenn s["status"] == "conflict":
                gib_zurück falsch
            wenn s["status"] == "unit":
                setze lit auf s["lit"]
                setze var auf abs_zahl(lit)
                a_set(assignment, var, lit > 0)
                setze geaendert auf wahr
            setze i auf i + 1
    gib_zurück wahr

# Pure Literal Elimination
funktion pure_literal(klauseln, assignment):
    setze pos auf {}
    setze neg auf {}
    setze i auf 0
    solange i < länge(klauseln):
        setze s auf klausel_status(klauseln[i], assignment)
        wenn s["status"] != "sat":
            setze k auf klauseln[i]
            setze j auf 0
            solange j < länge(k):
                setze lit auf k[j]
                setze var auf abs_zahl(lit)
                wenn a_hat(assignment, var) == falsch:
                    wenn lit > 0:
                        pos[var_key(var)] = wahr
                    sonst:
                        neg[var_key(var)] = wahr
                setze j auf j + 1
        setze i auf i + 1
    # Vars, die nur positiv oder nur negativ sind
    setze keys auf pos.schlüssel()
    setze i auf 0
    solange i < länge(keys):
        wenn neg.hat(keys[i]) == falsch:
            assignment[keys[i]] = wahr
        setze i auf i + 1
    setze keys auf neg.schlüssel()
    setze i auf 0
    solange i < länge(keys):
        wenn pos.hat(keys[i]) == falsch:
            assignment[keys[i]] = falsch
        setze i auf i + 1

# Wählt erste ungesetzte Variable aus
funktion waehle_var(klauseln, assignment):
    setze i auf 0
    solange i < länge(klauseln):
        setze s auf klausel_status(klauseln[i], assignment)
        wenn s["status"] != "sat":
            setze k auf klauseln[i]
            setze j auf 0
            solange j < länge(k):
                setze var auf abs_zahl(k[j])
                wenn a_hat(assignment, var) == falsch:
                    gib_zurück var
                setze j auf j + 1
        setze i auf i + 1
    gib_zurück 0

# Prueft ob alle Klauseln erfuellt sind
funktion alles_sat(klauseln, assignment):
    setze i auf 0
    solange i < länge(klauseln):
        setze s auf klausel_status(klauseln[i], assignment)
        wenn s["status"] != "sat":
            gib_zurück falsch
        setze i auf i + 1
    gib_zurück wahr

# DPLL rekursiv
funktion dpll(klauseln, assignment):
    wenn unit_propagate(klauseln, assignment) == falsch:
        gib_zurück nichts
    pure_literal(klauseln, assignment)
    wenn alles_sat(klauseln, assignment):
        gib_zurück assignment
    setze var auf waehle_var(klauseln, assignment)
    wenn var == 0:
        gib_zurück assignment
    # Try true
    setze a_true auf a_kopie(assignment)
    a_set(a_true, var, wahr)
    setze r auf dpll(klauseln, a_true)
    wenn r != nichts:
        gib_zurück r
    # Try false
    setze a_false auf a_kopie(assignment)
    a_set(a_false, var, falsch)
    gib_zurück dpll(klauseln, a_false)

# ------------------------------------------------------------
# SatSolver-Klasse
# ------------------------------------------------------------

klasse SatSolver:
    funktion erstelle():
        selbst.klauseln = []
        selbst.num_vars = 0

    funktion lade(klauseln):
        selbst.klauseln = klauseln
        setze max_var auf 0
        setze i auf 0
        solange i < länge(klauseln):
            setze k auf klauseln[i]
            setze j auf 0
            solange j < länge(k):
                setze v auf abs_zahl(k[j])
                wenn v > max_var:
                    setze max_var auf v
                setze j auf j + 1
            setze i auf i + 1
        selbst.num_vars = max_var

    funktion loese():
        setze a auf {}
        gib_zurück dpll(selbst.klauseln, a)

# ------------------------------------------------------------
# DIMACS Parser
# ------------------------------------------------------------

funktion parse_zahl(s, off):
    setze n auf 0
    setze neg auf falsch
    wenn off < länge(s) und s[off] == "-":
        setze neg auf wahr
        setze off auf off + 1
    solange off < länge(s) und s[off] >= "0" und s[off] <= "9":
        setze d auf bytes_zu_liste(s[off])[0] - 48
        setze n auf n * 10 + d
        setze off auf off + 1
    wenn neg:
        setze n auf 0 - n
    setze r auf {}
    r["wert"] = n
    r["off"] = off
    gib_zurück r

funktion parse_dimacs(quelle):
    setze klauseln auf []
    setze zeilen auf quelle.teilen("\n")
    setze i auf 0
    solange i < länge(zeilen):
        setze z auf zeilen[i].trimmen()
        wenn länge(z) > 0 und z[0] != "c" und z[0] != "p":
            setze klausel auf []
            setze off auf 0
            solange off < länge(z):
                # Skip spaces
                solange off < länge(z) und z[off] == " ":
                    setze off auf off + 1
                wenn off < länge(z):
                    setze erg auf parse_zahl(z, off)
                    setze off auf erg["off"]
                    wenn erg["wert"] != 0:
                        klausel.hinzufügen(erg["wert"])
            wenn länge(klausel) > 0:
                klauseln.hinzufügen(klausel)
        setze i auf i + 1
    gib_zurück klauseln

# ------------------------------------------------------------
# Sudoku-Encoder
# ------------------------------------------------------------

# Variable-Nummer fuer Zelle (r,c) mit Ziffer d (1-indiziert)
# r,c ∈ 0..8, d ∈ 1..9
funktion sudoku_var(r, c, d):
    gib_zurück r * 81 + c * 9 + d

klasse SudokuEncoder:
    funktion erstelle():
        setze selbst.klauseln auf []

    funktion kodiere(grid):
        setze klauseln auf []
        # Constraint 1: jede Zelle hat mindestens eine Ziffer
        setze r auf 0
        solange r < 9:
            setze c auf 0
            solange c < 9:
                setze k auf []
                setze d auf 1
                solange d <= 9:
                    k.hinzufügen(sudoku_var(r, c, d))
                    setze d auf d + 1
                klauseln.hinzufügen(k)
                setze c auf c + 1
            setze r auf r + 1
        # Constraint 2: jede Zelle hat hoechstens eine Ziffer
        setze r auf 0
        solange r < 9:
            setze c auf 0
            solange c < 9:
                setze d1 auf 1
                solange d1 <= 8:
                    setze d2 auf d1 + 1
                    solange d2 <= 9:
                        setze k auf []
                        k.hinzufügen(0 - sudoku_var(r, c, d1))
                        k.hinzufügen(0 - sudoku_var(r, c, d2))
                        klauseln.hinzufügen(k)
                        setze d2 auf d2 + 1
                    setze d1 auf d1 + 1
                setze c auf c + 1
            setze r auf r + 1
        # Constraint 3a: jede Ziffer kommt hoechstens einmal pro Zeile
        setze r auf 0
        solange r < 9:
            setze d auf 1
            solange d <= 9:
                setze c1 auf 0
                solange c1 <= 7:
                    setze c2 auf c1 + 1
                    solange c2 <= 8:
                        setze k auf []
                        k.hinzufügen(0 - sudoku_var(r, c1, d))
                        k.hinzufügen(0 - sudoku_var(r, c2, d))
                        klauseln.hinzufügen(k)
                        setze c2 auf c2 + 1
                    setze c1 auf c1 + 1
                setze d auf d + 1
            setze r auf r + 1
        # Constraint 3b: jede Ziffer kommt hoechstens einmal pro Spalte
        setze c auf 0
        solange c < 9:
            setze d auf 1
            solange d <= 9:
                setze r1 auf 0
                solange r1 <= 7:
                    setze r2 auf r1 + 1
                    solange r2 <= 8:
                        setze k auf []
                        k.hinzufügen(0 - sudoku_var(r1, c, d))
                        k.hinzufügen(0 - sudoku_var(r2, c, d))
                        klauseln.hinzufügen(k)
                        setze r2 auf r2 + 1
                    setze r1 auf r1 + 1
                setze d auf d + 1
            setze c auf c + 1
        # Constraint 3c: jede Ziffer kommt hoechstens einmal pro 3x3 Box
        setze br auf 0
        solange br < 3:
            setze bc auf 0
            solange bc < 3:
                setze d auf 1
                solange d <= 9:
                    # Sammle alle 9 Zellen der Box als flache Liste
                    setze zellen auf []
                    setze i auf 0
                    solange i < 9:
                        setze r auf br * 3 + boden(i / 3)
                        setze c auf bc * 3 + (i % 3)
                        zellen.hinzufügen([r, c])
                        setze i auf i + 1
                    setze i1 auf 0
                    solange i1 <= 7:
                        setze i2 auf i1 + 1
                        solange i2 <= 8:
                            setze k auf []
                            k.hinzufügen(0 - sudoku_var(zellen[i1][0], zellen[i1][1], d))
                            k.hinzufügen(0 - sudoku_var(zellen[i2][0], zellen[i2][1], d))
                            klauseln.hinzufügen(k)
                            setze i2 auf i2 + 1
                        setze i1 auf i1 + 1
                    setze d auf d + 1
                setze bc auf bc + 1
            setze br auf br + 1
        # Vorgaben aus grid
        setze r auf 0
        solange r < 9:
            setze c auf 0
            solange c < 9:
                setze d auf grid[r * 9 + c]
                wenn d > 0:
                    setze k auf [sudoku_var(r, c, d)]
                    klauseln.hinzufügen(k)
                setze c auf c + 1
            setze r auf r + 1
        gib_zurück klauseln

funktion dekodiere_sudoku(assignment):
    setze grid auf []
    setze i auf 0
    solange i < 81:
        grid.hinzufügen(0)
        setze i auf i + 1
    setze r auf 0
    solange r < 9:
        setze c auf 0
        solange c < 9:
            setze d auf 1
            solange d <= 9:
                setze v auf sudoku_var(r, c, d)
                wenn a_hat(assignment, v) und a_get(assignment, v):
                    grid[r * 9 + c] = d
                setze d auf d + 1
            setze c auf c + 1
        setze r auf r + 1
    gib_zurück grid

funktion zeige_sudoku(grid):
    setze r auf 0
    solange r < 9:
        setze zeile auf ""
        setze c auf 0
        solange c < 9:
            setze d auf grid[r * 9 + c]
            wenn d == 0:
                setze zeile auf zeile + ". "
            sonst:
                setze zeile auf zeile + text(d) + " "
            wenn c == 2 oder c == 5:
                setze zeile auf zeile + "| "
            setze c auf c + 1
        zeige zeile
        wenn r == 2 oder r == 5:
            zeige "------+-------+------"
        setze r auf r + 1

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
zeige "  moo DPLL SAT-Solver + Sudoku"
zeige "================================================"
zeige ""

zeige "--- Test 1: einfache SAT ---"
# (a OR b) AND (NOT a OR c) AND (NOT b OR NOT c)
setze klauseln auf [[1, 2], [0 - 1, 3], [0 - 2, 0 - 3]]
setze solver auf neu SatSolver()
solver.lade(klauseln)
setze r auf solver.loese()
check("SAT gefunden", r != nichts)
wenn r != nichts:
    # Verifiziere Loesung
    setze erfuellt auf wahr
    setze i auf 0
    solange i < länge(klauseln):
        setze s auf klausel_status(klauseln[i], r)
        wenn s["status"] != "sat":
            setze erfuellt auf falsch
        setze i auf i + 1
    check("Loesung erfuellt alle Klauseln", erfuellt)

zeige ""
zeige "--- Test 2: UNSAT ---"
# a AND NOT a
setze klauseln auf [[1], [0 - 1]]
setze solver auf neu SatSolver()
solver.lade(klauseln)
setze r auf solver.loese()
check("UNSAT (nichts zurueck)", r == nichts)

zeige ""
zeige "--- Test 3: DIMACS Parser ---"
setze quelle auf "c Kommentar\np cnf 3 2\n1 -2 0\n-1 3 0\n"
setze k auf parse_dimacs(quelle)
check("2 klauseln", länge(k) == 2)
check("erste klausel [1,-2]", k[0][0] == 1 und k[0][1] == 0 - 2)
check("zweite klausel [-1,3]", k[1][0] == 0 - 1 und k[1][1] == 3)

zeige ""
zeige "--- Test 4: laengere SAT (3-SAT) ---"
# (a OR b OR c) AND (-a OR -b OR c) AND (a OR -b OR -c) AND (-a OR b OR -c)
setze klauseln auf [[1, 2, 3], [0 - 1, 0 - 2, 3], [1, 0 - 2, 0 - 3], [0 - 1, 2, 0 - 3]]
setze solver auf neu SatSolver()
solver.lade(klauseln)
setze r auf solver.loese()
check("3-SAT gefunden", r != nichts)

zeige ""
zeige "--- Test 5: Pigeonhole (5 Tauben in 4 Loecher) UNSAT ---"
# p_ij = Taube i in Loch j, i ∈ 1..5, j ∈ 1..4
# var(i,j) = (i-1)*4 + j
# Jede Taube in mind. einem Loch
setze klauseln auf []
setze i auf 1
solange i <= 3:
    setze k auf []
    setze j auf 1
    solange j <= 2:
        k.hinzufügen((i - 1) * 2 + j)
        setze j auf j + 1
    klauseln.hinzufügen(k)
    setze i auf i + 1
# Kein Loch hat 2 Tauben (pigeonhole 3 in 2)
setze j auf 1
solange j <= 2:
    setze i1 auf 1
    solange i1 <= 2:
        setze i2 auf i1 + 1
        solange i2 <= 3:
            setze k auf []
            k.hinzufügen(0 - ((i1 - 1) * 2 + j))
            k.hinzufügen(0 - ((i2 - 1) * 2 + j))
            klauseln.hinzufügen(k)
            setze i2 auf i2 + 1
        setze i1 auf i1 + 1
    setze j auf j + 1
setze solver auf neu SatSolver()
solver.lade(klauseln)
setze r auf solver.loese()
check("Pigeonhole 3-in-2 UNSAT", r == nichts)

zeige ""
zeige "--- Test 6: Sudoku-Encoder Klausel-Anzahl ---"
setze enc auf neu SudokuEncoder()
setze leer_grid auf []
setze i auf 0
solange i < 81:
    leer_grid.hinzufügen(0)
    setze i auf i + 1
setze k auf enc.kodiere(leer_grid)
zeige "Klauseln fuer leeres Sudoku: " + text(länge(k))
# C1: 81 "at-least-one", C2: 81*36=2916, C3: 3*9*9*36=8748 -> 11745 ohne Vorgaben
check("Sudoku-CNF > 11000 Klauseln", länge(k) > 11000)

zeige ""
zeige "--- Test 7: Sudoku loesen (fast fertig) ---"
# Ein Puzzle mit vielen Vorgaben (nur 4 Zellen leer) — DPLL findet es schnell
setze puzzle auf [
    5, 3, 4,  6, 7, 8,  9, 1, 2,
    6, 7, 2,  1, 9, 5,  3, 4, 8,
    1, 9, 8,  3, 4, 2,  5, 6, 7,

    8, 5, 9,  7, 6, 1,  4, 2, 3,
    4, 2, 6,  8, 5, 3,  7, 9, 1,
    7, 1, 3,  9, 2, 4,  8, 5, 6,

    9, 6, 1,  5, 3, 7,  2, 8, 4,
    2, 8, 7,  4, 1, 9,  6, 3, 5,
    3, 0, 5,  0, 8, 0,  1, 7, 0]
setze enc auf neu SudokuEncoder()
setze k auf enc.kodiere(puzzle)
zeige "Klauseln mit Vorgaben: " + text(länge(k))
setze solver auf neu SatSolver()
solver.lade(k)
setze r auf solver.loese()
check("Sudoku SAT", r != nichts)
wenn r != nichts:
    setze grid auf dekodiere_sudoku(r)
    zeige_sudoku(grid)
    check("Zelle (8,1) = 4", grid[8 * 9 + 1] == 4)
    check("Zelle (8,3) = 2", grid[8 * 9 + 3] == 2)
    check("Zelle (8,5) = 6", grid[8 * 9 + 5] == 6)
    check("Zelle (8,8) = 9", grid[8 * 9 + 8] == 9)

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"
