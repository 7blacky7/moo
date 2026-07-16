# Regression: Default-Parameter auf Klassen-Methoden und Konstruktoren
# pruefen, ob sie korrekt evaluiert werden wenn sie beim Aufruf fehlen.

klasse Foo:
    funktion erstelle(a, b, c="hello"):
        selbst.a = a
        selbst.b = b
        selbst.c = c

    funktion meth(x, y="world"):
        gib_zurück x + " " + y

setze x auf neu Foo(1, 2)
zeige(x.c)
zeige(x.meth("hallo"))
zeige(x.meth("hallo", "moo"))
