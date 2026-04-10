# === NEUE FEATURES TEST ===

# 1. Range-Schleifen
zeige "--- Range ---"
für i in 0..5:
    zeige i

# 2. Listen-Sort
zeige "--- Sort ---"
setze zahlen auf [5, 3, 8, 1, 9, 2]
zahlen.sort()
zeige zahlen

# 3. Negative Indices
zeige "--- Negative Indices ---"
setze liste auf [10, 20, 30, 40, 50]
zeige liste[-1]
zeige liste[-2]

# 4. String-Index
zeige "--- String-Index ---"
setze wort auf "moo"
zeige wort[0]
zeige wort[2]

# 5. String-Wiederholung
zeige "--- String * Zahl ---"
zeige "ha" * 3
zeige "-" * 20

# 6. Universeller Index (Dict + Liste gemischt)
zeige "--- Universeller Index ---"
setze daten auf {"name": "moo", "version": 1}
zeige daten["name"]
daten["version"] = 2
zeige daten["version"]

# 7. Range-Summe
zeige "--- Range Summe ---"
setze summe auf 0
für n in 1..101:
    summe += n
zeige summe

# 8. Builtin-Funktionen
zeige "--- Builtins ---"
zeige länge([1, 2, 3, 4, 5])
zeige text(42)
zeige typ_von([])

# 9. Listen-Methoden komplett
zeige "--- Listen-Methoden ---"
setze l auf [3, 1, 2]
l.sort()
zeige l
l.reverse()
zeige l
l.append(0)
zeige l
zeige l.length()

zeige "=== NEUE FEATURES OK ==="
