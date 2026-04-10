# moo Standard-Bibliothek: Primzahlen

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

funktion lerp(a, b, t):
    gib_zurück a + (b - a) * t
