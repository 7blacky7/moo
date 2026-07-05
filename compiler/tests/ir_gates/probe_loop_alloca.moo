# REG-G1 IR-Gate-Probe: Konstrukte, die Temp-Slots im Codegen brauchen,
# bewusst IN Schleifenkoerpern. Jede alloca ausserhalb des Entry-Blocks
# ist ein Regressionsfund (Stack waechst pro Iteration, siehe run_ir_gates.sh).
# Abgedeckt: Listen-Literal, Dict-Literal, List-Comprehension (solange),
# Dict+Comprehension (für), für-in-solange (for_idx-Fund 2026-07-05),
# Schleife in Funktion (define-Reset des awk-Audits).

funktion helfer(n):
    setze j auf 0
    solange j < n:
        setze l2 auf [j, j * 2]
        setze j auf j + 1
    gib_zurück j

setze i auf 0
solange i < 10:
    setze l auf [i, i + 1]
    setze d auf {"a": i, "b": i * 2}
    setze c auf [x * 2 für x in l]
    für k in [1, 2]:
        setze nest auf [k]
    setze i auf i + 1
für k in [1, 2, 3]:
    setze e auf {"k": k}
    setze f auf [y + k für y in [4, 5]]
setze h auf helfer(5)
zeige "ok"
