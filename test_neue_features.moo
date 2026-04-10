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

setze namen auf ["Charlie", "Anna", "Bob"]
namen.sort()
zeige namen

# 3. Range + Rechnung
zeige "--- Range Berechnung ---"
setze summe auf 0
für n in 1..11:
    summe += n
zeige summe

# 4. Listen-Methoden
zeige "--- Listen-Methoden ---"
setze liste auf [10, 20, 30]
liste.append(40)
zeige liste
zeige liste.length()
liste.reverse()
zeige liste

zeige "=== NEUE FEATURES OK ==="
