# moo Mathe-Bibliothek (Basis)

funktion fakultaet(n):
    wenn n <= 1:
        gib_zurück 1
    gib_zurück n * fakultaet(n - 1)

funktion fibonacci(n):
    wenn n <= 0:
        gib_zurück 0
    wenn n == 1:
        gib_zurück 1
    setze a auf 0
    setze b auf 1
    für i in 2..n + 1:
        setze temp auf b
        setze b auf a + b
        setze a auf temp
    gib_zurück b

funktion ist_gerade(n):
    gib_zurück n % 2 == 0
