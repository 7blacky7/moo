# moo Benchmark Suite
# Misst Performance verschiedener Operationen

zeige "=== moo Benchmark Suite ==="
zeige ""

# --- 1. Fibonacci(35) rekursiv ---
funktion fib(n):
    wenn n <= 1:
        gib_zurück n
    gib_zurück fib(n - 1) + fib(n - 2)

setze t1 auf zeit()
setze ergebnis auf fib(35)
setze t2 auf zeit()
setze dauer auf t2 - t1
zeige f"Fibonacci(35) = {ergebnis}"
zeige f"  Zeit: {dauer} Sekunden"
zeige ""

# --- 2. Listen-Sort 10000 Elemente ---
setze liste auf []
setze i auf 0
solange i < 10000:
    liste.hinzufügen(zufall() * 10000)
    setze i auf i + 1

setze t1 auf zeit()
setze sortiert auf liste.sortieren()
setze t2 auf zeit()
setze dauer auf t2 - t1
zeige f"Listen-Sort (10000 Elemente)"
zeige f"  Zeit: {dauer} Sekunden"
zeige ""

# --- 3. Vektor-Ops 100000 Elemente ---
setze vec auf []
setze i auf 0
solange i < 100000:
    vec.hinzufügen(i * 1.0)
    setze i auf i + 1

setze t1 auf zeit()
setze ergebnis auf vec * 2.0
setze ergebnis auf ergebnis + 1.0
setze t2 auf zeit()
setze dauer auf t2 - t1
zeige f"Vektor-Ops (100000 Elemente, *2 +1)"
zeige f"  Zeit: {dauer} Sekunden"
zeige ""

# --- 4. String-Concat 10000x ---
setze t1 auf zeit()
setze text auf ""
setze i auf 0
solange i < 10000:
    setze text auf text + "x"
    setze i auf i + 1

setze t2 auf zeit()
setze dauer auf t2 - t1
zeige f"String-Concat (10000x)"
zeige f"  Laenge: {länge(text)}"
zeige f"  Zeit: {dauer} Sekunden"
zeige ""

zeige "=== Benchmark fertig ==="
