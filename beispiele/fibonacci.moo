# Fibonacci-Folge in moo
# Zeigt: Funktionen, Schleifen, Listen, String-Ops

funktion fibonacci(n):
    wenn n <= 1:
        gib_zurück n
    setze a auf 0
    setze b auf 1
    setze ende auf n + 1
    für i in 2..ende:
        setze temp auf b
        setze b auf a + b
        setze a auf temp
    gib_zurück b

# Die ersten 20 Fibonacci-Zahlen
zeige "Fibonacci-Folge:"
zeige "=" * 30

setze ergebnisse auf []
für i in 0..20:
    ergebnisse.append(fibonacci(i))

zeige ergebnisse

# Summe
setze summe auf 0
für z in ergebnisse:
    summe += z
zeige "Summe: " + text(summe)
