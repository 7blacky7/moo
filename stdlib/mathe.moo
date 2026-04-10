# moo Standard-Bibliothek: Mathematik

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

funktion ggt(a, b):
    solange b != 0:
        setze temp auf b
        setze b auf a % b
        setze a auf temp
    gib_zurück a

funktion ist_primzahl(n):
    wenn n < 2:
        gib_zurück falsch
    wenn n < 4:
        gib_zurück wahr
    wenn n % 2 == 0:
        gib_zurück falsch
    setze i auf 3
    solange i * i <= n:
        wenn n % i == 0:
            gib_zurück falsch
        setze i auf i + 2
    gib_zurück wahr

funktion klemme(wert, lo, hi):
    wenn wert < lo:
        gib_zurück lo
    wenn wert > hi:
        gib_zurück hi
    gib_zurück wert
