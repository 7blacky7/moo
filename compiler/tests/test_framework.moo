# Eingebautes Test-Framework Demo

teste "Addition":
    erwarte 2 + 2 == 4
    erwarte 10 + 20 == 30

teste "Strings":
    setze name auf "Anna"
    erwarte länge(name) == 4

teste "Listen":
    setze l auf [1, 2, 3]
    erwarte länge(l) == 3
    erwarte l[0] == 1

teste_alle()
